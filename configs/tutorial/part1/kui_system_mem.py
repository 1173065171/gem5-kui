import m5
from m5.objects import *

system = System()
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=VoltageDomain(),
)

# 官方 crossbar
system.membus = SystemXBar(
    width=8,
    frontend_latency=0,
    forward_latency=0,
    response_latency=0,
)

# SAU 模块
system.KuiSau = KuiSau(rng_seed=1234, clk_domain = system.clk_domain)

# SAU request 端连接到 XBar
system.KuiSau.port_KuiSau_sendto_mem = system.membus.cpu_side_ports

# Memory
system.physmem0 = SimpleMemory(
    range=AddrRange("16MB"),
    bandwidth="1GiB/s",
    latency="50ns",
)

system.physmem0.port = system.membus.mem_side_ports

root = Root(full_system=False, system = system)

m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate(200000)
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
