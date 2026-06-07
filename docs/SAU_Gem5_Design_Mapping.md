# SAU Software, RTL, and gem5 Mapping

This document is the first conversion artifact. It maps the software simulator
and RTL design to the current gem5 `KuiSau` model, then records the immediate
gaps that should drive code changes.

## Conversion Rule

- Functional correctness follows `src/sau/reference/simulator/SAU/SAU.py`.
- Architectural structure follows the RTL under `src/sau/reference/hardware`.
- gem5 should first become a deterministic functional model with correct memory
  transactions, then grow a timing model.

## Top-Level Flow

| Concern | Software simulator | RTL reference | Current gem5 | Gap |
| --- | --- | --- | --- | --- |
| Top module | `SAU` class | `SA_CORE.sv` | `KuiSau` SimObject | gem5 has the right shell but two execution paths are mixed. |
| CSR decode | `set_csr`, `update_ins1_config`, `update_csr` | `csr.sv` | `processCsrPacket`, `updateCsrFromRegisters` | gem5 now supports logical `0x200..0x207` words plus legacy 4-byte slots; write-while-busy behavior still needs RTL alignment. |
| Scheduling | `run`, `accumulate_flow_times`, `repair_backward` | `scheduler.sv` | `executeFlow` and random `sendOneKuiPkt` FSM | CSR-triggered flow is response-driven; random smoke traffic is isolated behind `enable_random_traffic`. |
| Address generation | `update_status` | `mem_addr.sv` | `updateStatusFromConfig` | gem5 covers the first normal/DW conv, pointwise, and GEMM formulas, issues segmented A/B/C reads and D writes, and has verified per-kind address summaries plus a global order hash for rich conv, stride, stride+shift, pointwise, and GEMM-shift vectors; broader RTL trace validation remains. |
| Data staging | `update_input_m1/m2/m3`, window helpers | `mem_ctrl.sv`, `feeder.sv`, `register_file.sv` | `handleResponse`, `updateInputMatrix*` | request type tagging exists; full byte/window ordering still needs more mode tests. |
| Preprocess | `preprocess`, `conv_mat_shift`, `conv_kernel_mask` | `feeder.sv`, `sa_feeder.sv`, transposer | `preprocess` | key endian/view conversions are ported for the first 16-lane paths; broader modes still need golden coverage. |
| Compute | `systolic_array` | `SA_ENGINE.sv`, `SA_ROW.sv`, `SA_PE.sv` | `systolicArrayExecute`, `KuiSauSystolic` | A transpose, GEMM, matrix-add, shift-mode GEMM, pointwise, normal-conv, normal-conv rich-data, normal-conv shift-mode, normal-conv stride-mode, normal-conv stride+shift, normal-conv stride+dequant, normal-conv stride+shift+dequant, depthwise-conv, depthwise-conv rich-data, depthwise-conv shift-mode, depthwise-conv stride-mode, depthwise-conv stride+shift, depthwise-conv stride+dequant, and depthwise-conv stride+shift+dequant vectors have deterministic coverage; additional conv modes and data shapes still need verification. |
| Accumulate/bias | `accumulate_array`, `c_plus` | SA engine accumulation and D_OUT | `accumulateResults`, `addBiasC` | pointwise+bias, pointwise `work_mode=2` C scaling plus dequant, normal-conv `work_mode=2` C scaling plus dequant, depthwise-conv `work_mode=2` C scaling plus dequant, small normal/DW conv C-read vectors, `flow_mode == 2` output retain, and `flow_mode == 3` transpose-retain now have deterministic coverage; broader data cases are still needed. |
| Dequant/output | `de_quant`, `transpose_output`, `update_output` | `SA_pkg::sat_truncate_func`, output datapath | `dequantize`, `transposeOutput`, `updateOutputMatrix` | saturation, byte packing, `flow_mode == 1` output transpose, `flow_mode == 2` retain, `flow_mode == 3` transpose-retain, positive/negative cutbit=2 int8 dequant, cutbit=2 int16 dequant, pointwise mixed-sign dequant, normal-conv mixed-sign dequant, and depthwise-conv mixed-sign dequant vectors have current golden coverage; broader mixed-mode dequant cases need more tests. |
| Done/status | `repair_backward` | `flow_end`, `busy`, `crossbar_done` | status read in `processCsrPacket` | gem5 needs busy/start/done semantics closer to RTL. |

