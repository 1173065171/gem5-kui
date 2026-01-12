# SAU.py - Parameter Documentation Only
# 
# Note: SAUConfig and SAUStatus should NOT be defined as SimObjects
# Their parameters are integrated into KuiSau SimObject
# 
# This file is retained only for documentation purposes

"""
SAU Parameter Documentation:

Basic Configuration:
- unit_size: Matrix unit size (8 or 16)
- base_addr: SAU memory base address

Operation Mode Parameters:
- work_mode: Operation mode (0-2=matrix multiply, 3=matrix add)
- conv_kernel: Convolution kernel size (0=matrix mult, 1/3/5=convolution)
- register_mode: Register mode (0=normal convolution, 2=depthwise convolution)

Data Processing Parameters:
- shift_mode: Shift mode flag
- cutbit: Bit truncation count
- stride: Convolution stride

Transpose and Flow Parameters:
- transpose_mode: Transpose mode (0=none, 1=A transpose, 2=B transpose)
- flow_mode: Flow processing mode
- flow_loop_times: Flow processing loop count

Address and Stride Configuration:
- A_address, A_address_xstep, A_address_chstep: Matrix A address config
- B_address, B_address_xstep, B_address_chstep: Matrix B address config
- C_address: Bias vector C base address
- D_address, D_address_xstep, D_address_chstep: Output matrix D address config

Status Parameters:
- running: SAU running state
- running_time: Running time in cycles
- flow_i: Current flow processing index
- memory_reads/writes: Memory access statistics
- compute_cycles/total_cycles: Compute cycle statistics

All these parameters are passed through KuiSau SimObject's Params.
See KuiSau.py for actual SimObject definition.
"""
