// Clean implementation of KuiSau timing FSM (ReadA -> ReadB -> Compute -> Write)
#include "KuiSau.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"
#include <random>

namespace gem5 {

// Mem side port methods
void
KuiSau::KuiSauMemSidePort::recvReqRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    sendPacket(pkt);
}

bool
KuiSau::KuiSauMemSidePort::recvTimingResp(PacketPtr pkt)
{
    std::cout << "[KuiSau] recvTimingResp tick=" << curTick() << std::endl;
    owner->handleResponse(pkt);
    delete pkt; // delete packet + its dynamic data
    return true;
}

void
KuiSau::KuiSauMemSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "MemSidePort: already blocked!");
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        isBlocked = true;
        return;
    }
    isBlocked = false;
}

// CSR side port: accepts CPU CSR accesses and stores them in a 32-bit control register
void
KuiSau::processCsrPacket(PacketPtr pkt, bool isAtomic)
{
    if (isAtomic)
        pkt->makeAtomicResponse();
    else
        pkt->makeResponse();

    const unsigned size = pkt->getSize();
    if (size != sizeof(uint32_t)) {
        warn("%s: CSR access size %u unexpected (expect 4)", name(), size);
    }

    if (pkt->isWrite()) {
        if (!pkt->hasData()) {
            warn("%s: CSR write without data", name());
            return;
        }
        csrControlReg = pkt->getLE<uint32_t>();
    } else if (pkt->isRead()) {
        pkt->setLE<uint32_t>(csrControlReg);
    }
}

bool
KuiSau::KuiSauCsrSidePort::recvTimingReq(PacketPtr pkt)
{
    if (blockedPacket) {
        return false;
    }

    owner->processCsrPacket(pkt, false);
    sendPacket(pkt);
    return !isBlocked;
}

Tick
KuiSau::KuiSauCsrSidePort::recvAtomic(PacketPtr pkt)
{
    owner->processCsrPacket(pkt, true);
    return owner->csrAccessLatency();
}

Tick
KuiSau::KuiSauCsrSidePort::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &backdoor)
{
    backdoor = nullptr;
    return recvAtomic(pkt);
}

void
KuiSau::KuiSauCsrSidePort::recvFunctional(PacketPtr pkt)
{
    owner->processCsrPacket(pkt, false);
}

void
KuiSau::KuiSauCsrSidePort::recvMemBackdoorReq(const MemBackdoorReq &req, MemBackdoorPtr &backdoor)
{
    backdoor = nullptr;
}

void
KuiSau::KuiSauCsrSidePort::recvRespRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    sendPacket(pkt);
}

void
KuiSau::KuiSauCsrSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "CSR port already blocked");
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
        isBlocked = true;
        return;
    }
    isBlocked = false;
}

// Startup schedules first event
void
KuiSau::startup()
{
    Tick first_tick = clockEdge();
    std::cout << "KuiSau startup tick=" << first_tick << " interval(cycles)=" << schedule_interval
              << " ticks=" << cyclesToTicks(Cycles(schedule_interval)) << std::endl;
    schedule(nextTickEvent, first_tick);
}

// Pack lower 8 accumulators of systolic into 128b value
static __uint128_t
pack_acc_lower8_to_128(KuiSauSystolic &s)
{
    __uint128_t v = 0;
    // 取第 0 行前 8 个累加器的低 8 位拼成 64 位（放在 128 位低部）
    for (int i = 0; i < 8; ++i) {
        uint32_t acc = static_cast<uint32_t>(s.readAcc(0, i)) & 0xFFFFFF; // 24-bit 有效
        v |= (__uint128_t(acc & 0xFF) << (i * 8));
    }
    return v;
}

// Main FSM tick
void
KuiSau::sendOneKuiPkt()
{
    std::cout << "[KuiSau] tick=" << curTick() << " state=";
    switch (flowState) {
        case FlowState::ReadA: std::cout << "ReadA"; break;
        case FlowState::ReadB: std::cout << "ReadB"; break;
        case FlowState::Compute: std::cout << "Compute"; break;
        case FlowState::Write: std::cout << "Write"; break;
    }
    std::cout << std::endl;

    if (maxStimulus > 0 && stimulusCount >= maxStimulus) {
        inform("%s: maxStimulus reached (%u)", name(), stimulusCount);
        return;
    }

    // Common constants
    const unsigned size = 16;
    const Request::Flags req_flags = Request::PHYSICAL;
    const RequestorID req_id = 1;

    if (port_KuiSau_sendto_mem.isBlocked) {
        // Just reschedule if still blocked
        schedule(nextTickEvent, curTick() + cyclesToTicks(Cycles(schedule_interval)));
        return;
    }

    switch (flowState) {
        case FlowState::ReadA: {
            addrA = addrDist(rng);
            addrOut = addrA; // for simplicity write back where A came from
            RequestPtr req = std::make_shared<Request>(addrA, size, req_flags, req_id);
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();
            std::cout << "[KuiSau] Send Timing ReadA @0x" << std::hex << addrA << std::dec << std::endl;
            port_KuiSau_sendto_mem.sendPacket(pkt);
            // State remains ReadA until response transitions it to ReadB
            break;
        }
        case FlowState::ReadB: {
            addrB = addrDist(rng);
            RequestPtr req = std::make_shared<Request>(addrB, size, req_flags, req_id);
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();
            std::cout << "[KuiSau] Send Timing ReadB @0x" << std::hex << addrB << std::dec << std::endl;
            port_KuiSau_sendto_mem.sendPacket(pkt);
            // State remains ReadB until response transitions it to Compute
            break;
        }
        case FlowState::Compute: {
            systolic.pulse();
            writeData128 = pack_acc_lower8_to_128(systolic);
            std::cout << "[KuiSau] Compute done writeData128 prepared" << std::endl;
            flowState = FlowState::Write;
            break;
        }
        case FlowState::Write: {
            RequestPtr req = std::make_shared<Request>(addrOut, size, req_flags, req_id);
            PacketPtr pkt = Packet::createWrite(req);
            pkt->allocate(); // allocate 16B
            uint8_t *buf = pkt->getPtr<uint8_t>();
            for (unsigned i = 0; i < size; ++i)
                buf[i] = (uint8_t)((writeData128 >> (i * 8)) & 0xFF);
            std::cout << "[KuiSau] Send Timing Write @0x" << std::hex << addrOut << std::dec << std::endl;
            port_KuiSau_sendto_mem.sendPacket(pkt);
            stimulusCount++;
            flowState = FlowState::ReadA;
            break;
        }
    }

    // Schedule next tick
    Tick next_tick = clockEdge(Cycles(schedule_interval));
    schedule(nextTickEvent, next_tick);
}

Port &
KuiSau::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port_KuiSau_sendto_mem")
        return port_KuiSau_sendto_mem;
    if (if_name == "port_KuiSau_getfrm_mem")
        return port_KuiSau_getfrm_mem;
    return SimObject::getPort(if_name, idx);
}

} // namespace gem5
