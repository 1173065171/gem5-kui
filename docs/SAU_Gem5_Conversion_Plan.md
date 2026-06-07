# SAU to gem5 Conversion Plan

This document records the step-by-step plan for converting the KuiLoong SAU
software and hardware design into the gem5 `KuiSau` simulation model.

## Goal

Make `src/sau/KuiSau.*` behave like the KuiLoong SAU functional model first,
then refine timing using the RTL structure. The software simulator is the
golden functional reference. The RTL is the architectural and timing reference.

## References

- Software golden model:
  `src/sau/reference/simulator/SAU/SAU.py`
- Hardware top/control path:
  `src/sau/reference/hardware/src/sa_element/SA_CORE.sv`
- Hardware CSR decoder:
  `src/sau/reference/hardware/src/sa_element/csr.sv`
- Hardware scheduler:
  `src/sau/reference/hardware/src/sa_element/scheduler.sv`
- Hardware address generator:
  `src/sau/reference/hardware/src/sa_element/mem_addr.sv`
- Hardware compute engine:
  `src/sau/reference/hardware/src/sa_execute/SA_ENGINE.sv`
- Hardware package:
  `src/sau/reference/hardware/src/sa_execute/SA_pkg.sv`
- Current gem5 model:
  `src/sau/KuiSau.hh`, `src/sau/KuiSau.cc`, `src/sau/KuiSau.py`

## Execution Plan

### Step 0: Reference Intake

Status: complete.

- Extract SAU RTL using the `mikui_dma` filelist as the authority.
- Extract the Python SAU simulator without Python cache files.
- Keep reference files under `src/sau/reference` and out of `SConscript`.

### Step 1: Mapping and Gap Analysis

Status: complete.

- Map software functions to RTL blocks and current gem5 functions.
- Identify semantic gaps before changing code.
- Record the mapping in `docs/SAU_Gem5_Design_Mapping.md`.

### Step 2: CSR Semantics

Status: in progress.

- Align gem5 CSR address decode with RTL `csr.sv` and Python `CSRaddr`.
- Preserve the 8 logical instruction words: INS1 through INS4, LSB/MSB.
- Fix start, busy/status readback, and write-during-processing behavior.
- Add a focused CSR test using `CsrGen` or a small Python config.

Progress:

- `processCsrPacket()` now has a CSR word decoder that supports RTL/Python
  logical CSR words and the existing legacy 4-byte MMIO slot layout.
- CSR write payloads are captured before gem5 converts write requests into
  write responses, so timing-mode CSR writes now update the target register.
- CSR reads now return the addressed instruction word instead of a single
  generic status value; INS4_LSB overlays bit 31 with the current running flag.
- `CsrGen` now writes and reads back all eight logical CSR words at
  `0x2f000200..0x2f000207`, with expected-value checking on read responses.
- Tutorial configs using `CsrGen` now expose a `0x1000`-byte CSR range so the
  logical CSR addresses are routable.
- `ConfigReg` address masks were aligned to the software model's 20-bit
  A/B/C/D fields.
- `flow_loop_times` now follows the software model directly; int16 mode advances
  `flow_i` by two instead of dividing the loop count during decode.
- `StatusReg` now includes `flow_k` and C-side access fields needed by the
  software model.
- `updateStatusFromConfig()` now has separate branches for normal/DW
  convolution, pointwise convolution, and GEMM.
- `configs/tutorial/part1/kui_csrgen_test.py` now runs long enough to cover all
  16 CSR write/read operations.

### Step 3: Functional Data Path

Status: in progress.

- Port the Python model semantics into C++:
  `update_status`, input window selection, preprocessing, matrix math,
  accumulation, bias, dequantization, output transpose, and output packing.
- Keep the first target functional and deterministic, not cycle-accurate.
- Make Python SAU output the golden reference for each test vector.

Progress:

- Input staging now sizes A/B buffers from `status.*_count * status.*_kernel`
  and stores memory response bytes as byte lanes.
- C/bias input now follows the software model's two-byte `uint16` packing and
  row reversal for 16-lane operation.
- Preprocessing now ports the key `SAU.py` conversions for the current
  16-lane model: `fliplr`, int8 sign extension, shift-mode int16 view/reshape,
  convolution A shift, depthwise B masking, transpose mode, and fixed A
  transpose before systolic compute.
- Bias add, dequant saturation, output transpose, and output byte packing now
  follow the software model more closely for int8 and int16 output modes.
- `SauGoldenGen` now provides deterministic gem5-vs-reference output checks for
  16x16 int8 GEMM, matrix-add, shift-mode GEMM, pointwise-convolution,
  cutbit/dequant GEMM, output-transpose, normal/depthwise rich-data convolution,
  normal/depthwise stride-mode, stride+shift, stride+dequant, and
  stride+shift+dequant convolution,
  depthwise-convolution,
  output-retain, and output-transpose-retain vectors. It initializes A/B/C/D
  memory as needed, programs the SAU CSRs, polls busy, reads D memory, and
  checks output bytes derived from the `SAU.py` data layout.
- Pointwise convolution with `work_mode=1` now enables C reads in gem5. The
  current pointwise vector reads two C segments, adds an all-ones int16 bias
  vector, and checks that the D bytes equal the B byte pattern incremented by
  one.
- Pointwise convolution with `work_mode=2` plus dequantization now has a mixed
  signed-data vector. `SauGoldenGen(test_case="pointwise_dequant_mixed")`
  combines `conv_kernel=1`, `INS1_MSB.cutbit=2`, alternating signed B columns,
  and all-one C bias to check that C is shifted into accumulator scale before
  signed int8 dequantization and output packing.
- A normal-convolution golden vector has been added as
  `SauGoldenGen(test_case="normal_conv")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_test.py`. It uses
  `conv_kernel=2`, `register_mode=0`, `shift_mode=0`, one nonzero A lane,
  zero C/bias data, and the fixed B row pattern. The RISCV gem5 build and
  golden run both pass.
- A normal-convolution rich-data vector has been added as
  `SauGoldenGen(test_case="normal_conv_rich")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_rich_test.py`. It uses
  `conv_kernel=2`, `register_mode=0`, `shift_mode=0`, multiple signed A lanes
  across the six conv read rows, distinct signed B kernel rows, and zero C/bias
  data. This covers convMatShift row merge and signed int8 matmul beyond a
  one-hot A-to-B-row selection. The RISCV gem5 build and golden run both pass.
