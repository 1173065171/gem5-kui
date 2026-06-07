`include "macro_gclk.svh"

module SA_CORE
import SA_pkg::*;
#(
    parameter int unsigned SRAM_ADDR_WIDTH = 32,
    parameter int unsigned SRAM_DATA_WIDTH = SA_pkg::SRAM_DATA_WIDTH,
    parameter int unsigned SRAM_DATA_BYTES = SRAM_DATA_WIDTH / 8,
    parameter int unsigned SRAM_DELAY = 1,
    parameter int unsigned BASE_ADDR = 32'h2000_0000,
    parameter int unsigned ROW_NUM = SA_pkg::SA_SIZE,
    parameter int unsigned COL_NUM = SA_pkg::SA_SIZE,
    parameter int unsigned REGDEEPTH = 48,
    parameter int unsigned INPUTDW = 8
)
(
    input   wire            clk,
    input   wire            rst_n,
    input   logic           sau_clk_en,

    input  logic            csr_we,
    input  logic            csr_re,
    input  logic   [1:0]    csr_write_type,
    input  logic   [11:0]   csr_addr,
    input  logic   [63:0]   csr_wdata,
    output logic   [31:0]   csr_rdata,
    output logic            csr_ready,

    // sram interface
    output  wire                        sau_sram_enable         ,
    output  wire [SRAM_DATA_BYTES-1:0]  sau_sram_wstrb          ,
    output  wire [SRAM_ADDR_WIDTH-1:0]  sau_sram_addr           ,
    input   wire [SRAM_DATA_WIDTH-1:0]  sau_sram_rdata          ,
    output  wire [SRAM_DATA_WIDTH-1:0]  sau_sram_wdata          ,

    // crossbar interface
    output  wire                        sau_crossbar_start      ,
    output  wire                        sau_crossbar_done       ,
    output  wire                        sau_crossbar_error
);
// ---------------------------- parameter ----------------------------
localparam [$clog2(ROW_NUM): 0]  ROW_VALID_NUM = ($clog2(ROW_NUM)+1)'(ROW_NUM);
localparam [$clog2(COL_NUM): 0]  COL_VALID_NUM = ($clog2(COL_NUM)+1)'(COL_NUM);
// ----------------------- signal declaration ------------------------
// state of core
SA_pkg::registerfile_state_e core_state_s;
logic           start;
logic           update_finished;
logic           load_done_flag;
logic           execute_finished;
logic           last_flow_time_clear_s;
logic           last_flow_time_s;
logic [1:0]     input_switch_s;
logic           regfile_clear_flag;
// ins-mem-ctrl
logic [20-1:0]  vertical_address;
logic [8-1:0]   vertical_x_step;
logic [8-1:0]   vertical_c_step;
logic [20-1:0]  horizontal_address;
logic [8-1:0]   horizontal_x_step;
logic [8-1:0]   horizontal_c_step;
logic [20-1:0]  output_address;
logic [8-1:0]   output_x_step;
logic [8-1:0]   output_c_step;
logic [19:0]    bias_address;
// ins-scheduler
logic [5:0]     flow_loop_times;
logic [2-1 : 0] reuse_mode;
logic [2-1 : 0] trans_mode;
logic [1:0]     pe_work_mode;
logic [1:0]     sa_flow_mode;
logic [1:0]     register_mode;
logic [4:0]     cutbit;
logic [3-1 : 0] conv_kernal;
logic           last_ins_flag;
logic           last_ins_wr_done;
logic           stride_flag;
logic           shift_flag;
logic           flow_end;
// mem-addr
logic [SRAM_ADDR_WIDTH-1:0]sram_mem_addr;
logic           sram_rd_enable;
logic           sram_wr_enable;
logic           sram_wr_last;
logic           sram_rdaddr_last;
logic           sram_rd_last;
// mem-ctrl
logic [SRAM_DATA_WIDTH-1:0]   core_register_data_in;
logic           core_register_data_in_last;
logic [SRAM_DATA_WIDTH-1:0]   core_register_data_out;
logic           core_register_data_out_valid;
logic           core_register_data_out_last;
// feeder
logic           data_A_valid;
logic           data_A_last;
logic [ROW_NUM*INPUTDW-1 : 0]data_A;
logic           data_B_valid;
logic           data_B_last;
logic [ROW_NUM*INPUTDW-1 : 0]data_B;
logic           data_C_valid;
logic [2*ROW_NUM*INPUTDW-1 : 0]data_C;
logic [1:0]     input_switch_f;
logic           last_flow_time_f;
logic           sram_wr_valid_ma;
logic           sram_wr_last_ma;
//register file
logic           register_file_wvalid;
logic           [ROW_NUM*INPUTDW-1 : 0]register_file_wdata;
logic           register_file_rden;
logic           register_file_rvalid;
logic           [ROW_NUM*INPUTDW-1 : 0]register_file_rdata;
logic           register_file_rlast;
// sa-feeder
logic           storage_ready_o;
logic [2*ROW_NUM*INPUTDW-1: 0]result_final_o;
logic           result_final_valid_o;
logic           result_last_o;
logic           sau_gclk;



// ----------------------- module instantiation ----------------------

`L_BIU_CG sau_core_clk_gate (
    .clk    (clk),
    .en     (sau_clk_en),
    .sen    (1'b0),
    .gclk   (sau_gclk)
);

csr csr_inst (
    .clk(sau_gclk),
    .rst_n(rst_n),
    .csr_we(csr_we),
    .csr_re(csr_re),
    .csr_operation(csr_write_type),
    .csr_addr(csr_addr),
    .csr_wdata(csr_wdata),
    .csr_rdata(csr_rdata),
    .csr_ready(csr_ready),
    .start(start),
    .flow_end(flow_end),
    .vertical_address(vertical_address),
    .vertical_x_step(vertical_x_step),
    .vertical_c_step(vertical_c_step),
    .horizontal_address(horizontal_address),
    .horizontal_x_step(horizontal_x_step),
    .horizontal_c_step(horizontal_c_step),
    .output_address(output_address),
    .output_x_step(output_x_step),
    .output_c_step(output_c_step),
    .bias_address(bias_address),
    .conv_kernal(conv_kernal),
    .reuse_mode(reuse_mode),
    .trans_mode(trans_mode),
    .flow_loop_times(flow_loop_times),
    .last_ins_flag(last_ins_flag),
    .pe_work_mode(pe_work_mode),
    .sa_flow_mode(sa_flow_mode),
    .register_mode(register_mode),
    .cutbit(cutbit),
    .stride_flag(stride_flag),
    .shift_flag(shift_flag),
    .crossbar_start(sau_crossbar_start),
    .crossbar_error(sau_crossbar_error)
);


scheduler # (
    .ADDR_DW(20)
)
scheduler_inst (
    .clk(sau_gclk),
    .rst_n(rst_n),
    .start(start),
    .conv_kernal_i(conv_kernal),
    .reuse_mode_i(reuse_mode),
    .trans_mode_i(trans_mode),
    .flow_times_i(flow_loop_times),
    .shift_flag_i(shift_flag),
    .last_ins_flag_i(last_ins_flag),
    .load_done_flag(load_done_flag),
    .execute_finished(execute_finished),
    .update_finished(update_finished),
    .write_finished(sram_wr_last),
    .last_ins_wr_done(last_ins_wr_done),
    .regfile_clear_flag(regfile_clear_flag),
    .input_switch_o(input_switch_s),
    .last_flow_time_o(last_flow_time_s),
    .last_flow_time_clear_i(last_flow_time_clear_s),
    .core_state_o(core_state_s),
    .flow_end_o(flow_end),
    .crossbar_done(sau_crossbar_done)
);

