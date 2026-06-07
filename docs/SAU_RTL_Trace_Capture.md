# SAU RTL Trace Capture

This note records the non-invasive RTL trace path for comparing `mem_addr.sv`
requests with gem5 `KuiSau` stats.

## RTL Location

The mikui DMA VCS case instantiates SAU at:

```text
top_mikui_dma_tb.dut_mikui_inst.SAU_1_inst.mem_addr_inst
```

The useful `mem_addr.sv` request signals are:

- `sram_mem_addr`
- `sram_rd_enable`
- `sram_wr_enable`
- `input_switch_d[ADDR_DELAY-2][0]`
- `bias_rd_valid`

The mapping used for gem5 comparison is:

- RTL vertical read: MatrixA
- RTL horizontal read: MatrixB
- RTL bias read: VectorC
- RTL write: OutputD

## Generate a Trace Filelist

The helper below copies the external VCS filelist into this repository's
`build/sau_trace` directory and appends the bind monitor:

```sh
./util/sau_prepare_rtl_trace_filelist.sh
```

It prints a `make -C $VCS_DIR run_verdi ...` command. The key overrides are:

```sh
export LPNPU_HOME=/home/zbn/code/npu_lpnpu
export SIM_DIR=/home/zbn/code/npu_lpnpu/sim
export VCS_DIR=/home/zbn/code/npu_lpnpu/sim/vcs
export DW_HOME=/home/ic/synopsys/syn/syn/T-2022.03-SP2/
export VCS_TIMESCALE=1ns/100ps
make -C "$VCS_DIR" run_verdi \
  CASE_NAME=mikui_dma \
  REGRESS_NAME=lkssfull_sau_stdconv \
  TESTCASE=10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0 \
  VERILOG_SRC="-f /home/zbn/code/gem5/gem5-kui/build/sau_trace/mikui_dma_verilog_with_trace.f -f $VCS_DIR/script/case_mikui_dma/define.f $SIM_DIR/testbench/tb/top_mikui_dma_tb.sv -f $VCS_DIR/script/case_mikui_dma/uvm.f" \
  SIM_VERDI_FLAG="-l sim.log -ucli +assert_enable +fsdb+autofsdb +uvm_set_config_string=*,test_mode,veu +UVM_TESTNAME=base_test -i $VCS_DIR/script/case_mikui_dma/simv_verdi.tcl +SAU_TRACE_CSV=sau_mem_addr_trace.csv"
```

The helper defaults to this existing smoke testcase:

```text
/home/zbn/code/npu_lpnpu/testcase/lkssfull_sau_stdconv/10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0
```

The actual VCS run writes into the external hardware repository under
`/home/zbn/code/npu_lpnpu/sim/vcs/build`, so run it only when external write
approval is available. The generated filelist itself is written inside this
gem5 repository.

The bind monitor writes:

```text
cycle,kind,addr,sram_rd_enable,sram_wr_enable,input_switch_bit,bias_rd_valid
```

## Summarize and Compare

After the RTL run, summarize the CSV:

```sh
python3 util/sau_trace_summary.py --rtl-trace /home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv
```

The captured default smoke testcase can also be checked as a fixed case:

```sh
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --rtl-trace /home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv
```

The verified summary for that testcase is:

```text
flowReadARequests/flowReadBRequests/flowReadCRequests/flowWriteDRequests = 2032/992/32/512
flowTraceRequests/flowTraceAddrFirst/flowTraceAddrLast/flowTraceAddrSum/flowTraceAddrXor/flowTraceOrderHash = 3568/537022560/537021456/1916096721408/15360/8164653382730703
```

Show the per-flow shape:

```sh
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --rtl-trace /home/zbn/code/npu_lpnpu/sim/vcs/build/mikui_dma/sau_mem_addr_trace.csv --show-flows
```

That command verifies the fixed summary and prints 16 flows. Each flow has 223
requests with counts `B=62, A=127, C=2, D=32`; the request-kind run order inside
a flow is `B,A,C,A,D`. Flow 0 begins with B `0x20025060`, A `0x20026c00`,
C `0x20025010`, and D `0x20022c20`. Flow 1 advances the first B/A/D addresses
by `0x40/0x40/0x20`; C remains at `0x20025010` and `0x20025030`. Each flow has
one final A read at `0x20025040` after the two C/bias reads and before D
writes.

Replay the same request stream from gem5:

```sh
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_lkssfull_stdconv_trace ./configs/tutorial/part1/kui_sau_lkssfull_stdconv_trace_test.py
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --gem5-stats m5out/sau_lkssfull_stdconv_trace/stats.txt
```

This config uses `KuiSau(trace_replay="lkssfull_sau_stdconv_10")` plus
`SauGoldenGen(test_case="lkssfull_sau_stdconv_10_trace")`. The driver emits 16
normconv-style CSR starts, with D offsets `0x22c20 + n * 0x20`; KuiSau maps each
start to one captured RTL flow. It verifies the address/order model only;
functional D bytes are intentionally not checked in this replay path. The
latest checked replay matches every fixed summary field, including
`flowTraceOrderHash=8164653382730703`.

The same CSR-derived request stream is also reachable through the ordinary
`executeFlow()` path without setting `trace_replay`:

```sh
docker exec -w /gem5/gem5-kui gem5-dev ./build/RISCV/gem5.opt --outdir=m5out/sau_lkssfull_stdconv_scheduler ./configs/tutorial/part1/kui_sau_lkssfull_stdconv_scheduler_test.py
python3 util/sau_trace_summary.py --case lkssfull_sau_stdconv_10 --gem5-stats m5out/sau_lkssfull_stdconv_scheduler/stats.txt
```

This scheduler config still verifies address and order only. It reuses the
same request-stream drain path, so full D-data checking remains a separate
functional-model task.

The default RTL smoke testcase is not guaranteed to match one of the four
gem5 RTL-gated representative cases below. First summarize its per-kind shape;
then compare against a matching gem5 CSR vector once the exact testcase settings
are mapped.

For raw waveform-derived CSV without an explicit `kind` column, the summary
tool accepts `input_switch`, `input_switch_d`, or `input_switch_bit` for the RTL
horizontal/vertical selector.

Compare a trace against one of the current RTL-gated representative cases:

```sh
python3 util/sau_trace_summary.py --case normal_conv_stride --rtl-trace /path/to/sau_mem_addr_trace.csv
python3 util/sau_trace_summary.py --case normal_conv_stride_shift --rtl-trace /path/to/sau_mem_addr_trace.csv
python3 util/sau_trace_summary.py --case pointwise --rtl-trace /path/to/sau_mem_addr_trace.csv
python3 util/sau_trace_summary.py --case gemm_shift --rtl-trace /path/to/sau_mem_addr_trace.csv
```

Compare the matching gem5 stats:

```sh
python3 util/sau_trace_summary.py --case normal_conv_stride --gem5-stats m5out/sau_order_normal_conv_stride_rtl_cgate/stats.txt
python3 util/sau_trace_summary.py --case normal_conv_stride_shift --gem5-stats m5out/sau_order_normal_conv_stride_shift_rtl_cgate/stats.txt
python3 util/sau_trace_summary.py --case pointwise --gem5-stats m5out/sau_order_pointwise_rtl_cgate/stats.txt
python3 util/sau_trace_summary.py --case gemm_shift --gem5-stats m5out/sau_order_gemm_shift_rtl_cgate/stats.txt
```

The first comparison should be per-kind first/last/sum/xor. Global
`flowTraceOrderHash` is intentionally order-sensitive and may expose scheduler
ordering differences between RTL and gem5.