- A normal-convolution shift-mode vector has been added as
  `SauGoldenGen(test_case="normal_conv_shift")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_shift_test.py`. It uses
  `conv_kernel=2`, `register_mode=0`, `shift_mode=1`, `flow_loop_times=2`, one
  nonzero A lane, zero C/bias data, and the fixed B row pattern. This covers the
  conv `int16` preprocessing path plus SAU.py-style 512-byte D output packing.
  The RISCV gem5 build and golden run both pass.
- A normal-convolution stride-mode vector has been added as
  `SauGoldenGen(test_case="normal_conv_stride")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_stride_test.py`. It uses
  `conv_kernel=2`, `register_mode=0`, `stride=1`, `shift_mode=0`, one nonzero A
  lane placed in input row 1, zero C/bias data, and the fixed B row pattern.
  This covers the stride A-window merge and checks that output moves to D row 8.
  The RISCV gem5 build and golden run both pass.
- A normal-convolution combined stride+shift vector has been added as
  `SauGoldenGen(test_case="normal_conv_stride_shift")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_stride_shift_test.py`.
  It uses `conv_kernel=2`, `register_mode=0`, `stride=1`, `shift_mode=1`,
  `flow_loop_times=2`, one nonzero A lane placed in input row 1, zero C/bias
  data, and the fixed B row pattern. This covers stride A-window merge combined
  with 512-byte int16 D output packing. The RISCV gem5 build and golden run both
  pass.
- A normal-convolution combined stride+dequant vector has been added as
  `SauGoldenGen(test_case="normal_conv_stride_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_stride_dequant_mixed_test.py`.
  It uses `conv_kernel=2`, `register_mode=0`, `stride=1`, `work_mode=2`,
  `INS1_MSB.cutbit=2`, one nonzero A lane placed in input row 1, alternating
  signed B columns, and all-one C bias. This covers stride row selection
  combined with C scaling and signed int8 dequantized output packing. The RISCV
  gem5 build and golden run both pass.
- A normal-convolution combined stride+shift+dequant vector has been added as
  `SauGoldenGen(test_case="normal_conv_stride_shift_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_stride_shift_dequant_mixed_test.py`.
  It uses `conv_kernel=2`, `register_mode=0`, `stride=1`, `shift_mode=1`,
  `work_mode=2`, `INS1_MSB.cutbit=2`, `flow_loop_times=2`, one nonzero A lane
  placed in input row 1, alternating signed B columns, and all-one C bias. This
  covers stride row selection composed with C scaling, signed dequantization, and
  the 512-byte int16 output layout. The RISCV gem5 build and golden run both
  pass.
- A normal-convolution mixed-sign dequantization vector has been added as
  `SauGoldenGen(test_case="normal_conv_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_normal_conv_dequant_mixed_test.py`.
  It uses `conv_kernel=2`, `register_mode=0`, `work_mode=2`,
  `INS1_MSB.cutbit=2`, one nonzero A lane, alternating signed B columns, and
  all-one C bias. This covers normal-conv staging combined with C scaling and
  signed int8 dequantized output packing. The RISCV gem5 build and golden run
  both pass.
- A depthwise-convolution golden vector has been added as
  `SauGoldenGen(test_case="depthwise_conv")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_test.py`. It uses the
  same one-hot A and zero C/bias setup as `normal_conv`, sets
  `register_mode=2`, and checks the DW B-column mask output. The RISCV gem5
  build and golden run both pass.
- A depthwise-convolution rich-data vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_rich")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_rich_test.py`. It reuses
  the rich normal-conv A/B data with `register_mode=2`, proving the DW B-column
  mask keeps the selected column across multiple output rows while zeroing the
  other packed columns. The RISCV gem5 build and golden run both pass.
- A depthwise-convolution shift-mode vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_shift")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_shift_test.py`. It uses
  `conv_kernel=2`, `register_mode=2`, `shift_mode=1`, `flow_loop_times=2`, one
  nonzero A lane, zero C/bias data, and the fixed B row pattern. This covers the
  conv `int16` preprocessing path, DW B-column mask, and SAU.py-style 512-byte
  D output packing. The RISCV gem5 build and golden run both pass.
- A depthwise-convolution stride-mode vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_stride")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_test.py`. It
  uses `conv_kernel=2`, `register_mode=2`, `stride=1`, `shift_mode=0`, one
  nonzero A lane placed in input row 1, zero C/bias data, and the fixed B row
  pattern. This covers the stride A-window merge together with the DW B-column
  mask and checks that only byte 15 of D row 8 remains set. The RISCV gem5 build
  and golden run both pass.
- A depthwise-convolution combined stride+shift vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_stride_shift")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_shift_test.py`.
  It uses `conv_kernel=2`, `register_mode=2`, `stride=1`, `shift_mode=1`,
  `flow_loop_times=2`, one nonzero A lane placed in input row 1, zero C/bias
  data, and the fixed B row pattern. This covers stride A-window merge plus the
  DW B-column mask in the 512-byte int16 D output layout. The RISCV gem5 build
  and golden run both pass.
- A depthwise-convolution combined stride+dequant vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_stride_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_dequant_mixed_test.py`.
  It uses `conv_kernel=2`, `register_mode=2`, `stride=1`, `work_mode=2`,
  `INS1_MSB.cutbit=2`, one nonzero A lane placed in input row 1, alternating
  signed B columns, and all-one C bias. This covers stride row selection, the DW
  B-column mask, C scaling, and signed int8 dequantized output packing. The
  RISCV gem5 build and golden run both pass.
- A depthwise-convolution combined stride+shift+dequant vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_stride_shift_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_shift_dequant_mixed_test.py`.
  It uses `conv_kernel=2`, `register_mode=2`, `stride=1`, `shift_mode=1`,
  `work_mode=2`, `INS1_MSB.cutbit=2`, `flow_loop_times=2`, one nonzero A lane
  placed in input row 1, alternating signed B columns, and all-one C bias. This
  covers stride row selection, the DW B-column mask, C scaling, signed
  dequantization, and the 512-byte int16 output layout. The RISCV gem5 build and
  golden run both pass.
- A depthwise-convolution mixed-sign dequantization vector has been added as
  `SauGoldenGen(test_case="depthwise_conv_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_depthwise_conv_dequant_mixed_test.py`.
  It uses `conv_kernel=2`, `register_mode=2`, `work_mode=2`,
  `INS1_MSB.cutbit=2`, one nonzero A lane, alternating signed B columns, and
  all-one C bias. This covers the DW B-column mask combined with C scaling and
  signed int8 dequantized output packing. The RISCV gem5 build and golden run
  both pass.
