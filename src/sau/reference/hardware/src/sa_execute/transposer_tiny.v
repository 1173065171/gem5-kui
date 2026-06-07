`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2024/11/11 16:55:17
// Design Name: 
// Module Name: transposer
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

//todo : DIM_i and DIM_R_i less than 16 mode
module transposer_tiny #(parameter DIM = 16,DIM_R =16, DATA_WIDTH = 8) (
    input  wire [DIM*DATA_WIDTH-1:0] inRow ,
    output reg [DIM_R*DATA_WIDTH-1:0] outCol ,
    output reg  valid_o ,
    output reg  error,
    output wire ready,
    output reg  ready_o,
    output reg  last_o,
    input  wire clk,
    input  wire reuse_en,
    input  wire transpose_en,
    input  wire en,
    input  wire en_o,
    input  wire rst_n
);
    wire in_last_flag;
    wire out_last_flag;
    reg  data_in_ready;
    reg  [$clog2(DIM_R)-1:0] cnt_in;
    reg  [$clog2(DIM_R)-1:0] cnt_out;
    assign ready = data_in_ready;
    assign in_last_flag = cnt_in == DIM_R -1;
    assign out_last_flag = cnt_out == DIM_R -1;
    always @(posedge clk or negedge rst_n)
        begin
            if(!rst_n)
            data_in_ready <= 1'd1;
            else if(last_o)
            data_in_ready <= 1'd1;
            else if(in_last_flag)
            data_in_ready <= 1'd0;
            else
            data_in_ready <= data_in_ready;
        end
    always @(posedge clk or negedge rst_n)
        begin
            if(!rst_n)
                ready_o <= 1'd0;
            else if(in_last_flag)
                ready_o <= 1'd1;
            else if (last_o && !reuse_en)
                ready_o <= 1'd0;
            else
                ready_o <= ready_o;
        end
    always @(posedge clk or negedge rst_n)
        begin
            if(!rst_n)
                error <= 1'd0;
            else if (out_last_flag)
                error <= 1'd0;
            else if(!data_in_ready && en)
                error <= 1'd1;
            else
                error <= error;
        end
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cnt_in <= 0;
        end
        else if (in_last_flag)begin
            cnt_in <= 0;
        end
        else if (en) begin
            cnt_in <= cnt_in + 1;
        end
        else begin
            cnt_in <= cnt_in;
        end
    end
        always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cnt_out <= 0;
        end
        else if (out_last_flag)begin
            cnt_out <= 0;
        end
        else if (en_o) begin
            cnt_out <= cnt_out + 1;
        end
        else begin
            cnt_out <= cnt_out;
        end
    end
    always @(posedge clk or negedge rst_n)
        begin
            if(!rst_n)
                valid_o <= 'd0;
            else if(en_o && !last_o)
                valid_o <= 'd1;
            else
                valid_o <= 'd0;
        end
    always @(posedge clk or negedge rst_n)
        begin
            if(!rst_n)
                last_o <= 'd0;
            else if(out_last_flag)
                last_o <= 'd1;
            else
                last_o <= 'd0;
        end
    wire [DIM_R*DATA_WIDTH-1:0] outCol_t;
    integer i;
    reg  [DATA_WIDTH*DIM-1:0] pe_outL [DIM_R-1:0];
    always @(posedge clk)
        begin
            if(en)
                pe_outL[(cnt_in)]  <= inRow;
            else
                for( i=0;i<DIM_R;i=i+1) begin
                    pe_outL[i] <= pe_outL[i];
                end
        end
    generate
        genvar a;
        for( a=0;a<DIM_R;a=a+1) begin
            `ifdef MODULE_TEST
                assign outCol_t[(a+1)*DATA_WIDTH-1:(a)*DATA_WIDTH] = pe_outL[(DIM-1)-a][((DIM-1)-cnt_out)*DATA_WIDTH +: DATA_WIDTH];
            `else
                assign outCol_t[(a+1)*DATA_WIDTH-1:(a)*DATA_WIDTH] = pe_outL[(DIM-1)-a][(cnt_out)*DATA_WIDTH +: DATA_WIDTH];
            `endif
        end
    endgenerate
    always @(posedge clk)
        begin
            if(en_o && !last_o)
                outCol <= transpose_en ? outCol_t : pe_outL[cnt_out];
            else
                outCol <= 'd0;//todo keep or clear
        end
endmodule