## CSR Mapping

The software simulator stores eight logical 32-bit words:

| Logical word | Python field | RTL readback | Main fields |
| --- | --- | --- | --- |
| INS1_MSB | `csr.ins1_msb` | `ins_reg_0` | `last_ins_flag`, `cutbit`, `trans_mode` |
| INS1_LSB | `csr.ins1_lsb` | `ins_reg_1` | `shift_flag`, `stride_flag`, `conv_kernal`, `register_mode` |
| INS2_MSB | `csr.ins2_msb` | `ins_reg_2` | output/horizontal/vertical channel steps |
| INS2_LSB | `csr.ins2_lsb` | `ins_reg_3` | output/horizontal/vertical x steps |
| INS3_MSB | `csr.ins3_msb` | `ins_reg_4` | A/vertical address |
| INS3_LSB | `csr.ins3_lsb` | `ins_reg_5` | B/horizontal address |
| INS4_MSB | `csr.ins4_msb` | `ins_reg_6` | flow loop, flow mode, work mode, bias/C address |
| INS4_LSB | `csr.ins4_lsb` | `ins_reg_7` | busy, output/D address, instruction id, start |

Important differences to fix:

- Python CSR addresses are `0x200` through `0x207`; RTL decodes device ID with
  `csr_addr[11:4] == 8'h20` and register index with `csr_addr[3:1]`.
- RTL writes 64-bit `csr_wdata` by logical register pair. Python `set_csr`
  writes `csr_wdata1` and `csr_wdata2` for adjacent LSB/MSB words.
- Current gem5 supports the logical `0x200..0x207` CSR space and keeps the old
  byte-offset layout as a compatibility decode.
- Software uses 20-bit A/B/C/D addresses. Current gem5 masks all four address
  fields to 20 bits.
- Software no longer divides `flow_loop_times` by two when `shift_mode == 1`;
  it advances `flow_i` by two in `accumulate_flow_times`.
- RTL exposes `busy` and detects writes while processing as `crossbar_error`.

## Address and Flow Mapping

Software `update_status()` is the functional reference for address generation.
It has three broad paths:

| Mode | Condition | Key behavior |
| --- | --- | --- |
| Normal/DW convolution | `conv_kernel > 1` | A/B base uses channel/x steps and flow index; C is enabled; A kernel accounts for stride and shift. |
| Pointwise convolution | `conv_kernel == 1` | A address advances by a full unit-size tile; B advances by channel/x steps; C is enabled. |
| GEMM | `conv_kernel == 0` | A/B addressing differs for unit size 8 vs 16 and for `shift_mode`. |

Current gem5 `updateStatusFromConfig()` has been expanded from the software
formulas for normal/DW convolution, pointwise convolution, and GEMM. It still
needs request-level validation against `mem_addr.sv`; current A/B/C reads are
segmented by software burst rows, and D writeback is segmented by output rows.

RTL `mem_addr.sv` notes from the first formula pass:

- `SA_CORE.sv` sets `BASE_ADDR = 0x20000000`, matching the gem5 golden memory
  base.
- RTL CSR word 2 maps horizontal=B and vertical=A; word 3 maps bias=C and
  output=D, matching gem5's logical A/B/C/D names after the horizontal/vertical
  rename.
- RTL output addresses are `output_base + BASE_ADDR +
  output_x_cnt * output_x_step * output_channel_step * 16`, with shift-mode
  writes adding an adjacent `+16` row on alternating output cycles. This matches
  the current 16-row and 32-row gem5 D summary shapes for the default
  `x_step=1`, `channel_step=1` vectors.
