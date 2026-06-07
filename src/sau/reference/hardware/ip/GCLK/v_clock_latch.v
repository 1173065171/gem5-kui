module v_clock_latch(
    clk,   // clock input
    en,    // enable
    sen,   // scan enable
    gclk   // clock output
);
// IOs
input  clk;
input  en;
input  sen;

output gclk;

    reg en_dly;
    //synopsys translate_off
    initial
    begin
        en_dly = 0;
    end
    //synopsys translate_on

    always@(en)
    begin
    `ifdef ASIC_SIM
        //#0.5;
        en_dly = en;
    `else
        en_dly = en;
    `endif
    end
    `ifdef LIB_GF40_RVT
        `ifndef ARM_40NMLP
        PREICG_X0P5B_A9TR_C40_SGA0 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
        `else
        PREICG_X0P5B_A9TR_C40 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
        `endif
    `elsif LIB_GF50_RVT
        `ifndef ARM_40NMLP
        PREICG_X0P5B_A9TR_C50_SGA0 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
        `else
        PREICG_X0P5B_A9TR_C50 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
        `endif
    `elsif LIB_GF50_HVT
        `ifndef ARM_40NMLP
        PREICG_X0P5B_A9TH_C50_SGA0 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
        `else
        PREICG_X0P5B_A9TH_C50 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
        `endif
    `elsif LIB_UMC55_7T
        LAGCEPM2TM u0 (.CK (clk), .E (en_dly), .SE (sen), .GCK (gclk));
    `elsif LIB_UMC55_9T
        LAGCEPM2T  u0 (.CK (clk), .E (en_dly), .SE (sen), .GCK (gclk));
//        PREICG_X0P5B_A9TR_C40_SGA0 u0 (.ECK(gclk), .CK(clk), .E(en_dly), .SE(sen));
    `elsif LIB_GSMC11_LP
        // GSMC110 LP
        CLKGTPSIXT2X u0 (.CK (clk), .E (en_dly), .TE (sen), .Z (gclk));
    `elsif LIB_GSMC13_LP
        // GSMC130 LP 
        CLKGTPHD2X u0 (.CK (clk), .E (en_dly), .TE (sen), .Z (gclk));
    `elsif LIB_SESAME_HD_DV
        // dolphin
        hd_ckgt1d1 u0 (.CLOCK (clk), .ENABLE (en_dly), .TEST (sen), .CLOCKGATED (gclk));
    `elsif LIB_TCB018GBWP7T
        // TSMC018, tcb018gbwp7t.v
        CKLNQD4BWP7T u0 (.CP (clk), .E (en_dly), .TE (sen), .Q (gclk));
    `else
        // RTL simulation fallback when no technology ICG library is available.
        reg en_latch;
        //synopsys translate_off
        initial
        begin
            en_latch = 1'b0;
        end
        //synopsys translate_on

        always @(*)
        begin
            if (!clk)
                en_latch = en_dly | sen;
        end

        assign gclk = clk & en_latch;
    `endif


endmodule
