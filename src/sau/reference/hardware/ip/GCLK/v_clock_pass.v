module v_clock_pass(
    clk,   // clock input
    en,    // enable
    sen,   // scan enable
    gclk   // clock output
);

input  clk;
input  en;
input  sen;
output gclk;

    assign gclk = clk;
endmodule
