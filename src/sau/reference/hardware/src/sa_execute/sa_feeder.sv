`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2025/01/05 16:53:52
// Design Name: 
// Module Name: sa_feeder
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
module sa_feeder
  import SA_pkg::*;
  #(
  parameter  type                   TagType      = logic,
  parameter  SA_pkg::int_format_e   IntFormat    = SA_pkg::INT8                  ,
  parameter  SA_pkg::int_format_e   IntFormat_q  = SA_pkg::INT16                 ,
  parameter  int unsigned           ROW_NUM      = 16                             ,
  parameter  int unsigned           COL_NUM      = 16                             ,
  parameter  int unsigned           OUTPUTDW     = 24                            ,
  parameter  int unsigned           CNT_DW       = 10                            ,
  parameter  int unsigned           INS_DW       = 64                            ,
  parameter  int unsigned           INPUTDW      = 8                             ,
  parameter  int unsigned           QUANTDW      = 16
  )
  (
    input        logic    clk_i             ,
    input        logic    rst_ni            ,
    input        logic    ins_valid_i       ,
    input        logic    [   1: 0]reuse_mode_i,// 00: no reuse 01:A reuse 10:B reuse
    input        logic    [   1: 0]register_mode_i,//00:stard_conv 01:stard_conv_pro 10:dw_conv
    input        logic    [   1: 0]trans_mode_i,// 00 :A*B =D 01: A^T*B =D^T 10: A*B^T =D^T 11: A*B =D^T
    input        logic    [   1: 0]sa_calmode_i,// 00 : gemm  01:transposer 10:conv  11:matrix add
    input        logic    [   1: 0]sa_flowmode_i,// 00 : clear  01:Output line by line  10:Output column by column
    input        logic    [   2: 0]conv_kernal_i,
    input        logic    last_ins_flag_i,
    input        logic    [   5: 0]flow_loop_times_i,
    input        logic    shift_flag_i,
    input        logic    [   1: 0]input_switch_i    ,// [0]current input 0:A 1:B [1]transposer input 0:A 1:B
    input        logic    [$clog2(ROW_NUM): 0]row_num_i,
    input        logic    [$clog2(COL_NUM): 0]col_num_i,
    input        logic    [$clog2(OUTPUTDW)-1: 0]cutbit_i,
    input        logic    [ROW_NUM*INPUTDW-1: 0]data_A_i,
    input        logic    data_A_valid_i,
    input        logic    [COL_NUM*INPUTDW-1: 0]data_B_i,
    input        logic    data_B_valid_i,
    input        logic    [COL_NUM*2*INPUTDW-1: 0]data_C_i,
    input        logic    data_C_valid_i,
    input        logic    last_flow_flag_i,
    output       logic    [COL_NUM*QUANTDW-1: 0]result_final_o,
    output       logic    result_final_valid_o,
    output       logic    execute_done_flag_o,
    output       logic    storage_ready_o,
  `ifdef MAX_USE
    output       logic    [OUTPUTDW-1: 0]data_max_row_o,
  `endif
    output       logic    update_finished,
    output       logic    result_last_o
  );  
  localparam REGISTER_MODE = 1'b0;
  typedef enum logic [2:0] {
    IDLE          = 3'b000,
    A_LOAD        = 3'b001,
    B_LOAD        = 3'b010,
    AB_LOAD       = 3'b011,
    D_OUT         = 3'b100
  } matrix_state_e;
  matrix_state_e cur_state, next_state;
  
//------------------------INS PIP------------------------
  logic   EN_i;
  logic   [1:0] input_switch_o;
  SA_pkg::transmode_e  trans_mode;
  SA_pkg::flowmode_e  sa_flowmode;
  logic   row_score_valid,cal_finish;//SA INTERFACE
  logic   [   1: 0]reuse_mode;
  logic   A_reuse_flag,B_reuse_flag;
  logic   result_trans_mode , result_trans_mode_flag;
  logic   result_trans_mode_delay;
  logic   sa_in_state;
  logic   keep_mode_flag;
  logic   result_out_flag;
  logic   last_ins_flag;
  logic   current_ins_out_flag;
  logic   ins_out_case;
  logic   shift_flag;
  logic   conv_mode;
  logic   [CNT_DW: 0]CALC_CYCLE_i;
  logic   flow_done_flag;

  `FF(input_switch_o , input_switch_i, '0, clk_i, rst_ni)
  `FFL(trans_mode , SA_pkg::mode_transpose(trans_mode_i), ins_valid_i, ABD, clk_i, rst_ni)
  `FFL(sa_flowmode , SA_pkg::mode_outflow(sa_flowmode_i), ins_valid_i, CNORMAL, clk_i, rst_ni)
  `FFL(reuse_mode , reuse_mode_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(result_trans_mode_delay , result_trans_mode_flag, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(last_ins_flag , last_ins_flag_i, ins_valid_i, '0, clk_i, rst_ni)
  `FFL(shift_flag , shift_flag_i, ins_valid_i, '0, clk_i, rst_ni)
  assign EN_i = data_A_valid_i | data_B_valid_i;
  assign CALC_CYCLE_i = SA_pkg::get_exe_cycle(conv_kernal_i) * flow_loop_times_i;
  assign result_trans_mode_flag = sa_flowmode == SA_pkg::CTRANS;
  assign conv_mode = (conv_kernal_i >= 3'd3);  
  assign A_reuse_flag = reuse_mode[0];
  assign B_reuse_flag = reuse_mode[1];
  assign keep_mode_flag = sa_flowmode == SA_pkg::RETAIN | sa_flowmode == SA_pkg::TRETAIN;
  assign sa_in_state = input_switch_i[0] ^ input_switch_i[1];//usually means the execute state(A ready and B ready)
  assign result_trans_mode = last_ins_flag ? result_trans_mode_flag : result_trans_mode_delay;
  assign current_ins_out_flag = last_ins_flag & ~keep_mode_flag;
  assign ins_out_case = current_ins_out_flag ? result_last_o : execute_done_flag_o;

//------------------------DATA PIP------------------------
  logic   [ROW_NUM*INPUTDW-1: 0]data_i_d;
  logic   EN_i_d;
  logic   [ROW_NUM*INPUTDW-1: 0]data_i;
  logic   [ROW_NUM*INPUTDW-1: 0]data_A , data_B;
  logic   [2*ROW_NUM*INPUTDW-1: 0]data_C;
  logic   last_flow_flag;
  logic   multi_data_en;
  logic   EN_i_negedge;
  logic   final_result_state_valid, final_result_state_clear;
  logic   final_result_state_reg;

  
  assign  multi_data_en = EN_i & !data_C_valid_i;
  assign  data_i = (input_switch_i[0] & ~conv_mode) ? data_B_i : data_A_i;
  assign  EN_i_negedge = !EN_i & EN_i_d;  
  assign  final_result_state_valid = current_ins_out_flag & row_score_valid;
  assign  final_result_state_clear = cur_state == D_OUT & cal_finish;
  `FFLARNC(final_result_state_reg, 1'd1, final_result_state_valid, final_result_state_clear, '0, clk_i, rst_ni)
  `FFLNR(data_i_d , data_i, EN_i, clk_i)
  `FFLNR(data_B , data_B_i, EN_i, clk_i)
  `FFLNR(data_A , data_A_i, EN_i, clk_i)
  `FF(EN_i_d , EN_i, '0, clk_i, rst_ni)
  `FF(last_flow_flag, last_flow_flag_i, '0, clk_i, rst_ni)
  `FFLNR(data_C, data_C_i, data_C_valid_i, clk_i)
//------------------------STATE MACHINE------------------------
  `FF(cur_state, next_state, IDLE, clk_i, rst_ni)
  always_comb begin: matrix_state
    next_state = cur_state;
    case(cur_state)
        IDLE: begin
            if(EN_i )begin
                case (trans_mode)
                    ABD,ABDT: next_state = conv_mode ? AB_LOAD : A_LOAD;
                    ATBD: next_state = A_LOAD;
                    ABTD: next_state = B_LOAD;
                endcase
            end
            else begin
                next_state = IDLE;
            end
        end
        A_LOAD: begin
            if(flow_done_flag && last_flow_flag_i) begin
            next_state = D_OUT;
            end
            else if(sa_in_state && data_B_valid_i && |reuse_mode) begin
            next_state = AB_LOAD;
            end
            else if(flow_done_flag && sa_in_state) begin
            next_state = B_LOAD;
            end
            else begin
            next_state = A_LOAD;
            end
        end
        B_LOAD: begin
            if(flow_done_flag && last_flow_flag_i) begin
            next_state = D_OUT;
            end
            else if(sa_in_state && data_A_valid_i && |reuse_mode) begin
            next_state = AB_LOAD;
            end
            else if(flow_done_flag && sa_in_state) begin
            next_state = A_LOAD;
            end
            else begin
            next_state = B_LOAD;
            end
        end
        AB_LOAD: begin
            if(flow_done_flag && last_flow_flag_i) begin
            next_state = D_OUT;
            end
            else if(sa_in_state && data_A_valid_i && !data_B_valid_i)begin
              next_state = A_LOAD;
            end
            else if(sa_in_state && data_B_valid_i && !data_A_valid_i)begin
              next_state = B_LOAD;
            end
            else begin
            next_state = AB_LOAD;
            end
        end
        D_OUT: begin
            if(ins_out_case & ~final_result_state_reg) begin//case past result or current result
            next_state = IDLE;
            end
        end
        default: begin
            next_state = IDLE;
        end
    endcase
  end
  //------------------------Transposer Arbiter------------------------
  logic   [ROW_NUM*INPUTDW-1: 0] trans0_inRow;
  logic   trans0_inRow_en;
  logic   [ROW_NUM*INPUTDW-1: 0] trans0_outCol;
  logic   trans0_valid_o;
  logic   trans0_rden;
  logic   trans0_ready_o;
  logic   trans0_error, trans0_last_o, trans0_ready;

  logic   [ROW_NUM*INPUTDW-1: 0] trans1_inRow;
  logic   trans1_inRow_en;
  logic   [ROW_NUM*INPUTDW-1: 0] trans1_outCol;
  logic   trans1_valid_o;
  logic   trans1_rden;
  logic   trans1_ready_o;
  logic   trans1_error, trans1_last_o, trans1_ready;

  logic   [ROW_NUM*INPUTDW-1: 0] trans2_inRow;
  logic   trans2_inRow_en;
  logic   [ROW_NUM*INPUTDW-1: 0] trans2_outCol;
  logic   trans2_valid_o;
  logic   trans2_rden;
  logic   trans2_ready_o;
  logic   trans2_error, trans2_last_o, trans2_ready;  
  
  // 控制信号
  logic   trans_input_req;      // 输入请求有效
  logic   trans_output_req;     // 输出请求有效
  logic   transposer_work_flag;
  // 数据源
  logic   [ROW_NUM*INPUTDW-1: 0] trans_in_result;
  logic   trans_load_valid, trans_load_valid_flag;

  logic   [COL_NUM*QUANTDW-1: 0] out_sum_final_q;
  logic   [COL_NUM*INPUTDW-1: 0] out_sum_final_split_d;
  logic   [COL_NUM*INPUTDW-1: 0] out_sum_final_split_h;

  assign transposer_work_flag = (trans_mode == SA_pkg::ATBD || trans_mode == SA_pkg::ABTD || (trans_mode == SA_pkg::ABD & ~conv_mode));
  // 1. 请求逻辑生成
  // 输入有效性检测
  assign trans_load_valid_flag = (data_A_valid_i && trans_mode == SA_pkg::ATBD) || 
                                 (data_B_valid_i && trans_mode == SA_pkg::ABTD) || 
                                 (trans_mode == SA_pkg::ABD && data_A_valid_i && !reuse_mode);
  
  `FF(trans_load_valid, trans_load_valid_flag, '0, clk_i, rst_ni)

  assign trans_input_req = trans_load_valid && !last_flow_flag;
  assign trans_output_req = row_score_valid;

  // 2. 资源仲裁与分配
  logic t0_avail_for_input;
  logic t1_avail_for_input;
  logic t1_reserved_for_output;
  
  // T1 是否被输出占用？ (Shift 模式下且有输出请求)
  assign t1_reserved_for_output = shift_flag && trans_output_req;

  // 输入资源可用性
  // T0: 只要不是纯输出模式且 T0 自身这就绪
  assign t0_avail_for_input = trans0_ready;
  
  // T1: 只要 T1 未被保留给输出且 T1 自身这就绪
  assign t1_avail_for_input = !t1_reserved_for_output && trans1_ready;

  // 3. 数据路由与使能生成
  always_comb begin : steer_logic
      trans0_inRow    = '0;
      trans0_inRow_en = 1'b0;
      trans1_inRow    = '0;
      trans1_inRow_en = 1'b0;
      trans2_inRow    = '0;
      trans2_inRow_en = 1'b0;
      // --- 输出路径
      if (trans_output_req) begin
          // Transposer 2 总是处理低位数据
          trans2_inRow    = out_sum_final_split_d;
          trans2_inRow_en = 1'b1;
          // Transposer 1 如果在 Shift 模式下，处理高位数据
          if (shift_flag) begin
              trans1_inRow    = out_sum_final_split_h;
              trans1_inRow_en = 1'b1;
          end
      end
      // --- 输入路径
      if (trans_input_req) begin
          // 策略：T0 优先，如果 T0 忙或禁用，则尝试 T1
          if (t0_avail_for_input) begin
              trans0_inRow    = data_i_d;
              trans0_inRow_en = 1'b1;
          end 
          else if (t1_avail_for_input) begin
              trans1_inRow    = data_i_d;
              trans1_inRow_en = 1'b1;
          end
      end
  end

  // 4. 读出逻辑控制
  logic trans_input_rden;
  logic trans_result_rden;
  logic result_out_transen[3]; // Transpose Enable 配置

  assign trans_input_rden = (EN_i & sa_in_state);
  // 读出使能分配
  assign trans0_rden = trans0_ready_o & trans_input_rden;
  
  // T1 读出逻辑：如果是 Output/Shift 模式，读逻辑不同
  assign trans1_rden = trans1_ready_o & (storage_ready_o ? (trans_result_rden & shift_flag) : trans_input_rden);

  assign trans2_rden = trans2_ready_o & trans_result_rden; // T2 专用于 Output
  assign result_out_transen[0] = (trans_mode != SA_pkg::ABD);
  assign result_out_transen[1] = (shift_flag && result_out_flag) ? (trans1_ready_o & result_trans_mode) : (trans_mode != SA_pkg::ABD);
  assign result_out_transen[2] = result_out_flag ? (trans2_ready_o & result_trans_mode) : (trans_mode != SA_pkg::ABD);

  // AB_LOAD 结束标志
  assign flow_done_flag = trans0_last_o | EN_i_negedge;

  // 5. 模块实例化

  transposer_tiny#(
    .DIM                                (ROW_NUM                   ),
    .DIM_R                              (COL_NUM                   ),
    .DATA_WIDTH                         (INPUTDW                   )
    )
    u_transposer0_input(
    .inRow                              (trans0_inRow               ),
    .en                                 (trans0_inRow_en            ),
    .outCol                             (trans0_outCol              ),
    .valid_o                            (trans0_valid_o             ),
    .last_o                             (trans0_last_o              ),
    .error                              (trans0_error               ),
    .ready                              (trans0_ready               ),
    .ready_o                            (trans0_ready_o             ),
    .clk                                (clk_i                      ),
    .reuse_en                           (1'b0                       ),
    .transpose_en                       (result_out_transen[0]      ),
    .en_o                               (trans0_rden                ),
    .rst_n                              (rst_ni                     )
    );
    //Transposer OUTPUT:
    transposer_tiny#(
    .DIM                                (ROW_NUM                   ),
    .DIM_R                              (COL_NUM                   ),
    .DATA_WIDTH                         (INPUTDW                   )
    )
    u_transposer1_input(
    .inRow                              (trans1_inRow               ),
    .en                                 (trans1_inRow_en            ),
    .outCol                             (trans1_outCol              ),
    .valid_o                            (trans1_valid_o             ),
    .last_o                             (trans1_last_o              ),
    .error                              (trans1_error               ),
    .ready                              (trans1_ready               ),
    .ready_o                            (trans1_ready_o             ),
    .clk                                (clk_i                      ),
    .reuse_en                           (1'b0                       ),
    .transpose_en                       (result_out_transen[1]      ),
    .en_o                               (trans1_rden                ),
    .rst_n                              (rst_ni                     )
    );
    transposer_tiny#(
    .DIM                                (ROW_NUM                   ),
    .DIM_R                              (COL_NUM                   ),
    .DATA_WIDTH                         (INPUTDW                   )
    )
    u_transposer2_output(
    .inRow                              (trans2_inRow               ),
    .en                                 (trans2_inRow_en            ),
    .outCol                             (trans2_outCol              ),
    .valid_o                            (trans2_valid_o             ),
    .last_o                             (trans2_last_o              ),
    .error                              (trans2_error               ),
    .ready                              (trans2_ready               ),
    .ready_o                            (trans2_ready_o             ),
    .clk                                (clk_i                      ),
    .reuse_en                           (1'b0                       ),
    .transpose_en                       (result_out_transen[2]      ),
    .en_o                               (trans2_rden                ),
    .rst_n                              (rst_ni                     )
    );
  //------------------------SA_Engine------------------------
  //SA_Engine interface
  logic  sa_output_flag;
  logic  sa_storage_ready;
  logic  [$clog2(ROW_NUM): 0]row_seq_o;
  logic  sa_en_i;
  logic  [ROW_NUM*INPUTDW-1: 0]sa_data_active_left;
  logic  [COL_NUM*INPUTDW-1: 0]sa_in_weight_above;
  logic  sa_shift_ctl;

  logic  result_last_t;
  logic  [COL_NUM*QUANTDW-1: 0]result_final_t;
  logic  result_final_valid_t;
  logic  storage_ready_d,sa_storage_ready_d,sa_in_state_d,sa_en_i_d;
  logic  sa_serial_o_flag,sa_pipe_o_flag;
  logic  sa_serial_o_flag_reg;
  logic  sa_serial_o_flag_clear;
  
  //在计算过程内输出计算结果
  assign sa_pipe_o_flag = REGISTER_MODE ? (cur_state == IDLE & EN_i & sa_storage_ready) : (sa_in_state & !sa_in_state_d & sa_storage_ready);
  assign sa_serial_o_flag = !sa_storage_ready_d & sa_storage_ready & (trans2_ready || trans2_last_o);
  assign sa_output_flag = (last_ins_flag && (cur_state != IDLE)) ? (sa_serial_o_flag | sa_pipe_o_flag) : sa_pipe_o_flag;//选择脉动真阵列输出的时刻，是计算过程内还是后
  assign trans_result_rden = sa_serial_o_flag_reg & ~result_last_t;
  assign sa_serial_o_flag_clear = result_last_t & ~sa_serial_o_flag;
  `FF(sa_in_state_d, sa_in_state, '0, clk_i, rst_ni)  
  `FF(sa_storage_ready_d, sa_storage_ready, '0, clk_i, rst_ni)
  `FF(storage_ready_d, storage_ready_o, '0, clk_i, rst_ni)
  `FF(sa_en_i_d, sa_en_i, '0, clk_i, rst_ni)
  `FFLARNC(storage_ready_o, 1'd1, cal_finish, result_last_t, '0, clk_i, rst_ni)
  `FFLARNC(sa_serial_o_flag_reg, 1'd1, sa_serial_o_flag, sa_serial_o_flag_clear, '0, clk_i, rst_ni)

  //OUTPUT INTERFACE
  function automatic logic [2 * COL_NUM * INPUTDW-1:0] combine_evenodd_optimal(
    input logic [COL_NUM * INPUTDW -1 :0] in_even_chunks,
    input logic [COL_NUM * INPUTDW -1 :0] in_odd_chunks
    );
    logic [2 * COL_NUM * INPUTDW -1 :0] out_128;
    for (int i = 0; i < COL_NUM; i = i + 1) begin
        out_128[ (2*i)*INPUTDW +: INPUTDW ] = in_even_chunks[ i*INPUTDW +: INPUTDW ];
        out_128[ (2*i+1)*INPUTDW +: INPUTDW ] = in_odd_chunks[ i*INPUTDW +: INPUTDW ];
    end
    return out_128;
  endfunction
  assign result_out_flag = storage_ready_o; 
  assign result_final_t = result_out_flag ? (shift_flag ? combine_evenodd_optimal(trans2_outCol, trans1_outCol) : {{(ROW_NUM*INPUTDW){1'd0}},trans2_outCol}) : '0;
  assign result_final_valid_t = result_out_flag ? trans2_valid_o: '0;
  assign result_last_t = result_out_flag ? trans2_last_o : '0; 
  `FFNR(result_final_o, result_final_t, clk_i)
  `FFNR(result_final_valid_o, result_final_valid_t, clk_i)
  `FFNR(result_last_o, result_last_t, clk_i)
  //------------------------SA_ENGINE DATA ARBITER---------------------
  always_comb begin 
    sa_data_active_left = 'd0;
    sa_in_weight_above  = 'd0;
    sa_en_i = 'd0;
    case(input_switch_i)
      2'b01: begin
        sa_data_active_left = (transposer_work_flag) ? (trans0_ready_o ? trans0_outCol : trans1_outCol) 
                                                : data_i_d;
        sa_in_weight_above = data_B;
        sa_en_i = EN_i_d;
      end
      2'b10: begin
        sa_data_active_left = data_A;
        sa_in_weight_above = (transposer_work_flag) ? (trans0_ready_o ? trans0_outCol : trans1_outCol) 
                                                : data_i_d;
        sa_en_i = EN_i_d;
      end
      default: begin
        sa_data_active_left = 'd0;
        sa_in_weight_above  = 'd0;
        sa_en_i = 'd0;
      end
    endcase
  end
  //------------------------8/16bit case---------------------
  //16bit matmul
  logic shift_switch_valid_matmul;
  logic shift_ctl_matmul;
  assign shift_switch_valid_matmul = shift_flag && ~shift_ctl_matmul && sa_en_i;
  `FF(shift_ctl_matmul, shift_switch_valid_matmul, '0, clk_i, rst_ni)
  //16bit conv
  logic shift_switch_valid_conv;
  logic shift_ctl_conv;
  logic shift_ctl_switch_conv;
  assign shift_ctl_switch_conv = ~shift_ctl_conv;
  assign shift_switch_valid_conv = shift_flag && sa_en_i_d && !sa_en_i;
  assign sa_shift_ctl = conv_mode ? shift_ctl_conv : shift_ctl_matmul;
  `FFLARNC(shift_ctl_conv, shift_ctl_switch_conv, shift_switch_valid_conv, execute_done_flag_o, '0, clk_i, rst_ni)
  //------------------------SA_ENGINE------------------------
    SA_ENGINE #(
    .ROW_NUM                            (ROW_NUM                   ),
    .COL_NUM                            (COL_NUM                   ),
    .OUTPUTDW                           (OUTPUTDW                  ),
    .CNT_DW                             (CNT_DW                    )
    )
    u_SA_TOP(
    .clk                                (clk_i                     ),// CLK = 200MHz
    .rst_n                              (rst_ni                    ),// RESET, Negedge is active
    .EN_i                               (sa_en_i                   ),// enable signal for the accelerator, high for active
    .Flag_o                             (sa_output_flag            ),// enable signal for the accelerator, high for active
    .Flag_o_ready                       (1'b1                      ),
    .ins_valid_i                        (ins_valid_i               ),// enable output for the accelerator, high for active
    .sa_calmode_i                       (sa_calmode_i              ),// 00 : gemm  01:transposer 10:conv  11:matrix add
    .sa_flowmode_i                      (sa_flowmode_i             ),// 00 : clear  01:Output line by line  10:Output column by column
    .CALC_CYCLE_i                       (CALC_CYCLE_i              ),
    .register_mode_i                    (register_mode_i           ),
    .shift_mode_i                       (shift_flag_i              ),   
    .row_num_i                          (row_num_i                 ),
    .col_num_i                          (col_num_i                 ),
    .cutbit                             (cutbit_i                  ),// 0628test
    .data_active_left                   (sa_data_active_left       ),
    .in_weight_above                    (sa_in_weight_above        ),
    .in_bias_above                      (data_C                    ),
    .shift_ctl_i                        (sa_shift_ctl              ),
    .out_sum_final_q                    (out_sum_final_q           ),
    .row_score_valid                    (row_score_valid           ),
    .row_seq_o                          (row_seq_o                 ),
    `ifdef MAX_USE
    .data_max_row_o                     (data_max_row_o            ),
    `endif
    .storage_ready                      (sa_storage_ready          ),
    .pe_finish_o                        (execute_done_flag_o       ),
    .cal_finish                         (cal_finish                )
    );
  always_comb begin
    SA_pkg::split_even_odd(
        .in_data        (out_sum_final_q),
        .out_even_chunks(out_sum_final_split_d),
        .out_odd_chunks (out_sum_final_split_h)
    );
  end
  //------------------------UPDATE FSM------------------------
  typedef enum logic [1:0] {
    FIRSTOUT      = 2'b00,
    PINGPONG      = 2'b01,
    LASTPINGPONG  = 2'b10
  } update_state_t;
  update_state_t update_state, update_next_state;
  logic  update_finished_flag;
  `FF(update_state, update_next_state, FIRSTOUT, clk_i, rst_ni)
  always_comb begin: update_state_fsm
    update_next_state = update_state;
    case(update_state)
        FIRSTOUT: begin
            if(current_ins_out_flag) begin //当前结果需要输出
                update_next_state = FIRSTOUT;
            end
            else if(execute_done_flag_o & ~keep_mode_flag) begin//当前结果需要输出（下条指令输出）
                update_next_state = PINGPONG;
            end
            else begin
                update_next_state = FIRSTOUT;//当前结果不需要输出
            end
        end
        PINGPONG: begin
            if(current_ins_out_flag) begin
                update_next_state = LASTPINGPONG;
            end
            else if(execute_done_flag_o & keep_mode_flag) begin//当前结果需要输出（下条指令输出）
                update_next_state = FIRSTOUT;
            end
            else begin
                update_next_state = PINGPONG;
            end
        end
        LASTPINGPONG: begin
            if((result_last_o & !EN_i_d)) begin
              update_next_state = FIRSTOUT;
            end
            else begin
              update_next_state = LASTPINGPONG;
            end
        end
        default: begin
            update_next_state = FIRSTOUT;
        end
    endcase
  end
  always_comb begin: update_finished_case
    update_finished_flag = 'd0;
    case(update_state)
        FIRSTOUT: begin
            update_finished_flag = current_ins_out_flag ? (result_last_o & !EN_i_d) : execute_done_flag_o;//16bit pingpong模式下第一次计算不输出
        end
        PINGPONG: begin
            update_finished_flag = 1'b0;//后续计算输出结果 ，done信号由别的模块控制
        end
        LASTPINGPONG: begin
            update_finished_flag = (result_last_o & !EN_i_d);//连续计算模式下连续输出两次结果
        end
        default: begin
            update_finished_flag = 1'b0;
        end
    endcase
  end
  `FF(update_finished, update_finished_flag, '0, clk_i, rst_ni)
endmodule