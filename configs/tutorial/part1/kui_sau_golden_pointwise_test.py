#!/usr/bin/env python3
"""
KuiSau deterministic pointwise-convolution golden-reference test.

This config runs conv_kernel=1 with work_mode=1. SauGoldenGen lays out A as an
identity matrix, writes an all-ones C/bias vector, and checks that the D bytes
match B incremented by one after SAU.py-style output packing.
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
)
system.kuisau.port_KuiSau_sendto_mem = system.membus.cpu_side_ports

system.golden = SauGoldenGen(
    clk_domain=system.clk_domain,
    interval=1,
    poll_interval=20,
    max_busy_polls=1000,
    test_case="pointwise",
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
print("KuiSau Golden Pointwise Conv Test")
print("=" * 80)
print("A/B/C/D base: 0x20000000 / 0x20001000 / 0x20002000 / 0x20003000")
print("CSR range: 0x2F000000-0x2F000FFF")
print("=" * 80)

exit_event = m5.simulate(10_000_000)

print("\n" + "=" * 80)
print("Simulation Complete")
print("=" * 80)
print(f"Exit Tick: {m5.curTick()}")
print(f"Exit Reason: {exit_event.getCause()}")
print(f"Exit Code: {exit_event.getCode()}")
print("=" * 80)
