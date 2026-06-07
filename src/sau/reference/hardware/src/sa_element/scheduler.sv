`include "registers.svh"
module scheduler
    import SA_pkg::*;
    #(
    parameter int unsigned  SA_SIZE = 16,
    parameter int unsigned  ADDR_DW = 20
)(
    // sys
    input wire clk,
    input wire rst_n,
    input wire start,
    //config
    input wire [3-1 : 0]conv_kernal_i,
    input wire [2-1 : 0]reuse_mode_i,
    input wire [2-1 : 0]trans_mode_i,
    input wire [6-1 : 0]flow_times_i,
    input wire shift_flag_i,
    input wire last_ins_flag_i,
    // controller
    input wire load_done_flag,//data_last
    input wire execute_finished,//sa_done_flag
    input wire update_finished,//data_last_i
    input wire write_finished,//sram_wr_last_ma
    input wire regfile_clear_flag,//output buffer clear
    input wire last_ins_wr_done,
    output reg crossbar_done,
    // state of core
    input  last_flow_time_clear_i,
    output [1:0] input_switch_o,
    output last_flow_time_o,
    output reg flow_end_o,
    output SA_pkg::registerfile_state_e core_state_o
);
// signal flag
  SA_pkg::registerfile_state_e cur_state,next_state;
  logic  ins_valid;
  logic  [2-1 : 0]reuse_mode;
  logic  [2-1 : 0]trans_mode;
  logic  [6   : 0]flow_times;
  logic  [2-1 : 0]input_switch;
  logic  reuse_flag;
  logic  trans_flag;
  logic  trans_reuse_flag;
  logic  execute_flag;
  logic  data_last;
  logic  data_last_d;
  logic  conv_mode;
  logic  conv_reuse_flag;
  logic  conv_reuse_flag_reg;
  logic  shift_mode;

  `FF(ins_valid, start, '0, clk, rst_n)
  assign reuse_mode = reuse_mode_i;
  assign trans_mode = trans_mode_i;
  assign shift_mode = shift_flag_i;
  assign flow_times = reuse_flag ?  {1'b0, flow_times_i} :
                                     {flow_times_i, 1'b0} -1 ;
  assign data_last = load_done_flag;
  assign reuse_flag = |reuse_mode;
  assign trans_flag = |trans_mode;
  assign trans_reuse_flag = (reuse_mode == trans_mode);
  assign execute_flag = (input_switch[0] ^ input_switch[1]);
  assign conv_mode = reuse_flag && execute_flag;
  assign conv_reuse_flag = (conv_kernal_i >= 3'd3);
  `FF(data_last_d, data_last, 1'b0, clk, rst_n)
  `FF(conv_reuse_flag_reg, conv_reuse_flag, 1'b0, clk, rst_n)

  logic  [$clog2(SA_SIZE)-1 : 0]transload_state_cnt;
  logic  [$clog2(SA_SIZE)-1 : 0]transload_state_cnt_result;
  logic  transload_state_cnt_clear, transload_state_cnt_valid;
  assign transload_state_cnt_valid = cur_state == TRANSPOSE_LOAD;
  assign transload_state_cnt_clear = transload_state_cnt == (SA_SIZE - 1);
  assign transload_state_cnt_result = transload_state_cnt + 'd1;
  `FFLARNC(transload_state_cnt, transload_state_cnt_result, transload_state_cnt_valid, transload_state_cnt_clear, 'd0, clk, rst_n)
// ------------------------  flow times counter  ------------------------
  logic  [6 : 0]flow_times_cnt;
  logic  [6 : 0]flow_times_cnt_result;
  logic  flow_times_cnt_clear;
  assign flow_times_cnt_result = flow_times_cnt + 'd1;
  assign flow_times_cnt_clear = (flow_times_cnt == flow_times) & data_last;
  `FFLARNC(flow_times_cnt, flow_times_cnt_result, data_last, flow_times_cnt_clear, 'd0, clk, rst_n)

// core state machine
  
  `FF(cur_state, next_state, IDLE, clk, rst_n)
  always_comb begin: registerfile_state
    next_state = cur_state;
    flow_end_o = 1'b0;
    case(cur_state)
        IDLE: begin
            if(ins_valid & conv_reuse_flag) begin
              next_state = REGISTER_LOAD;
            end
            else if(ins_valid) begin
              next_state = FIRST_LOAD;
            end
            else begin
              next_state = IDLE;
            end
        end
        REGISTER_LOAD: begin
            if(trans_flag && data_last)begin
              next_state = TRANSPOSE_LOAD;
            end
            else if(conv_reuse_flag_reg && data_last) begin
              next_state = REUSE_LOAD;
            end
            else begin
              next_state = REGISTER_LOAD;
            end
        end
        TRANSPOSE_LOAD: begin
          if(transload_state_cnt_clear | data_last)begin
            next_state = REUSE_LOAD;
          end
          else begin
            next_state = TRANSPOSE_LOAD;
          end
        end
        FIRST_LOAD: begin
            if(execute_finished) begin
              next_state = D_OUT;
            end
            else if(conv_mode && data_last) begin
              next_state = REUSE_LOAD;
            end
            else begin
              next_state = FIRST_LOAD;
            end
        end
        REUSE_LOAD: begin
            if(execute_finished) begin
              next_state = D_OUT;
            end
            else if(flow_times_cnt == (flow_times-1) && data_last && trans_flag)begin
              next_state = FIRST_LOAD;
            end
            else
              next_state = REUSE_LOAD;
        end
        D_OUT: begin
            if(last_ins_flag_i)begin
              next_state =  (regfile_clear_flag) ? IDLE : D_OUT;//当未输出表里为0 时，进入idle状态
              flow_end_o = regfile_clear_flag; 
            end else if(update_finished | write_finished)begin
              next_state =  IDLE;
              flow_end_o = 1'b1;
            end
        end
        default: begin
            next_state = IDLE;
            flow_end_o = 1'b0;
        end
    endcase
  end
// ------------------------  input switch for 9 mode  ------------------------
/*
CYCLE      : 1  2  3  4  5  6  7  8  9
ATB reuse A:00 01 01 01 01 01 01 01 01
ATB reuse B:00 01 00 00 00 00 00 00 01
ATB noreuse:00 01 00 01 00 01 00 01

ABT reuse A:11 10 11 11 11 11 11 11 10
ABT reuse B:11 10 10 10 10 10 10 10 10
ABT noreuse:11 10 11 10 11 10 11 10


AB  reuse A:00 01 01 01 01 01 01 01
AB  reuse B:11 10 10 10 10 10 10 10
AB  noreuse:00 01 00 01 00 01 00 01
*/
// input switch signal
  logic  last_flow_time;
  logic  last_flow_time_valid , last_flow_time_clear;
  assign last_flow_time_clear = last_flow_time_clear_i;
  assign last_flow_time_valid = flow_times_cnt_clear;
  `FFLARNC(last_flow_time, 1'd1, last_flow_time_valid, last_flow_time_clear, 'd0, clk, rst_n)

  always_ff @(posedge clk or negedge rst_n) begin
    if(!rst_n) begin
      input_switch <= 'd0;
    end
    else begin
      case(cur_state)
        IDLE: begin
          input_switch <= (trans_mode == 2'b10 || (trans_mode == 2'b00 && reuse_mode == 2'b10)) ? 2'b11 : 2'b00;
        end
        FIRST_LOAD,
        REGISTER_LOAD: begin
          if(data_last_d)begin
          input_switch <= (conv_mode ? input_switch : {input_switch[1],~input_switch[0]});
          end
        end
        TRANSPOSE_LOAD: begin
            input_switch <= input_switch;
        end
        REUSE_LOAD: begin
          if(conv_reuse_flag_reg)begin
            input_switch <= 2'b01;
          end
          else if(!(trans_mode==2'b00 || (trans_mode == reuse_mode)))begin
            input_switch <= {{~trans_mode[0]},{~trans_mode[0] ^ last_flow_time}};
          end
        end
        D_OUT:
          if(flow_end_o) begin
            input_switch <= 'd0;
          end
        default: begin
          input_switch <= 'd0;
        end
      endcase
    end
  end

  assign core_state_o = cur_state;
  assign input_switch_o = input_switch;
  assign last_flow_time_o = last_flow_time;
  `FF(crossbar_done, flow_end_o, '0, clk, rst_n)
endmodule
