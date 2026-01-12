# SAU (Systolic Array Unit) gem5集成总结

## 完成的工作

### 1. 核心C++实现 (KuiSau)

#### 数据结构 (KuiSau.hh)
- ✅ **CsrReg**: 8个CSR寄存器组 (INS1-INS4)
- ✅ **ConfigReg**: 配置寄存器 (从CSR解码而来)
- ✅ **StatusReg**: 执行状态寄存器
- ✅ **MatrixBuffer**: 矩阵缓冲区结构

#### 核心方法 (KuiSau.cc)

**CSR处理**:
- ✅ `updateCsrFromRegisters()`: CSR寄存器解码 (对标SAU.py的update_csr)
- ✅ `updateStatusFromConfig()`: 状态寄存器更新 (对标SAU.py的update_status)
- ✅ `processCsrPacket()`: CPU侧CSR包处理

**数据处理**:
- ✅ `updateInputMatrix1/2/3()`: 矩阵数据加载 (对标SAU.py的update_input_m1/2/3)
- ✅ `preprocess()`: 数据预处理，矩阵转置和格式转换
- ✅ `convMatrixShift()`: 卷积矩阵移位处理
- ✅ `updateOutputMatrix()`: 输出矩阵准备

**计算处理**:
- ✅ `systolicArrayExecute()`: 脉动阵列计算 (矩阵乘法/加法)
- ✅ `accumulateResults()`: flow间累加 (对标SAU.py的accumulate_array)
- ✅ `addBiasC()`: 加偏置向量C (对标SAU.py的c_plus)
- ✅ `dequantize()`: 截位去量化 (对标SAU.py的de_quant)
- ✅ `transposeOutput()`: 输出转置 (对标SAU.py的transpose_output)

**流程控制**:
- ✅ `executeFlow()`: 完整的flow执行流程
- ✅ `handleResponse()`: 内存响应处理

**内存操作**:
- ✅ `sendMemoryRead()`: 发送内存读请求
- ✅ `sendMemoryWrite()`: 发送内存写请求

### 2. Python配置层

#### SimObject定义
- ✅ `KuiSau.py`: SAU主模块参数定义
- ✅ `CsrGen.py`: CSR生成器参数定义
- ✅ `SAU.py`: SAU配置和状态结构定义

#### gem5配置脚本
- ✅ `configs/example/kui_sau_test.py`: 基础配置脚本
- ✅ `configs/example/kui_sau_matrix_test.py`: 矩阵测试脚本

### 3. 文档
- ✅ `docs/SAU_Integration_Guide.md`: 详细集成指南
  - 架构设计说明
  - 执行流程图
  - 指令集定义
  - 工作模式详解
  - 配置示例
  - 性能分析
  - 调试指南

## 关键特性

### 支持的计算模式
- ✅ 矩阵乘法 (work_mode=0-2)
  - 支持float32、int8、int16等多种精度
  - 累加和截位支持
  
- ✅ 矩阵加法 (work_mode=3)

- ✅ 卷积操作 (conv_kernel=1,3,5)
  - 普通卷积 (register_mode=0)
  - 深度卷积/DW (register_mode=2)
  - 可配置步长和转置

### 数据格式支持
- ✅ shift_mode=0: int8×int8→int24→int8
- ✅ shift_mode=1: int16×int8→int32→int16

### 内存访问优化
- ✅ 可配置的行步长 (xstep) 和通道步长 (chstep)
- ✅ 支持burst读写
- ✅ 灵活的寻址方式

### 执行流程
1. ✅ CSR指令接收和解码
2. ✅ 配置参数提取和验证
3. ✅ 状态寄存器计算
4. ✅ 内存数据加载 (A矩阵、B矩阵、C向量)
5. ✅ 数据预处理 (转置、格式转换、数据重排)
6. ✅ 脉动阵列计算 (矩阵乘/加运算)
7. ✅ Flow间累加
8. ✅ 后处理 (加偏置、截位、转置)
9. ✅ 结果写回内存

## gem5集成验证

### 端口连接
- ✅ **KuiSauMemSidePort**: 连接到SystemXBar (内存访问)
- ✅ **KuiSauCsrSidePort**: ResponsePort (CPU CSR访问)
- ✅ **CsrGen.csr_port**: 向SAU发送CSR指令

### 协议支持
- ✅ RequestPort: Timing协议 (内存读写)
- ✅ ResponsePort: Timing协议 (CSR访问)

