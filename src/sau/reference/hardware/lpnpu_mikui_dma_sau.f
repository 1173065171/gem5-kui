// Reduced SAU hardware filelist extracted from:
// /home/zbn/code/npu_lpnpu/syn/script/mikui_dma/lpnpu_mikui_dma.f
//
// Paths are relative to src/sau/reference.

+incdir+hardware/macro
+incdir+hardware/src/sa_execute
+incdir+hardware/src/sa_element

// Direct macro/package dependencies.
hardware/macro/macro.svh
hardware/macro/macro_gclk.svh
hardware/ip/GCLK/v_clock_latch.v
hardware/ip/GCLK/v_clock_pass.v
hardware/src/sa_execute/SA_pkg.sv

// SAU execute block.
hardware/src/sa_execute/active_delay.v
hardware/src/sa_execute/DW02_mult_2_stage.v
hardware/src/sa_execute/register_file.sv
hardware/src/sa_execute/registers.svh
hardware/src/sa_execute/SA_ENGINE.sv
hardware/src/sa_execute/sa_feeder.sv
hardware/src/sa_execute/SA_PE.sv
hardware/src/sa_execute/SA_ROW.sv
hardware/src/sa_execute/shift_register.sv
hardware/src/sa_execute/transposer_tiny.v
hardware/src/sa_execute/weight_delay.v

// SAU core/control block.
hardware/src/sa_element/feeder.sv
hardware/src/sa_element/scheduler.sv
hardware/src/sa_element/mem_ctrl.sv
hardware/src/sa_element/mem_addr.sv
hardware/src/sa_element/csr.sv
hardware/src/sa_element/registers.svh
hardware/src/sa_element/SA_CORE.sv
