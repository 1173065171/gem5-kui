#ifndef __CSR_GEN_HH__
#define __CSR_GEN_HH__

#include <iostream>
#include <random>

#include "params/CsrGen.hh"
#include "sim/sim_object.hh"
#include "sim/clock_domain.hh"
#include "base/trace.hh"
#include "base/logging.hh"
#include "mem/port.hh"
#include "mem/packet.hh"

namespace gem5
{

/**
 * CsrGen: CSR 指令生成器
 * 作为 RequestPort 端连接到 KuiSau 的 CSR ResponsePort
 * 定时发送固定的 CSR 读写指令
 */
class CsrGen : public SimObject
{
  public:
    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

    // CSR Request Port - 发送 CSR 请求到 KuiSau
    class CsrGenRequestPort : public RequestPort
    {
      private:
        CsrGen *owner;
        PacketPtr blockedPacket = nullptr;

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvRangeChange() override {}
        void recvReqRetry() override;

      public:
        bool isBlocked = false;
        void sendPacket(PacketPtr pkt);

        CsrGenRequestPort(const std::string &name, CsrGen *owner)
            : RequestPort(name, owner), owner(owner)
        {
        }
    };

  private:
    // Port
    CsrGenRequestPort csrPort;

    // Timing
    EventFunctionWrapper tickEvent;
    ClockDomain *clockDomain;
    const Cycles interval;
    const unsigned maxRequests;
    unsigned requestCount = 0;

    // CSR 配置
    const unsigned csrSize = 4; // CSR 大小固定为 4 字节

    // 预定义的 CSR 指令序列
    enum class CsrOpType { WRITE, READ };
    struct CsrOp {
        CsrOpType type;
		Addr addr;
        uint32_t value; // 仅对 WRITE 有效
    };
    std::vector<CsrOp> csrOps;
    unsigned currentOpIndex = 0;

    // Clock domain helpers
    Tick getClockPeriod() const { return clockDomain->clockPeriod(); }
    Tick cyclesToTicks(Cycles c) const { return c * getClockPeriod(); }
    Tick clockEdge(Cycles cycles = Cycles(0)) const {
        Tick tick = curTick();
        Tick period = getClockPeriod();
        Tick tick_in_cycle = tick % period;
        Tick next_edge = tick;
        if (tick_in_cycle != 0) {
            next_edge = tick + (period - tick_in_cycle);
        }
        return next_edge + cyclesToTicks(cycles);
    }

    // 处理 CSR 响应
    void handleResponse(PacketPtr pkt);

    // 发送下一个 CSR 请求
    void SendOneCsr();

  public:
    CsrGen(const CsrGenParams &params);
    void startup() override;
};

} // namespace gem5

#endif // __CSR_GEN_HH__