mem_addr  # (
    .ADDR_DELAY(6),
    .BASE_ADDR(BASE_ADDR),
    .SRAM_ADDR_WIDTH(SRAM_ADDR_WIDTH),
    .ROW_NUM(ROW_NUM)
)mem_addr_inst (
    .clk(sau_gclk),
    .rst_n(rst_n),
    .start(start),
    .core_state(core_state_s),
    .input_switch(input_switch_s),
    .last_flow_time(last_flow_time_s),
    .sram_wr_valid(sram_wr_valid_ma),
    .sram_wr_last_i(sram_wr_last_ma),
    .regfile_clear_flag(regfile_clear_flag),
    .load_done_flag(load_done_flag),
    .vertical_base_addr_i(vertical_address),
    .vertical_x_step_i(vertical_x_step),
    .vertical_channel_step_i(vertical_c_step),
    .horizontal_base_addr_i(horizontal_address),
    .horizontal_x_step_i(horizontal_x_step),
    .horizontal_channel_step_i(horizontal_c_step),
    .output_base_addr_i(output_address),
    .output_x_step_i(output_x_step),
    .output_channel_step_i(output_c_step),
    .bias_address_i(bias_address),
    .pe_work_mode_i(pe_work_mode),
    .conv_kernal_i(conv_kernal),
    .register_mode_i(register_mode),
    .flow_mode_i(sa_flow_mode),
    .flow_times_i(flow_loop_times),
    .stride_flag_i(stride_flag),
    .shift_flag_i(shift_flag),
    .sram_mem_addr(sram_mem_addr),
    .sram_rd_enable(sram_rd_enable),
    .sram_wr_enable(sram_wr_enable),
    .sram_wr_last_o(sram_wr_last),
    .sram_rdaddr_last(sram_rdaddr_last)
);


