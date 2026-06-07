// +++++-----------  Other include file   ------------------+++++

// These files are used to initialize the SRAMs in the design(sram.sv)
`define INITIALIZE_MEMORY
`define ARM_UD_MODEL

// +++++-----------  Simulation parameters   ------------------+++++
// sram has 4 banks, each bank has 8192 words, each word has 32 bits, it is a 32 x 8192 SRAM
// time delay ns to wait
`ifdef SIM_FLAG
// UVM ENV PARAMETER
// `define UVM_CSR_ACKTIME 240
`define UVM_CSR_ACKTIME 10
`define TCDM_SIM
`define SRAM_INIT_DELAY 20
`endif

// +++++-----------  SRAM file parameters   ------------------+++++
// `define LPNPU_HOME "/home/cxy/kuiloong/mi_kui/npu_lpnpu"
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/018"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/matmul_8_8_8"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/matmul_8_16_8"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/matmul_32_8_16"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/matmul_64_32_32"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/fc_8_8_8"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/fc_8_16_16"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/fc_16_8_32"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/mikui/test_reduce"}

// `define FIRMWARE {`LPNPU_HOME, "/testcase/minsys/test_app"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/minsys/test_matmul"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/minsys/test_fc_proc"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/minsys/test_gemm_proc"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/minsys/test_sau_matmul"}
// `define FIRMWARE {`LPNPU_HOME, "/testcase/minsys/test_softmax"}

`define FIRMWARE {`LPNPU_HOME, `TESTFIRMWARE_DEFINE}

`define RISCV_INS_FILE  {`FIRMWARE, "/instruction.hex"}
`define RISCV_INS_FILE0 {`FIRMWARE, "/instruction_mod4_0.hex"}
`define RISCV_INS_FILE1 {`FIRMWARE, "/instruction_mod4_1.hex"}
`define RISCV_INS_FILE2 {`FIRMWARE, "/instruction_mod4_2.hex"}
`define RISCV_INS_FILE3 {`FIRMWARE, "/instruction_mod4_3.hex"}

`define RISCV_DATA_FILE  {`FIRMWARE, "/memory.hex"}
`define RISCV_DATA_FILE0 {`FIRMWARE, "/memory_mod4_0.hex"}
`define RISCV_DATA_FILE1 {`FIRMWARE, "/memory_mod4_1.hex"}
`define RISCV_DATA_FILE2 {`FIRMWARE, "/memory_mod4_2.hex"}
`define RISCV_DATA_FILE3 {`FIRMWARE, "/memory_mod4_3.hex"}


`define RISCV_GLOBALA_FILE  {`FIRMWARE, "/globala.hex"}
`define RISCV_GLOBALA_FILE0 {`FIRMWARE, "/globala_mod4_0.hex"}
`define RISCV_GLOBALA_FILE1 {`FIRMWARE, "/globala_mod4_1.hex"}
`define RISCV_GLOBALA_FILE2 {`FIRMWARE, "/globala_mod4_2.hex"}
`define RISCV_GLOBALA_FILE3 {`FIRMWARE, "/globala_mod4_3.hex"}

`define RISCV_GLOBALB_FILE  {`FIRMWARE, "/globalb.hex"}
`define RISCV_GLOBALB_FILE0 {`FIRMWARE, "/globalb_mod4_0.hex"}
`define RISCV_GLOBALB_FILE1 {`FIRMWARE, "/globalb_mod4_1.hex"}
`define RISCV_GLOBALB_FILE2 {`FIRMWARE, "/globalb_mod4_2.hex"}
`define RISCV_GLOBALB_FILE3 {`FIRMWARE, "/globalb_mod4_3.hex"}

`define RISCV_GLOBALC_FILE  {`FIRMWARE, "/globalc.hex"}
`define RISCV_GLOBALC_FILE0 {`FIRMWARE, "/globalc_mod4_0.hex"}
`define RISCV_GLOBALC_FILE1 {`FIRMWARE, "/globalc_mod4_1.hex"}
`define RISCV_GLOBALC_FILE2 {`FIRMWARE, "/globalc_mod4_2.hex"}
`define RISCV_GLOBALC_FILE3 {`FIRMWARE, "/globalc_mod4_3.hex"}

`define SRAM_BASE_INST 32'h0000_0000
`define SRAM_BASE_A    32'h2001_0000
`define SRAM_BASE_B    32'h2001_8000
`define SRAM_BASE_C    32'h2002_0000

// design file has task to read data into sram's mem queue
`ifdef MODULE_TEST
        `define INS_INIT_FILE    {`FIRMWARE, "/ins.hex"}
        `define SRAM_INIT_FILE_0 {`FIRMWARE, "/sram_model_3.hex"}
        `define SRAM_INIT_FILE_1 {`FIRMWARE, "/sram_model_2.hex"}
        `define SRAM_INIT_FILE_2 {`FIRMWARE, "/sram_model_1.hex"}
        `define SRAM_INIT_FILE_3 {`FIRMWARE, "/sram_model_0.hex"}
        `define SRAM_OUT_FILE    {`FIRMWARE, "/sram_model_out.hex"}
        `define SRAM_WROUT_FILE  {`FIRMWARE, "/sram_write_out.hex"}
`else
        `define INS_INIT_FILE    {`FIRMWARE, "/ins.hex"}
        `define SRAM_INIT_FILE_0 {`FIRMWARE, "/sram_model_3.hex"}
        `define SRAM_INIT_FILE_1 {`FIRMWARE, "/sram_model_2.hex"}
        `define SRAM_INIT_FILE_2 {`FIRMWARE, "/sram_model_1.hex"}
        `define SRAM_INIT_FILE_3 {`FIRMWARE, "/sram_model_0.hex"}
        `define SRAM_OUT_FILE    {`FIRMWARE, "/sram_model_out.hex"}
        `define SRAM_WROUT_FILE  {`FIRMWARE, "/sram_write_out.hex"}
`endif

