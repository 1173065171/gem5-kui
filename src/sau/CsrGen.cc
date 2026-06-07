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
      maxRequests(params.max_requests)
{
    fatal_if(!clockDomain, "%s: ClockDomain must be set!", name());

    // 预定义 CSR 操作序列：覆盖 0x200-0x207 八个逻辑 CSR word。
    // INS4_LSB 的 start bit 保持为 0，避免 readback 测试触发执行流。
    csrOps = {
        {CsrOpType::WRITE, 0x2f000200, 0x00000024},
        {CsrOpType::READ,  0x2f000200, 0x00000024},
        {CsrOpType::WRITE, 0x2f000201, 0x00000014},
        {CsrOpType::READ,  0x2f000201, 0x00000014},
        {CsrOpType::WRITE, 0x2f000202, 0x00010203},
        {CsrOpType::READ,  0x2f000202, 0x00010203},
        {CsrOpType::WRITE, 0x2f000203, 0x00040506},
        {CsrOpType::READ,  0x2f000203, 0x00040506},
        {CsrOpType::WRITE, 0x2f000204, 0x00001000},
        {CsrOpType::READ,  0x2f000204, 0x00001000},
        {CsrOpType::WRITE, 0x2f000205, 0x00002000},
        {CsrOpType::READ,  0x2f000205, 0x00002000},
        {CsrOpType::WRITE, 0x2f000206, 0x00003000},
        {CsrOpType::READ,  0x2f000206, 0x00003000},
        {CsrOpType::WRITE, 0x2f000207, 0x04004000},
        {CsrOpType::READ,  0x2f000207, 0x04004000},
    };

    inform("%s created:", name());
    inform("  Clock period: %lld ticks", getClockPeriod());
    inform("  Interval: %d cycles", interval);
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
    std::cout << "[CsrGen] currentOpIndex=" << currentOpIndex << std::endl;
    currentOpIndex++;

    // 创建 CSR 请求
    const Request::Flags req_flags = Request::PHYSICAL;
    const RequestorID req_id = 2; // 使用不同的 requestor ID

    RequestPtr req = std::make_shared<Request>(op.addr, csrSize, req_flags, req_id);
    PacketPtr pkt = nullptr;

    if (op.type == CsrOpType::WRITE) {
        pkt = Packet::createWrite(req);
        pkt->allocate();
        pkt->setLE<uint32_t>(op.value);
        std::cout << "[CsrGen] Sending CSR Write @0x" << std::hex << op.addr 
                  << " value=0x" << op.value << std::dec << std::endl;
    } else {
        pkt = Packet::createRead(req);
        pkt->allocate();
        expectedReadValues.push_back(op.value);
        std::cout << "[CsrGen] Sending CSR Read @0x" << std::hex << op.addr 
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
        panic_if(expectedReadValues.empty(),
                 "%s: received unexpected CSR read response", name());
        uint32_t expected = expectedReadValues.front();
        expectedReadValues.pop_front();
        panic_if(value != expected,
                 "%s: CSR readback mismatch: got 0x%x expected 0x%x",
                 name(), value, expected);
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
