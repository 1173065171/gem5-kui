#ifndef __KUI_PKT_HH__
#define __KUI_PKT_HH__

#include "mem/packet.hh"
#include <cstdint>
#include <iostream>
#include <iomanip>


namespace gem5
{

/**
 * 128-bit 数据通道包
 */
class KuiPacket128 : public Packet
{
    private:
        /* ------------ 硬件信号域 ------------ */
        Addr paddr;            // 地址
        uint16_t wstrb_128;       // 写掩码
        __uint128_t wdata_128;    // 写入数据
        __uint128_t rdata_128;    // 读出数据

    public:
        KuiPacket128();
        KuiPacket128(Request *req, MemCmd cmd);

        /* 设置读包 */
        void makeRead(Addr addr);

        /* 设置写包 */
        void makeWrite(Addr addr, __uint128_t data, uint16_t wstrb);

        /* 调试输出 */
        void display() const;

        void randomWdata(Addr addr);

};

} // namespace gem5

#endif