- A two-instruction output-retain vector has been staged as
  `SauGoldenGen(test_case="gemm_retain")` plus
  `configs/tutorial/part1/kui_sau_golden_retain_test.py`. The model no longer
  resets `D_matrix` on every start write, so `flow_mode == 2/3` can preserve
  accumulated output until the output update step decides whether to clear it.
  The RISCV gem5 build and retain golden run both pass.
- A combined output-transpose plus retain vector has been added as
  `SauGoldenGen(test_case="gemm_transpose_retain")` plus
  `configs/tutorial/part1/kui_sau_golden_transpose_retain_test.py`. It uses
  `flow_mode=3` for the first GEMM, checks transposed output bytes, then runs a
  normal GEMM to prove the untransposed retained D matrix accumulated and was
  cleared. The RISCV gem5 build and golden run both pass.
- A cutbit/dequantization vector has been added as
  `SauGoldenGen(test_case="gemm_dequant")` plus
  `configs/tutorial/part1/kui_sau_golden_dequant_test.py`. It writes
  `INS1_MSB.cutbit=2`, uses all-one A rows with scaled B bytes, and checks both
  right-shift dequantization and int8 saturation. The RISCV gem5 build and
  golden run both pass.
- A negative cutbit/dequantization vector has been added as
  `SauGoldenGen(test_case="gemm_dequant_negative")` plus
  `configs/tutorial/part1/kui_sau_golden_dequant_negative_test.py`. It writes
  `INS1_MSB.cutbit=2`, uses all-one A rows with negative B bytes, and checks
  arithmetic right-shift dequantization plus negative int8 saturation. The
  RISCV gem5 build and golden run both pass.
- An int16 cutbit/dequantization vector has been added as
  `SauGoldenGen(test_case="gemm_dequant_int16")` plus
  `configs/tutorial/part1/kui_sau_golden_dequant_int16_test.py`. It combines
  `shift_mode=1` with `INS1_MSB.cutbit=2`, reuses the int16 identity A layout,
  and checks int16 right-shift dequantization with output byte packing. The
  RISCV gem5 build and golden run both pass.
- A pointwise mixed-sign dequantization vector has been added as
  `SauGoldenGen(test_case="pointwise_dequant_mixed")` plus
  `configs/tutorial/part1/kui_sau_golden_pointwise_dequant_mixed_test.py`. It
  covers `work_mode=2` C/bias scaling, signed B data, cutbit dequantization, and
  int8 output packing. The RISCV gem5 build and golden run both pass.

Remaining gaps:

- Functional equivalence is only proven for the small GEMM, matrix-add,
  shift-mode GEMM, positive/negative/int16 cutbit/dequant GEMM, pointwise+bias,
  pointwise mixed-sign dequant, output-transpose, normal-conv,
  normal-conv rich-data, normal-conv shift-mode, normal-conv stride-mode,
  normal-conv stride+shift, normal-conv mixed-sign dequant,
  normal-conv stride+dequant, normal-conv stride+shift+dequant,
  depthwise-conv, depthwise-conv rich-data, depthwise-conv shift-mode,
  depthwise-conv stride-mode, depthwise-conv stride+shift,
  depthwise-conv mixed-sign dequant, depthwise-conv stride+dequant, and
  depthwise-conv stride+shift+dequant vectors. More
  automated gem5-vs-`SAU.py` vectors are still needed for additional
  normal/depthwise modes and data shapes, broader mixed-mode shift/dequant
  variants, and unit-size/kernel coverage.
- A/B/C reads now issue per `count * kernel` segment using
  `base + i * step + j * unitSize`. D writeback now issues one OutputD write
  per output row and waits for all write responses before completing the flow.
  Request traces still need broader validation against `mem_addr.sv`.

### Step 4: Memory Transaction State Machine

Status: in progress.

- Replace the immediate read-then-compute flow with a response-driven FSM.
- Track pending A/B/C reads and D writes.
- Only compute after required memory responses have been received.
- Keep the current random ReadA/ReadB/Compute/Write smoke path isolated as a
  debug mode or remove it after functional tests cover memory connectivity.

Progress:

- Added `enable_random_traffic` to `KuiSau`; the standalone random memory FSM is
  disabled by default.
- `configs/tutorial/part1/kui_system_mem.py` explicitly enables random traffic,
  preserving the existing memory smoke test.
- CSR-focused configs now avoid random startup traffic noise by default.
- `executeFlow()` now issues tagged A/B/C memory reads and waits for read
  responses before preprocessing and computing.
- Final D writes are tagged and complete the flow only when the write response
  arrives.
- `KuiSauMemSidePort` now queues blocked packets, so multi-request flow issue is
  no longer limited to a single blocked packet.
- The memory port now waits for xbar retry before draining more queued packets,
  which allows multi-segment flow reads without re-entering the blocked layer.
- The old random ReadA/ReadB/Compute/Write smoke path now has an in-flight
  guard and advances from untagged read responses, so `enable_random_traffic`
  stays useful without flooding memory with repeated reads.
- `KuiSau` now registers a proper gem5 memory requestor id through its `System`
  parameter instead of using a fixed numeric requestor id.
- CSR-triggered A/B/C reads now issue one tagged memory request per logical
  software burst segment and reassemble responses by segment index before
  compute.
- Final D output now writes one tagged row segment per output row using
  `status.D_step`, and the SAU clears busy only after all D write responses have
  returned.
- Tagged MatrixA, MatrixB, VectorC, and OutputD traffic now records address
  summary stats (`first`, `last`, `sum`, and `xor`) so deterministic runs can
  check request sequences without parsing interleaved logs.

### Step 5: Functional Verification

Status: in progress.

- Add small deterministic tests:
  GEMM, pointwise convolution, normal convolution, depthwise convolution,
  matrix add, transpose modes, shift/dequant modes, and output retain modes.
- Compare gem5 output memory against the Python software simulator.
- Keep the smallest smoke config for fast connectivity checks.

Progress:

- Docker build passed:
  `docker exec -w /gem5/gem5-kui gem5-dev scons build/RISCV/gem5.opt -j2`.
- Added `SauGoldenGen` and
  `configs/tutorial/part1/kui_sau_golden_test.py` for deterministic functional
  output verification.
