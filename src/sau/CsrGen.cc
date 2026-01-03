#include "sau/CsrGen.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"

namespace gem5
{

// ============================================================================
// CsrGenRequestPort 实现
// ============================================================================

void
CsrGen::CsrGenRequestPort::recvReqRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    sendPacket(pkt);
}

bool
CsrGen::CsrGenRequestPort::recvTimingResp(PacketPtr pkt)
{
    std::cout << "[CsrGen] recvTimingResp tick=" << curTick() << std::endl;
    owner->handleResponse(pkt);
    delete pkt;
    return true;
}

void
CsrGen::CsrGenRequestPort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "CsrGen port already blocked!");
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        isBlocked = true;
        return;
    }
    isBlocked = false;
}

// ============================================================================
// CsrGen 主类实现
// ============================================================================

CsrGen::CsrGen(const CsrGenParams &params)
    : SimObject(params),
      csrPort(params.name + ".csr_port", this),
      tickEvent([this] { SendOneCsr(); }, name()),
      clockDomain(params.clk_domain),
      interval(params.interval),
      maxRequests(params.max_requests),
      csrAddr(params.csr_addr)
{
    fatal_if(!clockDomain, "%s: ClockDomain must be set!", name());

    // 预定义 CSR 操作序列：写入几个值，然后读取
    csrOps = {
        {CsrOpType::WRITE, 0x12345678},
        {CsrOpType::READ, 0},
        {CsrOpType::WRITE, 0xABCDEF00},
        {CsrOpType::READ, 0},
        {CsrOpType::WRITE, 0xDEADBEEF},
        {CsrOpType::READ, 0},
    };

    inform("%s created:", name());
    inform("  Clock period: %lld ticks", getClockPeriod());
    inform("  Interval: %d cycles", interval);
    inform("  CSR Address: 0x%x", csrAddr);
    inform("  Max requests: %d", maxRequests);
    inform("  Predefined ops: %d", csrOps.size());
}

void
CsrGen::startup()
{
    Tick first_tick = clockEdge();
    std::cout << "[CsrGen] startup tick=" << first_tick << std::endl;
    schedule(tickEvent, first_tick);
}

void
CsrGen::SendOneCsr()
{
    std::cout << "[CsrGen] tick=" << curTick() 
              << " requestCount=" << requestCount 
              << " opIndex=" << currentOpIndex << std::endl;

    // 检查是否达到最大请求数
    if (maxRequests > 0 && requestCount >= maxRequests) {
        inform("%s: max requests reached (%u)", name(), requestCount);
        return;
    }

    // 检查端口是否阻塞
    if (csrPort.isBlocked) {
        schedule(tickEvent, curTick() + cyclesToTicks(interval));
        return;
    }

    // 获取当前操作
    if (currentOpIndex >= csrOps.size()) {
        // 循环执行操作序列
        currentOpIndex = 0;
    }

    const CsrOp &op = csrOps[currentOpIndex];
    currentOpIndex++;

    // 创建 CSR 请求
    const Request::Flags req_flags = Request::PHYSICAL;
    const RequestorID req_id = 2; // 使用不同的 requestor ID

    RequestPtr req = std::make_shared<Request>(csrAddr, csrSize, req_flags, req_id);
    PacketPtr pkt = nullptr;

    if (op.type == CsrOpType::WRITE) {
        pkt = Packet::createWrite(req);
        pkt->allocate();
        pkt->setLE<uint32_t>(op.value);
        std::cout << "[CsrGen] Sending CSR Write @0x" << std::hex << csrAddr 
                  << " value=0x" << op.value << std::dec << std::endl;
    } else {
        pkt = Packet::createRead(req);
        pkt->allocate();
        std::cout << "[CsrGen] Sending CSR Read @0x" << std::hex << csrAddr 
                  << std::dec << std::endl;
    }

    csrPort.sendPacket(pkt);
    requestCount++;

    // 调度下一次 tick
    schedule(tickEvent, clockEdge(interval));
}

void
CsrGen::handleResponse(PacketPtr pkt)
{
    if (pkt->isWrite()) {
        std::cout << "[CsrGen] CSR Write Response received" << std::endl;
    } else if (pkt->isRead()) {
        uint32_t value = pkt->getLE<uint32_t>();
        std::cout << "[CsrGen] CSR Read Response: 0x" << std::hex << value 
                  << std::dec << std::endl;
    }
}

Port &
CsrGen::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "csr_port")
        return csrPort;
    return SimObject::getPort(if_name, idx);
}

} // namespace gem5
