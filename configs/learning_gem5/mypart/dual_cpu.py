#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gem5配置脚本：双CPU + 双SRAM + Crossbar系统 (ARM版本 - 简化版)
不需要外部二进制文件，使用gem5内建的简单测试
"""

import m5
from m5.objects import *
import os

class DualCPUCrossbarSystem(System):
    """双CPU通过Crossbar连接双SRAM的系统"""
    
    def __init__(self):
        super(DualCPUCrossbarSystem, self).__init__()
        
        # 基本系统配置
        self.clk_domain = SrcClockDomain()
        self.clk_domain.clock = '1GHz'
        self.clk_domain.voltage_domain = VoltageDomain()
        
        # 内存配置
        self.mem_mode = 'timing'
        # 总地址空间设为1MB，覆盖两个512KB SRAM
        self.mem_ranges = [AddrRange(1024*1024)]  # 1MB = 1048576 bytes
        
        # 创建两个CPU (TimingSimpleCPU)
        self.cpu0 = TimingSimpleCPU()
        self.cpu1 = TimingSimpleCPU()
        
        # 创建Crossbar
        self.membus = SystemXBar()
        
        # 连接CPU到Crossbar
        self.cpu0.icache_port = self.membus.cpu_side_ports
        self.cpu0.dcache_port = self.membus.cpu_side_ports
        self.cpu1.icache_port = self.membus.cpu_side_ports
        self.cpu1.dcache_port = self.membus.cpu_side_ports
        
        # 创建两个512KB的SRAM (使用SimpleMemory模拟)
        # SRAM 0: 地址范围 0x00000000 - 0x0007FFFF (512KB = 524288 bytes)
        self.sram0 = SimpleMemory()
        self.sram0.range = AddrRange(0x00000000, size=16*1024*1024)
        self.sram0.latency = '1ns'
        self.sram0.bandwidth = '128GiB/s'  # 使用GiB避免警告
        
        # SRAM 1: 地址范围 0x00080000 - 0x000FFFFF (512KB = 524288 bytes)
        self.sram1 = SimpleMemory()
        self.sram1.range = AddrRange(0x01000000, size=16*1024*1024)
        self.sram1.latency = '1ns'
        self.sram1.bandwidth = '128GiB/s'
        
        # 连接SRAM到Crossbar
        self.sram0.port = self.membus.mem_side_ports
        self.sram1.port = self.membus.mem_side_ports
        
        # 创建中断控制器 (SE模式需要)
        self.cpu0.createInterruptController()
        self.cpu1.createInterruptController()
        
        # 系统端口连接
        self.system_port = self.membus.cpu_side_ports


def find_test_binary():
    """查找可用的测试二进制文件"""
    # 可能的测试程序路径
    possible_paths = [
        'tests/test-progs/hello/bin/arm/linux/hello'
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            print(f"找到测试程序: {path}")
            return path
    
    # 如果都找不到，返回None
    return None


def create_simple_process():
    process = Process()
    binary = find_test_binary()
    
    if binary:
        process.executable = binary  # 改这里
        process.cmd = [binary]
    else:
        print("警告: 未找到测试程序")
        process.executable = '/bin/echo'
        process.cmd = ['/bin/echo', 'hello']
    
    return process


def create_system():
    """创建并返回配置好的系统"""
    system = DualCPUCrossbarSystem()
    return system



def set_workload(system):
    binary = find_test_binary()
    if not binary:
        print("错误: 找不到测试程序")
        sys.exit(1)
    
    system.workload = SEWorkload.init_compatible(binary)
    
    # CPU0
    process0 = Process(pid=100)  # 指定不同PID
    process0.cmd = [binary]
    system.cpu0.workload = process0
    system.cpu0.createThreads()
    
    # CPU1  
    process1 = Process(pid=101)  # 指定不同PID
    process1.cmd = [binary]
    system.cpu1.workload = process1
    system.cpu1.createThreads()


if __name__ == "__m5_main__":
    # 创建系统
    system = create_system()
    
    # 设置工作负载
    set_workload(system)
    
    # 创建根对象
    root = Root(full_system=False, system=system)
    
    # 实例化系统
    m5.instantiate()
    
    # 打印系统信息
    print("="*60)
    print("双CPU Crossbar系统配置:")
    print(f"CPU0: {system.cpu0}")
    print(f"CPU1: {system.cpu1}")
    print(f"SRAM0: {system.sram0.range}, Latency: {system.sram0.latency}")
    print(f"SRAM1: {system.sram1.range}, Latency: {system.sram1.latency}")
    print(f"Crossbar: {system.membus}")
    print("="*60)
    
    # 运行模拟
    print("开始模拟...")
    exit_event = m5.simulate()
    
    print(f"模拟结束: {exit_event.getCause()}")
    print(f"模拟时间: {m5.curTick()} ticks")