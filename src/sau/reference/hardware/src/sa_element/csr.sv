module csr#(
    parameter [7:0] DEVICE_ID = 8'h20
)(
    // sys
    input               clk,
    input               rst_n,

    // csr interface
    input  wire         csr_we,
    input  wire         csr_re,
    input  wire  [1:0]  csr_operation,
    input  wire  [11:0] csr_addr,
    input  wire  [63:0] csr_wdata,
    output wire  [31:0] csr_rdata,
    output wire         csr_ready,

    // 2D core register
    input  wire             flow_end,
    output wire             start,
    output wire [19:0]      vertical_address,
    output wire [8-1:0]     vertical_x_step,
    output wire [8-1:0]     vertical_c_step,
    output wire [19:0]      horizontal_address,
    output wire [8-1:0]     horizontal_x_step,
    output wire [8-1:0]     horizontal_c_step,
    output wire [19:0]      output_address,
    output wire [8-1:0]     output_x_step,
    output wire [8-1:0]     output_c_step,
    output wire [20-1:0]    bias_address,
    output wire [5:0]       flow_loop_times,
    output wire [2:0]       conv_kernal,
    output wire [1:0]       reuse_mode,
    output wire [1:0]       trans_mode,
    output wire [1:0]       pe_work_mode,
    output wire [1:0]       sa_flow_mode,
    output wire [1:0]       register_mode,
    output wire [1-1:0]     stride_flag,
    output wire [0:0]       last_ins_flag,
    output wire [0:0]       shift_flag,
    output wire [4:0]       cutbit,
    output wire             crossbar_start,
    output wire             crossbar_error

);

