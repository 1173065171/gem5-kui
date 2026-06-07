// TODO 1.Reduce cycles of once read and write (accelerate the speed of read and write)
// TODO 2.Add the function of read and write auto-address-transform for different mode

module mem_ctrl #(
    parameter int ADDR_DELAY = 6,
    parameter int SRAM_DELAY = 3,
    parameter int unsigned SRAM_ADDR_WIDTH = 32,
    parameter int unsigned SRAM_DATA_WIDTH = 128,
    parameter int unsigned SRAM_DATA_BYTES = SRAM_DATA_WIDTH / 8
)(
    input  wire clk,
    input  wire rst_n,

    // work state
    input  SA_pkg::registerfile_state_e core_state,
    input  wire last_ins_flag,
    output reg  last_ins_wr_done,
    // core data
    input  wire [SRAM_DATA_WIDTH-1:0] core_register_data_in,
    input  wire         core_register_data_in_last,
    output reg  [SRAM_DATA_WIDTH-1:0] core_register_data_out,
    output reg          core_register_data_out_valid,
    output reg          core_register_data_out_last,
    // sram_mem_addr
    input  wire [SRAM_ADDR_WIDTH-1:0]  sram_mem_addr,
    input  wire         sram_rd_enable,
    input  wire         sram_wr_enable,
    input  wire         sram_rdaddr_last,
    // sram data
    output reg          sram_enable,
    input  wire [SRAM_DATA_WIDTH-1:0] sram_rd_data,
    output reg          sram_rd_last,
    output reg  [SRAM_DATA_WIDTH-1:0] sram_wr_data,
    output reg  [SRAM_DATA_BYTES-1:0] sram_wstrb,
    output reg  [SRAM_ADDR_WIDTH-1:0]  sram_addr

);
    localparam int STATE_DELAY = SRAM_DELAY + 1;
    logic [0 : STATE_DELAY] sram_rdaddr_last_reg;
    logic [0 : STATE_DELAY] sram_rddata_valid_reg;
    logic sram_wr_last;
    logic sram_wr_valid;
    generate
        assign sram_rdaddr_last_reg[0] = sram_rdaddr_last;
        assign sram_rddata_valid_reg[0] = sram_rd_enable;
        for (genvar j = 0; j < STATE_DELAY; j++) begin
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    sram_rdaddr_last_reg[j+1] <= 'd0;
                    sram_rddata_valid_reg[j+1] <= 'd0;
                end else begin
                    sram_rdaddr_last_reg[j+1] <= sram_rdaddr_last_reg[j];
                    sram_rddata_valid_reg[j+1] <= sram_rddata_valid_reg[j];
                end
            end 
        end
    endgenerate
    // state machine declaration of 2 control
    typedef enum logic [1:0] {
        IDLE       = 2'b00,
        REQUESTING = 2'b01,
        WAITING    = 2'b10
    } mem_ctrl_current_state_t;

    mem_ctrl_current_state_t mem_ctrl_current_state, mem_ctrl_next_state;

    // signals declaration

    // ------------------------memory control state machine------------------------
    // layer-2 state machine
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mem_ctrl_current_state <= IDLE;
        end else begin
            mem_ctrl_current_state <= mem_ctrl_next_state;
        end
    end
    // state machine transition
    always_comb begin
        mem_ctrl_next_state = mem_ctrl_current_state;
        if(sram_rd_enable) begin
            unique case(mem_ctrl_current_state)
                IDLE: begin
                    if(sram_rd_enable) begin
                        mem_ctrl_next_state = REQUESTING;
                    end
                end
                REQUESTING: begin
                    mem_ctrl_next_state  = WAITING;
                end
                WAITING: begin
                    if(sram_rdaddr_last) begin
                        mem_ctrl_next_state = IDLE;
                    end
                end
                default: begin
                    mem_ctrl_next_state = IDLE;
                end
            endcase
        end
        else if(sram_wr_enable) begin
            unique case(mem_ctrl_current_state)
                IDLE: begin
                    mem_ctrl_next_state = REQUESTING;
                end
                REQUESTING: begin
                    mem_ctrl_next_state  = WAITING;
                end
                WAITING: begin
                    if(core_register_data_in_last) begin
                        mem_ctrl_next_state  = IDLE;
                    end
                end
                default: begin
                    mem_ctrl_next_state = IDLE;
                end
            endcase
        end
    end
    // different operation in different state
    always_ff @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            sram_addr       <= 'd0;
            sram_enable     <= 'd0;
            sram_rd_last    <= 'd0;
            sram_wr_data    <= 'd0;
            sram_wr_valid   <= 'd0;
            sram_wr_last    <= 'd0;
            sram_wstrb      <= 'd0;
            last_ins_wr_done <= 'd0;
            core_register_data_out  <= 'd0;
            core_register_data_out_valid  <= 'd0;
            core_register_data_out_last  <= 'd0;
        end else begin
            core_register_data_out <= sram_rd_data;
            core_register_data_out_valid <= sram_rddata_valid_reg[STATE_DELAY];
            core_register_data_out_last <= sram_rdaddr_last_reg[STATE_DELAY];
            if(sram_rd_enable) begin
                sram_wstrb    <= 16'h0000;
                unique case(mem_ctrl_current_state)
                    IDLE,REQUESTING,WAITING: begin
                        sram_enable     <= sram_rd_enable;
                        sram_addr       <= sram_mem_addr;
                        sram_rd_last    <= sram_rdaddr_last;
                    end
                    default: begin
                        sram_enable     <= sram_rd_enable;
                        sram_addr       <= sram_mem_addr;
                        sram_rd_last    <= sram_rdaddr_last;
                    end
                endcase
            end
            else if(sram_wr_enable) begin
                unique case(mem_ctrl_current_state)
                    IDLE,REQUESTING,WAITING: begin
                        sram_wr_valid <= sram_wr_enable;
                        sram_enable   <= sram_wr_enable;
                        sram_addr     <= sram_mem_addr;
                        sram_wr_last  <= core_register_data_in_last;
                        sram_wr_data  <= core_register_data_in;
                        sram_wstrb    <= {SRAM_DATA_BYTES{1'b1}};
                    end
                    default: begin
                        sram_enable     <= sram_rd_enable;
                        sram_addr       <= sram_mem_addr;
                        sram_rd_last    <= sram_rdaddr_last;
                    end
                endcase
                last_ins_wr_done <= core_register_data_in_last & last_ins_flag & (core_state == SA_pkg::D_OUT);
            end
            else begin
                sram_addr       <= 'd0;
                sram_enable     <= 'd0;
                sram_rd_last    <= 'd0;
                sram_wr_data    <= 'd0;
                sram_wr_valid   <= 'd0;
                sram_wstrb      <= 'd0;
                sram_wr_last    <= 'd0;
                last_ins_wr_done<= 'd0;
            end
        end
    end
endmodule