- RTL bias/C reads are exactly two 16-byte requests at `bias + BASE_ADDR` and
  `bias + BASE_ADDR + 16`, but only when `pe_work_mode == SA_pkg::CONV`
  (`work_mode=1`). The software model enables C reads for normal/DW conv
  regardless of `work_mode`, so gem5 defaults to that functional behavior.
  `KuiSau.rtl_c_read_gate=True` switches C/bias request gating to the RTL shape
  for trace checks. RTL-gated summaries now cover zero-bias conv with C
  disabled, pointwise `work_mode=1` with C enabled, and shift-mode GEMM with no
  C. The current `work_mode=2` dequant/C-scaling golden vectors are therefore
  software/gem5 functional coverage, not yet RTL request-equivalence evidence.
- RTL scheduler order is not the same as gem5's current functional issuing
  order. For example, the `conv_kernel=2` path starts with horizontal/B before
  vertical/A, while gem5 issues all tagged A/B/C requests as one response-driven
  batch. The per-kind first/last/sum/xor summaries should therefore be compared
  first. `flowTraceOrderHash` is available for the separate global ordering
  check once the RTL trace is converted into the same request-kind/address token
  stream. `util/sau_trace_summary.py` performs this conversion for gem5
  `stats.txt` and RTL CSV/text traces. `util/sau_mem_addr_trace_bind.sv` can be
  compiled into the external VCS mikui DMA case to emit that CSV without editing
  the RTL source tree.

The captured `lkssfull_sau_stdconv_10` branch gives a concrete 16-bit standard
convolution fixture for the RTL address generator:

| Field | Value | Meaning |
| --- | --- | --- |
| `ins1_lsb` | `0x0000002c` | `register_mode=0`, `conv_kernel=3`, `stride=0`, `shift=1` |
| `ins1_msb` | `0x00000088` | `cutbit=2`, `last_ins_flag=1` |
| `ins2_lsb` | `0x00020401` | `B_x=1`, `A_x=4`, `D_x=2` |
| `ins2_msb` | `0x00101209` | `B_ch=9`, `A_ch=18`, `D_ch=16` |
| `ins3_lsb` | `0x00025030` | B/kernel base |
| `ins3_msb` | `0x00024c30 + n*0x40` | A/input base for inner start `n` |
| `ins4_lsb` | `((0x22c20 + n*0x20) << 9) \| 1` | D/output base and start |
| `ins4_msb` | `0x0e525010` | C/bias base, `work_mode=1`, `flow_mode=1`, `flow_times=14` |

For this fixture, RTL `mem_addr.sv` does not issue the CSR bases directly.
The first B request is `B + 0x30 + n*0x40`, the first A request is
`0x20026c00 + n*0x40`, the following A reuse window remains fixed at
`0x20024c30`, C emits only `C` and `C + 0x20`, and D writes `D + row*0x200`
plus `D + row*0x200 + 0x10`. The shared
`issueLkssfullStdconv10RequestStream()` helper derives that stream from decoded
fixture fields. It is reachable both from
`KuiSau(trace_replay="lkssfull_sau_stdconv_10")` and from ordinary
`executeFlow()` when the CSR shape matches `isLkssfullStdconv10Config()`. The
general gem5 functional scheduler now uses the same 16-lane shift-mode output
address shape, `D + logical_row*D_step + lane*unitSize`; it still needs
conv-reuse B/A request ordering, unit-size 8 write-mask tightening, and
functional D-data checking for this larger stdconv16 case.

The firmware testcase also contains a full data oracle in rodata. The helper
`util/sau_lkssfull_fixture_data.py` parses
`10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0/elf_symbols.txt` plus
`globala.hex` and reports `bias_sa` (32 bytes), `kernel_sa` (1008 bytes),
`output_sa` (8192 bytes), and `input_sa` (8064 bytes). The current
`output_sa` SHA256 is
`e1b19a4576eb5bf9a299a9f4944444f71c8ba2b5dbab8b1a9223286f2a8877f2`; it is
4096 signed int16 values spanning `-32768..32767`. This proves the D-data
golden source exists, but the conversion still needs the firmware
copy/split-to-SAU-local mapping before that oracle can be used as a real gem5
functional check.

