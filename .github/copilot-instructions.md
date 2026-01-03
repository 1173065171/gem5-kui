# gem5 Simulator AI Agent Instructions

## Project Overview

gem5 is a modular platform for computer-system architecture research with a unique **dual-layer architecture**: Python configuration layer (`*.py` SimObjects) maps to C++ implementation layer (`*.cc`/`*.hh`). Understanding this binding is critical.

## Core Architecture Patterns

### SimObject System (Python ↔ C++)
- **Python SimObjects** ([src/python/m5/SimObject.py](src/python/m5/SimObject.py)): Define component parameters and hierarchy
- **C++ Implementation**: Each Python SimObject has corresponding C++ class via `cxx_header` and `cxx_class` attributes
- **Parameter binding**: Python `Param.*` types auto-generate C++ param structs in `build/{ISA}/params/`
- **Port system**: `RequestPort`/`ResponsePort` in C++ connect via Python port declarations (see [src/mem/port.hh](src/mem/port.hh))

Example pattern from [src/cpu/BaseCPU.py](src/cpu/BaseCPU.py):
```python
class BaseCPU(ClockedObject):
    type = "BaseCPU"
    cxx_header = "cpu/base.hh"
    cxx_class = "gem5::BaseCPU"
    # Python params become C++ BaseCPUParams struct
```

### Build System (SCons-based)
- **ISA-specific builds**: Each ISA compiled separately into `build/{ISA}/gem5.{variant}`
- **Build variants**: 
  - `.opt` - optimized with debug symbols (default for development)
  - `.debug` - debug info, no optimization, tracing enabled
  - `.fast` - fully optimized, no debug/tracing
- **ISA targets**: `ARM`, `RISCV`, `X86`, `SPARC`, `MIPS`, `POWER`, `NULL`, `VEGA_X86`
- **Build all ISAs**: `scons build/ALL/gem5.opt`

### Source Registration (SConscript files)
Key patterns in `src/**/SConscript`:
- `Source('file.cc')` - Register C++ compilation unit
- `SimObject('File.py', sim_objects=['ClassName'])` - Register Python→C++ binding
- `env['CONF']['USE_{ISA}_ISA']` - Check ISA conditional compilation

## Critical Developer Workflows

### Building
```bash
# Build specific ISA
scons build/RISCV/gem5.opt -j$(nproc)

# Build with all ISAs
scons build/ALL/gem5.opt -j$(nproc)

# Build unit tests
scons build/ALL/unittests.opt
```

### Running Simulations
```bash
# Basic execution pattern
./build/{ISA}/gem5.{variant} configs/path/to/config.py

# Output goes to m5out/ by default
# View stats: cat m5out/stats.txt
```

### Testing Requirements
**MANDATORY before pull requests:**
```bash
# 1. C++ unit tests
scons build/ALL/unittests.opt

# 2. Python unit tests
./build/ALL/gem5.opt tests/run_pyunit.py

# 3. System-level tests (minimum)
cd tests && ./main.py run  # Runs quick tests for X86, ARM, RISC-V
```

## Configuration System Conventions

### Config Script Structure
Standard pattern (see [configs/tutorial/part1/kui_csrgen_test.py](configs/tutorial/part1/kui_csrgen_test.py)):
```python
import m5
from m5.objects import *

# 1. Create system with clock/voltage domains
system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())

# 2. Create memory bus
system.membus = SystemXBar()

# 3. Instantiate SimObjects and connect ports
system.component.port = system.membus.cpu_side_ports

# 4. Create Root and run
root = Root(full_system=False, system=system)
m5.instantiate()
exit_event = m5.simulate()
```

### Port Connections
- **CPU→Memory**: Component's `RequestPort` → Bus's `cpu_side_ports` → Memory's `ResponsePort`
- **Naming**: Python port attributes map to C++ port names (must match exactly)
- **Connection syntax**: `object.port_name = target.port_name`

## Directory Structure Guide

- `src/` - C++ implementation organized by subsystem
  - `src/arch/{isa}/` - ISA-specific code (guarded by `USE_{ISA}_ISA`)
  - `src/cpu/` - CPU models (O3, timing, atomic)
  - `src/mem/` - Memory system, caches, buses
  - `src/sim/` - Core simulation framework
  - `src/python/` - Python→C++ bindings (pybind11)
- `configs/` - Example/tutorial configuration scripts
  - `configs/common/` - Reusable config components
  - `configs/example/` - Complete simulation examples
- `build/{ISA}/` - Generated build artifacts (params, headers, binaries)
- `tests/` - Test framework (`./main.py run` entry point)

## Kconfig Integration

gem5 uses Kconfig for build configuration ([KCONFIG.md](KCONFIG.md)):
- Settings in `env['CONF']` dict available to C++ via auto-generated headers
- Use `cont_choice` for extensible mutually-exclusive options
- Kconfig files define compile-time feature selection

## Adding New SimObjects

1. Create `src/subsystem/Component.py`:
   ```python
   class Component(SimObject):
       type = "Component"
       cxx_header = "subsystem/component.hh"
       cxx_class = "gem5::Component"
       param_name = Param.Type(default, "Description")
   ```

2. Create `src/subsystem/component.{hh,cc}` with matching class

3. Register in `src/subsystem/SConscript`:
   ```python
   SimObject('Component.py', sim_objects=['Component'])
   Source('component.cc')
   ```

4. Build regenerates param headers automatically

## Common Pitfalls

- **Port direction confusion**: `RequestPort` initiates requests, `ResponsePort` responds (formerly Master/Slave)
- **ISA guards**: Always check `env['CONF']['USE_{ISA}_ISA']` in SConscript for ISA-specific code
- **Python import order**: `from m5.objects import *` must come after `import m5`
- **Build artifacts**: Clean build dir when changing SimObject parameters: `rm -rf build/{ISA}`
- **Configuration vs. implementation**: Python configs are declarative; C++ provides behavior

## Key Files for Understanding Internals

- [src/python/m5/SimObject.py](src/python/m5/SimObject.py) - SimObject metaclass and registration
- [src/sim/sim_object.hh](src/sim/sim_object.hh) - C++ SimObject base class
- [src/SConscript](src/SConscript) - Master build file with Source/SimObject definitions
- [SConstruct](SConstruct) - Top-level build logic
- [build_tools/sim_object_param_struct_cc.py](build_tools/sim_object_param_struct_cc.py) - Generates C++ param structs

## Documentation & Help

- Main docs: <http://www.gem5.org/documentation>
- Contributing: [CONTRIBUTING.md](CONTRIBUTING.md) - GitHub PR workflow
- Testing: [TESTING.md](TESTING.md) - Comprehensive test guide
- Build info: <http://www.gem5.org/documentation/general_docs/building>