mem_ctrl  # (
    .SRAM_DELAY(SRAM_DELAY),
    .SRAM_ADDR_WIDTH(SRAM_ADDR_WIDTH),
    .SRAM_DATA_WIDTH(SRAM_DATA_WIDTH),
    .SRAM_DATA_BYTES(SRAM_DATA_BYTES)
)mem_ctrl_inst (
    .clk(sau_gclk),
    .rst_n(rst_n),
    .core_state(core_state_s),
    .last_ins_flag(last_ins_flag),
    .last_ins_wr_done(last_ins_wr_done),
    .core_register_data_in(core_register_data_in),
    .core_register_data_in_last(core_register_data_in_last),
    .core_register_data_out(core_register_data_out),
    .core_register_data_out_valid(core_register_data_out_valid),
    .core_register_data_out_last(core_register_data_out_last),
    .sram_mem_addr(sram_mem_addr),
    .sram_rd_enable(sram_rd_enable),
    .sram_wr_enable(sram_wr_enable),
    .sram_rdaddr_last(sram_rdaddr_last),
    .sram_rd_data(sau_sram_rdata),
    .sram_enable(sau_sram_enable),
    .sram_wstrb(sau_sram_wstrb),
    .sram_rd_last(sram_rd_last),
    .sram_wr_data(sau_sram_wdata),
    .sram_addr(sau_sram_addr)
);

