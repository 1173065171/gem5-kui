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

/**
 * CSR 控制状态寄存器结构
 * 8个CSR寄存器，分为4对(INS1-INS4)，每对包含MSB和LSB
 */
struct CsrReg {
    uint32_t ins1_msb = 0;
    uint32_t ins1_lsb = 0;
    uint32_t ins2_msb = 0;
    uint32_t ins2_lsb = 0;
    uint32_t ins3_msb = 0;
    uint32_t ins3_lsb = 0;
    uint32_t ins4_msb = 0;
    uint32_t ins4_lsb = 0;

    void reset() {
        ins1_msb = ins1_lsb = ins2_msb = ins2_lsb = 0;
        ins3_msb = ins3_lsb = ins4_msb = ins4_lsb = 0;
    }
};

/**
 * 配置寄存器结构
 * 存储从CSR寄存器解码后的配置参数
 */
struct ConfigReg {
    // 地址和步长配置
    uint32_t A_address = 0;
    uint8_t A_address_xstep = 0;
    uint8_t A_address_chstep = 0;
    uint32_t B_address = 0;
    uint8_t B_address_xstep = 0;
    uint8_t B_address_chstep = 0;
    uint32_t D_address = 0;
    uint8_t D_address_xstep = 0;
    uint8_t D_address_chstep = 0;
    
    // 控制参数
    uint8_t flow_loop_times = 0;
    uint8_t work_mode = 0;      // 0-2: 矩阵乘; 3: 矩阵加
    uint8_t flow_mode = 0;      // 流式处理模式
    uint8_t register_mode = 0;  // 0: 普通卷积; 2: DW卷积
    uint8_t conv_kernel = 0;    // 卷积核大小(1/3/5)
    uint8_t transpose_mode = 0; // 转置模式
    uint8_t stride = 0;         // 步长
    uint8_t shift_mode = 0;     // 是否需要移位
    uint8_t cutbit = 0;         // 截位数
    
    // 其他配置
    uint32_t C_address = 0;
    uint8_t ins_id = 0;
    uint8_t start = 0;
};

/**
 * 状态寄存器结构
 * 跟踪执行状态和当前地址/步长等
 */
struct StatusReg {
    uint8_t running = 0;
    uint32_t running_time = 0;
    uint16_t flow_i = 0;        // 当前flow迭代
    
    // A矩阵访问参数
    uint32_t A_address = 0;
    uint32_t A_step = 0;
    uint16_t A_count = 0;
    uint8_t A_kernel = 0;
    uint16_t A_bytes = 0;
    
    // B矩阵访问参数
    uint32_t B_address = 0;
    uint32_t B_step = 0;
    uint16_t B_count = 0;
    uint8_t B_kernel = 0;
    uint16_t B_bytes = 0;
    
    // C和D地址/参数
    uint32_t C_address = 0;
    uint32_t D_address = 0;
    uint32_t D_step = 0;
    uint16_t D_count = 0;
    uint8_t D_kernel = 0;
    std::vector<uint16_t> D_wstrb;
};

/**
 * 矩阵缓冲区
 * 存储中间矩阵数据
 */
struct MatrixBuffer {
    std::vector<std::vector<int32_t>> matrix;
    
