from m5.params import *
from m5.proxy import *
from m5.objects.SimObject import SimObject

class KuiSau(SimObject):
    type = 'KuiSau'
    cxx_header = "sau/KuiSau.hh"
    cxx_class = 'gem5::KuiSau'
    
    clk_domain = Param.ClockDomain("Clock domain")
    # 数据端口：连接到内存系统
    port_KuiSau_sendto_mem = RequestPort("Data port for matrix unit memory access")
    port_KuiSau_getfrm_mem = ResponsePort("CSR/control port for CPU CSR accesses")
    csr_addr_range = Param.AddrRange(AddrRange(0x2F000000, size=0x4),
                                     "Address range for CSR control register")
    csr_latency = Param.Cycles(1, "Latency (in cycles) for CSR accesses")
    
    # 系统参数
    system = Param.System(Parent.any, "System this matrix unit belongs to")
    rng_seed = Param.Unsigned(0, "Seed for KuiSau traffic generator (0 = default fixed seed)")
