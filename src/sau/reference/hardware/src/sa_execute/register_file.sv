`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2025/01/14 17:57:56
// Design Name: 
// Module Name: register_file
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
/*
four ways for 14/16:
1��base addr change
2��concat sequential addr data to one register(half store space)
3��reuse row register shift when last_flow_time_clear_o(former layer)
4��reuse col register shift when last_flow_time_clear_o(former layer)
*/
`include "registers.svh"
module register_file
  import SA_pkg::*;
  #(
  parameter  SA_pkg::int_format_e   IntFormat    = SA_pkg::INT8                  ,
  parameter  int unsigned           ROW_NUM      = 8                             ,
  parameter  int unsigned           BW           = 128                           ,
  parameter  int unsigned           REGDEEPTH    = 24                            ,
  parameter  int unsigned           INPUTDW      = 8
  )(
  input        logic    clk_i             ,
  input        logic    rst_ni            ,
  input        logic    [(BW / INPUTDW)-1 : 0]data_mask_i,
  input        logic    [3-1 : 0]conv_kernal_i,
  input        logic    [2-1 : 0]register_mode_i,
  input        logic    stride_flag,
  input        logic    shift_flag,
  input        logic    register_clear_flag,
  input        logic    EN_i              ,
  input        logic    [BW-1 : 0 ]data_i ,
  input        logic    data_rden_i,
  output       logic    data_valid_o,
  output       logic    [ROW_NUM*INPUTDW-1 : 0]data_o,
  output       logic    data_last_o
    );
  localparam BW_NUM = BW / INPUTDW;
  localparam CONCAT_NUM = 1;
  localparam STRIDE = 1'b1;//ADDR / REGISTER / SHIFT_R
  localparam HALF_DP = REGDEEPTH / 2;
  integer i;
  logic  dw_register_flag;
  logic  concat_flag;
  assign dw_register_flag = (register_mode_i==2'b10);
  assign concat_flag = (|conv_kernal_i[2:1]);

  logic  [CONCAT_NUM+STRIDE : 0]rden_d;
  logic  [CONCAT_NUM+STRIDE : 0]rden_valid;
  logic  [$clog2(REGDEEPTH)-1 : 0]register_in_cnt;
  logic  [$clog2(REGDEEPTH)-1 : 0]register_in_cnt_result;
  logic  [$clog2(REGDEEPTH)-1 : 0]register_in_cnt_result16;
  logic  register_in_cnt_valid;
  logic  register_cnt_clear_flag;
  logic  register_file_clear_flag;
  logic  [(ROW_NUM+2)*INPUTDW-1 : 0]register[REGDEEPTH];
  logic  register_rden_valid;
  assign register_in_cnt_result = register_in_cnt + 1;
  assign register_in_cnt_result16 = register_in_cnt + REGDEEPTH/2;
  assign register_cnt_clear_flag = (register_in_cnt == REGDEEPTH-1 ) & register_in_cnt_valid | register_clear_flag;
  `FFLARNC(register_in_cnt, register_in_cnt_result, register_in_cnt_valid, register_cnt_clear_flag, '0, clk_i, rst_ni)
  assign rden_d[0] = register_rden_valid & data_rden_i;
  assign rden_valid[0] = register_rden_valid & data_rden_i;
  generate //limited conv_kernal must >=3
      for(genvar k = 0; k < (CONCAT_NUM+STRIDE); k++) begin
          `FF(rden_d[k+1] , rden_d[k], '0, clk_i, rst_ni)
          assign rden_valid[k+1] = |{rden_d[k+1],rden_valid[k]};
      end
  endgenerate

  //------------------------SHIFT CNT------------------------
  logic  [0 : 0]shift_cnt;
  logic  [0 : 0]shift_cnt_result;
  logic  shift_cnt_clear_flag;
  assign shift_cnt_result = ~shift_cnt;
  
  //------------------------MASK REGISTER------------------------
  wire  [BW-1 : 0]data_mask_tmp;
  logic [ROW_NUM*INPUTDW-1 : 0]data_mask_tmp_16b_d;
  logic [ROW_NUM*INPUTDW-1 : 0]data_mask_tmp_16b_h;
  logic [ROW_NUM*INPUTDW-1 : 0]data_mask_tmp_in;
  logic [ROW_NUM*INPUTDW-1 : 0]data_mask_tmp_reg;
  logic [2*ROW_NUM*INPUTDW-1 : 0]data_mask_tmp_16b;
  generate
      for(genvar j = 0; j < BW_NUM; j++) begin
          assign data_mask_tmp[j*INPUTDW +: INPUTDW] = (!data_mask_i[j]) ? {INPUTDW{1'd0}} : (data_i[j*INPUTDW +: INPUTDW]);
      end
  endgenerate
  //16x16
  assign data_mask_tmp_16b = shift_flag ? {data_mask_tmp, data_mask_tmp_reg} : '0;
  `FFLNR(data_mask_tmp_reg, data_mask_tmp, shift_flag, clk_i)
  always_comb begin
      SA_pkg::split_even_odd(
          .in_data        (data_mask_tmp_16b),
          .out_even_chunks(data_mask_tmp_16b_d),
          .out_odd_chunks (data_mask_tmp_16b_h)
      );
  end    
  assign data_mask_tmp_in = shift_flag ? data_mask_tmp_16b_d : data_mask_tmp;
  //------------------------FEATURE MAP STORAGE FSM------------------------
  logic extern_kernal_flag;
  assign extern_kernal_flag = (conv_kernal_i > 3);
  typedef enum logic [2:0] {
    IDLE      = 3'b000,
    LOADING   = 3'b001,
    PADDING   = 3'b010,
    SHIFTING  = 3'b011,
    STRIDING  = 3'b100,
    STRSHIFT  = 3'b101,
    STORING   = 3'b110
  } reg_state_t;
  reg_state_t reg_state, reg_next_state;
  `FF(reg_state, reg_next_state, IDLE, clk_i, rst_ni)
  always_comb begin: reg_state_fsm
    reg_next_state = reg_state;
    case(reg_state)
        IDLE: begin
            if(conv_kernal_i >= 1 && EN_i) begin
                reg_next_state = LOADING;
            end
            else begin
                reg_next_state = IDLE;
            end
        end
        LOADING: begin
            if(EN_i) begin
              if(shift_flag)
                reg_next_state = SHIFTING;
              else if (stride_flag)
                reg_next_state = STRIDING;
              else
                reg_next_state = PADDING;
            end
            else begin
                reg_next_state = LOADING;
            end
        end
        PADDING: begin
            if(EN_i) begin
              reg_next_state = LOADING;
            end
            else begin
              reg_next_state = STORING;
            end
        end
        SHIFTING: begin
            if(stride_flag & EN_i) begin
              reg_next_state = STRIDING;
            end
            else begin
              reg_next_state = PADDING;
            end
        end
        STRIDING: begin
          if(shift_flag & EN_i) begin
            reg_next_state = STRSHIFT;
          end
          else begin
            reg_next_state = PADDING;
          end
        end
        STRSHIFT: begin
          if(EN_i) begin
            reg_next_state = PADDING;
          end
          else begin
            reg_next_state = STRSHIFT;
          end
        end
        STORING: begin
            if(register_file_clear_flag) begin
              reg_next_state = IDLE;
            end
            else if (EN_i) begin
              reg_next_state = LOADING;
            end
            else begin
              reg_next_state = STORING;
            end
        end
        default: begin
            reg_next_state = IDLE;
        end
    endcase
  end  
  logic register_in_cnt_valid_t;
  always_comb begin
    register_in_cnt_valid_t = 1'd0;
    if (!EN_i) begin
      register_in_cnt_valid_t = 1'd0;
    end
    else begin
      case(reg_state)
          IDLE, PADDING, STORING: 
              register_in_cnt_valid_t = !shift_flag && (extern_kernal_flag || stride_flag);
          LOADING: 
              register_in_cnt_valid_t = extern_kernal_flag || (stride_flag == shift_flag);
          SHIFTING:
              register_in_cnt_valid_t = !stride_flag;
          STRIDING:
              register_in_cnt_valid_t = !shift_flag || extern_kernal_flag;
          STRSHIFT:
               register_in_cnt_valid_t = 1'b1;
          default: 
               register_in_cnt_valid_t = 1'd0;
      endcase
    end
  end
  
  //------------------------ADDITIONAL FEATURE MAP STORAGE(fix the bug of 14/16 receptive field)------------------------
  logic  [(ROW_NUM)*INPUTDW-1 : 0]additional_register;
  logic  [(ROW_NUM)*INPUTDW-1 : 0]additional_register_16b;
  logic  [(ROW_NUM+2)*INPUTDW-1 : 0]additional_register_load;
  logic  [(ROW_NUM+2)*INPUTDW-1 : 0]additional_register_load_16b;
  logic  additional_flag;
  logic  [3-1 : 0]additional_cnt;
  logic  [3-1 : 0]additional_cnt_result;
  logic  additional_clear_flag;
  logic  [2*INPUTDW-1 : 0]additional_data;
  logic  [2*INPUTDW-1 : 0]additional_data_16b_h;
  assign additional_data_16b_h = data_mask_tmp_16b_h[64 +: 2*INPUTDW];
  assign additional_register_load = (additional_clear_flag & ~extern_kernal_flag) ? {additional_data, additional_register}
                                               : {{(2*INPUTDW){1'd0}}, data_mask_tmp_in };
  assign additional_register_load_16b = (additional_clear_flag & ~extern_kernal_flag) ? {additional_data_16b_h, additional_register_16b}
                                               : {{(2*INPUTDW){1'd0}}, data_mask_tmp_16b_h};
  assign additional_data = shift_flag ? data_mask_tmp_in[64 +: 2*INPUTDW] : data_mask_tmp_in[0 +: 2*INPUTDW];
  assign additional_cnt_result = additional_cnt + 1;
  assign additional_clear_flag = (additional_cnt == 3'(1 + (stride_flag << shift_flag) + shift_flag));
  assign register_in_cnt_valid = register_in_cnt_valid_t;
  `FFLARNC(additional_cnt, additional_cnt_result, EN_i, additional_clear_flag, '0, clk_i, rst_ni)
  `FFNR(additional_register, data_mask_tmp_in, clk_i)
  `FFNR(additional_register_16b, data_mask_tmp_16b_h, clk_i)
  //------------------------FEATURE MAP STORAGE------------------------
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (i = 0; i < REGDEEPTH; i++) begin
        register[i] <= '0;
      end
    end else if (EN_i) begin
      if (shift_flag && (register_in_cnt_result16 == register_in_cnt)) begin
        register[register_in_cnt] <= additional_register_load_16b;
      end else begin
        register[register_in_cnt] <= additional_register_load;
        if (shift_flag) begin
          register[register_in_cnt_result16] <= additional_register_load_16b;
        end
      end
    end
  end
  //------------------------FEATURE MAP output ctrl signal------------------------
  logic  [6-1 : 0]register_out_cnt;
  logic  [2-1 : 0]extended_num_t;
  logic  [2-1 : 0]extended_num;
  logic  [6-1 : 0]register_out_cnt_result;
  logic  register_out_clear_flag;
  logic  register_out_cnt_valid;
  logic  register_out_cnt_valid_d;  
  logic  register_out_done_flag;
  logic  register_out_done_flag_reg;
  logic  [6-1 : 0]register_out_cycle;
  logic  [6-1 : 0]register_out_cycle_result;
  logic  first_flow;//first flow from register output,start from 0,next flow start from last flow
  logic  first_flow_valid;
  assign register_out_cycle_result = (extended_num_t+1)*conv_kernal_i;
  assign extended_num_t = concat_flag + stride_flag;//selct additional featuremap or not
  assign register_out_cnt_valid = (register_out_cnt <= (6)'((extended_num+1)*conv_kernal_i-1) ) & (rden_valid[extended_num]);
  assign register_out_cnt_result = register_out_cnt + 1;
  assign additional_flag = extern_kernal_flag ? (|rden_d[1:0] | (stride_flag & rden_d[extended_num])) : (stride_flag ^ rden_d[concat_flag]);//need output one more
  assign first_flow_valid = shift_flag ? register_out_done_flag & shift_cnt : register_out_done_flag;
  assign register_out_done_flag = register_out_cnt_valid && (register_out_cnt == register_out_cycle-1);
  assign shift_cnt_clear_flag = register_file_clear_flag | ~shift_flag;
  `FF(register_out_cnt_valid_d, register_out_cnt_valid, '0, clk_i, rst_ni)
  `FF(register_out_done_flag_reg, register_out_done_flag, '0, clk_i, rst_ni)
  `FFARNC(register_out_cycle, register_out_cycle_result, register_file_clear_flag, '0, clk_i, rst_ni)
  `FFARNC(extended_num, extended_num_t, register_file_clear_flag, '0, clk_i, rst_ni)
  `FFLARNC(first_flow, 1'd1, first_flow_valid, register_file_clear_flag, '0, clk_i, rst_ni)
  `FFLARNC(shift_cnt, shift_cnt_result, register_out_done_flag_reg, shift_cnt_clear_flag, '0, clk_i, rst_ni)
  `FFLARNC(register_rden_valid, 1'd0, register_out_done_flag, data_rden_i, 1'd1, clk_i, rst_ni)
  //------------------------FEATURE MAP output data signal------------------------
  logic  [ROW_NUM*INPUTDW-1 : 0]data_o_result;
  logic  [$clog2(REGDEEPTH)-1 : 0]flow_cnt;  
  logic  flow_cnt_clear;
  logic  flow_cnt_clear_reg;
  logic  [$clog2(REGDEEPTH)-1 : 0]flow_cnt_16b_down;
  logic  [$clog2(REGDEEPTH)-1 : 0]flow_cnt_16b_high;
  logic  [$clog2(REGDEEPTH)-1 : 0]flow_cnt_16b_down_result;
  logic  [$clog2(REGDEEPTH)-1 : 0]flow_cnt_16b_high_result;
  logic  flow_cnt_16b_down_valid;
  logic  flow_cnt_16b_high_valid;
  logic  flow_cnt_result_valid;
  logic  additional_shift_flag;
  logic  data_valid_o_d;
  logic  [ROW_NUM*INPUTDW-1 : 0]data_o_d;  
  logic  fmap_reuse_flag;// dwc mode only 
  logic  fmap_reuse_flag_reg;// dwc mode only 
  assign flow_cnt_16b_down_result = fmap_reuse_flag_reg ? (flow_cnt_16b_down - 2) : (flow_cnt_16b_down + 1);//first flow from register output,start from 0
  assign flow_cnt_16b_high_result = fmap_reuse_flag_reg ? (flow_cnt_16b_high - 2) : (flow_cnt_16b_high + 1);//first flow from register output,start from 0
  assign flow_cnt_result_valid  =  register_out_cnt_valid & additional_flag | fmap_reuse_flag_reg;
  assign flow_cnt_clear = ((flow_cnt == REGDEEPTH-1) && flow_cnt_result_valid) | register_clear_flag;
  assign register_out_clear_flag = register_out_done_flag_reg;
  assign additional_shift_flag = ~extern_kernal_flag & rden_d[extended_num];
  assign data_o_result = register[flow_cnt][additional_shift_flag*2*INPUTDW +: ROW_NUM*INPUTDW];
  assign flow_cnt_16b_down_valid = flow_cnt_result_valid & ~shift_cnt;
  assign flow_cnt_16b_high_valid = flow_cnt_result_valid & shift_cnt;
  `FFLARNC(register_out_cnt, register_out_cnt_result, register_out_cnt_valid, register_out_clear_flag, '0, clk_i, rst_ni)
  `FFLARNC(flow_cnt_16b_high, flow_cnt_16b_high_result, flow_cnt_16b_high_valid, register_file_clear_flag, HALF_DP, clk_i, rst_ni)
  `FFLARNC(flow_cnt_16b_down, flow_cnt_16b_down_result, flow_cnt_16b_down_valid, register_file_clear_flag, '0, clk_i, rst_ni)
  assign flow_cnt = shift_cnt ? flow_cnt_16b_high : flow_cnt_16b_down;
  assign fmap_reuse_flag = dw_register_flag & register_out_clear_flag &first_flow;
  `FF(fmap_reuse_flag_reg, fmap_reuse_flag, '0, clk_i, rst_ni)
  `FF(flow_cnt_clear_reg, flow_cnt_clear, '0, clk_i, rst_ni)
  `FF(register_file_clear_flag, flow_cnt_clear_reg, '0, clk_i, rst_ni)

  //------------------------out signal------------------------
  `FFL(data_o_d, data_o_result, register_out_cnt_valid, '0, clk_i, rst_ni)
  `FF(data_valid_o_d,  register_out_cnt_valid, '0, clk_i, rst_ni)
  assign  data_valid_o = data_valid_o_d;
  assign  data_o = data_o_d;
  assign  data_last_o = register_out_done_flag_reg;

endmodule
