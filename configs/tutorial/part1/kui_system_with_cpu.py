import m5
from m5.objects import *
from m5.util import addToPath
import os

addToPath('../../../')

# 简单RISC-V系统，包含CPU和KuiSau
system = System()
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=VoltageDomain(),
)

# CPU配置
system.cpu = RISCVCPU(cpu_id=0)
system.cpu.clk_domain = system.clk_domain

# 内存总线 (用于通常的数据访问)
system.membus = SystemXBar(
    width=8,
    frontend_latency=0,
    forward_latency=0,
    response_latency=0,
)

# CSR总线 (用于控制寄存器访问)
system.csrbus = SystemXBar(
    width=8,
    frontend_latency=0,
    forward_latency=0,
    response_latency=0,
)

# KuiSau模块
system.kuisau = KuiSau(
    rng_seed=1234,
    clk_domain=system.clk_domain,
)

# 连接CPU数据端口到内存总线
system.cpu.dcache_port = system.membus.cpu_side_ports

# 连接KuiSau数据端口（用于读写矩阵数据）到内存总线
system.kuisau.port_KuiSau_sendto_mem = system.membus.cpu_side_ports

# 连接KuiSau的CSR端口到CSR总线
# 注意：这允许CPU通过专用CSR总线访问KuiSau的控制寄存器
system.kuisau.port_KuiSau_getfrm_mem = system.csrbus.cpu_side_ports

# CPU的CSR访问端口
system.cpu.csr_port = system.csrbus.cpu_side_ports

# 主内存
system.physmem = SimpleMemory(
    range=AddrRange("256MB"),
    bandwidth="100GiB/s",
    latency="50ns",
)
system.physmem.port = system.membus.mem_side_ports

# 设置根对象
root = Root(full_system=False, system=system)

m5.instantiate()

print("=" * 70)
print("KuiSau CSR Test Configuration")
print("=" * 70)
print(f"CPU: {system.cpu}")
print(f"KuiSau CSR Address Range: {system.kuisau.csr_addr_range}")
print(f"KuiSau CSR Latency: {system.kuisau.csr_latency} cycles")
print(f"Clock Frequency: 1 GHz")
print("=" * 70)
print("Starting simulation...")

# 运行仿真
exit_event = m5.simulate(1000000)

print("=" * 70)
print(f"Simulation exited @ tick {m5.curTick()}")
print(f"Exit reason: {exit_event.getCause()}")
print("=" * 70)