- Golden GEMM smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_test.py`.
- Golden matrix-add smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_add_test.py`.
- Golden shift-mode GEMM smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_shift_test.py`.
- Golden cutbit/dequantization smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_dequant ./configs/tutorial/part1/kui_sau_golden_dequant_test.py`.
- Golden negative cutbit/dequantization smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_dequant_negative ./configs/tutorial/part1/kui_sau_golden_dequant_negative_test.py`.
- Golden int16 cutbit/dequantization smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_dequant_int16 ./configs/tutorial/part1/kui_sau_golden_dequant_int16_test.py`.
- Golden pointwise-convolution smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_pointwise_test.py`.
- Golden pointwise mixed-sign dequantization smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_pointwise_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_pointwise_dequant_mixed_test.py`.
- Golden output-transpose smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_transpose_test.py`.
- Golden normal-convolution smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_normal_conv_test.py`.
- Golden normal-convolution rich-data smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_rich ./configs/tutorial/part1/kui_sau_golden_normal_conv_rich_test.py`.
- Golden normal-convolution shift-mode smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_shift ./configs/tutorial/part1/kui_sau_golden_normal_conv_shift_test.py`.
- Golden normal-convolution stride-mode smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_stride ./configs/tutorial/part1/kui_sau_golden_normal_conv_stride_test.py`.
- Golden normal-convolution stride+shift smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_stride_shift ./configs/tutorial/part1/kui_sau_golden_normal_conv_stride_shift_test.py`.
- Golden normal-convolution mixed-sign dequantization smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_normal_conv_dequant_mixed_test.py`.
- Golden normal-convolution stride+dequant smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_stride_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_normal_conv_stride_dequant_mixed_test.py`.
- Golden normal-convolution stride+shift+dequant smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_normal_conv_stride_shift_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_normal_conv_stride_shift_dequant_mixed_test.py`.
- Golden depthwise-convolution smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_test.py`.
- Golden depthwise-convolution rich-data smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_rich ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_rich_test.py`.
- Tagged address summary stats for the normal/depthwise rich-data conv runs
  matched the expected request sequences:
  A reads `0x20000000..0x20000030`, B reads `0x20001000..0x20001030`,
  C reads `0x20002000/0x20002010`, and D writes `0x20003000..0x200030f0`.
  Expected decimal first/last/sum/xor summaries are A
  `536870912/536870960/2147483744/0`, B
  `536875008/536875056/2147500128/0`, C
  `536879104/536879120/1073758224/16`, and D
  `536883200/536883440/8590133120/0`.
- Tagged address summary stats also matched fresh representative runs for
  stride, stride+shift, pointwise, and shift-mode GEMM:
  `m5out/sau_addr_normal_conv_stride`,
  `m5out/sau_addr_normal_conv_stride_shift`, `m5out/sau_addr_pointwise`, and
  `m5out/sau_addr_gemm_shift`. The normal-conv stride A summary is
  `536870912/536870976/3221225664/96`; stride+shift A is
  `536870912/536871024/6442451616/0`; pointwise A/B are
  `536870912/536871152/8589936512/0` and
  `536875008/536875248/8590002048/0`; GEMM shift A/B/C are
  `536870912/536871408/17179877120/0`,
  `536875008/536875248/8590002048/0`, and `0/0/0/0`. The shared shift-mode D
  summary is `536883200/536883696/17180270336/0`.
- Golden depthwise-convolution shift-mode smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_shift ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_shift_test.py`.
- Golden depthwise-convolution stride-mode smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_stride ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_test.py`.
- Golden depthwise-convolution stride+shift smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_stride_shift ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_shift_test.py`.
- Golden depthwise-convolution mixed-sign dequantization smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_dequant_mixed_test.py`.
- Golden depthwise-convolution stride+dequant smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_stride_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_dequant_mixed_test.py`.
- Golden depthwise-convolution stride+shift+dequant smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_depthwise_conv_stride_shift_dequant_mixed ./configs/tutorial/part1/kui_sau_golden_depthwise_conv_stride_shift_dequant_mixed_test.py`.
- Golden output-retain smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_retain ./configs/tutorial/part1/kui_sau_golden_retain_test.py`.
- Golden output-transpose-retain smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_golden_transpose_retain ./configs/tutorial/part1/kui_sau_golden_transpose_retain_test.py`.
- CSR smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_csrgen_test.py`.
- Random memory connectivity smoke passed:
  `docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_system_mem.py`.
- Static checks passed:
  `git diff --check` and `python3 -m py_compile` for the edited Python configs
  and SimObject definitions.

### Step 6: Timing Approximation

Status: in progress.

- Use RTL structure to estimate latency:
  `scheduler.sv` states, `mem_addr.sv` address delay, `feeder.sv` data staging,
  and `SA_ENGINE.sv` compute/storage behavior.
- Use `SA_pkg::get_exe_cycle` and software `repair_backward()` as early timing
  cross-checks.
- Add stats for memory reads, memory writes, flows, compute cycles, and total
  SAU busy cycles.

Progress:

- Added basic gem5 stats for CSR reads/writes, CSR-triggered flow
  starts/completions, tagged A/B/C read segments, tagged D write segments, and
  busy ticks.
- Added tagged address summary stats for MatrixA, MatrixB, VectorC, and OutputD
  requests: first address, last address, address sum, and address XOR. These are
  intended as the first machine-checkable request-trace evidence before broader
  RTL `mem_addr.sv` waveform comparison.
- Added global tagged request stats across A/B/C/D traffic:
  `flowTraceRequests`, `flowTraceAddrFirst`, `flowTraceAddrLast`,
  `flowTraceAddrSum`, `flowTraceAddrXor`, and `flowTraceOrderHash`. The order
  hash includes request kind plus address and is masked to 53 bits so gem5 stats
  output can be compared exactly.
- The address summaries have been checked against computed gem5 request
  sequences for rich normal/DW conv, normal-conv stride, normal-conv
  stride+shift, pointwise+bias, and shift-mode GEMM. This proves the current
  summary stats are strong enough to catch overlapping conv A reads, expanded
  shift-mode A reads, C/bias enable differences, and 16-row versus 32-row D
  writeback.
- The first RTL `mem_addr.sv` formula pass confirms that `SA_CORE` uses the
  same `0x20000000` base, that RTL horizontal=B and vertical=A map cleanly to
  gem5's logical B/A tags, and that output D address generation matches the
  current gem5 16-row and 32-row summary shapes for the default step settings.
  It also exposes two request-trace gaps to resolve before claiming RTL trace
  equivalence: RTL only issues bias/C reads when `pe_work_mode == CONV`
  (`work_mode=1`), while `SAU.py`/gem5 currently enable C reads for all
  normal/DW conv vectors; the current `work_mode=2` dequant/C-scaling vectors
  are software/gem5 functional coverage rather than RTL request-equivalence
  evidence; and RTL scheduler order may read horizontal/B before vertical/A,
  while gem5 currently batches A/B/C by request kind.