### 时钟和同步
- ✅ 时钟域配置
- ✅ 事件调度机制
- ✅ Cycle精确的时序模型

## 构建和运行

### 编译
```bash
cd /gem5/gem5-kui
scons build/RISCV/gem5.opt -j$(nproc)
```

### 运行基础配置
```bash
./build/RISCV/gem5.opt configs/example/kui_sau_test.py
```

### 运行矩阵测试
```bash
./build/RISCV/gem5.opt configs/example/kui_sau_matrix_test.py
```

### 启用调试
```bash
./build/RISCV/gem5.debug configs/example/kui_sau_test.py \
    --debug-flags=KuiSau,CsrGen \
    --debug-file=sau_debug.txt
```

## 文件清单

### 源代码
- `src/sau/KuiSau.hh` - SAU主模块头文件 (数据结构和接口)
- `src/sau/KuiSau.cc` - SAU主模块实现 (核心算法)
- `src/sau/CsrGen.hh` - CSR生成器头文件
- `src/sau/CsrGen.cc` - CSR生成器实现
- `src/sau/KuiSauRun.hh` - 脉动阵列核心计算
- `src/sau/KuiSauRun.cc` - 脉动阵列实现
- `src/sau/KuiPacket128.hh` - 128-bit数据包定义
- `src/sau/KuiPacket128.cc` - 数据包实现

### Python配置
- `src/sau/KuiSau.py` - SAU SimObject定义
- `src/sau/CsrGen.py` - CSR生成器SimObject定义
- `src/sau/SAU.py` - SAU配置和状态结构

### 配置脚本
- `configs/example/kui_sau_test.py` - 基础系统配置
- `configs/example/kui_sau_matrix_test.py` - 矩阵测试脚本

### 文档
- `docs/SAU_Integration_Guide.md` - 详细集成指南
- `tmp/SAU.py` - 原始Python仿真器 (参考)

### 构建配置
- `src/sau/SConscript` - 编译脚本

## 与SAU.py的映射关系

| SAU.py方法 | KuiSau.cc方法 | 功能 |
|-----------|---------------|------|
| update_csr | updateCsrFromRegisters | CSR解码 |
| update_status | updateStatusFromConfig | 状态更新 |
| update_input_m1 | updateInputMatrix1 | 矩阵A加载 |
| update_input_m2 | updateInputMatrix2 | 矩阵B加载 |
| update_input_m3 | updateInputMatrix3 | 向量C加载 |
| preprocess | preprocess | 数据预处理 |
| conv_mat_shift | convMatrixShift | 卷积移位 |
| systolic_array | systolicArrayExecute | 脉动计算 |
| accumulate_array | accumulateResults | 累加处理 |
| c_plus | addBiasC | 加偏置 |
| de_quant | dequantize | 去量化 |
| transpose_output | transposeOutput | 输出转置 |
| update_output | updateOutputMatrix | 输出准备 |
| run | executeFlow | 完整流程 |

## 性能考虑

### 时序准确性
- Cycle精确的脉动阵列计算
- 内存延迟建模 (5-10周期)
- 端口阻塞处理

### 可扩展性
- 支持多SAU单元配置
- 模块化的流程设计
- 易于扩展新的工作模式

### 调试能力
- 详细的日志输出
- gem5 DebugFlag支持
- 内存访问追踪

## 已知限制

1. **矩阵大小固定**: 当前为16×16，支持多flow处理更大矩阵
2. **没有激活函数**: 可作为后续扩展
3. **没有DMA**: 所有数据通过gem5内存系统访问
4. **没有缓存**: 直接访问内存，未来可添加L1/L2缓存

## 后续改进建议

1. **性能优化**
   - 添加数据预取机制
   - 支持流水线化的flow执行
   - 优化矩阵存储格式

2. **功能扩展**
   - 支持更多卷积模式
   - 集成激活函数单元
   - 支持量化感知训练 (QAT)

3. **系统集成**
   - 集成DRAM控制器模型
   - 添加性能计数器
   - 支持多SAU系统仿真

4. **验证和测试**
   - 编写更多的单元测试
   - 添加golden reference验证
   - 性能基准测试

## 总结

SAU已成功集成到gem5平台中，包含：
- ✅ 完整的指令级仿真器实现 (C++)
- ✅ 参数化的Python SimObject接口
- ✅ 功能完整的配置和测试脚本
- ✅ 详细的文档和使用指南

系统可用于：
- 硬件架构研究和评估
- 矩阵计算性能分析
- AI加速器仿真
- 系统级性能建模
