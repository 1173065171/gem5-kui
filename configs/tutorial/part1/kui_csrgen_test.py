#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KuiSau + CsrGen 集成测试配置

展示 CsrGen 如何向 KuiSau 的 CSR 端口发送指令
"""

import m5
from m5.objects import *

# ============================================================================
# 创建系统
# ============================================================================
system = System()

# 时钟域
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=VoltageDomain(),
)

# 主系统总线
system.membus = SystemXBar(
    width=8,
    frontend_latency=0,
    forward_latency=0,
    response_latency=0,
)

# ============================================================================
# 创建 KuiSau 计算模块
# ============================================================================
system.kuisau = KuiSau(
    rng_seed=1234,
    clk_domain=system.clk_domain,
    csr_latency=1,
)

# 连接 KuiSau 的内存端口到总线
system.kuisau.port_KuiSau_sendto_mem = system.membus.cpu_side_ports

# ============================================================================
# 创建 CsrGen 指令生成器
# ============================================================================
system.csrgen = CsrGen(
    clk_domain=system.clk_domain,
    interval=10,        # 每 10 个周期发送一个 CSR 指令
    max_requests=12,    # 发送 12 个指令后停止（6 对读写）
)

# 连接 CsrGen 的 CSR 端口到 KuiSau 的 CSR 响应端口
system.kuisau.port_KuiSau_getfrm_mem = system.csrgen.csr_port

# ============================================================================
# 创建内存系统
# ============================================================================
system.physmem = SimpleMemory(
    range=AddrRange("2GB"),
    bandwidth="100GiB/s",
    latency="50ns",
)
system.physmem.port = system.membus.mem_side_ports

# ============================================================================
# 创建根对象
# ============================================================================
root = Root(full_system=False, system=system)

# 初始化 m5
m5.instantiate()

# ============================================================================
# 打印配置信息
# ============================================================================
print("=" * 80)
print("KuiSau + CsrGen Integration Test")
print("=" * 80)
print(f"System Frequency: 1 GHz")
print(f"KuiSau CSR Address: 0x2F000000")
print(f"CsrGen Interval: 10 cycles")
print(f"System Memory: 0x00000000-0x80000000 (2GB)")
print("=" * 80)
print("Starting simulation...\n")

# ============================================================================
# 运行仿真
# ============================================================================
exit_event = m5.simulate(100000)

# ============================================================================
# 打印结果
# ============================================================================
print("\n" + "=" * 80)
print("Simulation Complete")
print("=" * 80)
print(f"Exit Tick: {m5.curTick()}")
print(f"Exit Reason: {exit_event.getCause()}")
print("=" * 80)
