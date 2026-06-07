#!/usr/bin/env python3
"""
KuiSau trace replay for the external lkssfull_sau_stdconv/10 RTL smoke case.

This config does not validate functional D bytes. It starts KuiSau with a named
RTL request-trace replay mode, waits until busy clears, and exits so
util/sau_trace_summary.py can compare the gem5 request stats against the RTL
CSV summary.
"""

import m5
from m5.objects import *


system = System()
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=VoltageDomain(),
)

system.membus = SystemXBar(
    width=8,
    frontend_latency=0,
    forward_latency=0,
    response_latency=0,
)

system.kuisau = KuiSau(
    rng_seed=1234,
    clk_domain=system.clk_domain,
    csr_addr_range=AddrRange(0x2f000000, size=0x1000),
    csr_latency=1,
    trace_replay="lkssfull_sau_stdconv_10",
)
system.kuisau.port_KuiSau_sendto_mem = system.membus.cpu_side_ports

system.golden = SauGoldenGen(
    clk_domain=system.clk_domain,
    interval=1,
    poll_interval=50,
    max_busy_polls=100000,
    test_case="lkssfull_sau_stdconv_10_trace",
)
system.golden.mem_port = system.membus.cpu_side_ports
system.golden.csr_port = system.kuisau.port_KuiSau_getfrm_mem

system.physmem = SimpleMemory(
    range=AddrRange("2GB"),
    bandwidth="100GiB/s",
    latency="50ns",
)
system.physmem.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)
m5.instantiate()

print("=" * 80)
print("KuiSau lkssfull_sau_stdconv_10 Trace Replay Test")
print("=" * 80)
print("CSR range: 0x2F000000-0x2F000FFF")
print("Trace replay: lkssfull_sau_stdconv_10")
print("Expected request order per flow: B,A,C,A,D")
print("=" * 80)

exit_event = m5.simulate(200_000_000)

print("\n" + "=" * 80)
print("Simulation Complete")
print("=" * 80)
print(f"Exit Tick: {m5.curTick()}")
print(f"Exit Reason: {exit_event.getCause()}")
print(f"Exit Code: {exit_event.getCode()}")
print("=" * 80)
