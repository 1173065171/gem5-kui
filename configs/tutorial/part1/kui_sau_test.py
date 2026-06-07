#!/usr/bin/env python3
"""
gem5 SAU (Systolic Array Unit) 仿真配置脚本

这个脚本演示了如何配置和运行SAU模块，包括：
- KuiSau: 优化的脉动阵列处理单元
- CsrGen: CSR指令生成器
- 内存系统和总线配置

对应SAU.py中的指令集仿真逻辑

使用方法:
  gem5.opt configs/example/kui_sau_test.py [选项]
"""

import m5
from m5.objects import *
from m5.util import addToPath
import argparse

# ============================================================================
# 解析命令行参数
# ============================================================================

parser = argparse.ArgumentParser(description='SAU仿真配置')
parser.add_argument('--mem-size', default='1GB', help='内存大小 (默认: 1GB)')
parser.add_argument('--csr-interval', type=int, default=10, 
                    help='CSR请求间隔 (周期)')
parser.add_argument('--max-csr-requests', type=int, default=100,
                    help='最大CSR请求数 (0=无限)')
parser.add_argument('--unit-size', type=int, default=16,
                    help='SAU矩阵大小 (8 or 16)')
args = parser.parse_args()


# ============================================================================
# 系统配置
# ============================================================================

class SauSystem(System):
    """包含SAU模块的系统"""
    
    def __init__(self, mem_size):
        super().__init__()
        
        # 时钟和电源域
        self.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
        
        # 系统总线
        self.membus = SystemXBar()
        
        # 创建SAU模块
        self.seu = KuiSau(
            clk_domain=self.clk_domain,
            csr_addr_range=AddrRange(0x2f000000, size=0x1000),
            csr_latency=5,  # 延迟周期数
            rng_seed=0
        )
        
        # 创建CSR指令生成器
        self.csr_gen = CsrGen(
            clk_domain=self.clk_domain,
            interval=args.csr_interval,
            max_requests=args.max_csr_requests
        )
        
        # 连接SAU的内存端口到总线
        self.seu.port_KuiSau_sendto_mem = self.membus.cpu_side_ports
        
        # 连接CsrGen的CSR端口到SAU的CSR端口
        self.csr_gen.csr_port = self.seu.port_KuiSau_getfrm_mem
        
        # 创建内存控制器
        self.mem_ctrl = MemCtrl(dram=DDR3_1600_8x8())
        self.mem_ctrl.dram.range = AddrRange(mem_size)
        self.membus.mem_side_ports = self.mem_ctrl.port


# ============================================================================
# 主程序
# ============================================================================

def main():
    """主程序入口"""
    
    # 创建根对象和系统
    system = SauSystem(args.mem_size)
    root = Root(full_system=False, system=system)
    
    # 实例化仿真
    m5.instantiate()
    
    # 打印配置信息
    print("\n" + "="*80)
    print("  SAU (Systolic Array Unit) Gem5 Simulation")
    print("="*80)
    print(f"系统时钟频率:      1 GHz")
    print(f"内存大小:          {args.mem_size}")
    print(f"SAU矩阵大小:      {args.unit_size}x{args.unit_size}")
    print(f"SAU CSR地址范围:  0x{0x2f000000:x} - 0x{0x2f001000:x}")
    print(f"CSR请求间隔:      {args.csr_interval} 周期")
    print(f"最大CSR请求数:    {args.max_csr_requests if args.max_csr_requests > 0 else '无限'}")
    print("="*80 + "\n")
    
    # 运行仿真
    print("启动仿真...\n")
    exit_event = m5.simulate()
    
    # 输出仿真结果
    print("\n" + "="*80)
    print(f"仿真完成")
    print("="*80)
    print(f"退出事件:          {exit_event.getCause()}")
    print(f"总仿真时间:        {m5.curTick():.0f} ticks ({m5.curTick() / 1e9:.6f} ns)")
    print(f"总仿真周期:        {m5.curTick() // 1000:.0f} cycles @ 1GHz")
    print("="*80 + "\n")


if __name__ == '__m5_main__':
    main()
