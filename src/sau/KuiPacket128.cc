#include "KuiPacket128.hh"

namespace gem5
{

/* ---------------- 构造函数 ---------------- */

KuiPacket128::KuiPacket128() : Packet(std::make_shared<Request>(0, 16, 0, 0), MemCmd::WriteReq),
    paddr(0),
    wstrb_128(0),
    wdata_128(0),
    rdata_128(0)
{
}

KuiPacket128::KuiPacket128(Request *req, MemCmd cmd) : Packet(RequestPtr(req), cmd),
    paddr(req->getPaddr()),
    wstrb_128(0),
    wdata_128(0),
    rdata_128(0)
{
}

/* ---------------- 包配置 API ---------------- */

void
KuiPacket128::makeRead(Addr addr)
{
    paddr = addr;

    this->cmd = MemCmd::ReadReq;
    this->setAddr(addr);
}

void
KuiPacket128::makeWrite(Addr addr, __uint128_t data, uint16_t wstrb)
{
    paddr = addr;
    wdata_128 = data;
    wstrb_128 = wstrb;

    this->cmd = MemCmd::WriteReq;
    this->setAddr(addr);
}

/* ---------------- 打印调试函数 ---------------- */

void
KuiPacket128::display() const
{
    auto hi = (uint64_t)(wdata_128 >> 64);
    auto lo = (uint64_t)(wdata_128 & 0xffffffffffffffffULL);

    std::cout << "===== KuiPacket128 =====\n";

    std::cout << "addr : 0x" << std::hex << paddr << std::dec << "\n";
    std::cout << "wstrb    : 0x" << std::hex << wstrb_128 << std::dec << "\n";

    std::cout << "wdata    : 0x" << std::hex << hi 
              << std::setfill('0') << std::setw(16)
              << lo << std::dec << "\n";

    std::cout << "==========================\n";
}

void
KuiPacket128::randomWdata(Addr addr)
{


}

} // namespace gem5