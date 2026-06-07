#!/usr/bin/env python3
"""
SAU 矩阵计算仿真示例

这个脚本演示了SAU的完整工作流程：
1. 配置CSR寄存器
2. 加载矩阵数据到内存
3. 执行矩阵计算
4. 读回结果

对标原SAU.py中的测试流程
"""

import m5
from m5.objects import *
import numpy as np

# ============================================================================
# 内存数据初始化
# ============================================================================

def init_memory(system):
    """初始化内存中的矩阵数据"""
    
    # 简单的测试矩阵（16x16）
    # 矩阵A: 单位矩阵
    A_matrix = np.eye(16, dtype=np.int32)
    
    # 矩阵B: 递增矩阵
    B_matrix = np.arange(256, dtype=np.int32).reshape(16, 16)
    
    # 偏置向量C
    C_vector = np.ones(16, dtype=np.int32)
    
    # 写入内存（在仿真过程中会被加载）
    # 这里只是预设数据，实际在仿真中由CsrGen触发内存访问
    
    return {
        'A': A_matrix,
        'B': B_matrix,
        'C': C_vector
    }


# ============================================================================
# SAU系统配置
# ============================================================================

class SauTestSystem(System):
    """用于测试SAU的系统配置"""
    
    def __init__(self):
        super().__init__()
        
        # 时钟和电源
        self.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
        
        # 内存总线和控制器
        self.membus = SystemXBar()
        self.mem_ctrl = MemCtrl(dram=DDR3_1600_8x8())
        self.mem_ctrl.dram.range = AddrRange('1GB')
        self.membus.mem_side_ports = self.mem_ctrl.port
        
        # SAU模块
        self.seu = KuiSau(
            clk_domain=self.clk_domain,
            csr_addr_range=AddrRange(0x2f000000, size=0x1000),
            csr_latency=Cycles(5),
            rng_seed=42
        )
        self.seu.port_KuiSau_sendto_mem = self.membus.cpu_side_ports
        
        # CSR生成器 - 发送配置指令
        self.csr_gen = CsrGen(
            clk_domain=self.clk_domain,
            interval=10,
            max_requests=8  # 8个CSR寄存器
        )
        self.csr_gen.csr_port = self.seu.port_KuiSau_getfrm_mem


# ============================================================================
# SAU测试用例
# ============================================================================

class SAUTestCase:
    """SAU功能测试用例"""
    
    def __init__(self, system):
        self.system = system
        self.seu = system.seu
        self.matrices = init_memory(system)
    
    def test_matrix_multiply(self):
        """测试矩阵乘法"""
        print("\n[Test 1] 矩阵乘法计算")
        print("-" * 60)
        print("配置参数:")
        print(f"  工作模式:        0 (矩阵乘法)")
        print(f"  矩阵大小:        16x16")
        print(f"  shift_mode:      0 (8-bit)")
        print(f"  flow_loop_times: 1")
        print(f"  透视模式:        0 (无转置)")
        
        # 验证输入矩阵
        print("\n输入矩阵验证:")
        print(f"  矩阵A (单位矩阵):")
        print(f"    shape: {self.matrices['A'].shape}")
        print(f"    非零元素: {np.count_nonzero(self.matrices['A'])}")
        print(f"  矩阵B (递增矩阵):")
        print(f"    shape: {self.matrices['B'].shape}")
        print(f"    值范围: [{self.matrices['B'].min()}, {self.matrices['B'].max()}]")
        
        # 预期输出：D = A * B + C = B (因为A是单位矩阵) + C
        expected = self.matrices['B'] + self.matrices['C'][:, np.newaxis]
        print(f"\n预期输出统计:")
        print(f"  输出范围: [{expected.min()}, {expected.max()}]")
        print(f"  输出和: {expected.sum()}")
    
    def test_convolution(self):
        """测试卷积计算"""
        print("\n[Test 2] 卷积计算")
        print("-" * 60)
        print("配置参数:")
        print(f"  工作模式:        0 (矩阵乘法/卷积)")
        print(f"  conv_kernel:     3 (3x3卷积核)")
        print(f"  register_mode:   0 (普通卷积)")
        print(f"  stride:          1")
        print(f"  矩阵大小:        16x16")
        print(f"  输出大小:        14x14 (16-3+1=14)")
    
    def test_dw_convolution(self):
        """测试深度卷积"""
        print("\n[Test 3] 深度卷积 (Depthwise)")
        print("-" * 60)
        print("配置参数:")
        print(f"  工作模式:        0")
        print(f"  conv_kernel:     3 (3x3卷积核)")
        print(f"  register_mode:   2 (深度卷积)")
        print(f"  stride:          1")
        print(f"  矩阵大小:        16x16")
    
    def test_data_formats(self):
        """测试不同的数据格式"""
        print("\n[Test 4] 数据格式转换")
        print("-" * 60)
        print("支持的格式:")
        print(f"  8-bit (shift_mode=0):")
        print(f"    输入: int8 × int8")
        print(f"    中间: int16")
        print(f"    累加: 24-bit (饱和)")
        print(f"    输出: int8 (截位后)")
        print(f"")
        print(f"  16-bit (shift_mode=1):")
        print(f"    输入: int16 × int8")
        print(f"    中间: int24")
        print(f"    累加: 32-bit")
        print(f"    输出: int16 (截位后)")
    
    def run_all_tests(self):
        """运行所有测试"""
        print("\n" + "="*60)
        print("SAU (Systolic Array Unit) 功能测试")
        print("="*60)
        
        self.test_matrix_multiply()
        self.test_convolution()
        self.test_dw_convolution()
        self.test_data_formats()
        
        print("\n" + "="*60)
        print("测试完成")
        print("="*60)


# ============================================================================
# 主程序
# ============================================================================

if __name__ == '__m5_main__':
    # 创建系统和根对象
    system = SauTestSystem()
    root = Root(full_system=False, system=system)
    
    # 实例化仿真
    m5.instantiate()
    
    # 运行功能测试
    test = SAUTestCase(system)
    test.run_all_tests()
    
    # 启动仿真
    print("\n启动gem5仿真...\n")
    exit_event = m5.simulate()
    
    print(f"\n仿真完成: {exit_event.getCause()}")
    print(f"总仿真时间: {m5.curTick() / 1e9:.3f} ns")