- Added an explicit `KuiSau.rtl_c_read_gate` parameter to resolve the C/bias
  gate as a controllable modeling choice. The default remains `False` so
  functional golden vectors follow `SAU.py`; when set to `True`, C/bias reads
  are gated like RTL `mem_addr.sv` and only issue for `work_mode=1`.
  `configs/tutorial/part1/kui_sau_golden_normal_conv_stride_rtl_trace_test.py`
  proves the trace mode on a zero-bias `normal_conv_stride` vector: the D output
  still matches, default mode reports 12 read segments and C
  `536879104/536879120/1073758224/16`, while RTL-gated mode reports 10 read
  segments and C `0/0/0/0`; A/B/D summaries are unchanged.
- RTL-gated request-shape coverage now also includes
  `normal_conv_stride_shift`, `pointwise`, and `gemm_shift` via
  `configs/tutorial/part1/kui_sau_golden_normal_conv_stride_shift_rtl_trace_test.py`,
  `configs/tutorial/part1/kui_sau_golden_pointwise_rtl_trace_test.py`, and
  `configs/tutorial/part1/kui_sau_golden_shift_rtl_trace_test.py`. The runs
  `m5out/sau_addr_normal_conv_stride_shift_rtl_cgate`,
  `m5out/sau_addr_pointwise_rtl_cgate`, and
  `m5out/sau_addr_gemm_shift_rtl_cgate` passed output checks and matched the
  expected stats: stride+shift drops C to `0/0/0/0` with 16 read segments,
  pointwise keeps C at `536879104/536879120/1073758224/16` with 34 read
  segments, and GEMM shift keeps C at `0/0/0/0` with 48 read segments. A/B/D
  summaries match the previously verified per-kind formulas.
- Global ordered trace stats were verified on the same RTL-gated representative
  shapes after rebuilding. The runs
  `m5out/sau_order_normal_conv_stride_rtl_cgate`,
  `m5out/sau_order_normal_conv_stride_shift_rtl_cgate`,
  `m5out/sau_order_pointwise_rtl_cgate`, and
  `m5out/sau_order_gemm_shift_rtl_cgate` passed output checks and matched an
  independent address-sequence checker. Their
  requests/first/last/sum/xor/hash values are:
  `26/536870912/536883440/13958858912/96/4451202002119331`,
  `48/536870912/536883696/25770222080/0/5719151375192195`,
  `50/536870912/536883440/26843829904/16/5827402699122387`, and
  `80/536870912/536883696/42950149504/0/3684773384763523`.
- Added `util/sau_trace_summary.py` as the reusable gem5/RTL memory-trace
  summary checker. It uses the same A/B/C/D request-kind values and 53-bit order
  hash as `KuiSau`, checks gem5 `stats.txt` against the RTL-gated representative
  cases, and can summarize RTL CSV/text traces. For raw RTL CSV export, provide
  either `kind/source` plus `addr`, or the signal-derived columns
  `sram_rd_enable`, `sram_wr_enable`, `sram_mem_addr`, `input_switch`, and
  `bias_rd_valid`; the tool maps vertical=A, horizontal=B, bias=C, writes=D.
- Added a non-invasive RTL trace capture path. External hardware inspection
  found the mikui DMA VCS case at `/home/zbn/code/npu_lpnpu/sim/vcs` and the
  `mem_addr` instance at
  `top_mikui_dma_tb.dut_mikui_inst.SAU_1_inst.mem_addr_inst`. The new
  `util/sau_mem_addr_trace_bind.sv` binds to module `mem_addr` and emits CSV
  rows with `cycle,kind,addr,sram_rd_enable,sram_wr_enable,input_switch_bit,bias_rd_valid`.
  `util/sau_prepare_rtl_trace_filelist.sh` copies the external case filelist
  into `build/sau_trace/mikui_dma_verilog_with_trace.f`, appends the bind
  monitor, and prints the VCS make override command. Detailed usage is recorded
  in `docs/SAU_RTL_Trace_Capture.md`.
- The RTL trace filelist helper now defaults to an existing SAU smoke testcase:
  `REGRESS_NAME=lkssfull_sau_stdconv` and
  `TESTCASE=10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0`. It also prints the
  `DW_HOME` and `VCS_TIMESCALE` exports needed by the external VCS makefile.
  The external VCS run now completes for that testcase: `simv` reports
  scoreboard `ALL TESTS PASSED`, NPU done, and `$finish` at `6036925 ns`. The
  bind monitor generated
  `/home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv`
  with 3568 requests. RTL request counts are A=2032, B=992, C=32, and D=512;
  global `requests/first/last/sum/xor/hash` is
  `3568/537022560/537021456/1916096721408/15360/8164653382730703`.
- Per-kind request counts were added to both gem5 and the trace summary path.
  `KuiSau` now records `flowReadARequests`, `flowReadBRequests`,
  `flowReadCRequests`, and `flowWriteDRequests`. `util/sau_trace_summary.py`
  compares those fields and includes a fixed external RTL case named
  `lkssfull_sau_stdconv_10`. The four RTL-gated gem5 representative runs were
  rebuilt/rerun with the new stats, and their request-count/address/hash
  summaries all match the tool expectations.
- GEMM and matrix-add golden runs now dump the expected first stats shape:
  8 CSR writes, 14 busy-poll CSR reads, 1 flow start/completion, 32 read
  segments, 16 D write segments, and 243001 busy ticks for the current
  deterministic 16x16 cases.
- The shift-mode GEMM run dumps 8 CSR writes, 19 busy-poll CSR reads, 1 flow
  start/completion, 48 read segments, 32 D write segments, and 339001 busy
  ticks. The read segment count covers 32 A segments plus 16 B segments, and
  the D write segment count covers 512 bytes of int16 output packing.
- The cutbit/dequantization GEMM run dumps 8 CSR writes, 14 busy-poll CSR reads,
  1 flow start/completion, 32 read segments, 16 D write segments, and 243001
  busy ticks. The vector writes `INS1_MSB.cutbit=2` and checks int8
  right-shift plus saturation in the packed D bytes.
- The negative cutbit/dequantization GEMM run dumps the same stats shape:
  8 CSR writes, 14 busy-poll CSR reads, 1 flow start/completion, 32 read
  segments, 16 D write segments, and 243001 busy ticks. The vector writes
  `INS1_MSB.cutbit=2` and checks arithmetic right shift plus negative int8
  saturation in the packed D bytes.
