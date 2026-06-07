// Optional SAU mem_addr trace monitor for RTL/gem5 request-shape comparison.
//
// Compile this file with the RTL simulation. It binds to every mem_addr
// instance and emits a CSV consumable by util/sau_trace_summary.py.
//
// Plusargs:
//   +SAU_TRACE_CSV=<path>     Output CSV path, default sau_mem_addr_trace.csv.
//   +SAU_TRACE_DISABLE=1      Disable tracing without removing this file.

module sau_mem_addr_trace_monitor (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] sram_mem_addr,
    input  logic        sram_rd_enable,
    input  logic        sram_wr_enable,
    input  logic        input_switch_bit,
    input  logic        bias_rd_valid
);
    integer fd;
    integer disabled_arg;
    bit disabled;
    string trace_path;
    longint unsigned cycle;

    initial begin
        cycle = 0;
        disabled = 1'b0;
        disabled_arg = 0;
        trace_path = "sau_mem_addr_trace.csv";
        if ($value$plusargs("SAU_TRACE_DISABLE=%d", disabled_arg)) begin
            disabled = disabled_arg != 0;
        end
        if ($value$plusargs("SAU_TRACE_CSV=%s", trace_path)) begin
        end

        if (!disabled) begin
            fd = $fopen(trace_path, "w");
            if (fd == 0) begin
                $fatal(1, "Failed to open SAU trace CSV: %s", trace_path);
            end
            $fwrite(fd, "cycle,kind,addr,sram_rd_enable,sram_wr_enable,");
            $fwrite(fd, "input_switch_bit,bias_rd_valid\n");
        end
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cycle <= 0;
        end else begin
            cycle <= cycle + 1;
            if (!disabled && (sram_rd_enable || sram_wr_enable)) begin
                if (sram_wr_enable) begin
                    $fwrite(fd, "%0d,D,0x%08x,%0d,%0d,%0d,%0d\n",
                            cycle, sram_mem_addr, sram_rd_enable,
                            sram_wr_enable, input_switch_bit, bias_rd_valid);
                end else if (bias_rd_valid) begin
                    $fwrite(fd, "%0d,C,0x%08x,%0d,%0d,%0d,%0d\n",
                            cycle, sram_mem_addr, sram_rd_enable,
                            sram_wr_enable, input_switch_bit, bias_rd_valid);
                end else if (input_switch_bit) begin
                    $fwrite(fd, "%0d,A,0x%08x,%0d,%0d,%0d,%0d\n",
                            cycle, sram_mem_addr, sram_rd_enable,
                            sram_wr_enable, input_switch_bit, bias_rd_valid);
                end else begin
                    $fwrite(fd, "%0d,B,0x%08x,%0d,%0d,%0d,%0d\n",
                            cycle, sram_mem_addr, sram_rd_enable,
                            sram_wr_enable, input_switch_bit, bias_rd_valid);
                end
                $fflush(fd);
            end
        end
    end

    final begin
        if (!disabled && fd != 0) begin
            $fclose(fd);
        end
    end
endmodule

bind mem_addr sau_mem_addr_trace_monitor sau_mem_addr_trace_monitor_i (
    .clk(clk),
    .rst_n(rst_n),
    .sram_mem_addr(sram_mem_addr),
    .sram_rd_enable(sram_rd_enable),
    .sram_wr_enable(sram_wr_enable),
    .input_switch_bit(input_switch_d[ADDR_DELAY-2][0]),
    .bias_rd_valid(bias_rd_valid)
);
