#ifndef __SAU_GOLDEN_GEN_HH__
#define __SAU_GOLDEN_GEN_HH__

#include <deque>
#include <string>
#include <vector>

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/SauGoldenGen.hh"
#include "sim/clock_domain.hh"
#include "sim/sim_object.hh"
#include "sim/system.hh"

namespace gem5
{

class SauGoldenGen : public SimObject
{
  public:
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void startup() override;

    explicit SauGoldenGen(const SauGoldenGenParams &params);

  private:
    enum class PortRole
    {
        Mem,
        Csr,
    };

    class GenPort : public RequestPort
    {
      public:
        GenPort(const std::string &name, SauGoldenGen *owner, PortRole role)
            : RequestPort(name, owner), owner(owner), role(role)
        {
        }

        void sendPacket(PacketPtr pkt);

      private:
        SauGoldenGen *owner;
        PortRole role;
        PacketPtr blockedPacket = nullptr;

        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override {}
    };

    enum class ActionType
    {
        MemWrite,
        MemReadD,
        CsrWrite,
        CsrReadBusy,
        Exit,
    };

    struct Action
    {
        ActionType type;
        Addr addr;
        std::vector<uint8_t> data;
        uint32_t value = 0;
        size_t size = 0;
    };

    GenPort memPort;
    GenPort csrPort;

    EventFunctionWrapper stepEvent;
    ClockDomain *clockDomain;
    System *system;
    RequestorID memRequestorId;
    RequestorID csrRequestorId;
    const Cycles interval;
    const Cycles pollInterval;
    const unsigned maxBusyPolls;
    const std::string testCase;

    std::deque<Action> actions;
    bool requestInFlight = false;
    unsigned busyPolls = 0;

    std::deque<std::vector<uint8_t>> expectedDReads;

    Tick getClockPeriod() const { return clockDomain->clockPeriod(); }
    Tick cyclesToTicks(Cycles cycles) const
    {
        return cycles * getClockPeriod();
    }
    Tick clockEdge(Cycles cycles = Cycles(0)) const;

    void buildScript();
    void issueNext();
    void scheduleStep(Cycles delay);

    void sendMemWrite(Addr addr, const std::vector<uint8_t> &data);
    void sendMemRead(Addr addr, size_t size);
    void sendCsrWrite(Addr addr, uint32_t value);
    void sendCsrRead(Addr addr);

    void handleResponse(PortRole role, PacketPtr pkt);
    void handleBusyRead(uint32_t value);
    void checkD(const uint8_t *data, size_t size);
};

} // namespace gem5

#endif // __SAU_GOLDEN_GEN_HH__
