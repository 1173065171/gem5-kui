# gem5 SAU (Systolic Array Unit) 集成指南

## 概述

本文档说明了如何将优化的脉动阵列单元 (SAU) 仿真器集成到gem5模拟平台中。SAU实现了一个指令级的矩阵计算引擎，支持多种计算模式和数据格式。

## 架构组件

### 1. KuiSau (主SAU模块)
**文件**: `src/sau/KuiSau.hh`, `src/sau/KuiSau.cc`

#### 关键数据结构

```cpp
// CSR 寄存器（8个32-bit寄存器）
struct CsrReg {
    uint32_t ins1_msb, ins1_lsb;
    uint32_t ins2_msb, ins2_lsb;
    uint32_t ins3_msb, ins3_lsb;
    uint32_t ins4_msb, ins4_lsb;
};

// 配置寄存器（由CSR解码而来）
struct ConfigReg {
    uint32_t A_address, B_address, D_address, C_address;
    uint8_t  conv_kernel, stride, shift_mode, cutbit;
    uint8_t  work_mode, flow_mode, register_mode;
    uint8_t  transpose_mode, flow_loop_times;
    // ... 地址步长和其他参数
};

// 执行状态寄存器
struct StatusReg {
    uint8_t  running;
    uint16_t flow_i;  // 当前flow迭代
    uint32_t A_address, B_address, C_address, D_address;
    uint16_t A_count, B_count, D_count;
    // ... 访问步长和计数
};
```

#### 执行流程

```
┌─────────────────────────────────────────────────────┐
│  1. CSR 写入 (processCsrPacket)                    │
│     └─> 寄存器解码 (updateCsrFromRegisters)        │
└────────────────────┬────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────┐
│  2. flow 执行 (executeFlow)                        │
│     ├─> 状态更新 (updateStatusFromConfig)          │
│     ├─> 内存读请求 (sendMemoryRead)                │
│     └─> 调度数据处理和计算                         │
└────────────────────┬────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────┐
│  3. 数据处理 (preprocess)                          │
│     ├─> 矩阵转置 (可选)                             │
│     ├─> 数据对齐和格式转换                         │
│     └─> 卷积矩阵移位 (可选)                         │
└────────────────────┬────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────┐
│  4. 脉动阵列计算 (systolicArrayExecute)            │
│     ├─> 矩阵乘法: D = A * B                        │
│     └─> 矩阵加法: D = A + B (work_mode=3)          │
└────────────────────┬────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────┐
│  5. 累加处理 (accumulateResults)                    │
│     └─> flow间数据累加                             │
└────────────────────┬────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────┐
│  6. 后处理 (最后一个flow)                          │
│     ├─> 加偏置 (addBiasC)                          │
│     ├─> 截位去量化 (dequantize)                   │
│     ├─> 输出转置 (transposeOutput)                 │
│     └─> 准备输出 (updateOutputMatrix)              │
└────────────────────┬────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────┐
│  7. 写回内存 (sendMemoryWrite)                     │
└─────────────────────────────────────────────────────┘
```

### 2. CsrGen (CSR指令生成器)
**文件**: `src/sau/CsrGen.hh`, `src/sau/CsrGen.cc`

向SAU发送CSR指令的生成器模块，支持：
- 预定义指令序列
- 周期可配置的发送间隔
- 最大请求数限制

### 3. KuiSauRun (脉动阵列核心计算)
**文件**: `src/sau/KuiSauRun.hh`, `src/sau/KuiSauRun.cc`

实现脉动阵列的核心计算逻辑：
- 128-bit数据加载
- int8×int8 -> int16乘法
- 24-bit饱和累加
- 支持多个并行单元

## 指令集定义

SAU支持8个CSR寄存器，分为4对，每对包含MSB和LSB：

### INS1 (0x200-0x201)
- **INS1_MSB[4:0]**: transpose_mode (转置模式)
- **INS1_MSB[9:5]**: cutbit (截位数)
- **INS1_LSB[1:0]**: register_mode (寄存器模式)
- **INS1_LSB[3:2]**: conv_kernel (卷积核大小)
- **INS1_LSB[4]**: stride (步长)
- **INS1_LSB[5]**: shift_mode (移位模式)

### INS2 (0x202-0x203)
- **INS2_MSB[7:0]**: B_address_chstep
- **INS2_MSB[15:8]**: A_address_chstep
- **INS2_MSB[23:16]**: D_address_chstep
- **INS2_LSB[6:0]**: B_address_xstep
- **INS2_LSB[14:8]**: A_address_xstep
- **INS2_LSB[22:16]**: D_address_xstep

### INS3 (0x204-0x205)
- **INS3_MSB[23:0]**: A_address (矩阵A基址)
- **INS3_LSB[23:0]**: B_address (矩阵B基址)

