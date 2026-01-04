from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class CsrGen(SimObject):
    """
    CSR Generator Module
    
    生成固定的 CSR 指令序列，作为 RequestPort 连接到 KuiSau 的 CSR ResponsePort
    """
    type = 'CsrGen'
    cxx_header = "sau/CsrGen.hh"
    cxx_class = 'gem5::CsrGen'

    # 时序参数
    clk_domain = Param.ClockDomain(Parent.any, "Clock domain")
    interval = Param.Cycles(10, "Cycles between CSR requests")
    max_requests = Param.UInt32(100, "Maximum number of CSR requests (0=unlimited)")
    
    # CSR RequestPort - 发送请求到 KuiSau 的 CSR ResponsePort
    csr_port = RequestPort("CSR request port")
