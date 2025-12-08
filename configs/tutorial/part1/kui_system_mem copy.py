import m5
from m5.objects import *

# system
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())

# crossbar
system.XBAR = venus_lane_mem_xbar(clk_domain=system.clk_domain,forward_latency=0,response_latency=0,frontend_latency=0,width=8)

# SAU
system.KuiSau= KuiSau(clk_domain=system.clk_domain)
system.XBAR.cpu_side_ports[0] =  system.KuiSau.port_KuiSau_sendto_mem

# memory
memory_size0 = '16MB'
system.physmem0 = SimpleMemory(range=AddrRange(start=0, size=memory_size0))
system.XBAR.mem_side_ports[0] =  system.physmem0.port

# root node
root = Root(full_system=False, system=system)

# 执行构建对象
m5.instantiate()

# 开始仿真
print("Beginning simulation!")
exit_event = m5.simulate(100000)
print(f'Exiting @ tick {m5.curTick()} because {exit_event.getCause()}')