### INS4 (0x206-0x207)
- **INS4_MSB[19:0]**: C_address (偏置向量C基址)
- **INS4_MSB[21:20]**: work_mode (工作模式)
- **INS4_MSB[23:22]**: flow_mode (流处理模式)
- **INS4_MSB[29:24]**: flow_loop_times (流循环次数)
- **INS4_LSB[0]**: start (启动标志)
- **INS4_LSB[8:1]**: ins_id (指令ID)
- **INS4_LSB[28:9]**: D_address (输出矩阵D基址)

## 工作模式

### work_mode = 0-2: 矩阵乘法
$$D = A \times B + C \quad (shift\_mode=0/1)$$

支持8-bit和16-bit两种精度

### work_mode = 3: 矩阵加法
$$D = A + B$$

### register_mode = 0: 普通卷积
支持标准卷积操作，卷积核大小3×3或5×5

### register_mode = 2: 深度卷积 (DW)
逐通道卷积，适合轻量级模型

## 数据格式

### shift_mode = 0 (8-bit)
```
输入:  int8 × int8
中间:  int16 (8+8 bit)
累加:  int24 (24-bit有符号)
输出:  int8 (截位后)
```

### shift_mode = 1 (16-bit)
```
输入:  int16 × int8
中间:  int24 (16+8 bit)
累加:  int32 (32-bit有符号)
输出:  int16 (截位后)
```

## 内存映射

```
基地址: 0x20000000

矩阵A:  A_address
矩阵B:  B_address
向量C:  C_address
输出D:  D_address
```

## 配置示例

### 例1: 16×16矩阵乘法
```python
config = KuiSau(
    clk_domain=system.clk_domain,
    csr_addr_range=AddrRange(0x2f000000, 0x2f000100),
    csr_latency=Cycles(5)
)

# CSR寄存器配置
csr.ins1_msb = 0x00000000      # no transpose, no shift
csr.ins1_lsb = 0x00000000      # register_mode=0, conv_kernel=0
csr.ins2_msb = 0x00000000      # step_ch=0
csr.ins2_lsb = 0x00000001      # step_x=1 for all
csr.ins3_msb = 0x00000000      # A_address=0x20000000
csr.ins3_lsb = 0x00001000      # B_address=0x20001000
csr.ins4_msb = 0x02000000      # C_address=0x20000000, work_mode=0
csr.ins4_lsb = 0x00002001      # D_address=0x20002000, start=1
```

### 例2: 3×3卷积
```python
# register_mode=0 (普通卷积)
# conv_kernel=3
# flow_loop_times=1
```

## 使用配置脚本

### 基础配置
```bash
gem5.opt configs/example/kui_sau_test.py \
    --mem-size=1GB \
    --csr-interval=10 \
    --max-csr-requests=100
```

### 矩阵测试
```bash
gem5.opt configs/example/kui_sau_matrix_test.py
```

## 编译和运行

1. **编译gem5**
```bash
scons build/ALL/gem5.opt -j$(nproc)
```

2. **运行SAU仿真**
```bash
./build/ALL/gem5.opt configs/example/kui_sau_test.py
```

## 调试

### 启用调试输出
```bash
./build/ALL/gem5.debug configs/example/kui_sau_test.py --debug-flags=KuiSau,CsrGen
```

### 关键调试点
- `processCsrPacket()`: CSR指令接收和解码
- `updateCsrFromRegisters()`: CSR参数提取
- `updateStatusFromConfig()`: 状态计算
- `executeFlow()`: flow执行流程
- `systolicArrayExecute()`: 核心计算逻辑

## 性能分析

### 关键指标
- **延迟**: 从CSR写入到结果输出的周期数
- **吞吐量**: 每周期处理的数据量 (元素/周期)
- **内存带宽**: 读写请求的数据量
- **利用率**: 实际计算周期 / 总周期

### 典型性能数据
- 16×16矩阵乘法: ~40-50周期
- 3×3卷积输出(14×14): ~100-150周期
- 内存读写延迟: 5-10周期

## 扩展和优化

### 支持的扩展
1. **多SAU单元**: 可配置多个并行KuiSau
2. **共享L2缓存**: 优化矩阵数据访问
3. **DMA引擎**: 异步数据传输
4. **性能计数器**: 详细的统计信息

### 优化建议
1. **数据预取**: 提前加载下一个flow的数据
2. **流水线执行**: 重叠计算和内存访问
3. **数据重排**: 优化矩阵存储格式
4. **量化优化**: 使用更低精度计算

## 参考文档

- [SAU.py](../tmp/SAU.py): 原始Python仿真器
- [KuiSau.hh](src/sau/KuiSau.hh): C++实现
- [gem5文档](http://www.gem5.org/documentation)

## 常见问题

**Q: 为什么矩阵大小固定为16×16？**
A: 这是硬件脉动阵列的配置，可通过修改unitSize参数扩展。

**Q: 如何支持更大的矩阵？**
A: 使用flow_loop_times参数将大矩阵分块计算，多个flow累加结果。

**Q: 如何优化内存带宽？**
A: 调整address_xstep和address_chstep参数，或添加缓存层。

**Q: 支持哪些激活函数？**
A: 当前版本不支持激活函数，可作为后续扩展。