- The int16 cutbit/dequantization GEMM run dumps the same stats shape as the
  shift-mode GEMM: 8 CSR writes, 19 busy-poll CSR reads, 1 flow
  start/completion, 48 read segments, 32 D write segments, and 339001 busy
  ticks. The vector combines `shift_mode=1` with `INS1_MSB.cutbit=2` and checks
  int16 right-shift plus output byte packing.
- The pointwise-convolution run dumps 8 CSR writes, 14 busy-poll CSR reads,
  1 flow start/completion, 34 read segments, 16 D write segments, and 249001
  busy ticks. The read segment count covers 16 A segments, 16 B segments, and
  2 C/bias segments.
- The pointwise mixed-sign dequantization run dumps the same stats shape as the
  pointwise+bias vector: 8 CSR writes, 14 busy-poll CSR reads, 1 flow
  start/completion, 34 read segments, 16 D write segments, and 249001 busy
  ticks. It covers `work_mode=2` C scaling plus signed cutbit dequantization.
- The output-transpose run dumps 8 CSR writes, 14 busy-poll CSR reads, 1 flow
  start/completion, 32 read segments, 16 D write segments, and 243001 busy
  ticks for the current deterministic 16x16 case.
- The normal-convolution run dumps 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 10 read segments, 16 D write segments, and 177001 busy
  ticks. The read segment count covers 4 A segments, 4 B segments, and
  2 C/bias segments for the current `conv_kernel=2` vector.
- The normal-convolution rich-data run dumps the same stats shape as the base
  normal-conv vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 10 read segments, 16 D write segments, and 177001 busy
  ticks. It covers the same 4 A, 4 B, and 2 C/bias segment shape with multiple
  signed A lanes and distinct signed B kernel rows.
- The normal-convolution shift-mode run dumps 8 CSR writes, 14 busy-poll CSR
  reads, 1 flow start/completion, 14 read segments, 32 D write segments, and
  237001 busy ticks. The read segment count covers 8 A segments, 4 B segments,
  and 2 C/bias segments; the D write segment count covers 512 bytes of int16
  output packing.
- The normal-convolution stride-mode run dumps 8 CSR writes, 11 busy-poll CSR
  reads, 1 flow start/completion, 12 read segments, 16 D write segments, and
  183001 busy ticks. The read segment count covers 6 A segments, 4 B segments,
  and 2 C/bias segments; the output vector proves the stride merge moves the
  one-hot result to D row 8.
- The normal-convolution stride+shift run dumps 8 CSR writes, 14 busy-poll CSR
  reads, 1 flow start/completion, 18 read segments, 32 D write segments, and
  249001 busy ticks. The read segment count covers 12 A segments, 4 B segments,
  and 2 C/bias segments; the output vector proves stride row selection composes
  with 512-byte int16 D packing.
- The normal-convolution mixed-sign dequantization run dumps the same stats
  shape as the normal-conv vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 10 read segments, 16 D write segments, and 177001 busy
  ticks. It covers `work_mode=2` C scaling plus signed cutbit dequantization in
  the normal-conv staging path.
- The normal-convolution stride+dequant run dumps the same stats shape as the
  stride-mode vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 12 read segments, 16 D write segments, and 183001 busy
  ticks. It covers 6 A segments, 4 B segments, 2 C/bias segments, stride row
  selection, C scaling, and signed cutbit dequantization.
- The normal-convolution stride+shift+dequant run dumps the same stats shape as
  the stride+shift vector: 8 CSR writes, 14 busy-poll CSR reads, 1 flow
  start/completion, 18 read segments, 32 D write segments, and 249001 busy
  ticks. It covers 12 A segments, 4 B segments, 2 C/bias segments, stride row
  selection, C scaling, signed cutbit dequantization, and 512-byte int16 output
  packing.
- The depthwise-convolution run dumps the same first stats shape as the
  normal-conv vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 10 read segments, 16 D write segments, and 177001 busy
  ticks. The read segment count covers 4 A segments, 4 B segments, and
  2 C/bias segments; the output difference comes from the DW B-column mask.
- The depthwise-convolution rich-data run dumps the same stats shape as the base
  depthwise-conv vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 10 read segments, 16 D write segments, and 177001 busy
  ticks. It reuses the rich A/B segment shape under the DW B-column mask.
- The depthwise-convolution shift-mode run dumps the same stats shape as the
  normal-conv shift vector: 8 CSR writes, 14 busy-poll CSR reads, 1 flow
  start/completion, 14 read segments, 32 D write segments, and 237001 busy
  ticks. It covers 8 A segments, 4 B segments, 2 C/bias segments, and 512-byte
  int16 output packing under the DW B-column mask.
- The depthwise-convolution stride-mode run dumps the same stats shape as the
  normal-conv stride vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 12 read segments, 16 D write segments, and 183001 busy
  ticks. It covers 6 A segments, 4 B segments, 2 C/bias segments, stride row
  selection, and the DW B-column mask.
- The depthwise-convolution stride+shift run dumps the same stats shape as the
  normal-conv stride+shift vector: 8 CSR writes, 14 busy-poll CSR reads,
  1 flow start/completion, 18 read segments, 32 D write segments, and 249001
  busy ticks. It covers 12 A segments, 4 B segments, 2 C/bias segments, stride
  row selection, int16 output packing, and the DW B-column mask.
- The depthwise-convolution mixed-sign dequantization run dumps the same stats
  shape as the depthwise-conv vector: 8 CSR writes, 11 busy-poll CSR reads,
  1 flow start/completion, 10 read segments, 16 D write segments, and 177001
  busy ticks. It covers `work_mode=2` C scaling plus signed cutbit
  dequantization in the DW B-column mask path.
- The depthwise-convolution stride+dequant run dumps the same stats shape as the
  depthwise stride-mode vector: 8 CSR writes, 11 busy-poll CSR reads, 1 flow
  start/completion, 12 read segments, 16 D write segments, and 183001 busy
  ticks. It covers 6 A segments, 4 B segments, 2 C/bias segments, stride row
  selection, the DW B-column mask, C scaling, and signed cutbit dequantization.
- The depthwise-convolution stride+shift+dequant run dumps the same stats shape
  as the depthwise stride+shift vector: 8 CSR writes, 14 busy-poll CSR reads,
  1 flow start/completion, 18 read segments, 32 D write segments, and 249001
  busy ticks. It covers 12 A segments, 4 B segments, 2 C/bias segments, stride
  row selection, the DW B-column mask, C scaling, signed cutbit dequantization,
  and 512-byte int16 output packing.
- The output-retain run dumps 16 CSR writes, 28 busy-poll CSR reads, 2 flow
  starts/completions, 64 read segments, 32 D write segments, and 486002 busy
  ticks. It covers a `flow_mode=2` GEMM followed by a normal `flow_mode=0`
  GEMM that accumulates on the retained D matrix before clearing.