For the captured firmware case, the first-fit heap returns input
`0x20025060`, bias `0x20025030`, kernel `0x20024c30`, and output
`0x20022c20`. The RTL trace kind names are request-stream tags, not direct
semantic object names: the traced B/horizontal stream starts at the input
buffer, the traced A/vertical reuse window starts at the kernel buffer, and the
C/bias read pair is `0x20025010/0x20025030`. Keep this role mapping explicit
when converting the trace-aligned path into a functional D-data test.
`util/sau_lkssfull_fixture_data.py --rtl-trace ...` now verifies that the
captured trace's payloads are fully covered by the reconstructed runtime image:
B reads 992 input segments; A reads 2000 kernel segments, 16 input-tail
segments, and 16 bias segments; C reads 16 kernel-tail segments and 16 bias
segments. The 512 D chunks cover the output heap contiguously when sorted by
address, matching `output_sa` exactly. This gives a precise data-layout oracle
for the next gem5 functional model step.

State variables now represented in gem5 and still needing wider validation:

- `flow_k`
- `C_step`, `C_count`, `C_kernel`, `C_bytes`, `C_en`
- `D_kernel = shift_mode + 1`
- `D_wstrb` count of `unit_size * (shift_mode + 1)`

## Data and Compute Mapping

Software input staging includes:

- `_select_unit_window()` for unit-size dependent SRAM row slicing.
- endian-style `fliplr()` conversions before viewing data as `int8`, `int16`,
  or `uint16`.
- `update_input_m3()` packing two bytes into one bias element.

Software compute includes:

- `systolic_array()` transposes `A_matrix` before matrix multiplication.
- `work_mode` 0, 1, and 2 use matrix multiply.
- `work_mode` 3 uses matrix add.
- `c_plus()` only adds bias for `work_mode` 1 and 2, with `work_mode` 2 shifting
  bias by `cutbit`.
- `de_quant()` right shifts and saturates to int8 or int16.
- `update_output()` converts the dequantized matrix into byte-oriented SRAM
  rows, including output endian and int16 row ordering.

Current gem5 has ported the main 16-lane int8/int16 conversions used by the
first functional targets. GEMM, matrix-add, shift-mode GEMM,
positive/negative/int16 cutbit/dequant GEMM, pointwise+bias, pointwise
mixed-sign dequant, `flow_mode == 1` output transpose, small normal-conv
cases including rich signed int8 data, `shift_mode=1` int16 output packing,
`work_mode=2` mixed-sign dequant, plus `stride=1` output-row selection, and
small depthwise-conv cases including rich signed int8 data, `shift_mode=1`
int16 output packing, `work_mode=2` mixed-sign dequant, and `stride=1`
output-row selection now have deterministic golden vectors.
Normal/depthwise combined `stride=1` plus
`shift_mode=1` vectors pass, proving the stride A-window merge composes with
512-byte int16 D packing. Normal/depthwise combined `stride=1` plus
`work_mode=2`/`cutbit=2` vectors pass, proving stride row selection composes
with C scaling and signed int8 dequantization. Normal/depthwise combined
`stride=1`, `shift_mode=1`, `work_mode=2`, and `cutbit=2` vectors also pass,
proving the currently modeled path composes stride row selection, C scaling,
signed dequantization, and 512-byte int16 D packing.
`flow_mode == 2` retain passes as a two-instruction vector that keeps D across
the first instruction and clears after the second. `flow_mode == 3` also passes
as a two-instruction vector that transposes the first output, retains the
underlying D matrix, and accumulates again before a normal clear. The next
functional conversion work should prove those semantics across broader
normal/DW modes and data shapes plus broader mixed-mode dequant variants, then
tighten any mode-specific differences found by the tests.

## Memory Transaction Gap

Current gem5 `executeFlow()` now issues tagged A, B, and optional C reads, then
waits for all required responses before preprocessing, computing, accumulating,
and writing D. D write completion advances the flow. `KuiSauMemSidePort` queues
blocked packets so multiple flow reads can be outstanding. Basic stats now
report CSR traffic, flow starts/completions, tagged read/write segment counts,
busy ticks, per-kind tagged address summaries for MatrixA, MatrixB, VectorC,
and OutputD requests, and global A/B/C/D trace summaries including a 53-bit
order-sensitive hash.

