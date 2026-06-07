`ifndef MACRO_GCLK_SVH
`define MACRO_GCLK_SVH

`ifdef L_BIU_CG
    `undef L_BIU_CG
`endif

`ifdef FPGA
    `define L_BIU_CG v_clock_pass
`else
    `define L_BIU_CG v_clock_latch
`endif

`endif
