`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2025/01/13 13:22:11
// Design Name: 
// Module Name: shift_register
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
module shift_register //todo nhwc nchw
  import SA_pkg::*;
  #(
  parameter  type                   TagType      = logic,
  parameter  SA_pkg::int_format_e   IntFormat    = SA_pkg::INT8                  ,
  parameter  SA_pkg::int_format_e   IntFormat_q  = SA_pkg::INT16                 ,
  parameter  int unsigned           ROW_NUM      = 8                             ,
  parameter  int unsigned           BW           = 128                           ,
  parameter  int unsigned           INPUTDW      = 8
  )(
    input        logic    clk_i             ,
    input        logic    rst_ni            ,
    input        logic    EN_i              ,
    input        logic    [ROW_NUM * INPUTDW -1 : 0 ]data_i ,
    input        logic    data_last_i       ,
    input        logic    [3-1 : 0]conv_kernal_i,
    input        logic    stride_flag       ,
    output       logic    data_valid_o      ,
    output       logic    data_last_o       ,
    output       logic    data_almost_last_o,
    output       logic    [ROW_NUM * INPUTDW-1 : 0]data_o
    );
  localparam BW_NUM = BW / INPUTDW;
  localparam STRIDE = 1;
  //cnt 
  logic [3-1 : 0]kernal_cnt;
  logic [3-1 : 0]kernal_cnt_result;
  logic kernal_cnt_valid_tmp;
  logic kernal_cnt_valid;
  logic kernal_cnt_clear_flag;
  logic extern_kernal_flag;
  assign extern_kernal_flag = (conv_kernal_i > 3);
  assign kernal_cnt_clear_flag = (kernal_cnt == conv_kernal_i -1);
  assign kernal_cnt_result = kernal_cnt + 'd1;
  assign kernal_cnt_valid = kernal_cnt_valid_tmp | EN_i;
  `FFLARNC(kernal_cnt_valid_tmp, 1'd1, EN_i, kernal_cnt_clear_flag, '0, clk_i, rst_ni)
  `FFLARNC(kernal_cnt, kernal_cnt_result, kernal_cnt_valid, kernal_cnt_clear_flag, '0, clk_i, rst_ni)
  //shift data
  logic [(ROW_NUM+1)*INPUTDW-1 : 0]data_i_tmp;
  logic [ROW_NUM*INPUTDW-1 : 0]shift_data_tmp;
  logic [ROW_NUM*INPUTDW-1 : 0]shift_data_result;
  logic [(ROW_NUM)*INPUTDW-1 : 0]shift_data;
  generate
    assign data_i_tmp = {data_i, {INPUTDW{1'b0}}};
  endgenerate
  generate
    assign shift_data_result = (kernal_cnt == 0) ? data_i_tmp[(ROW_NUM+1)*INPUTDW-1 : INPUTDW] 
                              : extern_kernal_flag ? {data_i_tmp[(kernal_cnt)*INPUTDW +: INPUTDW], shift_data[ROW_NUM*INPUTDW-1 : INPUTDW]} 
                                :{data_i_tmp[(ROW_NUM-2+kernal_cnt)*INPUTDW +: INPUTDW], shift_data[ROW_NUM*INPUTDW-1 : INPUTDW]};
  endgenerate
  assign shift_data_tmp = kernal_cnt_valid ? shift_data_result : '0;
  `FFNR(shift_data, shift_data_tmp, clk_i)

  // STRIDE
  logic [2*ROW_NUM*INPUTDW-1 : 0]data_stride_tmp;
  logic [2*ROW_NUM*INPUTDW-1 : 0]data_stride_tmp_result;
  logic [2*ROW_NUM*INPUTDW-1 : 0]data_stride_tmp_d;
  logic [ROW_NUM*INPUTDW-1 : 0]data_stride_o;
  logic [2*INPUTDW-1:0] insert_data;
  logic stride_concat_flag , stride_shift_flag;
  logic data_stride_tmp_d_clear;
  assign stride_concat_flag = stride_flag ? (kernal_cnt == 1) : '0;
  assign stride_shift_flag = stride_flag ? (|kernal_cnt[2:1]) : '0;
  always_comb begin
      insert_data = '0;
      if (extern_kernal_flag) begin
          case(kernal_cnt)
              3'd2: insert_data = data_i[2*INPUTDW-1 : 0];
              3'd4: insert_data = data_i[4*INPUTDW-1 : 2*INPUTDW];
              3'd6: insert_data = data_i[6*INPUTDW-1 : 4*INPUTDW];
              default: insert_data = '0;
          endcase
      end else begin
          insert_data = data_i[ROW_NUM*INPUTDW-1 -: 2*INPUTDW];
      end
  end
  assign data_stride_tmp_result = stride_shift_flag 
                                    ? {insert_data, data_stride_tmp[(2*ROW_NUM*INPUTDW-INPUTDW)-1 : INPUTDW]} 
                                    : (data_stride_tmp >> INPUTDW);
  assign data_stride_tmp = (stride_concat_flag) ? {data_i, shift_data} : data_stride_tmp_d;
  generate
    for(genvar k = ROW_NUM; k >0; k--) begin
      assign data_stride_o[(k)*INPUTDW-1 : (k-1)*INPUTDW] = data_stride_tmp[(2*k-1)*INPUTDW-1 :(2*k-2)*INPUTDW];
    end
  endgenerate
  
  `FFARNC(data_stride_tmp_d, data_stride_tmp_result, data_stride_tmp_d_clear, '0, clk_i, rst_ni)
  `FF(data_stride_tmp_d_clear, data_last_i, '0, clk_i, rst_ni)
  //output signal
  logic almost_last_flag;
  assign almost_last_flag = (kernal_cnt == 3'(conv_kernal_i -2));
  `FF(data_almost_last_o, almost_last_flag, '0, clk_i, rst_ni)
  `FF(data_last_o, kernal_cnt_clear_flag, '0, clk_i, rst_ni)
  `FF(data_valid_o, kernal_cnt_valid, '0, clk_i, rst_ni)
  assign data_o = (stride_flag) ? data_stride_o : shift_data;


endmodule