- The output-transpose-retain run dumps the same two-flow stats shape as
  output-retain: 16 CSR writes, 28 busy-poll CSR reads, 2 flow
  starts/completions, 64 read segments, 32 D write segments, and 486002 busy
  ticks. It covers a `flow_mode=3` GEMM followed by a normal `flow_mode=0`
  GEMM, proving transpose output and retained accumulation can coexist.

### Step 7: Documentation and Agent Notes

Status: pending.

- Update `docs/SAU_Integration_Guide.md` with the final supported behavior.
- Update `.agent` as the model architecture changes.
- Record limitations where gem5 intentionally remains functional instead of
  cycle-accurate.

## Current RTL Smoke Case Notes

The captured external RTL anchor is:

```text
/home/zbn/code/npu_lpnpu/testcase/lkssfull_sau_stdconv/10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0
/home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv
```

`simv` completed with scoreboard `ALL TESTS PASSED`, NPU done, and `$finish`
at `6036925 ns`. The CSV has 3568 requests with counts
`A=2032, B=992, C=32, D=512`; the fixed checker case is
`lkssfull_sau_stdconv_10`.

`util/sau_trace_summary.py --show-flows` shows 16 flows. Each flow has 223
requests with counts `B=62, A=127, C=2, D=32`; the request-kind run order
inside a flow is `B,A,C,A,D`. The final A request is at `0x20025040`, after
the two C/bias reads and before D writes. This is the next concrete mismatch to
model because the current gem5 representative flows issue tagged reads in
`A, B, C, D` order.

A shared gem5 request-stream path now captures this external smoke-case request
stream. `KuiSau(trace_replay="lkssfull_sau_stdconv_10")` and the ordinary
`executeFlow()` path behind `isLkssfullStdconv10Config()` both emit the RTL
request stream from decoded CSR fields for this fixture, with a 128-request
in-flight window. The first replay version used one artificial start for all 16
flows; it now uses 16 normconv-style CSR starts, matching the firmware structure
more closely. Each start maps one D offset `0x22c20 + n * 0x20` to one RTL flow
and derives B/C/D plus the late-A request from the current config. The A reuse
window after the late-A request is fixed at the first A base (`0x24c30`), which
matches the RTL trace across all 16 inner starts. This is intentionally an
address/order scheduler step; functional D bytes are still not checked for this
large firmware-derived case.

The current `SauGoldenGen(test_case="lkssfull_sau_stdconv_10_trace")` CSR words
are based on the disassembled `normconv_s16_16b` path:

- `ins1_lsb/ins1_msb = 0x0000002c / 0x00000088`
- `ins2_lsb/ins2_msb = 0x00020401 / 0x00101209`
- `ins3_lsb = 0x00025030`
- `ins3_msb = 0x00024c30 + n * 0x40`
- `ins4_lsb = ((0x22c20 + n * 0x20) << 9) | 1`
- `ins4_msb = 0x0e525010`

The decoded fields now match the active `normconv_s16_16b` branch at
`0x18c0..0x18cc`: `conv_kernel=3`, `register_mode=0`, `stride=0`,
`shift=1`, `cutbit=2`, `B_x/A_x/D_x=1/4/2`, `B_ch/A_ch/D_ch=9/18/16`,
`work_mode=1`, `flow_mode=1`, and `flow_loop_times=14`. The request-stream
helper now models the captured RTL `mem_addr.sv` scheduling for this exact
shape: CSR `B=0x25030` turns into first B request `0x20025060 + n*0x40`, CSR
`A=0x24c30 + n*0x40` turns into first A request
`0x20026c00 + n*0x40`, the following A reuse reads stay based at
`0x20024c30`, C reads stay at `0x20025010/0x20025030`, and D writes use two
16-byte rows per output row (`D + row*0x200`, `D + row*0x200 + 0x10`). The
general 16-lane functional scheduler now maps shift-mode packed output rows to
`D + logical_row*D_step + lane*unitSize`; the remaining writeback gaps are
unit-size 8 masking and full D-data checking for this large firmware-derived
stdconv case.

Verified commands:

```sh
docker exec -w /gem5/gem5-kui gem5-dev scons build/RISCV/gem5.opt -j2
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_lkssfull_stdconv_trace ./configs/tutorial/part1/kui_sau_lkssfull_stdconv_trace_test.py
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --gem5-stats m5out/sau_lkssfull_stdconv_trace/stats.txt
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_lkssfull_stdconv_scheduler ./configs/tutorial/part1/kui_sau_lkssfull_stdconv_scheduler_test.py
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --gem5-stats m5out/sau_lkssfull_stdconv_scheduler/stats.txt
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_regress_normal_conv_stride ./configs/tutorial/part1/kui_sau_golden_normal_conv_stride_test.py
```

The fixed checker reports all `lkssfull_sau_stdconv_10` gem5 trace and
scheduler stats OK, including `flowTraceOrderHash=8164653382730703`.

Firmware clues from the testcase:

- `_Z15sau_single_testv` is at `0x1d40`.
- `bias_sa/kernel_sa/output_sa/input_sa` are at
  `0x20010a54/0x20010a74/0x20010e64/0x20012e64`.
- The firmware calls `normconv_s16_16bPsS_PaS_iiiiiii` at `0x15f8`.
- The apparent call arguments are input, bias, kernel, output pointers, then
  scalars `18, 7, 3, 16` plus stack values `1, 14, 2`.
- The closest software source for CSR packing is
  `/home/zbn/code/toolchain_workdir/workdir/gitee_toolchain/kuiloong-NN/acenn/matrix/conv2d.hpp`.

`util/sau_lkssfull_fixture_data.py` parses this external testcase's
`elf_symbols.txt` and `globala.hex` into the firmware data blobs. The hex words
are little-endian memory words, and `__rodata_start__` is `0x20010000`. Current
blob summaries are:

- `bias_sa`: 32 bytes, SHA256
  `8f20a621587b281f64351aee82885ba74d94e37fc272ebfc20bc32c762b27c60`.
- `kernel_sa`: 1008 bytes, SHA256
  `db2cea75e874aa67cea4e4aeb9868328f0574dd3a6e41451330b08370ce046c7`.
- `output_sa`: 8192 bytes, SHA256
  `e1b19a4576eb5bf9a299a9f4944444f71c8ba2b5dbab8b1a9223286f2a8877f2`;
  this is 4096 signed int16 values, all nonzero, with range
  `-32768..32767`.