Remaining conversion:

1. Compare the verified gem5 first/last/sum/xor address summaries against RTL
   `mem_addr.sv` traces, using `rtl_c_read_gate=True` when checking RTL request
   shape. The trace can be exported either as explicit `kind/source,addr` rows
   or as `sram_rd_enable`, `sram_wr_enable`, `sram_mem_addr`, `input_switch`,
   and `bias_rd_valid` columns for `util/sau_trace_summary.py`. The current
   capture helper is documented in `docs/SAU_RTL_Trace_Capture.md`; optionally
   merge adjacent segments only after the trace semantics are understood.
2. Compare global request order with `flowTraceOrderHash` after converting RTL
   trace events into the same request-kind/address token stream, because gem5
   batches A/B/C by request kind while RTL scheduler can change horizontal and
   vertical issue order.
3. Tighten `D_wstrb` masking for unit-size 8 and additional output modes.
4. Extend address-summary checks to retain and dequant sequences, then compare
   those multi-instruction or C-scaling cases with direct RTL trace output.
5. Extend stats with full address dumps or debug flags only if the summaries are
   too weak for a specific RTL comparison.
6. Keep the random untagged memory path isolated as a smoke test only.

## First Code Targets

1. Expand golden vectors beyond the current GEMM, matrix-add, shift-mode GEMM,
   pointwise+bias, output-transpose, normal-conv, depthwise-conv, rich-data
   conv, stride-mode conv, stride+shift conv, stride+dequant conv,
   stride+shift+dequant conv, retain, and transpose-retain cases.
2. Compare A/B/C/D segment addresses, counts, and global order to RTL using the
   new stats as the first checkpoint.
3. Finish CSR write-while-busy/crossbar-error behavior.
4. Add RTL-informed timing estimates.

## Verification Targets

The first tests should compare gem5 memory output against Python SAU output:

- GEMM, `conv_kernel == 0`, `shift_mode == 0` - first deterministic vector
  implemented by `SauGoldenGen`.
- Matrix add, `work_mode == 3` - deterministic zero-A/vector-B case implemented
  by `SauGoldenGen(test_case="add")`.
- GEMM, `shift_mode == 1` - deterministic int16 output-packing vector
  implemented by `SauGoldenGen(test_case="gemm_shift")`.
- GEMM, `cutbit == 2` - deterministic int8 right-shift and saturation vector
  implemented by `SauGoldenGen(test_case="gemm_dequant")`.
- GEMM, `cutbit == 2` with negative int8 accumulator values - deterministic
  right-shift and negative saturation vector implemented by
  `SauGoldenGen(test_case="gemm_dequant_negative")`.
- GEMM, `shift_mode == 1`, `cutbit == 2` - deterministic int16 right-shift and
  output-packing vector implemented by
  `SauGoldenGen(test_case="gemm_dequant_int16")`.
- Pointwise convolution, `conv_kernel == 1` - deterministic work_mode=1
  C/bias vector implemented by `SauGoldenGen(test_case="pointwise")`.
- Pointwise convolution, `conv_kernel == 1`, `work_mode == 2`, `cutbit == 2` -
  deterministic mixed-sign B plus C-scaling/dequant vector implemented by
  `SauGoldenGen(test_case="pointwise_dequant_mixed")`.
- Output transpose, `flow_mode == 1` - deterministic GEMM-derived vector
  implemented by `SauGoldenGen(test_case="gemm_out_transpose")`.
- Normal convolution, `register_mode == 0` - deterministic `conv_kernel=2`,
  `stride=0`, `shift=0` vector implemented by
  `SauGoldenGen(test_case="normal_conv")`.
- Normal convolution, `register_mode == 0`, rich signed A/B data -
  deterministic `conv_kernel=2`, `stride=0`, `shift=0` vector implemented by
  `SauGoldenGen(test_case="normal_conv_rich")`.
