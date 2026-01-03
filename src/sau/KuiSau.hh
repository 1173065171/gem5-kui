#ifndef __KUISAU_H__
#define __KUISAU_H__

#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <cassert>
#include <cstring>

#include "params/KuiSau.hh" // 包含自动生成的Params类

#include "sim/sim_object.hh"
#include "sim/clock_domain.hh"

#include "base/trace.hh"
#include "base/logging.hh"
#include "base/addr_range.hh"
#include "mem/port.hh"
#include "mem/packet.hh"

#include "KuiPacket128.hh"
#include "KuiSauRun.hh"

namespace gem5
{
// C++实体类声明
class KuiSau : public SimObject{
  
	public:
		// 重载端口获取函数
		Port &getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
	
		// -- 端口声明
		// -- Mem Side Port
		class KuiSauMemSidePort : public RequestPort
		{
			private:
				KuiSau *owner;
				PacketPtr blockedPacket = nullptr;
			protected:
				bool recvTimingResp(PacketPtr pkt) override;
				void recvRangeChange() override { std::cout << "recvRangeChange unimpl." << std::endl; }
			public:
				bool isBlocked = false;
				void sendPacket(PacketPtr pkt);
				void recvReqRetry() override;
				// 构造
				KuiSauMemSidePort(const std::string& name, KuiSau *owner) : RequestPort(name, owner), owner(owner)
				{
				}
		};

		// -- Csr Side Port
		class KuiSauCsrSidePort : public ResponsePort
		{
			private:
				KuiSau *owner;
				PacketPtr blockedPacket = nullptr;
			protected:
				bool recvTimingReq(PacketPtr pkt) override;
				// Atomic protocol
				Tick recvAtomic(PacketPtr pkt) override;
				Tick recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &backdoor) override;
				// Functional protocol
				void recvFunctional(PacketPtr pkt) override;
				void recvMemBackdoorReq(const MemBackdoorReq &req, MemBackdoorPtr &backdoor) override;
				// General
				AddrRangeList getAddrRanges() const override { return AddrRangeList{owner->csrAddrRange}; }
			public:
				bool isBlocked = false;
				void sendPacket(PacketPtr pkt);
				void recvRespRetry() override;
				// 构造
				KuiSauCsrSidePort(const std::string& name, KuiSau *owner) : ResponsePort(name, owner), owner(owner)
				{
				}
		};		

	private:
		// -- 关键仿真组件
		// mem port
		KuiSauMemSidePort port_KuiSau_sendto_mem;
		KuiSauCsrSidePort port_KuiSau_getfrm_mem;
		const AddrRange csrAddrRange;

		EventFunctionWrapper nextTickEvent;
		ClockDomain *clockDomain;
		Cycles csrAccessCycles;

		// clock domain function
		Tick getClockPeriod() const {
			return clockDomain->clockPeriod();
		}

		Tick cyclesToTicks(Cycles c) const {
			return c * getClockPeriod();
		}

		Cycles ticksToCycles(Tick t) const {
			return Cycles(t / getClockPeriod());
		}

		Cycles getCurCycles() const{
			return ticksToCycles(curTick());
		}

		Tick clockEdge(Cycles cycles = Cycles(0)) const {
			Tick tick = curTick();
			Tick period = getClockPeriod();

			Tick tick_in_cycle = tick % period;

			Tick next_edge = tick;

			if(tick_in_cycle != 0){
				next_edge = tick + (period - tick_in_cycle);
			}
			return next_edge + cyclesToTicks(cycles);
		}

		void handleResponse(PacketPtr pkt)
		{
			// 仅处理 16B 的读响应，将数据推进计算流程
			const unsigned sz = pkt->getSize();
			if (sz != 16) {
				inform("%s: Unexpected response size %u (expect 16)", name(), sz);
				return;
			}
			const uint8_t *p = pkt->getConstPtr<uint8_t>();
			if (!p) {
				warn("%s: Response payload ptr is null", name());
				return;
			}

			__uint128_t val = 0;
			for (unsigned i = 0; i < 16; ++i) {
				val |= (static_cast<__uint128_t>(p[i]) << (i * 8));
			}

			std::cout << "[KuiSau] ReadResp 128b value: 0x";
			uint64_t hi = (uint64_t)(val >> 64);
			uint64_t lo = (uint64_t)(val & 0xFFFFFFFFFFFFFFFFULL);
			std::cout << std::hex << hi << std::setfill('0') << std::setw(16) << lo << std::dec << std::endl;

			// 将读到的数据推进到阵列，并推进状态机
			switch (flowState) {
				case FlowState::ReadA:
					lastReadA = val;
					systolic.loadDataA(val);
					flowState = FlowState::ReadB;
					break;
				case FlowState::ReadB:
					lastReadB = val;
					systolic.loadDataB(val);
					flowState = FlowState::Compute;
					break;
				default:
					// 非预期的响应阶段，忽略。
					break;
			}
		}
	
		// 内部私有参数
		unsigned int reschedule_interval = 2;
		unsigned int schedule_interval = 10;
		unsigned int maxStimulus = 1000;
		unsigned int stimulusCount = 0;
		// 内部私有状态
		enum class FlowState { ReadA, ReadB, Compute, Write };
		FlowState flowState = FlowState::ReadA;

		// RNG for deterministic address generation
		const uint64_t rngSeed;
		std::mt19937_64 rng;
		std::uniform_int_distribution<Addr> addrDist;

		// 内部私有变量
		KuiPacket128* kui_data_pkt;
		KuiPacket128* kui_read_pkt;
		enum class PktMode{
			WRITE,
			READ
		};
		PktMode curMode = PktMode::WRITE;
		MemCmd::Command memcmdmode = MemCmd::Command::WriteReq;

		// 计算与数据通道
		KuiSauSystolic systolic;
		Addr addrA = 0;
		Addr addrB = 0;
		Addr addrOut = 0;
		__uint128_t lastReadA = 0;
		__uint128_t lastReadB = 0;
		__uint128_t writeData128 = 0;
		uint32_t csrControlReg = 0;
		Tick csrAccessLatency() const { return cyclesToTicks(csrAccessCycles); }
		void processCsrPacket(PacketPtr pkt, bool isAtomic);

	public:
		// 构造函数
		void startup() override;
		KuiSau(const KuiSauParams &params) : SimObject(params),
			port_KuiSau_sendto_mem(params.name + ".port_KuiSau_sendto_mem", this),
			port_KuiSau_getfrm_mem(params.name + ".port_KuiSau_getfrm_mem", this),
			csrAddrRange(params.csr_addr_range),
			nextTickEvent([this]{sendOneKuiPkt();},name()),
			clockDomain(params.clk_domain),
			csrAccessCycles(params.csr_latency),
			rngSeed(params.rng_seed),
			rng(rngSeed == 0 ? 0xC001D00Du : rngSeed),
			addrDist(0, Addr(0x10000 - 1))
		{
			// Check ClockDomain
			fatal_if(!clockDomain, 
					"%s: ClockDomain must be set! "
					"Please set clk_domain in Python config.", 
					name());
			
			// print check info
			inform("%s created:", name());
			inform("  Clock period: %lld ticks", getClockPeriod());
			inform("  Clock frequency: %.2f GHz", 1000000.0 / getClockPeriod());
			inform("  Interval: %d cycles = %lld ticks", Cycles(reschedule_interval), cyclesToTicks(Cycles(reschedule_interval)));
			inform("  Max stimulus: %d", maxStimulus);
		}

		// 发送函数
		void sendOneKuiPkt();
		
};
}
#endif // __KUISAU_H__
