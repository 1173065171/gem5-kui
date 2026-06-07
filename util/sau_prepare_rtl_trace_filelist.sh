#!/usr/bin/env sh
set -eu

case_name="${CASE_NAME:-mikui_dma}"
lpnpu_home="${LPNPU_HOME:-/home/zbn/code/npu_lpnpu}"
sim_dir="${SIM_DIR:-${lpnpu_home}/sim}"
vcs_dir="${VCS_DIR:-${sim_dir}/vcs}"
dw_home="${DW_HOME:-/home/ic/synopsys/syn/syn/T-2022.03-SP2/}"
vcs_timescale="${VCS_TIMESCALE:-1ns/100ps}"
regress_name="${REGRESS_NAME:-lkssfull_sau_stdconv}"
testcase="${TESTCASE:-10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0}"
trace_csv="${SAU_TRACE_CSV:-sau_mem_addr_trace.csv}"
repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out_dir="${OUT_DIR:-${repo_root}/build/sau_trace}"
out_file="${OUT_FILE:-${out_dir}/${case_name}_verilog_with_trace.f}"

src_file="${vcs_dir}/script/case_${case_name}/verilog.f"
monitor_file="${repo_root}/util/sau_mem_addr_trace_bind.sv"
testcase_dir="${lpnpu_home}/testcase/${regress_name}/${testcase}"

if [ ! -f "${src_file}" ]; then
    echo "missing source filelist: ${src_file}" >&2
    exit 1
fi

if [ ! -f "${monitor_file}" ]; then
    echo "missing trace monitor: ${monitor_file}" >&2
    exit 1
fi

if [ ! -d "${testcase_dir}" ]; then
    echo "warning: testcase path not found: ${testcase_dir}" >&2
fi

mkdir -p "${out_dir}"
cp "${src_file}" "${out_file}"
{
    echo ""
    echo "// gem5 SAU RTL trace monitor"
    echo "${monitor_file}"
} >> "${out_file}"

cat <<EOF
created: ${out_file}

Use this with the NPU VCS makefile by overriding VERILOG_SRC, for example:

export LPNPU_HOME=${lpnpu_home}
export SIM_DIR=${sim_dir}
export VCS_DIR=${vcs_dir}
export DW_HOME=${dw_home}
export VCS_TIMESCALE=${vcs_timescale}
make -C "\$VCS_DIR" run_verdi \\
  CASE_NAME=${case_name} \\
  REGRESS_NAME=${regress_name} \\
  TESTCASE=${testcase} \\
  VERILOG_SRC="-f ${out_file} -f \$VCS_DIR/script/case_${case_name}/define.f \$SIM_DIR/testbench/tb/top_${case_name}_tb.sv -f \$VCS_DIR/script/case_${case_name}/uvm.f" \\
  SIM_VERDI_FLAG="-l sim.log -ucli +assert_enable +fsdb+autofsdb +uvm_set_config_string=*,test_mode,veu +UVM_TESTNAME=base_test -i \$VCS_DIR/script/case_${case_name}/simv_verdi.tcl +SAU_TRACE_CSV=${trace_csv}"

Then summarize the RTL CSV:

python3 ${repo_root}/util/sau_trace_summary.py --rtl-trace "\$VCS_DIR/build/${case_name}/${trace_csv}"
EOF