register_file #(
    .ROW_NUM          (ROW_NUM                    ),
    .BW               (SRAM_DATA_WIDTH            ),
    .REGDEEPTH        (REGDEEPTH                  ),
    .INPUTDW          (INPUTDW                    )
)u_register_file
    (
    .clk_i             (sau_gclk                ),
    .rst_ni            (rst_n                    ),
    .data_mask_i       ({SRAM_DATA_BYTES{1'b1}}   ),
    .conv_kernal_i     (conv_kernal               ),
    .register_mode_i   (register_mode             ),
    .shift_flag        (shift_flag                ),
    .stride_flag       (stride_flag               ),
    .register_clear_flag(last_flow_time_clear_s   ),
    .EN_i              (register_file_wvalid       ),
    .data_i            (register_file_wdata        ),
    .data_rden_i       (register_file_rden        ),
    .data_valid_o      (register_file_rvalid      ),
    .data_o            (register_file_rdata       ),
    .data_last_o       (register_file_rlast       )
);

feeder # (
    .ADDR_DELAY                         (6                         ),
    .BW                                 (SRAM_DATA_WIDTH           ),
    .ROW_NUM                            (ROW_NUM                   ),
    .COL_NUM                            (COL_NUM                   ),
    .SRAM_DELAY                         (SRAM_DELAY                )
)u_feeder(
    .clk_i                              (sau_gclk                  ),
    .rst_ni                             (rst_n                     ),
    .EN_i                               (core_register_data_out_valid),
    .data_i                             (core_register_data_out    ),//todo : must be continous
    .data_mask_i                        ({SRAM_DATA_BYTES{1'b1}}   ),
    .data_last_i                        (core_register_data_out_last),
    .sa_result_in_i                     (result_final_o            ),
    .sa_result_in_valid_i               (result_final_valid_o      ),
    .sa_result_in_last_i                (result_last_o             ),
    .conv_kernal_i                      (conv_kernal               ),
    .flow_times_i                       (flow_loop_times           ),
    .last_ins_flag_i                    (last_ins_flag             ),
    .stride_flag_i                      (stride_flag               ),
    .shift_flag_i                       (shift_flag                ),
    .reuse_mode_i                       (reuse_mode                ),
    .register_mode_i                    (register_mode             ),
    .trans_mode_i                       (trans_mode                ),
    .ins_valid_i                        (start                     ),
    .core_state_i                       (core_state_s              ),
    .input_switch_i                     (input_switch_s            ),
    .last_flow_time_i                   (last_flow_time_s          ),
    .register_file_wvalid_o             (register_file_wvalid      ),
    .register_file_wdata_o              (register_file_wdata       ),
    .register_file_rden_o               (register_file_rden        ),
    .register_file_rvalid_i             (register_file_rvalid      ),
    .register_file_rdata_i              (register_file_rdata       ),
    .register_file_rlast_i              (register_file_rlast       ),
    .data_A_o                           (data_A                    ),
    .data_A_valid_o                     (data_A_valid              ),
    .data_A_last_o                      (data_A_last               ),
    .data_B_valid_o                     (data_B_valid              ),
    .data_B_last_o                      (data_B_last               ),
    .data_B_o                           (data_B                    ),
    .data_C_o                           (data_C                    ),
    .data_C_valid_o                     (data_C_valid              ),
    .sram_wr_data_o                     (core_register_data_in     ),
    .sram_wr_data_last_o                (core_register_data_in_last),
    .sram_wr_valid_o                    (sram_wr_valid_ma          ),
    .sram_wr_last_o                     (sram_wr_last_ma           ),
    .last_flow_time_clear_o             (last_flow_time_clear_s    ),
    .last_flow_time_o                   (last_flow_time_f          ),
    .input_switch_o                     (input_switch_f            )
);


sa_feeder#(
  .ROW_NUM                            (ROW_NUM                   ),
  .COL_NUM                            (COL_NUM                   ),
  .OUTPUTDW                           (24                        ),
  .CNT_DW                             (10                        )
)
u_trans2sa_top(
  .clk_i                              (sau_gclk                  ),
  .rst_ni                             (rst_n                     ),
  .ins_valid_i                        (start                     ),
  .sa_calmode_i                       (pe_work_mode              ),
  .sa_flowmode_i                      (sa_flow_mode              ),
  .trans_mode_i                       (trans_mode                ),
  .reuse_mode_i                       (reuse_mode                ),
  .register_mode_i                    (register_mode             ),
  .conv_kernal_i                      (conv_kernal               ),
  .shift_flag_i                       (shift_flag                ),
  .last_ins_flag_i                    (last_ins_flag             ),
  .input_switch_i                     (input_switch_f            ),
  .last_flow_flag_i                   (last_flow_time_f          ),
  .flow_loop_times_i                  (flow_loop_times           ),
  .row_num_i                          (ROW_VALID_NUM             ),
  .col_num_i                          (COL_VALID_NUM             ),
  .cutbit_i                           (cutbit                    ),
  .data_A_i                           (data_A                    ),
  .data_A_valid_i                     (data_A_valid              ),
  .data_B_i                           (data_B                    ),
  .data_B_valid_i                     (data_B_valid              ),
  .data_C_i                           (data_C                    ),
  .data_C_valid_i                     (data_C_valid              ),
  .result_final_o                     (result_final_o            ),
  .result_final_valid_o               (result_final_valid_o      ),
  .result_last_o                      (result_last_o             ),
  .storage_ready_o                    (storage_ready_o           ),
  .update_finished                    (update_finished           ),
  .execute_done_flag_o                (execute_finished          )
);

endmodule