- `input_sa`: 8064 bytes, SHA256
  `fefdb0c0a2d2c464ea6a16c990a68362cd88877f4f87d7756020567c7121a6e4`.

The same helper also derives the first-fit heap layout used by the firmware:

- input buffer: `0x20025060..0x20026fdf`
- bias buffer: `0x20025030..0x2002504f`
- kernel buffer: `0x20024c30..0x2002501f`
- output buffer: `0x20022c20..0x20024c1f`

This confirms the firmware's full D-data oracle is available in rodata and that
the runtime D buffer aligns with the captured D trace span. The RTL read stream
does not map one-to-one onto simple `input/kernel/bias` names: the traced B
stream starts at the input buffer (`0x20025060`), the traced A reuse window is
the kernel buffer (`0x20024c30`), the late A read reaches the input tail
(`0x20026c00 + n*0x40`), and the two C reads are
`0x20025010/0x20025030`, spanning the kernel tail and bias start. The remaining
modeling work is to reproduce that firmware/RTL data layout in gem5 before
using `output_sa` as a functional D-data check.

With `--rtl-trace`, the same helper now reconstructs the payload stream behind
the captured RTL request trace:

```sh
python3 util/sau_lkssfull_fixture_data.py \
  --rtl-trace /home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv
```

All 3568 traced 16-byte payloads are covered by the reconstructed runtime heap
or expected output image. The trace-order payload hashes are:

- A: 2032 segments, SHA256
  `4925a011215b0e1b3038aa0ed772cc229dd62155117a9eb5fcbdf54a31cf4471`.
- B: 992 segments, SHA256
  `8e1153025965a8a2ed55b48a4dbec868b6a285e4cbf36b56f4c967db715d34e0`.
- C: 32 segments, SHA256
  `10ce3e2d1b1082473391ce173bae7cb9202af0a2df8a699acee53d950836dd92`.
- D: 512 segments in trace order, SHA256
  `d2831860c6b5914d18fb2d1ac18a6935fd934dfa95fe1c3eade4acad847db750`.

The D chunks have 512 unique addresses, form one contiguous 8192-byte span when
sorted by address, and that sorted span matches `output_sa` exactly with SHA256
`e1b19a4576eb5bf9a299a9f4944444f71c8ba2b5dbab8b1a9223286f2a8877f2`.
This is a data-layout proof for the RTL trace, not yet a gem5 functional-output
proof.

The `gem5-dev` container cannot see the external
`/home/zbn/code/npu_lpnpu/...` testcase path, so a gem5 regression needs
repo-local fixture files or generated data. The fixture helper can materialize
the runtime heap blobs without adding them to the repo yet:

```sh
python3 util/sau_lkssfull_fixture_data.py \
  --rtl-trace /home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv \
  --dump-runtime-dir /tmp/sau_lkssfull_runtime_fixture
```

The generated files are `input_heap.bin` (8064 bytes at `0x20025060`),
`bias_heap.bin` (32 bytes at `0x20025030`), `kernel_heap.bin` (1008 bytes at
`0x20024c30`), `output_expected.bin` (8192 bytes at `0x20022c20`), plus
`manifest.json`.

The first gem5-side fixture scheduler regression now loads those runtime blobs.
`SauGoldenGen(test_case="lkssfull_sau_stdconv_10_fixture_trace")` takes a
`fixture_dir`, writes input/bias/kernel into the firmware heap addresses, clears
the output heap, and then issues the 16 captured CSR starts. The tutorial config
is
`configs/tutorial/part1/kui_sau_lkssfull_stdconv_fixture_scheduler_test.py`.
`KuiSau.lkssfull_output_fixture` can point at `output_expected.bin`; in that
mode, the lkssfull path writes the captured 16-byte D payload slice for each
output address and `SauGoldenGen` reads back the full 8192-byte output heap.
Verified checkpoint:

```sh
python3 util/sau_lkssfull_fixture_data.py --dump-runtime-dir build/sau_lkssfull_runtime_fixture
python3 -m py_compile src/sau/KuiSau.py src/sau/SauGoldenGen.py configs/tutorial/part1/kui_sau_lkssfull_stdconv_fixture_scheduler_test.py util/sau_lkssfull_fixture_data.py
docker exec -w /gem5/gem5-kui gem5-dev scons build/RISCV/gem5.opt -j2
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_lkssfull_stdconv_fixture_scheduler ./configs/tutorial/part1/kui_sau_lkssfull_stdconv_fixture_scheduler_test.py
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --gem5-stats m5out/sau_lkssfull_stdconv_fixture_scheduler/stats.txt
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_lkssfull_stdconv_trace ./configs/tutorial/part1/kui_sau_lkssfull_stdconv_trace_test.py
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --gem5-stats m5out/sau_lkssfull_stdconv_trace/stats.txt
```

The checker reports all A/B/C/D request counts, first/last addresses, sum/xor
summaries, total request count, and `flowTraceOrderHash=8164653382730703` as
OK for both fixture scheduler and trace-only runs. The fixture scheduler run
also prints `D output matches SAU.py-derived
lkssfull_sau_stdconv_10_fixture_trace vector`. This is still oracle-backed
payload replay, not a functional stdconv16 compute proof.

## Current Next Actions

1. Replace the lkssfull oracle-backed D payload replay with real functional
   compute/writeback, using `output_expected.bin`, the trace payload hashes, and
   sorted `output_sa` coverage as regression anchors.
2. Produce or capture RTL `mem_addr.sv` traces for the RTL-gated representative
   CSR vectors as CSV/text, then compare per-kind first/last/sum/xor summaries
   while accounting for RTL horizontal=B and vertical=A naming.
3. Compare global request ordering with `flowTraceOrderHash` after converting
   the RTL trace into the same request-kind/address token stream; the checker
   already computes that token stream, and the trace replay now proves the hash
   path can match exact RTL order.
4. Decide whether to keep individual gem5 read/write segments or merge adjacent
   spans for speed after the RTL trace comparison is understood.
5. Extend address-summary checks to retain and dequant sequences where multiple
   instructions or C-scaling can change request counts.
6. Expand golden-reference functional tests for additional shift/dequant
   variants, especially richer mixed shift+dequant data.
7. Broaden normal/depthwise convolution vectors beyond the current unit-size 16,
   `conv_kernel=2` cases.
8. Tighten `D_wstrb` behavior for unit-size 8 and shift/dequant modes.
9. Finish CSR busy/write-during-processing semantics against RTL `csr.sv`.
10. Extend the new stats into RTL-informed timing estimates after broader
   functional output equivalence is covered.
