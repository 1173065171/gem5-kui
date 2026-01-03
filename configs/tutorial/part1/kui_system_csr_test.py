import m5
from m5.objects import *

# ============================================================================
# KuiSau CSR Test Configuration
# ============================================================================

# Create the system
system = System()
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=VoltageDomain(),
)

# Main system bus
system.membus = SystemXBar(
    width=8,
    frontend_latency=0,
    forward_latency=0,
    response_latency=0,
)

# Create KuiSau module
system.kuisau = KuiSau(
    rng_seed=1234,
    clk_domain=system.clk_domain,
    csr_latency=1,
)

# Connect KuiSau's data port (for matrix operations) to main bus
system.kuisau.port_KuiSau_sendto_mem = system.membus.cpu_side_ports

# Main memory - 2GB
system.physmem = SimpleMemory(
    range=AddrRange("2GB"),
    bandwidth="100GiB/s",
    latency="50ns",
)
system.physmem.port = system.membus.mem_side_ports

# Create root object
root = Root(full_system=False, system=system)

# Instantiate m5
m5.instantiate()

# Print information
print("=" * 80)
print("KuiSau CSR Test Simulation")
print("=" * 80)
print(f"System Frequency: 1 GHz")
print(f"KuiSau CSR Address: 0x2F000000 (4 bytes)")
print(f"System Memory: 0x00000000-0x80000000 (2GB)")
print("=" * 80)
print("Starting simulation...\n")

# Run the simulation for a fixed number of cycles
exit_event = m5.simulate(1000000)

# Print results
print("\n" + "=" * 80)
print("Simulation Complete")
print("=" * 80)
print(f"Exit Tick: {m5.curTick()}")
print(f"Exit Reason: {exit_event.getCause()}")
print("=" * 80)
