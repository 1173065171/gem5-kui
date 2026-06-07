from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class SauGoldenGen(SimObject):
    """
    Deterministic SAU golden-reference test driver.

    The C++ object initializes A/B/D memory, writes KuiSau CSRs, polls busy,
    reads D memory, and compares the result with a small SAU.py-derived output
    vector.
    """

    type = "SauGoldenGen"
    cxx_header = "sau/SauGoldenGen.hh"
    cxx_class = "gem5::SauGoldenGen"

    system = Param.System(Parent.any, "System this golden driver belongs to")
    clk_domain = Param.ClockDomain(Parent.any, "Clock domain")
    interval = Param.Cycles(1, "Cycles between scripted operations")
    poll_interval = Param.Cycles(20, "Cycles between busy polls")
    max_busy_polls = Param.UInt32(1000, "Maximum busy polls before failure")
    test_case = Param.String(
        "gemm",
        "Golden vector: gemm, add, pointwise, normal_conv, depthwise_conv, "
        "gemm_shift, gemm_dequant, gemm_dequant_negative, "
        "gemm_dequant_int16, pointwise_dequant_mixed, "
        "normal_conv_rich, normal_conv_stride, normal_conv_stride_shift, "
        "normal_conv_shift, "
        "normal_conv_dequant_mixed, normal_conv_stride_dequant_mixed, "
        "normal_conv_stride_shift_dequant_mixed, depthwise_conv_rich, "
        "depthwise_conv_stride, depthwise_conv_stride_shift, depthwise_conv_shift, "
        "depthwise_conv_dequant_mixed, "
        "depthwise_conv_stride_dequant_mixed, "
        "depthwise_conv_stride_shift_dequant_mixed, gemm_out_transpose, "
        "gemm_retain, gemm_transpose_retain, or "
        "lkssfull_sau_stdconv_10_trace")

    mem_port = RequestPort("Memory request port")
    csr_port = RequestPort("CSR request port")
