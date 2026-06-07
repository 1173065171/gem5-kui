`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2025/01/13 15:24:00
// Design Name: 
// Module Name: register_file_feeder
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////

`include "registers.svh"
module feeder
  import SA_pkg::*;
  #(
  parameter  type                   TagType      = logic,
  parameter  SA_pkg::int_format_e   IntFormat    = SA_pkg::INT8                  ,
  parameter  SA_pkg::int_format_e   IntFormat_q  = SA_pkg::INT16                 ,
  parameter  int unsigned           ROW_NUM      = 8                             ,
  parameter  int unsigned           COL_NUM      = 8                             ,
  parameter  int unsigned           BW           = 128                           ,
  parameter  int unsigned           INS_DW       = 64                            ,
  parameter  int unsigned           ADDR_DELAY   = 6                             ,
  parameter  int unsigned           SRAM_DELAY   = 3                             ,
  parameter  int unsigned           INPUTDW      = 8                             ,
  parameter  int unsigned           QUANTDW      = 16
  )(
  input        logic    clk_i,
  input        logic    rst_ni,
  // mem_ctrl
  input        logic    EN_i,
  input        logic    [BW-1 : 0 ]data_i,
  input        logic    [(BW / INPUTDW)-1 : 0]data_mask_i,
  input        logic    data_last_i,  
  output       logic    [BW-1 :0] sram_wr_data_o,
  output       logic    sram_wr_data_last_o,
  // csr
  input        logic    [3-1 : 0]conv_kernal_i,
  input        logic    [2-1 : 0]reuse_mode_i,
  input        logic    [2-1 : 0]register_mode_i,
  input        logic    [2-1 : 0]trans_mode_i,
  input        logic    [6-1 : 0]flow_times_i,
  input        logic    stride_flag_i,
  input        logic    shift_flag_i,
  input        logic    ins_valid_i,
  input        logic    last_ins_flag_i,
  // scheduler
  input        SA_pkg::registerfile_state_e   core_state_i,
  input        logic    [2-1 : 0]input_switch_i,
  input        logic    last_flow_time_i,
  output       logic    last_flow_time_clear_o,
  output       logic    last_flow_time_o,
  // register file
  output       logic    register_file_wvalid_o,
  output       logic    [ROW_NUM*INPUTDW-1 : 0]register_file_wdata_o,
  output       logic    register_file_rden_o,
  input        logic    register_file_rvalid_i,
  input        logic    [ROW_NUM*INPUTDW-1 : 0]register_file_rdata_i,
  input        logic    register_file_rlast_i,
  // sa_feeder
  input        logic    [COL_NUM * QUANTDW-1 :0] sa_result_in_i,
  input        logic    sa_result_in_valid_i,
  input        logic    sa_result_in_last_i,
  output       logic    data_A_valid_o,
  output       logic    data_A_last_o,
  output       logic    [ROW_NUM*INPUTDW-1 : 0]data_A_o,
  output       logic    data_B_valid_o,
  output       logic    data_B_last_o,
  output       logic    [ROW_NUM*INPUTDW-1 : 0]data_B_o,
  output       logic    [2*ROW_NUM*INPUTDW-1 : 0]data_C_o,
  output       logic    data_C_valid_o,
  output       logic    [2-1 : 0]input_switch_o,
  // mem addr
  output       logic    sram_wr_valid_o,
  output       logic    sram_wr_last_o

  );
  localparam int MEMCTRL_DELAY = 2;
  localparam int STATE_DELAY = SRAM_DELAY + ADDR_DELAY + MEMCTRL_DELAY;
  localparam int HALF_BW = (COL_NUM * QUANTDW) / 2;
  //------------------------input pip--------------------------
  logic  concat_flag_i;
  logic  [3-1 : 0]conv_kernal;
  logic  [2-1 : 0]reuse_mode;
  logic  [2-1 : 0]register_mode;
  logic  [2-1 : 0]trans_mode;
  logic  [6-1 : 0]flow_times;
  logic  reuse_flag;
  logic  trans_flag;
  logic  concat_flag;
  logic  stride_flag;
  logic  conv_reuse_flag;
  logic  pwc_flag;
  logic  last_ins_flag;
  logic  dw_register_flag;
  logic  conv_reuse_flag_reg;
  logic  shift_flag;
  `FFL(conv_kernal, conv_kernal_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(reuse_mode, reuse_mode_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(trans_mode, trans_mode_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(concat_flag, concat_flag_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(stride_flag, stride_flag_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(last_ins_flag, last_ins_flag_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(flow_times, flow_times_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(register_mode, register_mode_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(shift_flag, shift_flag_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(conv_reuse_flag_reg, conv_reuse_flag, ins_valid_i, '0, clk_i, rst_ni)
  assign concat_flag_i = (|conv_kernal_i[2:1]);
  assign reuse_flag = (reuse_mode!=2'b00);
  assign trans_flag = (trans_mode!=2'b00);
  assign conv_reuse_flag = (conv_kernal_i >=3'd3);
  assign pwc_flag = (conv_kernal_i == 3'd1);
  assign dw_register_flag = (register_mode==2'b10);
  logic  EN_i_d;
  logic  [BW-1 : 0 ]data_i_d, data_i_d2;
  logic  EN_i_start;//EN_i posedge
  logic  data_last_d;
  assign EN_i_start = (EN_i & !EN_i_d);
  `FF(EN_i_d, EN_i, '0, clk_i, rst_ni)
  `FF(data_last_d, data_last_i, '0, clk_i, rst_ni)
  `FFNR(data_i_d, data_i, clk_i)
  `FFNR(data_i_d2, data_i_d, clk_i)
  

  //------------------------Label of input data--------------------------
  logic [1 : 0]input_switch;
  SA_pkg::registerfile_state_e  cur_state;
  SA_pkg::registerfile_state_e  cur_state_d;
  logic [0 : STATE_DELAY] [1 : 0]input_switch_reg;
  SA_pkg::registerfile_state_e   [0 : STATE_DELAY]cur_state_reg;
  logic [0 : STATE_DELAY]last_flow_time;
  generate
    assign input_switch_reg[0] = input_switch_i;
    assign cur_state_reg[0] = core_state_i;
    assign last_flow_time[0] = last_flow_time_i;
    for (genvar i = 0; i < STATE_DELAY; i++) begin
        `FF(input_switch_reg[i+1], input_switch_reg[i], '0, clk_i, rst_ni)
        `FF(cur_state_reg[i+1], cur_state_reg[i], IDLE, clk_i, rst_ni)
        `FF(last_flow_time[i+1], last_flow_time[i], '0, clk_i, rst_ni)
    end
  endgenerate
  assign input_switch = input_switch_reg[STATE_DELAY];
  assign cur_state = cur_state_reg[STATE_DELAY];
  `FF(cur_state_d, cur_state, IDLE, clk_i, rst_ni)

  logic  reuse_load_state , register_load_state;
  logic  first_load_state_flag , reuse_load_state_flag , register_load_state_flag;
  assign first_load_state_flag = (cur_state_reg[STATE_DELAY-1] == FIRST_LOAD);
  assign reuse_load_state_flag = (cur_state_reg[STATE_DELAY-1] == REUSE_LOAD) | (cur_state_reg[STATE_DELAY-1] == TRANSPOSE_LOAD);
  assign register_load_state_flag = (cur_state_reg[STATE_DELAY-1] == REGISTER_LOAD);
  `FF(reuse_load_state, reuse_load_state_flag, '0, clk_i, rst_ni)
  `FF(register_load_state, register_load_state_flag, '0, clk_i, rst_ni)
//------------------------last flow time--------------------------
  logic  last_regfile_flag_tmp;
  logic  last_regfile_flag;
  logic  last_regfile_flag_d;
  logic  last_flow_time_d; 
  assign last_regfile_flag_tmp = (!last_flow_time[STATE_DELAY] & last_flow_time[STATE_DELAY-1]);
  `FF(last_flow_time_d, last_flow_time[STATE_DELAY], '0, clk_i, rst_ni)
  `FF(last_regfile_flag, last_regfile_flag_tmp, '0, clk_i, rst_ni)
  `FF(last_regfile_flag_d, last_regfile_flag, '0, clk_i, rst_ni)
//------------------------register file ctrl signal--------------------------
  //interface
  logic  shift_almost_last , shift_data_valid_o , shift_data_last_o;
  logic  [ROW_NUM*INPUTDW-1 : 0]shift_data_o;
  logic  register_file_wvalid;
  assign register_file_wvalid = register_load_state & EN_i;
  `FF(register_file_wvalid_o, register_file_wvalid, '0, clk_i, rst_ni)
  assign register_file_wdata_o = data_i_d;
  //register file output signals
  logic  register_file_out_state;
  logic  register_file_out_flag_d;
  logic  register_file_out_flag;
  logic  register_file_out_in_flag;
  logic  last_regfile_out_flag;
  logic  last_regfile_out_flag_d;
  assign last_regfile_out_flag = (last_regfile_flag_d && flow_times!='d1);
  assign register_file_out_state = (reuse_load_state) | last_flow_time[STATE_DELAY];
  assign register_file_out_in_flag = (cur_state == TRANSPOSE_LOAD & cur_state_d != TRANSPOSE_LOAD) | (EN_i_start & !last_flow_time_d);//each flow's start signal
  assign register_file_rden_o = (register_file_out_flag_d | shift_almost_last) & conv_reuse_flag_reg;
  assign register_file_out_flag = ((register_file_out_in_flag | (last_regfile_out_flag_d & !conv_reuse_flag_reg)) 
                                    & register_file_out_state); //usually according to EN_i_start, only when last flow without EN_i_start
  `FF(last_regfile_out_flag_d, last_regfile_out_flag, '0, clk_i, rst_ni)
  `FF(register_file_out_flag_d, register_file_out_flag, '0, clk_i, rst_ni)

  shift_register #(
    .ROW_NUM          (ROW_NUM                    ),
    .BW               (BW                         ),
    .INPUTDW          (INPUTDW                    )
  )u_shift_register
  (
    .clk_i             (clk_i                     ),
    .rst_ni            (rst_ni                    ),
    .EN_i              (register_file_rvalid_i    ),
    .data_i            (register_file_rdata_i     ),
    .data_last_i       (register_file_rlast_i     ),
    .conv_kernal_i     (conv_kernal               ),
    .stride_flag       (stride_flag               ),
    .data_almost_last_o(shift_almost_last         ),
    .data_valid_o      (shift_data_valid_o        ),
    .data_last_o       (shift_data_last_o         ),
    .data_o            (shift_data_o              ) 
  );
//------------------------register file out pip--------------------------
  logic  [1:0]input_switch_d;
  `FF(input_switch_d, input_switch, '0, clk_i, rst_ni)

  logic  [5:0]shift_data_cnt;
  logic  [5:0]shift_data_cnt_result;
  logic  shift_data_cnt_valid;
  logic  shift_data_cnt_clear;

  assign shift_data_cnt_result = shift_data_cnt + 'd1;
  assign shift_data_cnt_valid = (shift_data_cnt != 'd0) | shift_data_valid_o;
  assign shift_data_cnt_clear = shift_data_cnt == (6)'(SA_pkg::get_exe_cycle(conv_kernal)-1);
  `FFLARNC(shift_data_cnt, shift_data_cnt_result, shift_data_cnt_valid, shift_data_cnt_clear, '0, clk_i, rst_ni)
//------------------------output num stat machine--------------------------
    localparam REGISTER_DELAY = 2;
    typedef enum logic [1:0] {
    NO_INPUT  = 2'b00,
    ONE_INPUT = 2'b01,
    TWO_INPUT = 2'b10
    } outputnum_state_t;
    outputnum_state_t cur_out_state, next_out_state;
    `FF(cur_out_state, next_out_state, NO_INPUT, clk_i, rst_ni)
    always_comb begin
      next_out_state = cur_out_state;
      case(cur_out_state)
        NO_INPUT: begin
          if(!trans_flag & data_last_i)
            next_out_state = TWO_INPUT;
          else if(EN_i_start & ~conv_reuse_flag_reg)
            next_out_state = ONE_INPUT;
          else if(trans_flag & conv_reuse_flag_reg & data_last_i)
            next_out_state = ONE_INPUT;
          else
            next_out_state = NO_INPUT;
        end
        ONE_INPUT: begin
          if(last_regfile_flag)
            next_out_state = NO_INPUT;
          else if(conv_reuse_flag_reg & reuse_load_state & (cur_state == REUSE_LOAD & cur_state_d == TRANSPOSE_LOAD))
            next_out_state = TWO_INPUT;
          else 
            next_out_state = ONE_INPUT;
        end
        TWO_INPUT: begin
            if(last_regfile_flag & conv_reuse_flag_reg)
              next_out_state = NO_INPUT;
            else if(!(trans_flag | reuse_flag) & data_last_i)
              next_out_state = ONE_INPUT;
            else
              next_out_state = TWO_INPUT;
        end
      endcase
    end
//------------------------out pipe --------------------------
    outputnum_state_t  [REGISTER_DELAY-1:0]cur_out_state_o;
    logic  [REGISTER_DELAY:0]EN_i_d_o;
    logic  [REGISTER_DELAY:0][1:0]input_switch_d_o;
    logic  [REGISTER_DELAY:0]EN_last_flag_o;
    logic  [REGISTER_DELAY:0]last_flow_time_d_o;

//------------------------data in case according to bandwidth for pwconv--------------------------    
    logic  [ROW_NUM * INPUTDW -1 : 0]data_i_case0;
    logic  [ROW_NUM * INPUTDW -1 : 0]data_i_case0_reg;
    logic  pwc_16b_flag;

    logic  [2*ROW_NUM * INPUTDW-1 : 0]data_i_combine;
    logic  [2*ROW_NUM * INPUTDW-1 : 0]data_i_combine_reg;
    
    // 控制信号
    logic  data_i_combine_valid;
    logic  data_i_combine_valid_reg;
    logic  data_i_combine_valid_clr;
    
    // 移位控制
    logic  data_i_combine_shift_flag;
    logic  data_i_combine_shift_flag_reg;
    logic  [ROW_NUM * INPUTDW-1 : 0] data_slice_even;
    logic  [ROW_NUM * INPUTDW-1 : 0] data_slice_odd;
    logic  [ROW_NUM * INPUTDW-1 : 0] data_slice_selected;

    assign pwc_16b_flag = (pwc_flag & shift_flag_i);
    assign data_i_combine_valid = ~data_i_combine_valid_reg;
    assign data_i_combine_valid_clr = (input_switch[0] ^ input_switch[1]) | ~pwc_16b_flag | last_flow_time_d;//仅当int16 pwconv 读取特征图的时候生效

    logic combine_reg_wr_en;
    assign combine_reg_wr_en = data_i_combine_valid_reg & pwc_16b_flag;
    assign data_i_combine = {data_i[ROW_NUM * INPUTDW -1 : 0] , data_i_d[ROW_NUM * INPUTDW -1 : 0]};
    `FFLARNC(data_i_combine_valid_reg, data_i_combine_valid, EN_i, data_i_combine_valid_clr, '0, clk_i, rst_ni)
    `FFLNR(data_i_combine_reg, data_i_combine, combine_reg_wr_en, clk_i)

    `FFNR(data_i_combine_shift_flag, data_i_combine_valid_reg, clk_i)
    `FFNR(data_i_combine_shift_flag_reg, data_i_combine_shift_flag, clk_i)
    generate
      for(genvar k = 1; k <= ROW_NUM; k++) begin : wire_splitting
         // 提取偶数位置的数据块 (Base 0, 2, 4...)
         assign data_slice_even[(k-1)*INPUTDW +: INPUTDW] = data_i_combine_reg[(2*k - 2)*INPUTDW +: INPUTDW];
         // 提取奇数位置的数据块 (Base 1, 3, 5...)
         assign data_slice_odd[(k-1)*INPUTDW +: INPUTDW]  = data_i_combine_reg[(2*k - 1)*INPUTDW +: INPUTDW];
      end
    endgenerate
    assign data_slice_selected = (data_i_combine_shift_flag_reg) ? data_slice_odd : data_slice_even;

    logic use_combined_data;
    assign use_combined_data = pwc_16b_flag && (input_switch_d[0] == input_switch_d[1]);
    assign data_i_case0 = use_combined_data ? data_slice_selected : data_i_d2[ROW_NUM * INPUTDW -1 : 0];
    `FFNR(data_i_case0_reg, data_i_case0, clk_i)
//------------------------ out pipe -------------------------
    assign EN_i_d_o[0] = EN_i_d & !last_flow_time_d;
    assign input_switch_d_o[0] = input_switch_d;
    assign EN_last_flag_o[0] = data_last_d;
    assign last_flow_time_d_o[0] = last_flow_time_d;
    assign cur_out_state_o[0] = cur_out_state;
    generate
      for (genvar k = 0; k < REGISTER_DELAY; k++) begin
        `FFNR(input_switch_d_o[k+1], input_switch_d_o[k], clk_i)
        `FFNR(EN_i_d_o[k+1], EN_i_d_o[k], clk_i)
        `FFNR(EN_last_flag_o[k+1], EN_last_flag_o[k], clk_i)
        `FFNR(last_flow_time_d_o[k+1], last_flow_time_d_o[k], clk_i)
      end
      for (genvar l = 0; l < REGISTER_DELAY-1; l++) begin
        `FFNR(cur_out_state_o[l+1], cur_out_state_o[l], clk_i)
      end
    endgenerate
//------------------------A B ARBITER--------------------------
  logic  NO_INPUT_state_flag , ONE_INPUT_state_flag;
  logic  NO_INPUT_state , ONE_INPUT_state;
  assign NO_INPUT_state_flag  = (cur_out_state_o[REGISTER_DELAY-1] == NO_INPUT);
  assign ONE_INPUT_state_flag = (cur_out_state_o[REGISTER_DELAY-1] == ONE_INPUT);
  `FF(NO_INPUT_state, NO_INPUT_state_flag, '0, clk_i, rst_ni)
  `FF(ONE_INPUT_state, ONE_INPUT_state_flag, '0, clk_i, rst_ni)
  
  logic  input_switch_case_flag;
  logic  input_switch_case;// ATB: 01(input_switch) , ABT: 10(input_switch) , AB: 01(input_switch)
  assign input_switch_case_flag = (~input_switch_d_o[REGISTER_DELAY-1][0]);
  `FF(input_switch_case, input_switch_case_flag, '0, clk_i, rst_ni)

  logic data_A_enable, data_B_enable;
  assign data_A_enable = input_switch_case & ONE_INPUT_state;
  assign data_B_enable = (!input_switch_case) & (!NO_INPUT_state);
  // Data A Path
  logic  [ROW_NUM*INPUTDW-1 : 0]data_A;
  logic  data_A_valid, data_A_last;
  always_comb begin
      if (conv_reuse_flag_reg) begin
          // CONV 模式
          data_A      = shift_data_o;
          data_A_valid = shift_data_cnt_valid; // 假设 shift_data_cnt_valid 已经是有效信号
          data_A_last  = shift_data_cnt_clear;
      end else if (data_A_enable) begin
          // 普通模式 A 通道
          data_A      = data_i_case0_reg;
          data_A_valid = EN_i_d_o[REGISTER_DELAY];
          data_A_last  = EN_last_flag_o[REGISTER_DELAY];
      end else begin
          // 空闲/无效
          data_A       = '0;
          data_A_valid = 1'b0;
          data_A_last  = 1'b0;
      end
  end
  // Data B Path
  logic  [ROW_NUM*INPUTDW-1 : 0]data_B;
  logic  data_B_valid, data_B_last;
  always_comb begin
      if (data_B_enable) begin
          data_B       = data_i_case0_reg;
          data_B_valid = EN_i_d_o[REGISTER_DELAY];
          data_B_last  = EN_last_flag_o[REGISTER_DELAY];
      end else begin
          data_B       = '0;
          data_B_valid = 1'b0;
          data_B_last  = 1'b0;
      end
  end
  // Data C Path
  logic  [2*ROW_NUM*INPUTDW-1 : 0]data_C;
  logic  data_C_valid;
  assign data_C_valid = EN_i & last_flow_time_d;
  assign data_C = data_C_valid ? (dw_register_flag ? {COL_NUM{data_i[(INPUTDW * COL_NUM -1) -: (2 * INPUTDW)]}} : data_i_combine)
                              : '0;
  // pipeline registers
  `FFNR(data_A_o, data_A, clk_i)
  `FFNR(data_B_o, data_B, clk_i)
  `FFNR(data_C_o, data_C, clk_i)
  `FF(data_A_valid_o, data_A_valid, '0, clk_i, rst_ni)
  `FF(data_B_valid_o, data_B_valid, '0, clk_i, rst_ni)
  `FF(data_C_valid_o, data_C_valid, '0, clk_i, rst_ni)
  `FF(data_A_last_o, data_A_last, '0, clk_i, rst_ni)
  `FF(data_B_last_o, data_B_last, '0, clk_i, rst_ni)
  `FF(input_switch_o, input_switch_d_o[REGISTER_DELAY], '0, clk_i, rst_ni)
  `FF(last_flow_time_o, last_flow_time_d_o[REGISTER_DELAY], '0, clk_i, rst_ni)
  assign last_flow_time_clear_o = last_regfile_flag;
  //------------------------output data buffer--------------------------
  logic  out_buffer_valid;
  logic  [COL_NUM*QUANTDW-1 : 0]out_buffer[ROW_NUM];
  logic  pingpong_out_flag;
  logic  sequential_out_flag;
  logic  special_out_flag;
  assign pingpong_out_flag = ((~last_flow_time[4] & last_flow_time[3]) & out_buffer_valid);
  assign sequential_out_flag = (last_ins_flag && sa_result_in_valid_i && core_state_i == SA_pkg::D_OUT);
  assign special_out_flag = (flow_times == 1 || (flow_times == 2 && (shift_flag || conv_reuse_flag))) && sa_result_in_valid_i && core_state_i == SA_pkg::D_OUT;
  typedef struct packed {
    logic [$clog2(ROW_NUM)-1:0]cnt;
    logic [$clog2(ROW_NUM)-1:0]cnt_result;
    logic cnt_valid;
    logic cnt_clear;
  } out_buffer_incnt_t;
  typedef struct packed {
    logic [$clog2(ROW_NUM*2)-1:0]cnt;       
    logic [$clog2(ROW_NUM*2)-1:0]cnt_result;
    logic cnt_valid;
    logic cnt_clear;
  } out_buffer_outcnt_t;
  out_buffer_incnt_t out_buffer_incnt; 
  out_buffer_outcnt_t out_buffer_outcnt, sram_wr_cnt;
  //out_buffer_incnt
  assign out_buffer_incnt.cnt_valid = sa_result_in_valid_i;
  assign out_buffer_incnt.cnt_clear = sa_result_in_last_i;
  assign out_buffer_incnt.cnt_result = out_buffer_incnt.cnt +1;
  `FFLARNC(out_buffer_incnt.cnt, out_buffer_incnt.cnt_result, out_buffer_incnt.cnt_valid, out_buffer_incnt.cnt_clear, '0, clk_i, rst_ni)
  `FFLARNC(out_buffer_valid, 1'b1, out_buffer_incnt.cnt_valid, out_buffer_outcnt.cnt_clear, '0, clk_i, rst_ni)
  //sram_wr_cnt
  logic  sram_wr_start_flag;// ((flow_times=='d2) 要换成 8bit时 flow_times=='d1 16bit时 flow_times=='d2)
  logic [$clog2(ROW_NUM*2)-1:0] max_cnt_val;
  assign max_cnt_val = shift_flag ? (2*ROW_NUM - 1) : (ROW_NUM - 1);
  assign sram_wr_start_flag =  |{special_out_flag, sequential_out_flag, pingpong_out_flag};
  assign sram_wr_cnt.cnt_clear = (sram_wr_cnt.cnt == max_cnt_val);
  assign sram_wr_cnt.cnt_result = sram_wr_cnt.cnt + 1;
  assign sram_wr_last_o = sram_wr_cnt.cnt_clear;
  `FFLARNC(sram_wr_cnt.cnt, sram_wr_cnt.cnt_result, sram_wr_cnt.cnt_valid, sram_wr_cnt.cnt_clear, '0, clk_i, rst_ni)
  `FFLARNC(sram_wr_cnt.cnt_valid, 1'b1, sram_wr_start_flag, sram_wr_cnt.cnt_clear, '0, clk_i, rst_ni)
  assign sram_wr_valid_o = sram_wr_cnt.cnt_valid;//case
  //out_buffer_outcnt
  logic  outcnt_start_flag;
  logic  sram_wr_start_flag_d[ADDR_DELAY];
  assign sram_wr_start_flag_d[0] = sram_wr_start_flag;
  generate
    for (genvar l = 0; l < (ADDR_DELAY-1); l++) begin
      `FF(sram_wr_start_flag_d[l+1], sram_wr_start_flag_d[l], '0, clk_i, rst_ni)
    end
  endgenerate
  assign outcnt_start_flag = sram_wr_start_flag_d[ADDR_DELAY-1];
  assign out_buffer_outcnt.cnt_clear = out_buffer_outcnt.cnt == max_cnt_val;
  assign out_buffer_outcnt.cnt_result = out_buffer_outcnt.cnt + 1;
  `FFLARNC(out_buffer_outcnt.cnt, out_buffer_outcnt.cnt_result, out_buffer_outcnt.cnt_valid, out_buffer_outcnt.cnt_clear, '0, clk_i, rst_ni)
  `FFLARNC(out_buffer_outcnt.cnt_valid, 1'b1, outcnt_start_flag, out_buffer_outcnt.cnt_clear, '0, clk_i, rst_ni)
  `FF(sram_wr_data_last_o, out_buffer_outcnt.cnt_clear, '0, clk_i, rst_ni)
  //out_buffer
  generate
    for (genvar j = 0; j < ROW_NUM; j++) begin : OUT_BUFFER
      logic  element_write_en;
      assign element_write_en = out_buffer_incnt.cnt_valid & (out_buffer_incnt.cnt == j);
      always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
          out_buffer[j] <= {(COL_NUM*QUANTDW){1'b0}};
        end else if (element_write_en) begin
          out_buffer[j] <= sa_result_in_i;
        end
        else begin
          out_buffer[j] <= out_buffer[j];
        end
      end
    end
  endgenerate

  logic [$clog2(ROW_NUM)-1 : 0] read_row_idx;
  logic read_high_part;
  logic [HALF_BW*2 -1 : 0] current_row_data;
  logic [HALF_BW - 1 : 0] final_out_data;
  assign read_row_idx = shift_flag ? out_buffer_outcnt.cnt[$clog2(ROW_NUM*2)-1 : 1] : out_buffer_outcnt.cnt[$clog2(ROW_NUM)-1 : 0];
  assign read_high_part = shift_flag & out_buffer_outcnt.cnt[0];
  assign current_row_data = out_buffer[read_row_idx];
  assign final_out_data = read_high_part ? current_row_data[2*HALF_BW-1 : HALF_BW] : current_row_data[HALF_BW-1 : 0];
  `FFLNR(sram_wr_data_o, final_out_data, out_buffer_outcnt.cnt_valid, clk_i)

endmodule