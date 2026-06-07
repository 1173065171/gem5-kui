#!/usr/bin/env python3
"""
KuiSau deterministic normal-convolution rich-data golden-reference test.

This config runs conv_kernel=2 with register_mode=0. SauGoldenGen writes
multiple signed A lanes and distinct signed B kernel rows, then checks the
SAU.py-derived D bytes after normal-conv staging and int8 output packing.
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
    test_case="normal_conv_rich",
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
print("KuiSau Golden Normal-Conv Rich-Data Test")
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