- Normal convolution, `register_mode == 0`, `shift_mode == 1` -
  deterministic int16 conv staging and output-packing vector implemented by
  `SauGoldenGen(test_case="normal_conv_shift")`.
- Normal convolution, `register_mode == 0`, `stride == 1` - deterministic
  stride A-window merge and D row-8 selection vector implemented by
  `SauGoldenGen(test_case="normal_conv_stride")`.
- Normal convolution, `register_mode == 0`, `stride == 1`,
  `shift_mode == 1` - deterministic stride A-window merge plus int16 output
  packing vector implemented by
  `SauGoldenGen(test_case="normal_conv_stride_shift")`.
- Normal convolution, `register_mode == 0`, `work_mode == 2`, `cutbit == 2` -
  deterministic mixed-sign B plus C-scaling/dequant vector implemented by
  `SauGoldenGen(test_case="normal_conv_dequant_mixed")`.
- Normal convolution, `register_mode == 0`, `stride == 1`, `work_mode == 2`,
  `cutbit == 2` - deterministic stride row-selection plus mixed-sign
  C-scaling/dequant vector implemented by
  `SauGoldenGen(test_case="normal_conv_stride_dequant_mixed")`.
- Normal convolution, `register_mode == 0`, `stride == 1`, `shift_mode == 1`,
  `work_mode == 2`, `cutbit == 2` - deterministic stride row-selection plus
  int16 output packing and mixed-sign C-scaling/dequant vector implemented by
  `SauGoldenGen(test_case="normal_conv_stride_shift_dequant_mixed")`.
- Depthwise convolution, `register_mode == 2` - deterministic `conv_kernel=2`,
  `stride=0`, `shift=0` B-mask vector implemented by
  `SauGoldenGen(test_case="depthwise_conv")`.
- Depthwise convolution, `register_mode == 2`, rich signed A/B data -
  deterministic `conv_kernel=2`, `stride=0`, `shift=0` B-mask vector implemented
  by `SauGoldenGen(test_case="depthwise_conv_rich")`.
- Depthwise convolution, `register_mode == 2`, `shift_mode == 1` -
  deterministic int16 conv staging plus B-mask output-packing vector implemented
  by `SauGoldenGen(test_case="depthwise_conv_shift")`.
- Depthwise convolution, `register_mode == 2`, `stride == 1` - deterministic
  stride A-window merge plus B-mask D row-8 selection vector implemented by
  `SauGoldenGen(test_case="depthwise_conv_stride")`.
- Depthwise convolution, `register_mode == 2`, `stride == 1`,
  `shift_mode == 1` - deterministic stride A-window merge plus B-mask int16
  output-packing vector implemented by
  `SauGoldenGen(test_case="depthwise_conv_stride_shift")`.
- Depthwise convolution, `register_mode == 2`, `work_mode == 2`,
  `cutbit == 2` - deterministic mixed-sign B plus C-scaling/dequant vector
  implemented by `SauGoldenGen(test_case="depthwise_conv_dequant_mixed")`.
- Depthwise convolution, `register_mode == 2`, `stride == 1`, `work_mode == 2`,
  `cutbit == 2` - deterministic stride row-selection plus B-mask mixed-sign
  C-scaling/dequant vector implemented by
  `SauGoldenGen(test_case="depthwise_conv_stride_dequant_mixed")`.
- Depthwise convolution, `register_mode == 2`, `stride == 1`,
  `shift_mode == 1`, `work_mode == 2`, `cutbit == 2` - deterministic stride
  row-selection plus B-mask int16 output packing and mixed-sign C-scaling/dequant
  vector implemented by
  `SauGoldenGen(test_case="depthwise_conv_stride_shift_dequant_mixed")`.
- Output retain, `flow_mode == 2` - two-instruction GEMM vector implemented by
  `SauGoldenGen(test_case="gemm_retain")`; golden run passes.
- Combined output transpose plus retain, `flow_mode == 3` - two-instruction
  GEMM vector implemented by `SauGoldenGen(test_case="gemm_transpose_retain")`;
  golden run passes.