// 2D core state register
reg           busy;
reg   [31:0]  csr_rdata_reg;
logic [2:0]   csr_addr_reg;
logic         csr_addr_last;
logic [31:0]  ins_reg_0;
logic [31:0]  ins_reg_1;
logic [31:0]  ins_reg_2;
logic [31:0]  ins_reg_3;
logic [31:0]  ins_reg_4;
logic [31:0]  ins_reg_5;
logic [31:0]  ins_reg_6;
logic [31:0]  ins_reg_7;
// 2D core ctrl register
reg           start_reg;
reg [19:0]    vertical_address_reg;
reg [19:0]    horizontal_address_reg;
reg [19:0]    output_address_reg;
reg [20-1:0]  bias_address_reg;
reg [5:0]     flow_loop_times_reg;
reg [2:0]     conv_kernal_reg;
reg [1:0]     reuse_mode_reg;
reg [1:0]     trans_mode_reg;
reg [2-1:0]   pe_work_mode_reg;
reg [2-1:0]   sa_flow_mode_reg;
reg [2-1:0]   register_mode_reg;
reg [8-1:0]   vertical_x_step_reg;
reg [8-1:0]   vertical_c_step_reg;
reg [8-1:0]   horizontal_x_step_reg;
reg [8-1:0]   horizontal_c_step_reg;
reg [8-1:0]   output_x_step_reg;
reg [8-1:0]   output_c_step_reg;
reg [4:0]     cutbit_reg;
reg [7:0]     ins_id;
reg [0:0]     stride_flag_reg;
reg [0:0]     last_ins_flag_reg;
reg [0:0]     shift_flag_reg;
reg [0:0]     csr_ready_reg;
// 2D core ctrl register
always_ff @( posedge clk or negedge rst_n ) begin
    if(!rst_n) begin
        vertical_address_reg    <= 'h0;
        horizontal_address_reg  <= 'h0;
        output_address_reg      <= 'h0;
        flow_loop_times_reg     <= 'h0;
        reuse_mode_reg          <= 'h0;
        trans_mode_reg          <= 'h0;
        pe_work_mode_reg        <= 'h0;
        sa_flow_mode_reg        <= 'h0;
        register_mode_reg       <= 'h0;
        conv_kernal_reg         <= 'h0;
        ins_id                  <= 'h0;
        // csr state
        start_reg               <= 'h0;
        busy                    <= 'h0;
        csr_rdata_reg           <= 'h0;
        vertical_x_step_reg     <= 'h0;
        vertical_c_step_reg     <= 'h0;
        horizontal_x_step_reg   <= 'h0;
        horizontal_c_step_reg   <= 'h0;
        output_x_step_reg       <= 'h0;
        output_c_step_reg       <= 'h0;
        cutbit_reg              <= 'h0;
        stride_flag_reg         <= 'h0;
        bias_address_reg        <= 'h0;
        last_ins_flag_reg       <= 'h0;
        shift_flag_reg          <= 'h0;
        csr_ready_reg           <= 'h0;
    end else if(start || flow_end) begin
        busy       <= ~ flow_end;
        start_reg  <= 'h0;
        csr_ready_reg <= csr_re;
    end else if(csr_we && (csr_addr[11:4]==DEVICE_ID)) begin
        unique case (csr_addr_reg)
        /*
        csr_addr: 0xABC
        A:Device    |   (1D or 2D)
        B:Index     |   (Example:2D No.Index)
        C:Register  |   (ins set)
        */
            3'h0: begin
                cutbit_reg              <= csr_wdata[38:34];
                trans_mode_reg          <= csr_wdata[33:32];
                shift_flag_reg          <= csr_wdata[5:5];
                stride_flag_reg         <= csr_wdata[4:4];
                conv_kernal_reg         <= csr_wdata[3:2];
                register_mode_reg       <= csr_wdata[1:0];
                reuse_mode_reg          <= {1'b0,csr_wdata[3]};//tmp martix
                last_ins_flag_reg       <= csr_wdata[39:39];//for debug
                csr_ready_reg           <= 1'b1;
            end
            3'h1: begin
                output_c_step_reg       <= csr_wdata[55:48];
                horizontal_c_step_reg   <= csr_wdata[47:40];
                vertical_c_step_reg     <= csr_wdata[39:32];
                output_x_step_reg       <= csr_wdata[23:16];
                horizontal_x_step_reg   <= csr_wdata[15:8];
                vertical_x_step_reg     <= csr_wdata[7:0];
                csr_ready_reg           <= 1'b1;
            end
            3'h2: begin
                horizontal_address_reg  <= csr_wdata[51:32];
                vertical_address_reg    <= csr_wdata[19:0];
                csr_ready_reg           <= 1'b1;
            end
            3'h3: begin
                flow_loop_times_reg     <= csr_wdata[61:56];
                sa_flow_mode_reg        <= csr_wdata[55:54];
                pe_work_mode_reg        <= csr_wdata[53:52];
                bias_address_reg        <= csr_wdata[51:32];
                output_address_reg      <= csr_wdata[28:9];
                ins_id                  <= csr_wdata[8:1];
                start_reg               <= csr_operation[0] & (start_reg | csr_wdata[0:0]) |
                                           csr_operation[1] & (start_reg & csr_wdata[0:0]);
                csr_ready_reg           <= 1'b1;
            end
            default: begin
                vertical_address_reg    <= vertical_address_reg  ;
                horizontal_address_reg  <= horizontal_address_reg;
                output_address_reg      <= output_address_reg    ;
                flow_loop_times_reg     <= flow_loop_times_reg   ;
                pe_work_mode_reg        <= pe_work_mode_reg      ;
                sa_flow_mode_reg        <= sa_flow_mode_reg      ;
                register_mode_reg       <= register_mode_reg     ;
                start_reg               <= start_reg             ;
                busy                    <= busy                  ;
                vertical_x_step_reg     <= vertical_x_step_reg   ;
                vertical_c_step_reg     <= vertical_c_step_reg   ;
                horizontal_x_step_reg   <= horizontal_x_step_reg ;
                horizontal_c_step_reg   <= horizontal_c_step_reg ;
                output_x_step_reg       <= output_x_step_reg     ;
                output_c_step_reg       <= output_c_step_reg     ;
                conv_kernal_reg         <= conv_kernal_reg       ;
                reuse_mode_reg          <= reuse_mode_reg        ;
                trans_mode_reg          <= trans_mode_reg        ;
                cutbit_reg              <= cutbit_reg            ;
                stride_flag_reg         <= stride_flag_reg       ;
                bias_address_reg        <= bias_address_reg      ;
                last_ins_flag_reg       <= last_ins_flag_reg     ;
                shift_flag_reg          <= shift_flag_reg        ;
                csr_ready_reg           <= 1'b0                  ;
            end
        endcase
    end else if(csr_re && (csr_addr[11:4]==DEVICE_ID)) begin
        unique case (csr_addr_reg)
            3'h0: begin
                csr_rdata_reg           <= csr_addr_last ? ins_reg_0 : ins_reg_1;
                csr_ready_reg           <= 1'b1;
            end
            3'h1: begin
                csr_rdata_reg           <= csr_addr_last ? ins_reg_2 : ins_reg_3;
                csr_ready_reg           <= 1'b1;
            end
            3'h2: begin
                csr_rdata_reg           <= csr_addr_last ? ins_reg_4 : ins_reg_5;
                csr_ready_reg           <= 1'b1;
            end
            3'h3: begin
                csr_rdata_reg           <= csr_addr_last ? ins_reg_6 : ins_reg_7;
                csr_ready_reg           <= 1'b1;
            end
            default: begin
                csr_rdata_reg           <= csr_rdata_reg;
                csr_ready_reg           <= 1'b0;
            end
        endcase
    end else begin
        vertical_address_reg    <= vertical_address_reg  ;
        horizontal_address_reg  <= horizontal_address_reg;
        output_address_reg      <= output_address_reg    ;
        flow_loop_times_reg     <= flow_loop_times_reg   ;
        pe_work_mode_reg        <= pe_work_mode_reg      ;
        sa_flow_mode_reg        <= sa_flow_mode_reg      ;
        register_mode_reg       <= register_mode_reg     ;
        start_reg               <= start_reg             ;
        busy                    <= busy                  ;
        vertical_x_step_reg     <= vertical_x_step_reg   ;
        vertical_c_step_reg     <= vertical_c_step_reg   ;
        horizontal_x_step_reg   <= horizontal_x_step_reg ;
        horizontal_c_step_reg   <= horizontal_c_step_reg ;
        output_x_step_reg       <= output_x_step_reg     ;
        output_c_step_reg       <= output_c_step_reg     ;
        conv_kernal_reg         <= conv_kernal_reg       ;
        reuse_mode_reg          <= reuse_mode_reg        ;
        trans_mode_reg          <= trans_mode_reg        ;
        cutbit_reg              <= cutbit_reg            ;
        csr_rdata_reg           <= 'h0                   ;
        csr_ready_reg           <= 1'b0                  ;
        stride_flag_reg         <= stride_flag_reg       ;
        bias_address_reg        <= bias_address_reg      ;
        last_ins_flag_reg       <= last_ins_flag_reg     ;
        shift_flag_reg          <= shift_flag_reg        ;
    end
end

// ins info for csr_rdata, cpu can apply the ins by csr bus
always_comb begin : ins_for_rdata
    csr_addr_reg  = csr_addr[3:1];
    csr_addr_last = csr_addr[0];
    ins_reg_0 = {{24{1'b0}}, last_ins_flag_reg, cutbit_reg, trans_mode_reg};
    ins_reg_1 = {{26{1'b0}}, shift_flag_reg, stride_flag_reg, conv_kernal_reg, register_mode_reg};
    ins_reg_2 = {{8{1'b0}}, output_c_step_reg, horizontal_c_step_reg, vertical_c_step_reg};
    ins_reg_3 = {{8{1'b0}}, output_x_step_reg, horizontal_x_step_reg, vertical_x_step_reg};
    ins_reg_4 = {{12{1'b0}}, horizontal_address_reg};
    ins_reg_5 = {{12{1'b0}}, vertical_address_reg};
    ins_reg_6 = {{2{1'b0}}, flow_loop_times_reg, sa_flow_mode_reg, pe_work_mode_reg, bias_address_reg};
    ins_reg_7 = {busy, {2{1'b0}}, output_address_reg, ins_id, start_reg};
end

//error detect
logic is_processing, crossbar_error_reg;
always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        is_processing <= 1'b0;
    end else if (flow_end) begin
        is_processing <= 1'b0;
    end else if (start) begin
        is_processing <= 1'b1;
    end
end
always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        crossbar_error_reg <= 1'b0;
    end else begin
        if (is_processing && csr_we && (csr_addr[11:4] == DEVICE_ID)) begin
            crossbar_error_reg <= 1'b1;
        end else begin
            crossbar_error_reg <= 1'b0;
        end
    end
end

// signals assign
assign csr_rdata           = csr_rdata_reg          ;
assign start               = start_reg              ;
assign vertical_address    = vertical_address_reg   ;
assign horizontal_address  = horizontal_address_reg ;
assign output_address      = output_address_reg     ;
assign flow_loop_times     = flow_loop_times_reg    ;
assign pe_work_mode        = pe_work_mode_reg       ;
assign sa_flow_mode        = sa_flow_mode_reg       ;
assign register_mode       = register_mode_reg      ;
assign vertical_x_step     = vertical_x_step_reg    ;
assign vertical_c_step     = vertical_c_step_reg    ;
assign horizontal_x_step   = horizontal_x_step_reg  ;
assign horizontal_c_step   = horizontal_c_step_reg  ;
assign output_x_step       = output_x_step_reg      ;
assign output_c_step       = output_c_step_reg      ;
assign conv_kernal         = conv_kernal_reg        ;
assign reuse_mode          = reuse_mode_reg         ;
assign trans_mode          = trans_mode_reg         ;
assign cutbit              = cutbit_reg             ;
assign stride_flag         = stride_flag_reg        ;
assign bias_address        = bias_address_reg       ;
assign last_ins_flag       = last_ins_flag_reg      ;
assign shift_flag          = shift_flag_reg         ;
assign csr_ready           = csr_ready_reg          ;
assign crossbar_start      = start_reg              ;
assign crossbar_error      = crossbar_error_reg     ;
endmodule