    MatrixBuffer() = default;
    MatrixBuffer(size_t rows, size_t cols) {
        matrix.resize(rows, std::vector<int32_t>(cols, 0));
    }
};

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
		// ==================== SAU 核心数据结构 ====================
		const uint32_t baseAddr = 0x20000000;
		const unsigned unitSize = 16;  // 矩阵大小（16x16）
		
		// CSR 和配置
		CsrReg csr;
		ConfigReg config;
		StatusReg status;
		
		// 矩阵缓冲区和中间数据
		MatrixBuffer input_matrix1;     // 输入矩阵A
		MatrixBuffer input_matrix2;     // 输入矩阵B
		std::vector<int32_t> input_matrix3; // 输入向量C
		std::vector<std::vector<int32_t>> A_matrix;
		std::vector<std::vector<int32_t>> B_matrix;
		std::vector<int32_t> C_matrix;
		std::vector<std::vector<int32_t>> D_matrix;
		std::vector<std::vector<int32_t>> D_matrix_deq;
		std::vector<std::vector<int32_t>> D_matrix_tmp;
		MatrixBuffer output_matrix;
		
		// SAU 执行状态
		enum class ExecutionState {
			Idle,           // 等待指令
			FetchConfigA,   // 取A矩阵配置
			FetchConfigB,   // 取B矩阵配置
			FetchData,      // 取数据
			Preprocess,     // 数据预处理
			Execute,        // 脉动阵列计算
			Accumulate,     // 累加
			Postprocess,    // 后处理
			WriteBack       // 写回结果
		};
		ExecutionState execState = ExecutionState::Idle;
		
		// ==================== 关键仿真组件 ====================
		// mem port
		KuiSauMemSidePort port_KuiSau_sendto_mem;
		KuiSauCsrSidePort port_KuiSau_getfrm_mem;
		const AddrRange csrAddrRange;

		EventFunctionWrapper nextTickEvent;
		EventFunctionWrapper executeFlowEvent;
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

	void handleResponse(PacketPtr pkt);
		
		/**
		 * 从CSR指令解码配置参数
		 * 根据SAU.py中的update_csr逻辑
		 */
		void updateCsrFromRegisters();
		
		/**
		 * 根据配置参数更新状态寄存器
		 * 计算数据访问地址、步长、计数等
		 */
		void updateStatusFromConfig();
		
		/**
		 * 读取矩阵A的数据
		 * 处理数据对齐和格式转换
		 */
		void updateInputMatrix1(const std::vector<int32_t> &inputData);
		
		/**
		 * 读取矩阵B的数据
		 */
		void updateInputMatrix2(const std::vector<int32_t> &inputData);
		
		/**
		 * 读取向量C的数据
		 */
		void updateInputMatrix3(const std::vector<int32_t> &inputData);
		
		/**
		 * 数据预处理：转置、格式转换、数据重排
		 */
		void preprocess();
		
		/**
		 * 卷积操作的矩阵移位
		 */
		void convMatrixShift();
		
		/**
		 * 脉动阵列计算
		 */
		void systolicArrayExecute();
		
		/**
		 * 累加处理
		 */
		void accumulateResults();
		
		/**
		 * 加偏置项C
		 */
		void addBiasC();
		
		/**
		 * 截位去量化
		 */
		void dequantize();
		
		/**
		 * 输出转置
		 */
		void transposeOutput();
		
		/**
		 * 更新输出矩阵，准备写回
		 */
		void updateOutputMatrix();
		
		/**
		 * 发送读请求到内存
		 */
		void sendMemoryRead(Addr addr, size_t size);
		
		/**
		 * 发送写请求到内存
		 */
		void sendMemoryWrite(Addr addr, const std::vector<int32_t> &data);
		
		/**
		 * SAU执行流程的主循环
		 */
		void executeFlow();
	
		// ==================== CSR处理相关 ====================
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
		
		/**
		 * 处理来自CPU的CSR包
		 * 根据写入的CSR寄存器触发指令解码和执行
		 */
		void processCsrPacket(PacketPtr pkt, bool isAtomic);

	public:
		// 构造函数
		void startup() override;
		KuiSau(const KuiSauParams &params) : SimObject(params),
			port_KuiSau_sendto_mem(params.name + ".port_KuiSau_sendto_mem", this),
			port_KuiSau_getfrm_mem(params.name + ".port_KuiSau_getfrm_mem", this),
			csrAddrRange(params.csr_addr_range),
			nextTickEvent([this]{sendOneKuiPkt();},name()),		executeFlowEvent([this]{executeFlow();},name()),			clockDomain(params.clk_domain),
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
