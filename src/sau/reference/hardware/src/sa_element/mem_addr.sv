`include "registers.svh"
module mem_addr 
    import SA_pkg::*;
    #(
    parameter int unsigned  SRAM_ADDR_WIDTH = 32,
    parameter int unsigned  ADDR_DW = 20,
    parameter int unsigned  ADDR_DELAY = 6,
    parameter int ROW_NUM = 8,
    parameter int BASE_ADDR = 32'h1000_0000
)(
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    // scheduler
    input  SA_pkg::registerfile_state_e  core_state,
    input  wire [1:0]  input_switch,
    input  wire        last_flow_time,
    input  wire        sram_wr_valid,    
    input  wire        sram_wr_last_i,
    output wire        regfile_clear_flag,
    output wire        load_done_flag,
    // csr
    input  wire [ADDR_DW-1 : 0]vertical_base_addr_i,
    input  wire [8-1 : 0]vertical_x_step_i,
    input  wire [8-1 : 0]vertical_channel_step_i,
    input  wire [ADDR_DW-1 : 0]horizontal_base_addr_i,
    input  wire [8-1 : 0]horizontal_x_step_i,
    input  wire [8-1 : 0]horizontal_channel_step_i,
    input  wire [ADDR_DW-1 : 0]output_base_addr_i,
    input  wire [8-1 : 0]output_x_step_i,
    input  wire [8-1 : 0]output_channel_step_i,
    input  wire [ADDR_DW-1 : 0]bias_address_i,
    input  wire [3-1 : 0]conv_kernal_i,
    input  wire [2-1 : 0]register_mode_i,
    input  wire [6-1 : 0]flow_times_i,
    input  wire [2-1 : 0]flow_mode_i,
    input  wire [2-1 : 0]pe_work_mode_i,
    input  wire          stride_flag_i,
    input  wire          shift_flag_i,
    // sram_ctrl
    output wire [SRAM_ADDR_WIDTH-1 :0] sram_mem_addr,
    output wire        sram_rd_enable,
    output wire        sram_wr_enable,
    output wire        sram_wr_last_o,
    output wire        sram_rdaddr_last
);
    // localparam for mem change
    localparam int WAIT_NUM  = 'd9;//minimum wait time for mem change (3x3 kernal)
    localparam int PADDING = 0;//padding function not avail
    localparam int TRANS_FLAG = 0;
    //input pip
    logic  concat_flag_i;
    logic  [ADDR_DW-1 : 0]vertical_base_addr;
    logic  [8-1 : 0]vertical_x_step;
    logic  [8-1 : 0]vertical_channel_step;
    logic  [ADDR_DW-1 : 0]horizontal_base_addr;
    logic  [8-1 : 0]horizontal_x_step;
    logic  [8-1 : 0]horizontal_channel_step;
    logic  [ADDR_DW-1 : 0]output_base_addr;
    logic  [8-1 : 0]output_x_step;
    logic  [8-1 : 0]output_channel_step;
    logic  [ADDR_DW-1 : 0]bias_address;
    logic  ins_valid;
    logic  [3-1 : 0]conv_kernal;
    logic  [6-1 : 0]flow_times;
    logic  [2-1 : 0]pe_work_mode;
    logic  concat_flag;
    logic  stride_flag;
    logic  shift_flag;
    logic  [0 : (ADDR_DELAY-1)]sram_wr_last_d;
    logic  [1 : 0]input_switch_d[ADDR_DELAY-1];
    logic  last_flow_time_d[ADDR_DELAY];
    logic  output_reg_clear;
    logic  conv_reuse_flag;    
    
    SA_pkg::registerfile_state_e  core_state_delay;
    `FFL(vertical_base_addr, vertical_base_addr_i, start, '0, clk, rst_n)
    `FFL(vertical_x_step, vertical_x_step_i, start, '0, clk, rst_n)
    `FFL(vertical_channel_step, vertical_channel_step_i, start, '0, clk, rst_n)
    `FFL(horizontal_base_addr, horizontal_base_addr_i, start, '0, clk, rst_n)
    `FFL(horizontal_x_step, horizontal_x_step_i, start, '0, clk, rst_n)
    `FFL(horizontal_channel_step, horizontal_channel_step_i, start, '0, clk, rst_n)
    `FFL(bias_address, bias_address_i, start, '0, clk, rst_n)
    `FF(ins_valid, start, '0, clk, rst_n)
    `FFL(conv_kernal, conv_kernal_i, start, '0, clk, rst_n)
    `FFL(flow_times, flow_times_i, start, '0, clk, rst_n)
    `FFL(concat_flag,concat_flag_i, start, '0, clk, rst_n)
    `FFL(stride_flag,stride_flag_i, start, '0, clk, rst_n)
    `FFL(pe_work_mode, pe_work_mode_i, start, '0, clk, rst_n)
    `FFL(shift_flag, shift_flag_i, start, '0, clk, rst_n)
    `FF(core_state_delay, core_state, IDLE, clk, rst_n)
    
    logic  dw_register_flag;
    logic  pwc_flag;
    assign concat_flag_i = pwc_flag ? shift_flag_i : |conv_kernal_i[2:1];
    assign output_reg_clear = sram_wr_last_d[ADDR_DELAY-1];
    assign pwc_flag = (conv_kernal_i == 3'd1);
    assign dw_register_flag = (register_mode_i == 2'b10);
    assign conv_reuse_flag = (conv_kernal_i >= 3'd3);

    //output addr update fsm
    typedef enum logic [1:0] {
    ZERO_START  = 2'b00,
    ONE_START = 2'b01,
    TWO_START = 2'b10
    } output_state_t;
    output_state_t cur_out_state, next_out_state;
    `FF(cur_out_state, next_out_state, ZERO_START, clk, rst_n)
    always_comb begin
        next_out_state = cur_out_state;
        case(cur_out_state)
        ZERO_START:begin
            if(start && ~flow_mode_i[1])//first ins result need to be written
            next_out_state = ONE_START;
        end
        ONE_START:begin
            if(start && ~flow_mode_i[1])//second ins result need to be written
            next_out_state = TWO_START;
            else if(output_reg_clear)//first ins result has been written
            next_out_state = ZERO_START;
        end
        TWO_START:begin
            if(output_reg_clear)//first ins result has been written
            next_out_state = ONE_START;
        end
        default:
            next_out_state = ZERO_START;
        endcase
    end
    logic  [ADDR_DW-1 : 0]output_base_addr_pingpong;
    logic  [8-1 : 0]output_x_step_pingpong;
    logic  [8-1 : 0]output_channel_step_pingpong;
    logic  [ADDR_DW-1 : 0]output_base_addr_case;
    logic  [8-1 : 0]output_x_step_case;
    logic  [8-1 : 0]output_channel_step_case;
    logic  output_update;
    logic  pingpong_flag;
    assign pingpong_flag = (cur_out_state == ONE_START) && start;
    assign output_update = (cur_out_state == ZERO_START && start) || (cur_out_state == TWO_START && output_reg_clear);
    assign output_base_addr_case = (cur_out_state == ZERO_START) ? output_base_addr_i : output_base_addr_pingpong;
    assign output_x_step_case = (cur_out_state == ZERO_START) ? output_x_step_i : output_x_step_pingpong;
    assign output_channel_step_case = (cur_out_state == ZERO_START) ? output_channel_step_i : output_channel_step_pingpong;//xiugai

    `FFL(output_base_addr_pingpong, output_base_addr_i, pingpong_flag, '0, clk, rst_n)
    `FFL(output_x_step_pingpong, output_x_step_i, pingpong_flag, '0, clk, rst_n)
    `FFL(output_channel_step_pingpong, output_channel_step_i, pingpong_flag, '0, clk, rst_n)

    `FFL(output_base_addr, output_base_addr_case, output_update, '0, clk, rst_n)
    `FFL(output_x_step, output_x_step_case, output_update, '0, clk, rst_n)
    `FFL(output_channel_step, output_channel_step_case, output_update, '0, clk, rst_n)

    // reg for store the current address, and the read/write enable to mem_ctrl
    reg [SRAM_ADDR_WIDTH-1:0] mem_addr;
    reg        rd_enable;
    reg        wr_enable;
    reg        wraddr_last;
    reg        rdaddr_last;
    logic      rdaddr_last_flag;
    // ------------------------  addr cnt  ------------------------
    logic  vertical_cnt_valid;
    logic  horizontal_cnt_valid;
    logic  vertical_cnt_clear;
    logic  horizontal_cnt_clear;
    logic  vertical_cnt_start;
    logic  horizontal_cnt_start;
    logic  next_rdaddr_start;
    logic  next_rdaddr_start_flag;
    logic  [0 : (ADDR_DELAY-1)]rdaddr_last_d;
    logic  rd_clear_flag;
    assign rd_clear_flag = (core_state == SA_pkg::D_OUT && core_state_delay != SA_pkg::D_OUT);
    assign next_rdaddr_start_flag = (core_state != SA_pkg::TRANSPOSE_LOAD) & (rdaddr_last_d[1] | (core_state_delay == SA_pkg::TRANSPOSE_LOAD & core_state != SA_pkg::TRANSPOSE_LOAD));
    `FFARNC(next_rdaddr_start, next_rdaddr_start_flag, last_flow_time_d[0], 1'b0, clk, rst_n)
    // ------------------------  concat cnt  ------------------------
    reg    [2:0] concat_cnt;
    wire   [2:0] concat_cnt_num_t;
    reg    [2:0] concat_cnt_num;
    wire   concat_cnt_clear;
    wire   [2:0] concat_cnt_result;
    wire   concat_cnt_valid;
    wire   next_row_valid;
    assign concat_cnt_num_t = pwc_flag ? concat_flag : concat_flag + (stride_flag << shift_flag) + shift_flag;//additional data from the same row (eg ,conv, pwc)
    assign concat_cnt_valid = (vertical_cnt_valid | horizontal_cnt_valid) & concat_flag;
    assign concat_cnt_clear = ((concat_cnt == concat_cnt_num) && concat_flag) | next_rdaddr_start;
    assign concat_cnt_result = concat_cnt + 1;
    assign next_row_valid = (concat_cnt != 0);
    `FFLARNC(concat_cnt, concat_cnt_result, concat_cnt_valid, concat_cnt_clear, 'd0, clk, rst_n)
    `FFARNC(concat_cnt_num, concat_cnt_num_t, rd_clear_flag, 'd0, clk, rst_n)
    // ------------------------vertical addr counter------------------------
    logic  [0 : PADDING] [ADDR_DW-1 : 0]vertical_addr;
    logic  [ADDR_DW-1 : 0]vertical_addr_result;
    logic  [6-1 : 0]vertical_x_cnt;
    logic  [6-1 : 0]vertical_c_cnt;
    logic  [6-1 : 0]vertical_x_cnt_num_case;
    logic  [6-1 : 0]vertical_c_cnt_num_case;
    logic  [6-1 : 0]vertical_x_cnt_num;
    logic  [6-1 : 0]vertical_c_cnt_num;
    logic  [6-1 : 0]vertical_x_cnt_result,vertical_c_cnt_result;
    logic  vertical_x_cnt_clear,vertical_c_cnt_clear;
    logic  vertical_x_cnt_valid;
    logic  vertical_cnt_last;
    logic  vertical_c_cnt_valid;
    logic  vertical_c_cnt_shift_clear;
    logic  vertical_c_cnt_shift_clear_reg;
    logic  vertical_shift_flag;
    logic  vertical_shift_flag_reg;

    assign vertical_x_cnt_num_case = SA_pkg::get_exe_cycle(conv_kernal_i) >> (shift_flag_i & ~conv_reuse_flag);//for 16bit matmul or pwc
    assign vertical_c_cnt_num_case = (flow_times_i >> shift_flag_i);//for 16bit matmul or pwc
    assign vertical_cnt_start = (ins_valid | next_rdaddr_start) & (input_switch[0]);
    assign vertical_x_cnt_result = vertical_x_cnt+ 1;
    assign vertical_c_cnt_result = vertical_c_cnt+ 1;
    assign vertical_x_cnt_clear = (vertical_x_cnt== (vertical_x_cnt_num - 1)) & vertical_x_cnt_valid;
    assign vertical_c_cnt_clear = (vertical_c_cnt== (vertical_c_cnt_num - 1)) & vertical_x_cnt_clear & vertical_c_cnt_valid;
    assign vertical_cnt_last = vertical_x_cnt_clear;
    assign vertical_cnt_clear = (vertical_cnt_last | last_flow_time_d[(ADDR_DELAY-1)]);
    assign vertical_c_cnt_shift_clear = !vertical_c_cnt_shift_clear_reg;
    assign vertical_c_cnt_valid = (shift_flag)? (vertical_x_cnt_clear & vertical_c_cnt_shift_clear_reg) : vertical_x_cnt_clear;
    assign vertical_shift_flag = (vertical_cnt_valid & ~vertical_shift_flag_reg) | ~shift_flag | conv_reuse_flag;//前一半用于翻转，后一个or用于模式切换（仅在矩阵乘法16bit模式生效）
    assign vertical_x_cnt_valid = vertical_cnt_valid & vertical_shift_flag_reg;
    `FFLARNC(vertical_c_cnt_shift_clear_reg, vertical_c_cnt_shift_clear, vertical_x_cnt_clear, rd_clear_flag , 1'b0, clk, rst_n)
    `FFLARNC(vertical_x_cnt_num, vertical_x_cnt_num_case, start, rd_clear_flag , 'd0, clk, rst_n)
    `FFLARNC(vertical_c_cnt_num, vertical_c_cnt_num_case, start, rd_clear_flag , 'd0, clk, rst_n)
    `FFLARNC(vertical_cnt_valid, 1'd1, vertical_cnt_start, vertical_cnt_clear , 'd0, clk, rst_n)
    `FFLARNC(vertical_x_cnt, vertical_x_cnt_result, vertical_x_cnt_valid, vertical_x_cnt_clear, 'd0, clk, rst_n)
    `FFLARNC(vertical_c_cnt, vertical_c_cnt_result, vertical_c_cnt_valid, vertical_c_cnt_clear, 'd0, clk, rst_n)
    `FF(vertical_shift_flag_reg, vertical_shift_flag, 'd0, clk, rst_n)
    // ------------------------vertical addr caculate------------------------
    //stage 1
    logic  [14-1 : 0]vertical_x_result;
    logic  [14-1 : 0]vertical_channel_result;
    logic  [0 : (ADDR_DELAY-1)]vertical_cnt_valid_d;
    logic  [6-1 : 0] vertical_x_cnt_case;
    assign vertical_x_cnt_case = (shift_flag & !conv_reuse_flag & vertical_c_cnt_shift_clear_reg) ? 6'(vertical_x_cnt + 8) : vertical_x_cnt;//only for int16 matmul pw fc
    DW02_mult_2_stage #(.A_width(6),.B_width(8))vertical_mult0(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (vertical_x_cnt_case       ),
    .B                                  (vertical_x_step           ),
    .PRODUCT                            (vertical_x_result         )
    );
    DW02_mult_2_stage #(.A_width(6),.B_width(8))vertical_mult1(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (vertical_c_cnt            ),
    .B                                  (vertical_channel_step     ),
    .PRODUCT                            (vertical_channel_result   )
    );
    //stage 2
    logic  [14-1 : 0]vertical_x_result_reg;
    logic  [14-1 : 0]vertical_channel_result_reg;
    `FF(vertical_x_result_reg, vertical_x_result, 'd0, clk, rst_n)
    `FF(vertical_channel_result_reg, vertical_channel_result, 'd0, clk, rst_n)

    //stage 3 
    logic  [22-1 : 0]vertical_xr_m_c_result;
    logic  [22-1 : 0]vertical_cr_m_x_result;
    DW02_mult_2_stage #(.A_width(14),.B_width(8))vertical_mult2(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (vertical_x_result_reg     ),
    .B                                  (vertical_channel_step     ),
    .PRODUCT                            (vertical_xr_m_c_result    )
    );
    DW02_mult_2_stage #(.A_width(8),.B_width(14))vertical_mult3(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (vertical_x_step           ),
    .B                                  (vertical_channel_result_reg),
    .PRODUCT                            (vertical_cr_m_x_result    )
    );
    logic  [14-1 : 0]vertical_x_result_d;
    `FF(vertical_x_result_d, vertical_x_result_reg, 'd0, clk, rst_n)
    //stage 4
    logic  [ADDR_DW-1 : 0]vertical_result_case1_tmp;
    logic  [ADDR_DW-1 : 0]vertical_result_case2_tmp;
    logic  [ADDR_DW-1 : 0]vertical_result_case1_reg;
    logic  [ADDR_DW-1 : 0]vertical_result_case2_reg;
    assign vertical_result_case1_tmp = (ADDR_DW)'((vertical_xr_m_c_result[ADDR_DW-1 : 0])<<4);
    assign vertical_result_case2_tmp = (ADDR_DW)'((vertical_cr_m_x_result[ADDR_DW-1 : 0] + vertical_x_result_d)<<4);
    `FFLARNC(vertical_result_case1_reg, vertical_result_case1_tmp, vertical_cnt_valid_d[3], last_flow_time_d[3] , 'd0, clk, rst_n)
    `FFLARNC(vertical_result_case2_reg, vertical_result_case2_tmp,  vertical_cnt_valid_d[3], last_flow_time_d[3] , 'd0, clk, rst_n)
    //stage 5
    logic  [ADDR_DW-1 : 0]vertical_addr_result_tmp;
    assign vertical_addr_result_tmp = (flow_times == 6'(6'd1 << shift_flag)) ? (vertical_base_addr + vertical_result_case1_reg) : (vertical_base_addr + vertical_result_case2_reg);
    `FFLARNC(vertical_addr_result, vertical_addr_result_tmp, vertical_cnt_valid_d[4], last_flow_time_d[4] , 'd0, clk, rst_n)
    assign vertical_addr[0] = vertical_addr_result;

    // ------------------------horizontal addr counter------------------------
    logic  [0 : PADDING] [ADDR_DW-1 : 0]horizontal_addr;
    logic  [ADDR_DW-1 : 0]horizontal_addr_result;
    logic  [6-1 : 0]horizontal_x_cnt;
    logic  [6-1 : 0]horizontal_c_cnt;
    logic  [10-1 : 0]horizontal_pw_x_cnt;
    logic  [6-1 : 0]horizontal_x_cnt_num , horizontal_c_cnt_num;
    logic  [6-1 : 0]horizontal_x_cnt_num_case , horizontal_c_cnt_num_case;
    logic  [6-1 : 0]horizontal_x_cnt_result , horizontal_c_cnt_result;
    logic  [10-1 : 0]horizontal_pw_x_cnt_result;
    logic  horizontal_cnt_last , horizontal_x_cnt_clear , horizontal_c_cnt_clear;
    logic  horizontal_x_cnt_valid;
    logic  horizontal_c_cnt_valid;
    logic  horizontal_pw_x_cnt_valid;
    logic  horizontal_c_cnt_clear_flag, horizontal_c_cnt_clear_flag_valid;

    assign horizontal_x_cnt_result = horizontal_x_cnt+ 1;
    assign horizontal_c_cnt_result = horizontal_c_cnt+ 1;
    assign horizontal_pw_x_cnt_result = (10)'(horizontal_pw_x_cnt + 1);
    assign horizontal_cnt_start = (ins_valid | next_rdaddr_start) & ~input_switch[0];
    assign horizontal_x_cnt_num_case = (conv_reuse_flag) ? 
                                            (dw_register_flag ?
                                                1 : conv_kernal_i)//CONV/DWCONV MODE
                                                    : (ROW_NUM >> (pwc_flag & shift_flag_i));//PWCONV MODE or MM MODE
    assign horizontal_c_cnt_num_case = dw_register_flag ? 
                                            stride_flag_i ? (((flow_times_i<<1)>>shift_flag_i) + 1) 
                                                : ((flow_times_i>>shift_flag_i) + 2) : (flow_times_i >> (shift_flag_i & conv_reuse_flag));                                             
    assign horizontal_x_cnt_clear = (horizontal_x_cnt == horizontal_x_cnt_num-1) & horizontal_x_cnt_valid;
    assign horizontal_c_cnt_clear = (horizontal_c_cnt == horizontal_c_cnt_num -1) & horizontal_x_cnt_clear & horizontal_c_cnt_valid;
    assign horizontal_cnt_last = (conv_reuse_flag) ? horizontal_c_cnt_clear : horizontal_x_cnt_clear;
    assign horizontal_cnt_clear = (horizontal_cnt_last | last_flow_time_d[(ADDR_DELAY-1)]);
    assign horizontal_x_cnt_valid = concat_flag ? (horizontal_cnt_valid & concat_cnt_clear) : horizontal_cnt_valid;
    assign horizontal_c_cnt_valid = horizontal_x_cnt_clear;
    assign horizontal_pw_x_cnt_valid = horizontal_x_cnt_valid;
    `FFLARNC(horizontal_x_cnt_num, horizontal_x_cnt_num_case, start, rd_clear_flag , 'd0, clk, rst_n)
    `FFLARNC(horizontal_c_cnt_num, horizontal_c_cnt_num_case, start, rd_clear_flag , 'd0, clk, rst_n)
    `FFLARNC(horizontal_cnt_valid, 1'd1, horizontal_cnt_start, horizontal_cnt_clear, 'd0, clk, rst_n)
    `FFLARNC(horizontal_x_cnt, horizontal_x_cnt_result, horizontal_x_cnt_valid, horizontal_x_cnt_clear, 'd0, clk, rst_n)
    `FFLARNC(horizontal_c_cnt, horizontal_c_cnt_result, horizontal_c_cnt_valid, horizontal_c_cnt_clear, 'd0, clk, rst_n)
    `FFLARNC(horizontal_pw_x_cnt, horizontal_pw_x_cnt_result, horizontal_pw_x_cnt_valid, horizontal_c_cnt_clear, 'd0, clk, rst_n)
    // ------------------------horizontal addr caculate------------------------
    //stage 1
    logic  [0 : (ADDR_DELAY-1)]concat_cnt_clear_d;
    logic  [0 : (ADDR_DELAY-1)]horizontal_cnt_valid_d;
    logic  [18-1 : 0]horizontal_x_result;
    logic  [14-1 : 0]horizontal_channel_result;
    logic  [10-1 : 0]horizontal_x_cnt_case;
    logic  [6-1 : 0]horizontal_c_cnt_case;
    assign horizontal_x_cnt_case = (pwc_flag) ? 10'(horizontal_pw_x_cnt) : 10'(horizontal_x_cnt);
    assign horizontal_c_cnt_case = (pwc_flag) ? (horizontal_c_cnt>>shift_flag) : horizontal_c_cnt;
    DW02_mult_2_stage #(.A_width(10),.B_width(8))horizontal_mult0(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (horizontal_x_cnt_case     ),
    .B                                  (horizontal_x_step         ),
    .PRODUCT                            (horizontal_x_result       )
    );
    DW02_mult_2_stage #(.A_width(6),.B_width(8))horizontal_mult1(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (horizontal_c_cnt_case     ),
    .B                                  (horizontal_channel_step   ),
    .PRODUCT                            (horizontal_channel_result )
    );
    //stage 2
    logic  [14-1 : 0]horizontal_x_result_reg;
    logic  [14-1 : 0]horizontal_channel_result_reg;
    logic  [6-1 : 0]horizontal_c_cnt_d1[3];
    logic  [14-1 : 0]horizontal_x_result_cut;
    assign horizontal_x_result_cut = horizontal_x_result[13:0];
    `FF(horizontal_x_result_reg, horizontal_x_result_cut, 'd0, clk, rst_n)
    `FF(horizontal_channel_result_reg, horizontal_channel_result, 'd0, clk, rst_n)
    generate
        assign horizontal_c_cnt_d1[0] = horizontal_c_cnt;
        for (genvar i = 0; i < (3-1); i++) begin
            `FF(horizontal_c_cnt_d1[i+1], horizontal_c_cnt_d1[i], 'd0, clk, rst_n)
        end
    endgenerate

    //stage 3
    logic  [22-1 : 0]horizontal_xr_m_c_result;
    logic  [22-1 : 0]horizontal_cr_m_x_result;
    DW02_mult_2_stage #(.A_width(14),.B_width(8))horizontal_mult2(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (horizontal_x_result_reg   ),
    .B                                  (horizontal_channel_step   ),
    .PRODUCT                            (horizontal_xr_m_c_result  )
    );
    DW02_mult_2_stage #(.A_width(8),.B_width(14))horizontal_mult3(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (horizontal_x_step         ),
    .B                                  (horizontal_channel_result_reg),
    .PRODUCT                            (horizontal_cr_m_x_result  )
    );
    logic  [14-1 : 0]horizontal_x_result_d;
    logic  [6-1 : 0]horizontal_c_cnt_d2[3];
    `FF(horizontal_x_result_d, horizontal_x_result_reg, 'd0, clk, rst_n)
    generate
        assign horizontal_c_cnt_d2[0] = horizontal_c_cnt_d1[2];
        for (genvar i = 0; i < (3-1); i++) begin
            `FF(horizontal_c_cnt_d2[i+1], horizontal_c_cnt_d2[i], 'd0, clk, rst_n)
        end
    endgenerate
    //stage 4
    logic  [ADDR_DW-1 : 0]horizontal_result_case1_tmp;
    logic  [ADDR_DW-1 : 0]horizontal_result_case2_tmp;
    logic  [ADDR_DW-1 : 0]horizontal_result_case1_reg;
    logic  [ADDR_DW-1 : 0]horizontal_result_case2_reg;
    assign horizontal_result_case1_tmp = (conv_kernal == 0) ? (ADDR_DW)'((horizontal_c_cnt_d2[2] + horizontal_x_result_d)<<4) : (ADDR_DW)'((horizontal_xr_m_c_result)<<4);
    assign horizontal_result_case2_tmp = (conv_kernal == 0) ? (ADDR_DW)'((horizontal_c_cnt_d2[2] + horizontal_x_result_d)<<4) : (ADDR_DW)'((horizontal_cr_m_x_result + horizontal_x_result_d)<<4);
    `FFLARNC(horizontal_result_case1_reg, horizontal_result_case1_tmp, horizontal_cnt_valid_d[3], last_flow_time_d[3] , 'd0, clk, rst_n)
    `FFLARNC(horizontal_result_case2_reg, horizontal_result_case2_tmp,  horizontal_cnt_valid_d[3], last_flow_time_d[3] , 'd0, clk, rst_n)

    //stage 5
    logic  [ADDR_DW-1 : 0]horizontal_addr_result_tmp;
    assign horizontal_addr_result_tmp = ((flow_times == 6'(6'd1 << shift_flag) || pwc_flag) && !dw_register_flag) ? (horizontal_base_addr + horizontal_result_case1_reg) : (horizontal_base_addr + horizontal_result_case2_reg);
    `FFLARNC(horizontal_addr_result, horizontal_addr_result_tmp, horizontal_cnt_valid_d[4], last_flow_time_d[4] , 'd0, clk, rst_n)
    assign horizontal_addr[0] = (concat_cnt_clear_d[5]& horizontal_cnt_valid_d[5])? (mem_addr[19:0] + (ADDR_DW)'(20'd16)) //additional row value
                                                                                : horizontal_addr_result;

    // ------------------------output addr counter------------------------
    logic  [ADDR_DW-1 : 0]output_address;
    logic  [0 : (ADDR_DELAY-1)]output_state;
    logic  [0 : (ADDR_DELAY-1)]output_shift_ctl_reg_d;
    logic  [6-1 : 0]output_x_cnt;
    logic  [6-1 : 0]output_x_cnt_result;
    logic  output_x_cnt_clear, output_x_cnt_valid;
    logic  [ADDR_DW-1 : 0]output_addr_result;
    logic  output_shift_ctl;
    logic  output_shift_ctl_reg;
    assign output_shift_ctl = shift_flag && ~output_shift_ctl_reg && sram_wr_valid;
    assign output_x_cnt_valid = sram_wr_valid & (~shift_flag | output_shift_ctl_reg);
    assign output_x_cnt_clear = (output_x_cnt == ROW_NUM - 1) && output_x_cnt_valid;
    assign output_x_cnt_result = output_x_cnt+ 1;
    `FF(output_shift_ctl_reg, output_shift_ctl, '0, clk, rst_n)
    `FFLARNC(output_x_cnt, output_x_cnt_result, output_x_cnt_valid, output_x_cnt_clear, 'd0, clk, rst_n)
    assign output_state[0] = sram_wr_valid;
    assign output_shift_ctl_reg_d[0] = output_shift_ctl_reg;
    // ------------------------output addr caculate------------------------
    //stage 1
    logic  [14-1 : 0]output_x_result;
    DW02_mult_2_stage #(.A_width(6),.B_width(8))output_mult0(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (output_x_cnt              ),
    .B                                  (output_x_step             ),
    .PRODUCT                            (output_x_result           )
    );
    //stage 2
    logic  [14-1 : 0]output_x_result_reg;
    `FF(output_x_result_reg, output_x_result, 'd0, clk, rst_n)

    //stage 3  
    logic  [22-1 : 0]output_xr_m_c_result;
    DW02_mult_2_stage #(.A_width(14),.B_width(8))output_mult1(
    .CLK                                (clk                       ),
    .TC                                 (1'd0                      ),
    .A                                  (output_x_result_reg       ),
    .B                                  (output_channel_step       ),
    .PRODUCT                            (output_xr_m_c_result      )
    );

    //stage 4
    logic  [ADDR_DW-1 : 0]output_result_case1_tmp;
    logic  [ADDR_DW-1 : 0]output_result_case1_reg;
    assign output_result_case1_tmp = (ADDR_DW)'((output_xr_m_c_result)<<4);
    `FFLARNC(output_result_case1_reg, output_result_case1_tmp, output_state[3], output_reg_clear, 'd0, clk, rst_n)

    //stage 5
    logic  [ADDR_DW-1 : 0]output_addr_result_tmp;
    assign output_addr_result_tmp = output_shift_ctl_reg_d[4] ? (output_address + 20'd16) : (output_base_addr + output_result_case1_reg);
    `FFLARNC(output_addr_result, output_addr_result_tmp, output_state[4], output_reg_clear, 'd0, clk, rst_n)
    assign output_address = output_addr_result;
    // ------------------------bias addr ctrl------------------------
    logic  bias_start_flag;
    logic  bias_start, bias_start_tmp, bias_addition;
    logic  bias_rd_valid;
    assign bias_start_flag = !last_flow_time_d[ADDR_DELAY-2] & last_flow_time_d[ADDR_DELAY-3] && pe_work_mode == SA_pkg::CONV;
    `FF(bias_start_tmp, bias_start_flag, 'd0, clk, rst_n)
    `FF(bias_start, bias_start_tmp, 'd0, clk, rst_n)
    `FF(bias_addition, bias_start, 'd0, clk, rst_n)
    assign bias_rd_valid = bias_start | bias_addition;
    // ------------------------memory address control------------------------
    assign horizontal_cnt_valid_d[0] = horizontal_cnt_valid;
    assign vertical_cnt_valid_d[0] = vertical_cnt_valid;
    assign rdaddr_last_d[0] = rdaddr_last_flag;
    assign concat_cnt_clear_d[0] = next_row_valid;
    assign sram_wr_last_d[0] = sram_wr_last_i;
    assign input_switch_d[0] = input_switch;
    assign last_flow_time_d[0] = last_flow_time;
    generate
        for (genvar i = 0; i < (ADDR_DELAY-1); i++) begin
            `FF(sram_wr_last_d[i+1], sram_wr_last_d[i], 'd0, clk, rst_n)
            `FF(concat_cnt_clear_d[i+1], concat_cnt_clear_d[i], 'd0, clk, rst_n)
            `FF(output_state[i+1], output_state[i], 'd0, clk, rst_n)
            `FF(rdaddr_last_d[i+1], rdaddr_last_d[i], 'd0, clk, rst_n)
            `FF(horizontal_cnt_valid_d[i+1], horizontal_cnt_valid_d[i], 'd0, clk, rst_n)
            `FF(vertical_cnt_valid_d[i+1], vertical_cnt_valid_d[i], 'd0, clk, rst_n)
            `FF(last_flow_time_d[i+1], last_flow_time_d[i], 'd0, clk, rst_n)
            `FF(output_shift_ctl_reg_d[i+1], output_shift_ctl_reg_d[i], 'd0, clk, rst_n)
        end
        for (genvar k = 0; k < (ADDR_DELAY-2); k++) begin
            `FF(input_switch_d[k+1], input_switch_d[k], 'd0, clk, rst_n)
        end
        for (genvar j = 0; j < (PADDING); j++) begin
            `FF(vertical_addr[j+1], vertical_addr[j], 'd0, clk, rst_n)
            `FF(horizontal_addr[j+1], horizontal_addr[j], 'd0, clk, rst_n)
        end
    endgenerate
    always_ff @( posedge clk or negedge rst_n ) begin
        if (!rst_n) begin
            mem_addr  <= 'd0;
            rd_enable <= 'b0;
            wr_enable <= 'b0;
            wraddr_last <= 'b0;
            rdaddr_last <= 'b0;
        end else if (output_state[(ADDR_DELAY-1)]) begin
            mem_addr  <= (SRAM_ADDR_WIDTH)'(output_address + BASE_ADDR);
            wr_enable <= output_state[(ADDR_DELAY-1)];
            wraddr_last <= sram_wr_last_d[ADDR_DELAY-1];
            rd_enable <= 1'b0;
            rdaddr_last <= 'b0;
        end else if (bias_rd_valid) begin
            mem_addr  <= (SRAM_ADDR_WIDTH)'(bias_address + BASE_ADDR + (bias_addition << 4));
            rd_enable <= 1'b1;
            rdaddr_last <= 1'b0;
        end else if (core_state != SA_pkg::IDLE) begin
            case(input_switch_d[ADDR_DELAY-2][0])
                1'b0: begin
                    mem_addr  <= (SRAM_ADDR_WIDTH)'(horizontal_addr[PADDING] + BASE_ADDR);
                    wr_enable <= 1'b0;
                    wraddr_last <= 1'b0;
                    rd_enable <= horizontal_cnt_valid_d[(ADDR_DELAY-1)];
                    rdaddr_last <= rdaddr_last_d[(ADDR_DELAY-1)];
                end
                1'b1: begin
                    mem_addr <= (SRAM_ADDR_WIDTH)'(vertical_addr[0] + BASE_ADDR);
                    rd_enable <= vertical_cnt_valid_d[(ADDR_DELAY-1)];
                    wr_enable <= 1'b0;
                    wraddr_last <= 1'b0;
                    rdaddr_last <= rdaddr_last_d[(ADDR_DELAY-1)];
                end
            endcase
        end else begin
            mem_addr  <= 'd0;
            rd_enable <= 1'b0;
            wr_enable <= 1'b0;
            wraddr_last <= 1'b0; 
            rdaddr_last <= 1'b0;
        end
    end
    // ------------------------ data last recrify ------------------------
    logic  [6-1 : 0]test_cnt;
    logic  [6-1 : 0]test_cnt_result;
    logic  test_cnt_valid;
    logic  test_cnt_clear;
    logic  test_cnt_start;
    logic  data_last_case;
    assign test_cnt_start = (vertical_cnt_start) | (horizontal_cnt_start);
    `FFLARNC(test_cnt_valid, 1'd1, test_cnt_start, test_cnt_clear, 'd0, clk, rst_n)
    assign test_cnt_clear = (test_cnt == WAIT_NUM-1) & test_cnt_valid | last_flow_time_d[(ADDR_DELAY-1)];
    assign test_cnt_result = test_cnt + 1;
    `FFLARNC(test_cnt, test_cnt_result, test_cnt_valid, test_cnt_clear, 'd0, clk, rst_n)
    assign data_last_case = (input_switch_d[ADDR_DELAY-2][0] ? vertical_cnt_last : horizontal_cnt_last);
    assign rdaddr_last_flag = test_cnt_valid ? (test_cnt_clear & (data_last_case | ~(horizontal_cnt_valid | vertical_cnt_valid))) : data_last_case;//fixme
    // ------------------------output assign------------------------
    assign sram_mem_addr  = mem_addr;
    assign sram_rd_enable = rd_enable;
    assign sram_wr_enable = wr_enable;
    assign sram_wr_last_o = wraddr_last;
    assign sram_rdaddr_last = rdaddr_last;
    assign load_done_flag  = rdaddr_last_d[(1-1)];
    assign regfile_clear_flag = cur_out_state ==ZERO_START;
endmodule
