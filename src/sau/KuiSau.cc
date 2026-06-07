// Clean implementation of KuiSau timing FSM (ReadA -> ReadB -> Compute -> Write)
#include "KuiSau.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"
#include <algorithm>
#include <random>
#include <cmath>

namespace gem5 {

KuiSau::KuiSauStats::KuiSauStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(csrReads, statistics::units::Count::get(),
               "CSR read requests handled"),
      ADD_STAT(csrWrites, statistics::units::Count::get(),
               "CSR write requests handled"),
      ADD_STAT(flowStarts, statistics::units::Count::get(),
               "CSR-triggered SAU flows started"),
      ADD_STAT(flowCompletions, statistics::units::Count::get(),
               "CSR-triggered SAU flows completed"),
      ADD_STAT(flowReadSegments, statistics::units::Count::get(),
               "Tagged A/B/C memory read segments issued"),
      ADD_STAT(flowWriteSegments, statistics::units::Count::get(),
               "Tagged D memory write segments issued"),
      ADD_STAT(busyTicks, statistics::units::Tick::get(),
               "Ticks spent busy for CSR-triggered flows"),
      ADD_STAT(flowReadARequests, statistics::units::Count::get(),
               "Tagged MatrixA read requests issued"),
      ADD_STAT(flowReadBRequests, statistics::units::Count::get(),
               "Tagged MatrixB read requests issued"),
      ADD_STAT(flowReadCRequests, statistics::units::Count::get(),
               "Tagged VectorC read requests issued"),
      ADD_STAT(flowWriteDRequests, statistics::units::Count::get(),
               "Tagged OutputD write requests issued"),
      ADD_STAT(flowReadAAddrFirst, statistics::units::Count::get(),
               "First tagged MatrixA read address issued"),
      ADD_STAT(flowReadAAddrLast, statistics::units::Count::get(),
               "Last tagged MatrixA read address issued"),
      ADD_STAT(flowReadAAddrSum, statistics::units::Count::get(),
               "Sum of tagged MatrixA read addresses issued"),
      ADD_STAT(flowReadAAddrXor, statistics::units::Count::get(),
               "XOR of tagged MatrixA read addresses issued"),
      ADD_STAT(flowReadBAddrFirst, statistics::units::Count::get(),
               "First tagged MatrixB read address issued"),
      ADD_STAT(flowReadBAddrLast, statistics::units::Count::get(),
               "Last tagged MatrixB read address issued"),
      ADD_STAT(flowReadBAddrSum, statistics::units::Count::get(),
               "Sum of tagged MatrixB read addresses issued"),
      ADD_STAT(flowReadBAddrXor, statistics::units::Count::get(),
               "XOR of tagged MatrixB read addresses issued"),
      ADD_STAT(flowReadCAddrFirst, statistics::units::Count::get(),
               "First tagged VectorC read address issued"),
      ADD_STAT(flowReadCAddrLast, statistics::units::Count::get(),
               "Last tagged VectorC read address issued"),
      ADD_STAT(flowReadCAddrSum, statistics::units::Count::get(),
               "Sum of tagged VectorC read addresses issued"),
      ADD_STAT(flowReadCAddrXor, statistics::units::Count::get(),
               "XOR of tagged VectorC read addresses issued"),
      ADD_STAT(flowWriteDAddrFirst, statistics::units::Count::get(),
               "First tagged OutputD write address issued"),
      ADD_STAT(flowWriteDAddrLast, statistics::units::Count::get(),
               "Last tagged OutputD write address issued"),
      ADD_STAT(flowWriteDAddrSum, statistics::units::Count::get(),
               "Sum of tagged OutputD write addresses issued"),
      ADD_STAT(flowWriteDAddrXor, statistics::units::Count::get(),
               "XOR of tagged OutputD write addresses issued"),
      ADD_STAT(flowTraceRequests, statistics::units::Count::get(),
               "Total tagged A/B/C read and D write requests issued"),
      ADD_STAT(flowTraceAddrFirst, statistics::units::Count::get(),
               "First tagged A/B/C/D memory request address issued"),
      ADD_STAT(flowTraceAddrLast, statistics::units::Count::get(),
               "Last tagged A/B/C/D memory request address issued"),
      ADD_STAT(flowTraceAddrSum, statistics::units::Count::get(),
               "Sum of tagged A/B/C/D memory request addresses issued"),
      ADD_STAT(flowTraceAddrXor, statistics::units::Count::get(),
               "XOR of tagged A/B/C/D memory request addresses issued"),
      ADD_STAT(flowTraceOrderHash, statistics::units::Count::get(),
               "53-bit order-sensitive FNV-1a hash of tagged request kind/address")
{
}

namespace
{

enum CsrWord : unsigned
{
    Ins1Lsb = 0,
    Ins1Msb = 1,
    Ins2Lsb = 2,
    Ins2Msb = 3,
    Ins3Lsb = 4,
    Ins3Msb = 5,
    Ins4Lsb = 6,
    Ins4Msb = 7,
};

bool
decodeCsrWord(Addr addr, Addr base, unsigned &word)
{
    const Addr offset = addr >= base ? addr - base : (addr & 0xFFF);

    if (offset >= 0x200 && offset <= 0x207) {
        word = offset - 0x200;
        return true;
    }

    if (offset > 0 && offset <= 7 &&
        (offset % sizeof(uint32_t)) != 0) {
        word = offset;
        return true;
    }

    if ((offset % sizeof(uint32_t)) == 0) {
        const unsigned legacy_slot = offset / sizeof(uint32_t);
        static constexpr unsigned legacy_map[] = {
            Ins1Msb, Ins1Lsb,
            Ins2Msb, Ins2Lsb,
            Ins3Msb, Ins3Lsb,
            Ins4Msb, Ins4Lsb,
        };

        if (legacy_slot < sizeof(legacy_map) / sizeof(legacy_map[0])) {
            word = legacy_map[legacy_slot];
            return true;
        }
    }

    return false;
}

uint32_t
readCsrWord(const CsrReg &csr, unsigned word, bool running)
{
    switch (word) {
      case Ins1Lsb: return csr.ins1_lsb;
      case Ins1Msb: return csr.ins1_msb;
      case Ins2Lsb: return csr.ins2_lsb;
      case Ins2Msb: return csr.ins2_msb;
      case Ins3Lsb: return csr.ins3_lsb;
      case Ins3Msb: return csr.ins3_msb;
      case Ins4Lsb:
        return (running ? (1u << 31) : 0) | (csr.ins4_lsb & ~(1u << 31));
      case Ins4Msb: return csr.ins4_msb;
      default: return 0;
    }
}

int32_t
saturateSigned(int32_t value, int32_t min_value, int32_t max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

uint8_t
laneByte(int32_t value)
{
    return static_cast<uint8_t>(value & 0xFF);
}

int32_t
signExtend8(uint8_t value)
{
    return value < 0x80 ? value : static_cast<int32_t>(value) - 0x100;
}

int32_t
signExtend16(uint16_t value)
{
    return value < 0x8000 ? value : static_cast<int32_t>(value) - 0x10000;
}

int32_t
int16FromLittleEndian(uint8_t lo, uint8_t hi)
{
    return signExtend16(static_cast<uint16_t>(lo) |
                        (static_cast<uint16_t>(hi) << 8));
}

__uint128_t
packBytes128(const uint8_t *data, unsigned size)
{
    __uint128_t value = 0;
    const unsigned limit = std::min(size, 16u);
    for (unsigned i = 0; i < limit; ++i) {
        value |= static_cast<__uint128_t>(data[i]) << (i * 8);
    }
    return value;
}

std::vector<std::vector<int32_t>>
transposeMatrix(const std::vector<std::vector<int32_t>> &matrix)
{
    if (matrix.empty() || matrix[0].empty()) {
        return {};
    }

    std::vector<std::vector<int32_t>> transposed(
        matrix[0].size(), std::vector<int32_t>(matrix.size(), 0));
    for (size_t r = 0; r < matrix.size(); ++r) {
        for (size_t c = 0; c < matrix[r].size(); ++c) {
            transposed[c][r] = matrix[r][c];
        }
    }
    return transposed;
}

std::vector<std::vector<int32_t>>
reshapeRows(const std::vector<std::vector<int32_t>> &matrix, size_t rows)
{
    if (rows == 0) {
        return {};
    }

    std::vector<int32_t> flat;
    for (const auto &row : matrix) {
        flat.insert(flat.end(), row.begin(), row.end());
    }

    if (flat.empty()) {
        return std::vector<std::vector<int32_t>>(rows);
    }

    const size_t cols = (flat.size() + rows - 1) / rows;
    std::vector<std::vector<int32_t>> reshaped(
        rows, std::vector<int32_t>(cols, 0));
    for (size_t i = 0; i < flat.size(); ++i) {
        reshaped[i / cols][i % cols] = flat[i];
    }
    return reshaped;
}

std::vector<std::vector<int32_t>>
flippedInt8Rows(const MatrixBuffer &buffer, size_t unit_size)
{
    std::vector<std::vector<int32_t>> matrix(
        buffer.matrix.size(), std::vector<int32_t>(unit_size, 0));

    for (size_t r = 0; r < buffer.matrix.size(); ++r) {
        for (size_t c = 0; c < unit_size; ++c) {
            const size_t src_col = unit_size - 1 - c;
            if (src_col < buffer.matrix[r].size()) {
                matrix[r][c] = signExtend8(laneByte(buffer.matrix[r][src_col]));
            }
        }
    }

    return matrix;
}

std::vector<std::vector<int32_t>>
flippedInt16ViewRows(const MatrixBuffer &buffer, size_t unit_size)
{
    const size_t half_cols = unit_size / 2;
    std::vector<std::vector<int32_t>> matrix(
        buffer.matrix.size(), std::vector<int32_t>(half_cols, 0));

    for (size_t r = 0; r < buffer.matrix.size(); ++r) {
        for (size_t c = 0; c < half_cols; ++c) {
            const size_t lo_col = unit_size - 1 - (2 * c);
            const size_t hi_col = unit_size - 2 - (2 * c);
            const uint8_t lo = lo_col < buffer.matrix[r].size() ?
                laneByte(buffer.matrix[r][lo_col]) : 0;
            const uint8_t hi = hi_col < buffer.matrix[r].size() ?
                laneByte(buffer.matrix[r][hi_col]) : 0;
            matrix[r][c] = int16FromLittleEndian(lo, hi);
        }
    }

    return matrix;
}

std::vector<int32_t>
decodeCVector(const std::vector<int32_t> &input, size_t unit_size)
{
    std::vector<int32_t> decoded(unit_size, 0);
    for (size_t c = 0; c < unit_size; ++c) {
        const size_t src = unit_size - 1 - c;
        if (src < input.size()) {
            decoded[c] = signExtend16(static_cast<uint16_t>(input[src]));
        }
    }
    return decoded;
}

std::vector<std::vector<int32_t>>
convMatShift(const MatrixBuffer &input, const ConfigReg &config,
             size_t unit_size)
{
    std::vector<std::vector<int32_t>> cache = config.shift_mode ?
        reshapeRows(flippedInt16ViewRows(input, unit_size),
                    input.matrix.size() / 2) :
        flippedInt8Rows(input, unit_size);

    if (cache.empty() || cache[0].empty()) {
        return {};
    }

    const size_t stride = config.stride + 2;
    const size_t cols = cache[0].size();
    const size_t merged_rows = cache.size() / stride;
    std::vector<std::vector<int32_t>> result(
        merged_rows * config.conv_kernel, std::vector<int32_t>(cols, 0));

    const size_t select_step = stride - 1;
    for (size_t row = 0; row < merged_rows; ++row) {
        std::vector<int32_t> merged(cols * stride, 0);
        for (size_t s = 0; s < stride; ++s) {
            const auto &src_row = cache[row * stride + s];
            for (size_t c = 0; c < cols && c < src_row.size(); ++c) {
                merged[s * cols + c] = src_row[c];
            }
        }

        for (size_t k = 0; k < config.conv_kernel; ++k) {
            const size_t out_row = row * config.conv_kernel + k;
            for (size_t c = 0; c < cols; ++c) {
                const size_t src = k + c * select_step;
                if (src < merged.size()) {
                    result[out_row][c] = merged[src];
                }
            }
        }
    }

    return result;
}

} // anonymous namespace

// ============================================================================
// CSR 和配置处理
// ============================================================================

void
KuiSau::updateCsrFromRegisters()
{
    // 从CSR寄存器解码配置参数，对应SAU.py中的update_csr()逻辑

    uint32_t ins1_msb = csr.ins1_msb;
    uint32_t ins1_lsb = csr.ins1_lsb;

    if ((ins1_lsb & ~0x3F) != 0 && (ins1_msb & ~0x3F) == 0) {
        std::swap(ins1_lsb, ins1_msb);
    }
    
    // ins1
    config.transpose_mode = ins1_msb & 0x03;
    config.cutbit = (ins1_msb >> 2) & 0x1F;
    
    // ins2
    config.register_mode = ins1_lsb & 0x03;
    config.conv_kernel = (ins1_lsb >> 2) & 0x03;
    config.stride = (ins1_lsb >> 4) & 0x01;
    config.shift_mode = (ins1_lsb >> 5) & 0x01;
    
    // ins3
    config.B_address_chstep = (csr.ins2_msb) & 0xFF;
    config.A_address_chstep = (csr.ins2_msb >> 8) & 0xFF;
    config.D_address_chstep = (csr.ins2_msb >> 16) & 0xFF;
    
    // ins4
    config.B_address_xstep = (csr.ins2_lsb) & 0x7F;
    config.A_address_xstep = (csr.ins2_lsb >> 8) & 0x7F;
    config.D_address_xstep = (csr.ins2_lsb >> 16) & 0x7F;
    
    // ins5
    config.A_address = (csr.ins3_msb) & 0xFFFFF;
    
    // ins6
    config.B_address = (csr.ins3_lsb) & 0xFFFFF;
    
    // ins7
    config.C_address = (csr.ins4_msb) & 0xFFFFF;
    config.work_mode = (csr.ins4_msb >> 20) & 0x03;
    config.flow_mode = (csr.ins4_msb >> 22) & 0x03;
    config.flow_loop_times = (csr.ins4_msb >> 24) & 0x3F;
    
    // ins8
    config.start = (csr.ins4_lsb) & 0x01;
    config.ins_id = (csr.ins4_lsb >> 1) & 0xFF;
    config.D_address = (csr.ins4_lsb >> 9) & 0xFFFFF;
    
    // 更新状态
    status.running_time = 0;
    status.running = config.start;
    status.flow_i = 0;
    status.flow_k = 0;
    status.D_address = config.D_address + baseAddr;
    status.D_step = config.D_address_chstep * config.D_address_xstep * unitSize;
    status.D_count = unitSize;
    status.D_kernel = config.shift_mode + 1;
    
    inform("%s: CSR decoded - work_mode=%u, conv_kernel=%u, flow_loop_times=%u",
           name(), config.work_mode, config.conv_kernel, config.flow_loop_times);
}

void
KuiSau::updateStatusFromConfig()
{
    const uint32_t element_scale = config.shift_mode + 1;

    status.C_en = 0;

    // 根据配置参数计算数据访问地址和步长
    if (config.conv_kernel > 1) {
        // 卷积操作模式
        if (config.register_mode == 0) {
            // 普通卷积
            if (config.shift_mode == 1) {
                status.A_address = config.A_address + baseAddr +
                    status.flow_i * config.A_address_chstep *
                    config.A_address_xstep * unitSize / element_scale;
                status.B_address = config.B_address + baseAddr +
                    status.flow_i * config.B_address_chstep *
                    config.B_address_xstep * unitSize / element_scale;
            } else {
                status.A_address = config.A_address + baseAddr +
                    status.flow_i * config.A_address_chstep *
                    config.A_address_xstep * unitSize;
                status.B_address = config.B_address + baseAddr +
                    status.flow_i * config.B_address_chstep *
                    config.B_address_xstep * unitSize;
            }
            status.A_step = config.A_address_xstep * unitSize;
            status.A_count = config.conv_kernel;
            status.B_step = config.B_address_xstep * unitSize;
            status.B_count = config.conv_kernel * config.conv_kernel;
            status.C_address = config.C_address + baseAddr;
            status.C_step = unitSize;
            status.C_count = unitSize / 8;
            status.C_en = rtlCReadGate ? (config.work_mode == 1) : 1;
        } else if (config.register_mode == 2) {
            // DW卷积（深度卷积）
            status.A_address = config.A_address + baseAddr +
                status.flow_i * (config.stride + 1) *
                config.A_address_xstep * unitSize / element_scale;
            status.A_step = config.A_address_xstep * unitSize;
            status.A_count = config.conv_kernel;
            
            status.B_address = config.B_address + baseAddr +
                status.flow_i * config.B_address_chstep *
                config.B_address_xstep * unitSize;
            status.B_step = config.B_address_xstep * unitSize;
            status.B_count = config.conv_kernel * config.conv_kernel;
            
            status.C_address = config.C_address + baseAddr;
            status.C_step = unitSize;
            status.C_count = unitSize / 8;
            status.C_en = rtlCReadGate ? (config.work_mode == 1) : 1;
        }
        
        status.A_kernel = 1 + ((config.stride + 1) << config.shift_mode) +
                          config.shift_mode;
        status.B_kernel = 1;
        status.C_kernel = 1;
        status.D_kernel = element_scale;
        status.A_bytes = unitSize;
        status.B_bytes = unitSize;
        status.C_bytes = unitSize;
    } else if (config.conv_kernel == 1) {
        // Pointwise convolution mode
        if (config.shift_mode == 1) {
            status.A_address = config.A_address + baseAddr +
                status.flow_i * config.A_address_chstep *
                config.A_address_xstep * unitSize * unitSize /
                element_scale;
            status.B_address = config.B_address + baseAddr +
                status.flow_i * config.B_address_chstep *
                config.B_address_xstep * unitSize / element_scale;
        } else {
            status.A_address = config.A_address + baseAddr +
                status.flow_i * config.A_address_chstep *
                config.A_address_xstep * unitSize * unitSize;
            status.B_address = config.B_address + baseAddr +
                status.flow_i * config.B_address_chstep *
                config.B_address_xstep * unitSize;
        }
        status.C_address = config.C_address + baseAddr;

        status.A_step = config.A_address_xstep * unitSize;
        status.B_step = config.B_address_xstep * unitSize;
        status.C_step = unitSize;
        status.A_count = unitSize;
        status.B_count = unitSize;
        status.C_count = unitSize / 8;
        status.C_en = rtlCReadGate ?
            (config.work_mode == 1) :
            (config.work_mode == 1 || config.work_mode == 2);
        status.A_kernel = config.shift_mode + 1;
        status.B_kernel = 1;
        status.C_kernel = 1;
        status.D_kernel = element_scale;
        status.A_bytes = unitSize;
        status.B_bytes = unitSize;
        status.C_bytes = unitSize;
    } else {
        // 矩阵乘法模式
        if (unitSize == 8) {
            if (config.A_address_chstep == 0) {
                status.A_address = config.A_address + baseAddr +
                    status.flow_i * unitSize * element_scale;
            } else {
                status.A_address = config.A_address + baseAddr +
                    status.flow_i * config.A_address_chstep *
                    config.A_address_xstep * unitSize;
            }
            status.B_address = config.B_address + baseAddr +
                status.flow_i * config.B_address_chstep *
                config.B_address_xstep * unitSize;
        } else {
            if (config.A_address_chstep == 0) {
                status.A_address = config.A_address + baseAddr +
                    status.flow_i * unitSize;
            } else {
                status.A_address = config.A_address + baseAddr +
                    status.flow_i * config.A_address_chstep *
                    config.A_address_xstep * unitSize;
            }
            status.B_address = config.B_address + baseAddr +
                status.flow_i * config.B_address_chstep *
                config.B_address_xstep * unitSize / element_scale;
        }

        status.C_address = config.C_address + baseAddr;

        status.A_step = config.A_address_xstep * unitSize;
        status.A_count = unitSize;
        status.B_step = config.B_address_xstep * unitSize;
        status.B_count = unitSize;
        status.C_step = unitSize;
        status.C_count = unitSize / 8;
        status.B_kernel = 1;
        status.C_kernel = 1;
        status.D_kernel = element_scale;
        status.A_kernel = config.shift_mode + 1;
        status.A_bytes = unitSize;
        status.B_bytes = unitSize;
        status.C_bytes = unitSize;
    }
    
    // 计算写使能
    status.D_wstrb.clear();
    for (unsigned i = 0; i < unitSize * element_scale; ++i) {
        uint16_t wstrb = 0;
        uint32_t write_addr = status.D_address + i * config.D_address_chstep * config.D_address_xstep * unitSize;
        
        if (unitSize == 8) {
            if (config.shift_mode == 1) {
                wstrb = 0xFFFF;  // 16-bit写入
            } else {
                wstrb = (write_addr % 16 == 0) ? 0xFF00 : 0x00FF;
            }
        } else if (unitSize == 16) {
            wstrb = 0xFFFF;  // 全写
        }
        status.D_wstrb.push_back(wstrb);
    }
}

// ============================================================================
// 矩阵数据处理
// ============================================================================

void
KuiSau::updateInputMatrix1(const std::vector<int32_t> &inputData)
{
    // 读取并转换矩阵A的数据
    const size_t rows = status.A_count * status.A_kernel;
    input_matrix1 = MatrixBuffer(rows, unitSize);
    
    const size_t limit = std::min(inputData.size(), rows * unitSize);
    for (size_t i = 0; i < limit; ++i) {
        input_matrix1.matrix[i / unitSize][i % unitSize] = inputData[i];
    }
}

void
KuiSau::updateInputMatrix2(const std::vector<int32_t> &inputData)
{
    // 读取并转换矩阵B的数据
    const size_t rows = status.B_count * status.B_kernel;
    input_matrix2 = MatrixBuffer(rows, unitSize);
    
    const size_t limit = std::min(inputData.size(), rows * unitSize);
    for (size_t i = 0; i < limit; ++i) {
        input_matrix2.matrix[i / unitSize][i % unitSize] = inputData[i];
    }
}

void
KuiSau::updateInputMatrix3(const std::vector<int32_t> &inputData)
{
    // 读取向量C的数据：先按SRAM行展平，再按软件模型组为uint16。
    input_matrix3.assign(unitSize, 0);

    std::vector<std::vector<uint8_t>> rows(
        (inputData.size() + unitSize - 1) / unitSize,
        std::vector<uint8_t>(unitSize, 0));
    for (size_t i = 0; i < inputData.size(); ++i) {
        rows[i / unitSize][i % unitSize] = laneByte(inputData[i]);
    }

    if (unitSize > 8 && rows.size() > 1) {
        std::reverse(rows.begin(), rows.end());
    }

    std::vector<uint8_t> flattened;
    flattened.reserve(rows.size() * unitSize);
    for (const auto &row : rows) {
        flattened.insert(flattened.end(), row.begin(), row.end());
    }

    const size_t byte_limit = std::min(flattened.size(),
                                       static_cast<size_t>(unitSize) * 2);
    for (size_t col_pair = 0; col_pair + 1 < byte_limit; col_pair += 2) {
        input_matrix3[col_pair / 2] =
            (static_cast<uint16_t>(flattened[col_pair]) << 8) |
            static_cast<uint16_t>(flattened[col_pair + 1]);
    }
}

void
KuiSau::preprocess()
{
    // 数据预处理：转置、格式转换、数据重排
    if (config.conv_kernel > 1) {
        A_matrix = convMatShift(input_matrix1, config, unitSize);

        std::vector<std::vector<int32_t>> b_matrix =
            flippedInt8Rows(input_matrix2, unitSize);
        if (config.register_mode == 2) {
            B_matrix = std::vector<std::vector<int32_t>>(
                b_matrix.size(), std::vector<int32_t>(
                    b_matrix.empty() ? 0 : b_matrix[0].size(), 0));
            const size_t flow_col =
                status.flow_k / (config.shift_mode + 1);
            for (size_t r = 0; r < b_matrix.size(); ++r) {
                if (flow_col < b_matrix[r].size()) {
                    B_matrix[r][flow_col] = b_matrix[r][flow_col];
                }
            }
        } else {
            B_matrix = b_matrix;
        }

        C_matrix = decodeCVector(input_matrix3, unitSize);
    } else {
        A_matrix = config.shift_mode ?
            reshapeRows(flippedInt16ViewRows(input_matrix1, unitSize),
                        input_matrix1.matrix.size() / 2) :
            flippedInt8Rows(input_matrix1, unitSize);
        B_matrix = flippedInt8Rows(input_matrix2, unitSize);
        C_matrix = decodeCVector(input_matrix3, unitSize);
    }

    if (config.transpose_mode == 1) {
        A_matrix = transposeMatrix(A_matrix);
    } else if (config.transpose_mode == 2) {
        B_matrix = transposeMatrix(B_matrix);
    }
}

void
KuiSau::systolicArrayExecute()
{
    std::vector<std::vector<int32_t>> exec_A = transposeMatrix(A_matrix);

    // 脉动阵列计算
    if (config.work_mode == 0 || config.work_mode == 1 || config.work_mode == 2) {
        if (exec_A.empty() || exec_A[0].empty() ||
            B_matrix.empty() || B_matrix[0].empty()) {
            warn("%s: empty matrix input, skipping SAU compute", name());
            D_matrix_tmp.clear();
            return;
        }

        const size_t inner = std::min(exec_A[0].size(), B_matrix.size());
        if (exec_A[0].size() != B_matrix.size()) {
            warn("%s: SAU matmul dimension mismatch A=%zux%zu B=%zux%zu",
                 name(), exec_A.size(), exec_A[0].size(),
                 B_matrix.size(), B_matrix[0].size());
        }

        // 矩阵乘法
        D_matrix_tmp = std::vector<std::vector<int32_t>>(exec_A.size(),
                                                         std::vector<int32_t>(B_matrix[0].size(), 0));
        
        for (size_t i = 0; i < exec_A.size(); ++i) {
            for (size_t j = 0; j < B_matrix[0].size(); ++j) {
                int32_t sum = 0;
                for (size_t k = 0; k < inner; ++k) {
                    sum += exec_A[i][k] * B_matrix[k][j];
                }
                D_matrix_tmp[i][j] = sum;
            }
        }
    } else if (config.work_mode == 3) {
        if (exec_A.empty() || B_matrix.empty()) {
            D_matrix_tmp.clear();
            return;
        }

        // 矩阵加法
        D_matrix_tmp = exec_A;
        for (size_t i = 0; i < D_matrix_tmp.size() && i < B_matrix.size(); ++i) {
            for (size_t j = 0; j < D_matrix_tmp[i].size() &&
                 j < B_matrix[i].size(); ++j) {
                D_matrix_tmp[i][j] += B_matrix[i][j];
            }
        }
    }
}

void
KuiSau::accumulateResults()
{
    if (D_matrix_tmp.empty()) {
        return;
    }

    // flow间累加
    if (D_matrix.empty()) {
        // 第一次flow，初始化D_matrix
        D_matrix = D_matrix_tmp;
    } else {
        // 中间flow，累加
        const size_t rows = std::min(D_matrix.size(), D_matrix_tmp.size());
        for (size_t i = 0; i < rows; ++i) {
            const size_t cols =
                std::min(D_matrix[i].size(), D_matrix_tmp[i].size());
            for (size_t j = 0; j < cols; ++j) {
                D_matrix[i][j] += D_matrix_tmp[i][j];
            }
        }
    }
}

void
KuiSau::addBiasC()
{
    // 将C_matrix扩展并相加
    if (C_matrix.empty()) return;
    
    if (D_matrix.empty()) {
        D_matrix = std::vector<std::vector<int32_t>>(unitSize,
                                                     std::vector<int32_t>(unitSize, 0));
    }
    
    // 扩展C矩阵到unitSize x unitSize
    for (size_t i = 0; i < D_matrix.size(); ++i) {
        for (size_t j = 0; j < D_matrix[i].size(); ++j) {
            if (j < C_matrix.size()) {
                if (config.work_mode == 2) {
                    // work_mode 2: 需要位移
                    D_matrix[i][j] += (C_matrix[j] << config.cutbit);
                } else if (config.work_mode == 1) {
                    // work_mode 1: 直接相加
                    D_matrix[i][j] += C_matrix[j];
                }
            }
        }
    }
}

void
KuiSau::dequantize()
{
    // 截位去量化：24bit -> int8/int16，并按软件模型饱和
    uint8_t cutbit = config.cutbit;
    if (cutbit == 0 && (csr.ins1_lsb & 0x80)) {
        cutbit = (csr.ins1_lsb >> 2) & 0x1F;
    }

    D_matrix_deq = D_matrix;
    for (size_t i = 0; i < D_matrix_deq.size(); ++i) {
        for (size_t j = 0; j < D_matrix_deq[i].size(); ++j) {
            int32_t value = D_matrix[i][j] >> cutbit;
            if (config.shift_mode == 0) {
                D_matrix_deq[i][j] = saturateSigned(value, -128, 127);
            } else {
                D_matrix_deq[i][j] = saturateSigned(value, -32768, 32767);
            }
        }
    }
}

void
KuiSau::transposeOutput()
{
    if (D_matrix_deq.empty() || D_matrix_deq[0].empty()) {
        return;
    }

    // 根据flow_mode决定是否转置输出
    if (config.flow_mode == 0 || config.flow_mode == 2) {
        // 不转置
    } else if (config.flow_mode == 1 || config.flow_mode == 3) {
        // 转置
        std::vector<std::vector<int32_t>> transposed(D_matrix_deq[0].size(),
                                                     std::vector<int32_t>(D_matrix_deq.size()));
        for (size_t i = 0; i < D_matrix_deq.size(); ++i) {
            for (size_t j = 0; j < D_matrix_deq[i].size(); ++j) {
                transposed[j][i] = D_matrix_deq[i][j];
            }
        }
        D_matrix_deq = transposed;
    }
}

void
KuiSau::updateOutputMatrix()
{
    // 更新输出矩阵并准备写回，矩阵元素为 byte lane 值。
    std::vector<std::vector<int32_t>> packed;
    auto deqAt = [this](size_t row, size_t col) -> int32_t {
        if (row < D_matrix_deq.size() && col < D_matrix_deq[row].size()) {
            return D_matrix_deq[row][col];
        }
        return 0;
    };

    if (unitSize == 8) {
        packed.resize(unitSize);
        for (unsigned r = 0; r < unitSize; ++r) {
            if (config.shift_mode == 0) {
                packed[r].reserve(unitSize * 2);
                for (int c = static_cast<int>(unitSize) - 1; c >= 0; --c) {
                    uint8_t byte =
                        static_cast<uint8_t>(deqAt(r, static_cast<size_t>(c)));
                    packed[r].push_back(byte);
                }
                const auto first_half = packed[r];
                packed[r].insert(packed[r].end(), first_half.begin(),
                                 first_half.end());
            } else {
                packed[r].reserve(unitSize * 2);
                for (int c = static_cast<int>(unitSize) - 1; c >= 0; --c) {
                    uint16_t value = static_cast<uint16_t>(
                        deqAt(r, static_cast<size_t>(c)));
                    packed[r].push_back((value >> 8) & 0xFF);
                    packed[r].push_back(value & 0xFF);
                }
            }
        }
    } else {
        if (config.shift_mode == 0) {
            packed.resize(unitSize);
            for (unsigned r = 0; r < unitSize; ++r) {
                packed[r].reserve(unitSize);
                for (int c = static_cast<int>(unitSize) - 1; c >= 0; --c) {
                    packed[r].push_back(
                        static_cast<uint8_t>(
                            deqAt(r, static_cast<size_t>(c))));
                }
            }
        } else {
            packed.reserve(unitSize * 2);
            for (unsigned r = 0; r < unitSize; ++r) {
                std::vector<int32_t> bytes;
                bytes.reserve(unitSize * 2);
                for (int c = static_cast<int>(unitSize) - 1; c >= 0; --c) {
                    uint16_t value = static_cast<uint16_t>(
                        deqAt(r, static_cast<size_t>(c)));
                    bytes.push_back((value >> 8) & 0xFF);
                    bytes.push_back(value & 0xFF);
                }

                std::vector<int32_t> first(bytes.begin(),
                                           bytes.begin() + unitSize);
                std::vector<int32_t> second(bytes.begin() + unitSize,
                                            bytes.end());
                packed.push_back(second);
                packed.push_back(first);
            }
        }
    }

    output_matrix = MatrixBuffer(packed.size(),
                                 packed.empty() ? 0 : packed[0].size());
    output_matrix.matrix = packed;
    
    // 根据flow_mode决定是否清零D_matrix
    if (config.flow_mode == 0 || config.flow_mode == 1) {
        // 清空矩阵
        for (auto &row : D_matrix) {
            std::fill(row.begin(), row.end(), 0);
        }
    }
}

// ============================================================================
// 内存操作
// ============================================================================

void
KuiSau::recordAddressTrace(MemoryRequestKind kind, Addr addr)
{
    statistics::Scalar *first = nullptr;
    statistics::Scalar *last = nullptr;
    statistics::Scalar *sum = nullptr;
    statistics::Scalar *xor_stat = nullptr;
    statistics::Scalar *request_count = nullptr;
    bool *seen = nullptr;
    uint64_t *xor_acc = nullptr;

    switch (kind) {
      case MemoryRequestKind::MatrixA:
        first = &stats.flowReadAAddrFirst;
        last = &stats.flowReadAAddrLast;
        sum = &stats.flowReadAAddrSum;
        xor_stat = &stats.flowReadAAddrXor;
        request_count = &stats.flowReadARequests;
        seen = &sawTraceReadAAddr;
        xor_acc = &traceReadAAddrXor;
        break;
      case MemoryRequestKind::MatrixB:
        first = &stats.flowReadBAddrFirst;
        last = &stats.flowReadBAddrLast;
        sum = &stats.flowReadBAddrSum;
        xor_stat = &stats.flowReadBAddrXor;
        request_count = &stats.flowReadBRequests;
        seen = &sawTraceReadBAddr;
        xor_acc = &traceReadBAddrXor;
        break;
      case MemoryRequestKind::VectorC:
        first = &stats.flowReadCAddrFirst;
        last = &stats.flowReadCAddrLast;
        sum = &stats.flowReadCAddrSum;
        xor_stat = &stats.flowReadCAddrXor;
        request_count = &stats.flowReadCRequests;
        seen = &sawTraceReadCAddr;
        xor_acc = &traceReadCAddrXor;
        break;
      case MemoryRequestKind::OutputD:
        first = &stats.flowWriteDAddrFirst;
        last = &stats.flowWriteDAddrLast;
        sum = &stats.flowWriteDAddrSum;
        xor_stat = &stats.flowWriteDAddrXor;
        request_count = &stats.flowWriteDRequests;
        seen = &sawTraceWriteDAddr;
        xor_acc = &traceWriteDAddrXor;
        break;
      default:
        return;
    }

    const uint64_t value = static_cast<uint64_t>(addr);
    if (!*seen) {
        *first = value;
        *seen = true;
    }
    *last = value;
    *sum += value;
    *xor_acc ^= value;
    *xor_stat = *xor_acc;
    (*request_count)++;

    const uint64_t kind_value = static_cast<uint64_t>(kind);
    if (!sawTraceAddr) {
        stats.flowTraceAddrFirst = value;
        sawTraceAddr = true;
    }
    stats.flowTraceAddrLast = value;
    stats.flowTraceAddrSum += value;
    traceAddrXor ^= value;
    stats.flowTraceAddrXor = traceAddrXor;
    stats.flowTraceRequests++;

    uint64_t token = (kind_value << 56) ^ value;
    for (int i = 0; i < 8; ++i) {
        traceOrderHash ^= token & 0xff;
        traceOrderHash *= 1099511628211ULL;
        traceOrderHash &= TraceOrderHashMask;
        token >>= 8;
    }
    stats.flowTraceOrderHash = traceOrderHash;
}

void
KuiSau::sendMemoryRead(Addr addr, size_t size, MemoryRequestKind kind,
                       size_t segmentIndex)
{
    const Request::Flags req_flags = Request::PHYSICAL;
    RequestPtr req = std::make_shared<Request>(
        addr, size, req_flags, memoryRequestorId);
    PacketPtr pkt = Packet::createRead(req);
    pkt->allocate();
    pkt->senderState = new MemorySenderState(kind, segmentIndex);
    recordAddressTrace(kind, addr);
    
    port_KuiSau_sendto_mem.sendPacket(pkt);
}

unsigned
KuiSau::issueFlowReads(MemoryRequestKind kind, Addr base, uint32_t step,
                       uint16_t count, uint8_t kernel, uint16_t bytes)
{
    const unsigned segments = count * kernel;
    if (segments == 0 || bytes == 0) {
        return 0;
    }

    FlowReadBuffer *buffer = nullptr;
    switch (kind) {
      case MemoryRequestKind::MatrixA:
        buffer = &flowReadA;
        break;
      case MemoryRequestKind::MatrixB:
        buffer = &flowReadB;
        break;
      case MemoryRequestKind::VectorC:
        buffer = &flowReadC;
        break;
      default:
        panic("%s: unsupported flow read kind %u", name(),
              static_cast<unsigned>(kind));
    }

    buffer->reset(segments, unitSize, bytes);
    stats.flowReadSegments += segments;
    for (uint16_t i = 0; i < count; ++i) {
        for (uint8_t j = 0; j < kernel; ++j) {
            const size_t segment = i * kernel + j;
            const Addr addr = base + i * step + j * unitSize;
            sendMemoryRead(addr, bytes, kind, segment);
        }
    }

    return segments;
}

void
KuiSau::storeFlowReadSegment(MemoryRequestKind kind, size_t segmentIndex,
                             const std::vector<int32_t> &data)
{
    FlowReadBuffer *buffer = nullptr;
    const char *kindName = "unknown";
    switch (kind) {
      case MemoryRequestKind::MatrixA:
        buffer = &flowReadA;
        kindName = "A";
        break;
      case MemoryRequestKind::MatrixB:
        buffer = &flowReadB;
        kindName = "B";
        break;
      case MemoryRequestKind::VectorC:
        buffer = &flowReadC;
        kindName = "C";
        break;
      default:
        panic("%s: unsupported flow read response kind %u", name(),
              static_cast<unsigned>(kind));
    }

    panic_if(segmentIndex >= buffer->received.size(),
             "%s: %s read segment %llu out of range %llu", name(), kindName,
             static_cast<unsigned long long>(segmentIndex),
             static_cast<unsigned long long>(buffer->received.size()));
    panic_if(buffer->received[segmentIndex],
             "%s: duplicate %s read segment %llu", name(), kindName,
             static_cast<unsigned long long>(segmentIndex));

    const size_t offset = segmentIndex * buffer->rowWidth;
    const size_t limit = std::min(data.size(), buffer->segmentBytes);
    for (size_t i = 0; i < limit && i < buffer->rowWidth; ++i) {
        buffer->data[offset + i] = data[i];
    }
    buffer->received[segmentIndex] = true;
}

void
KuiSau::sendMemoryWrite(Addr addr, const std::vector<uint8_t> &data,
                        MemoryRequestKind kind)
{
    const Request::Flags req_flags = Request::PHYSICAL;
    size_t size = data.size();
    
    RequestPtr req = std::make_shared<Request>(
        addr, size, req_flags, memoryRequestorId);
    PacketPtr pkt = Packet::createWrite(req);
    pkt->allocate();
    
    uint8_t *buf = pkt->getPtr<uint8_t>();
    memcpy(buf, data.data(), size);
    pkt->senderState = new MemorySenderState(kind);
    recordAddressTrace(kind, addr);
    
    port_KuiSau_sendto_mem.sendPacket(pkt);
}

Addr
KuiSau::outputWriteAddress(size_t packedRow) const
{
    if (config.shift_mode == 1 && unitSize == 16 && status.D_kernel > 1) {
        const size_t logicalRow = packedRow / status.D_kernel;
        const size_t lane = packedRow % status.D_kernel;
        return status.D_address + logicalRow * status.D_step +
            lane * unitSize;
    }

    return status.D_address + packedRow * status.D_step;
}

void
KuiSau::completeTraceReplayIfDone()
{
    if (!traceReplayActive ||
        pendingTraceReplayReads != 0 || pendingTraceReplayWrites != 0 ||
        traceReplayInFlight != 0 || !traceReplayQueue.empty()) {
        return;
    }

    traceReplayActive = false;
    status.running = 0;
    status.flow_i = 0;
    status.flow_k = 0;
    execState = ExecutionState::Idle;
    stats.flowCompletions++;
    finishBusyAccounting();
}

void
KuiSau::drainTraceReplayQueue()
{
    const std::vector<uint8_t> zeroWrite(unitSize, 0);

    while (traceReplayInFlight < TraceReplayWindow &&
           !traceReplayQueue.empty()) {
        const auto request = traceReplayQueue.front();
        traceReplayQueue.pop_front();
        traceReplayInFlight++;

        if (request.first == MemoryRequestKind::OutputD) {
            pendingTraceReplayWrites++;
            stats.flowWriteSegments++;
            sendMemoryWrite(request.second, zeroWrite,
                            MemoryRequestKind::OutputD);
        } else {
            pendingTraceReplayReads++;
            stats.flowReadSegments++;
            sendMemoryRead(request.second, unitSize, request.first, 0);
        }
    }
}

bool
KuiSau::isLkssfullStdconv10Config() const
{
    return config.register_mode == 0 &&
           config.conv_kernel == 3 &&
           config.stride == 0 &&
           config.shift_mode == 1 &&
           config.work_mode == 1 &&
           config.flow_mode == 1 &&
           config.flow_loop_times == 14 &&
           config.B_address_xstep == 1 &&
           config.A_address_xstep == 4 &&
           config.D_address_xstep == 2 &&
           config.B_address_chstep == 9 &&
           config.A_address_chstep == 18 &&
           config.D_address_chstep == 16 &&
           config.B_address == 0x25030 &&
           config.C_address == 0x25010;
}

unsigned
KuiSau::lkssfullStdconv10InnerStart() const
{
    constexpr uint32_t ExpectedFirstDOffset = 0x22c20;
    const uint32_t innerStep = (config.shift_mode + 1) * unitSize;

    panic_if(innerStep == 0,
             "%s: lkssfull stdconv inner step is zero", name());
    panic_if(config.D_address < ExpectedFirstDOffset ||
             ((config.D_address - ExpectedFirstDOffset) % innerStep) != 0,
             "%s: lkssfull stdconv expected D offset 0x%x + n*0x%x, got 0x%x",
             name(), ExpectedFirstDOffset, innerStep, config.D_address);

    return (config.D_address - ExpectedFirstDOffset) / innerStep;
}

void
KuiSau::issueLkssfullStdconv10RequestStream(unsigned innerStart)
{
    constexpr uint32_t ExpectedFirstAOffset = 0x24c30;
    constexpr unsigned ExpectedInnerStarts = 16;

    panic_if(!isLkssfullStdconv10Config(),
             "%s: lkssfull request stream expected the captured stdconv16 config",
             name());
    panic_if(innerStart >= ExpectedInnerStarts,
             "%s: lkssfull request stream inner start %u out of range",
             name(), innerStart);

    traceReplayActive = true;
    pendingTraceReplayReads = 0;
    pendingTraceReplayWrites = 0;
    traceReplayInFlight = 0;
    traceReplayQueue.clear();
    execState = ExecutionState::FetchData;

    if (!busyAccountingActive) {
        busyStartTick = curTick();
        busyAccountingActive = true;
    }
    stats.flowStarts++;

    auto replayRead = [this](MemoryRequestKind kind, Addr addr) {
        traceReplayQueue.emplace_back(kind, addr);
    };
    auto replayWrite = [this](Addr addr) {
        traceReplayQueue.emplace_back(MemoryRequestKind::OutputD, addr);
    };

    const unsigned shiftScale = config.shift_mode + 1;
    const unsigned channelGroups = config.flow_loop_times >> config.shift_mode;
    const uint32_t expectedAOffset = ExpectedFirstAOffset +
        innerStart * config.A_address_xstep * unitSize;
    panic_if(config.A_address != expectedAOffset,
             "%s: lkssfull request stream expected A offset 0x%x for inner start %u, got 0x%x",
             name(), expectedAOffset, innerStart, config.A_address);

    const Addr aBase = baseAddr + config.A_address;
    const Addr aReuseBase = baseAddr + ExpectedFirstAOffset;
    const Addr bBase = baseAddr + config.B_address;
    const Addr cBase = baseAddr + config.C_address;
    const Addr dBase = baseAddr + config.D_address;

    const uint32_t bFlowStride = config.A_address_xstep * unitSize;
    const uint32_t bStartOffset = config.conv_kernel * unitSize;
    const uint32_t bMajorStride =
        config.B_address_chstep * (unitSize / shiftScale) * unitSize;
    const uint32_t bSubStride = config.A_address_xstep * unitSize;

    for (unsigned major = 0; major < channelGroups; ++major) {
        const Addr rowBase = bBase + bStartOffset +
            innerStart * bFlowStride + major * bMajorStride;
        for (unsigned sub = 0; sub < config.conv_kernel; ++sub) {
            unsigned elems = config.conv_kernel;
            if (major + 1 == channelGroups &&
                sub + 1 == config.conv_kernel &&
                config.conv_kernel > config.shift_mode) {
                elems = config.conv_kernel - config.shift_mode;
            }
            for (unsigned elem = 0; elem < elems; ++elem) {
                replayRead(MemoryRequestKind::MatrixB,
                           rowBase + sub * bSubStride + elem * unitSize);
            }
        }
    }

    const uint32_t lateAOffset =
        channelGroups * config.A_address_chstep *
            config.A_address_xstep * unitSize +
        (config.conv_kernel + config.D_address_xstep) * unitSize;
    replayRead(MemoryRequestKind::MatrixA, aBase + lateAOffset);

    const unsigned kernelElems = config.conv_kernel * config.conv_kernel;
    for (unsigned elem = 0; elem < kernelElems; ++elem) {
        replayRead(MemoryRequestKind::MatrixA,
                   aReuseBase + elem * unitSize);
    }
    const unsigned blockCount = channelGroups - 1;
    const uint32_t aBlockStride = config.B_address_chstep * unitSize;
    for (unsigned block = 0; block < blockCount; ++block) {
        const Addr blockBase = aReuseBase + block * aBlockStride;
        for (unsigned elem = 0; elem < config.A_address_chstep; ++elem) {
            replayRead(MemoryRequestKind::MatrixA,
                       blockBase + elem * unitSize);
        }
    }
    const unsigned tailElems = unitSize / shiftScale;
    const Addr tailBase = aReuseBase + blockCount * aBlockStride;
    for (unsigned elem = 0; elem < tailElems; ++elem) {
        replayRead(MemoryRequestKind::MatrixA,
                   tailBase + elem * unitSize);
    }

    replayRead(MemoryRequestKind::VectorC, cBase);
    replayRead(MemoryRequestKind::VectorC, cBase + shiftScale * unitSize);
    replayRead(MemoryRequestKind::MatrixA, bBase + unitSize);

    const uint32_t dRowStride =
        config.D_address_chstep * config.D_address_xstep * unitSize;
    for (unsigned row = 0; row < unitSize; ++row) {
        replayWrite(dBase + row * dRowStride);
        replayWrite(dBase + row * dRowStride + unitSize);
    }

    drainTraceReplayQueue();
    completeTraceReplayIfDone();
}

void
KuiSau::executeTraceReplay()
{
    if (traceReplayActive) {
        return;
    }

    if (traceReplayMode == "lkssfull_sau_stdconv_10") {
        const unsigned innerStart = lkssfullStdconv10InnerStart();
        issueLkssfullStdconv10RequestStream(innerStart);
        return;
    }

    fatal("%s: unsupported trace replay mode '%s'",
          name(), traceReplayMode.c_str());
}

// ============================================================================
// SAU执行流程控制
// ============================================================================

void
KuiSau::executeFlow()
{
    // SAU执行流程的主循环
    if (!status.running) {
        status.running = 0;
        status.flow_i = 0;
        status.flow_k = 0;
        return;
    }

    if (!traceReplayMode.empty()) {
        executeTraceReplay();
        return;
    }

    if (status.flow_i >= config.flow_loop_times) {
        status.running = 0;
        status.flow_i = 0;
        status.flow_k = 0;
        return;
    }

    if (isLkssfullStdconv10Config()) {
        const unsigned innerStart = lkssfullStdconv10InnerStart();
        issueLkssfullStdconv10RequestStream(innerStart);
        return;
    }
    
    // 执行一个flow的完整流程
    updateStatusFromConfig();
    stats.flowStarts++;
    if (!busyAccountingActive) {
        busyStartTick = curTick();
        busyAccountingActive = true;
    }

    execState = ExecutionState::FetchData;
    pendingFlowReads = 0;
    pendingFlowWrites = 0;
    flowReadA.clear();
    flowReadB.clear();
    flowReadC.clear();

    pendingFlowReads += issueFlowReads(
        MemoryRequestKind::MatrixA, status.A_address, status.A_step,
        status.A_count, status.A_kernel, status.A_bytes);

    pendingFlowReads += issueFlowReads(
        MemoryRequestKind::MatrixB, status.B_address, status.B_step,
        status.B_count, status.B_kernel, status.B_bytes);

    if (status.C_en) {
        pendingFlowReads += issueFlowReads(
            MemoryRequestKind::VectorC, status.C_address, status.C_step,
            status.C_count, status.C_kernel, status.C_bytes);
    } else {
        input_matrix3.clear();
    }

    processFlowData();
}

void
KuiSau::processFlowData()
{
    if (execState != ExecutionState::FetchData || pendingFlowReads != 0) {
        return;
    }

    execState = ExecutionState::Preprocess;
    if (!flowReadA.data.empty()) {
        updateInputMatrix1(flowReadA.data);
    }
    if (!flowReadB.data.empty()) {
        updateInputMatrix2(flowReadB.data);
    }
    if (!flowReadC.data.empty()) {
        updateInputMatrix3(flowReadC.data);
    }
    preprocess();

    execState = ExecutionState::Execute;
    systolicArrayExecute();

    execState = ExecutionState::Accumulate;
    accumulateResults();
    
    // 最后一个flow时进行后处理和写回
    const uint16_t flow_step = config.shift_mode == 1 ? 2 : 1;
    const bool last_flow =
        status.flow_i + flow_step >= config.flow_loop_times;

    if (last_flow) {
        execState = ExecutionState::Postprocess;
        addBiasC();
        dequantize();
        transposeOutput();
        updateOutputMatrix();
        
        execState = ExecutionState::WriteBack;
        pendingFlowWrites = 0;
        for (size_t row = 0; row < output_matrix.matrix.size(); ++row) {
            if (row < status.D_wstrb.size() && status.D_wstrb[row] == 0) {
                continue;
            }

            std::vector<uint8_t> output_data;
            output_data.reserve(output_matrix.matrix[row].size());
            for (int32_t value : output_matrix.matrix[row]) {
                output_data.push_back(static_cast<uint8_t>(value));
            }
            if (output_data.empty()) {
                continue;
            }

            const Addr addr = outputWriteAddress(row);
            pendingFlowWrites++;
            stats.flowWriteSegments++;
            sendMemoryWrite(addr, output_data, MemoryRequestKind::OutputD);
        }
        if (pendingFlowWrites == 0) {
            completeFlowStep();
        }
        return;
    }

    completeFlowStep();
}

void
KuiSau::completeFlowStep()
{
    const uint16_t flow_step = config.shift_mode == 1 ? 2 : 1;

    status.flow_i += flow_step;
    status.flow_k += flow_step;

    if (status.flow_i >= config.flow_loop_times) {
        status.running = 0;
        status.flow_i = 0;
        status.flow_k = 0;
        execState = ExecutionState::Idle;
        stats.flowCompletions++;
        finishBusyAccounting();
        return;
    }

    execState = ExecutionState::Idle;
    schedule(executeFlowEvent, clockEdge(Cycles(10)));
}

void
KuiSau::finishBusyAccounting()
{
    if (!busyAccountingActive) {
        return;
    }

    stats.busyTicks += curTick() - busyStartTick;
    busyStartTick = 0;
    busyAccountingActive = false;
}

// ============================================================================
// 内存响应处理
// ============================================================================

void
KuiSau::handleResponse(PacketPtr pkt)
{
    MemoryRequestKind kind = MemoryRequestKind::Untagged;
    size_t segmentIndex = 0;
    if (pkt->senderState) {
        auto *state = dynamic_cast<MemorySenderState *>(pkt->senderState);
        if (state) {
            kind = state->kind;
            segmentIndex = state->segmentIndex;
            delete state;
            pkt->senderState = nullptr;
        }
    }

    if (traceReplayActive && kind != MemoryRequestKind::Untagged) {
        if (pkt->isWrite() && kind == MemoryRequestKind::OutputD) {
            panic_if(pendingTraceReplayWrites == 0,
                     "%s: received extra trace replay write response", name());
            panic_if(traceReplayInFlight == 0,
                     "%s: trace replay write response without in-flight request",
                     name());
            pendingTraceReplayWrites--;
            traceReplayInFlight--;
            drainTraceReplayQueue();
            completeTraceReplayIfDone();
            return;
        }
        if (pkt->isRead() &&
            (kind == MemoryRequestKind::MatrixA ||
             kind == MemoryRequestKind::MatrixB ||
             kind == MemoryRequestKind::VectorC)) {
            panic_if(pendingTraceReplayReads == 0,
                     "%s: received extra trace replay read response", name());
            panic_if(traceReplayInFlight == 0,
                     "%s: trace replay read response without in-flight request",
                     name());
            pendingTraceReplayReads--;
            traceReplayInFlight--;
            drainTraceReplayQueue();
            completeTraceReplayIfDone();
            return;
        }
    }

    if (pkt->isWrite()) {
        if (kind == MemoryRequestKind::OutputD) {
            panic_if(pendingFlowWrites == 0,
                     "%s: received extra D write response", name());
            pendingFlowWrites--;
            if (pendingFlowWrites == 0) {
                completeFlowStep();
            }
        } else if (kind == MemoryRequestKind::Untagged && enableRandomTraffic) {
            randomTrafficRequestInFlight = false;
        }
        return;
    }

    // 处理来自内存的读响应
    if (!pkt->isRead()) {
        return;
    }
    
    const unsigned sz = pkt->getSize();
    const uint8_t *data = pkt->getConstPtr<uint8_t>();
    
    if (!data) {
        warn("%s: Response payload ptr is null", name());
        return;
    }

    if (kind == MemoryRequestKind::Untagged && enableRandomTraffic) {
        const __uint128_t payload = packBytes128(data, sz);
        randomTrafficRequestInFlight = false;
        if (flowState == FlowState::ReadA) {
            lastReadA = payload;
            systolic.loadDataA(lastReadA);
            flowState = FlowState::ReadB;
        } else if (flowState == FlowState::ReadB) {
            lastReadB = payload;
            systolic.loadDataB(lastReadB);
            flowState = FlowState::Compute;
        }
        return;
    }

    // 将响应数据按 byte lane 转换为 int32_t 向量。
    std::vector<int32_t> response_data;
    response_data.reserve(sz);
    for (unsigned i = 0; i < sz; ++i) {
        response_data.push_back(static_cast<int32_t>(data[i]));
    }
    
    bool tagged_flow_read = false;

    switch (kind) {
      case MemoryRequestKind::MatrixA:
        inform("%s: Received matrix A segment %llu at 0x%lx", name(),
               static_cast<unsigned long long>(segmentIndex), pkt->getAddr());
        storeFlowReadSegment(kind, segmentIndex, response_data);
        tagged_flow_read = true;
        break;
      case MemoryRequestKind::MatrixB:
        inform("%s: Received matrix B segment %llu at 0x%lx", name(),
               static_cast<unsigned long long>(segmentIndex), pkt->getAddr());
        storeFlowReadSegment(kind, segmentIndex, response_data);
        tagged_flow_read = true;
        break;
      case MemoryRequestKind::VectorC:
        inform("%s: Received vector C segment %llu at 0x%lx", name(),
               static_cast<unsigned long long>(segmentIndex), pkt->getAddr());
        storeFlowReadSegment(kind, segmentIndex, response_data);
        tagged_flow_read = true;
        break;
      default:
        break;
    }

    if (tagged_flow_read) {
        panic_if(pendingFlowReads == 0,
                 "%s: received extra tagged flow read response", name());
        pendingFlowReads--;
        processFlowData();
        return;
    }

    // 兼容未打标签的旧 smoke traffic：根据当前地址判断这是哪个矩阵的数据
    Addr pktAddr = pkt->getAddr();
    
    if (pktAddr == status.A_address) {
        inform("%s: Received matrix A data at 0x%lx", name(), pktAddr);
        updateInputMatrix1(response_data);
    } else if (pktAddr == status.B_address) {
        inform("%s: Received matrix B data at 0x%lx", name(), pktAddr);
        updateInputMatrix2(response_data);
    } else if (pktAddr == status.C_address) {
        inform("%s: Received vector C data at 0x%lx", name(), pktAddr);
        updateInputMatrix3(response_data);
    }
}

// Mem side port methods
void
KuiSau::KuiSauMemSidePort::recvReqRetry()
{
    assert(!blockedPackets.empty());
    isBlocked = false;
    trySendQueued();
}

bool
KuiSau::KuiSauMemSidePort::recvTimingResp(PacketPtr pkt)
{
    std::cout << "[KuiSau] recvTimingResp tick=" << curTick() << std::endl;
    owner->handleResponse(pkt);
    delete pkt; // delete packet + its dynamic data
    return true;
}

void
KuiSau::KuiSauMemSidePort::sendPacket(PacketPtr pkt)
{
    blockedPackets.push_back(pkt);
    if (isBlocked) {
        return;
    }
    trySendQueued();
}

void
KuiSau::KuiSauMemSidePort::trySendQueued()
{
    while (!blockedPackets.empty()) {
        PacketPtr pkt = blockedPackets.front();
        blockedPackets.pop_front();

        if (!sendTimingReq(pkt)) {
            blockedPackets.push_front(pkt);
            isBlocked = true;
            return;
        }
    }

    isBlocked = false;
}

// CSR side port: accepts CPU CSR accesses and decodes SAU instructions
void
KuiSau::processCsrPacket(PacketPtr pkt, bool isAtomic)
{
    const bool is_write = pkt->isWrite();
    const bool is_read = pkt->isRead();
    uint32_t write_data = 0;

    if (is_write) {
        stats.csrWrites++;
        if (!pkt->hasData()) {
            warn("%s: CSR write without data", name());
            if (isAtomic) {
                pkt->makeAtomicResponse();
            } else {
                pkt->makeResponse();
            }
            return;
        }
        write_data = pkt->getLE<uint32_t>();
    }

    if (isAtomic)
        pkt->makeAtomicResponse();
    else
        pkt->makeResponse();

    const unsigned size = pkt->getSize();
    if (size != sizeof(uint32_t)) {
        warn("%s: CSR access size %u unexpected (expect 4)", name(), size);
    }

    if (is_write) {
        uint32_t data = write_data;
        Addr addr = pkt->getAddr();
        
        unsigned csr_word = 0;
        if (!decodeCsrWord(addr, csrAddrRange.start(), csr_word)) {
            warn("%s: Unknown CSR address 0x%lx", name(), addr);
            csrControlReg = data;
            return;
        }
        
        std::cout << "[KuiSau] CSR Write word=" << csr_word
                  << " addr=0x" << std::hex << addr
                  << " value=0x" << data << std::dec << std::endl;
        
        // 将数据写入对应的CSR寄存器
        switch (csr_word) {
            case Ins1Lsb: csr.ins1_lsb = data; break;
            case Ins1Msb: csr.ins1_msb = data; break;
            case Ins2Lsb: csr.ins2_lsb = data; break;
            case Ins2Msb: csr.ins2_msb = data; break;
            case Ins3Lsb: csr.ins3_lsb = data; break;
            case Ins3Msb: csr.ins3_msb = data; break;
            case Ins4Msb: csr.ins4_msb = data; break;
            case Ins4Lsb:
                csr.ins4_lsb = data;
                updateCsrFromRegisters();
                
                if (D_matrix.empty()) {
                    D_matrix = std::vector<std::vector<int32_t>>(
                        unitSize, std::vector<int32_t>(unitSize, 0));
                }
                
                // 调度第一个flow执行
                if (status.running) {
                    inform("%s: CSR start bit set, triggering execution",
                           name());
                    schedule(executeFlowEvent, clockEdge(Cycles(5)));
                }
                break;
            default:
                warn("%s: Unknown CSR word %u", name(), csr_word);
                break;
        }
        
        csrControlReg = data;
    } else if (is_read) {
        stats.csrReads++;
        unsigned csr_word = 0;
        uint32_t readValue = 0;

        if (decodeCsrWord(pkt->getAddr(), csrAddrRange.start(), csr_word)) {
            readValue = readCsrWord(csr, csr_word, status.running);
        } else {
            warn("%s: Unknown CSR read address 0x%lx", name(), pkt->getAddr());
        }
        
        pkt->setLE<uint32_t>(readValue);
        
        std::cout << "[KuiSau] CSR Read word=" << csr_word
                  << " response=0x" << std::hex << readValue
                  << std::dec << std::endl;
    }
}


bool
KuiSau::KuiSauCsrSidePort::recvTimingReq(PacketPtr pkt)
{
    if (blockedPacket) {
        return false;
    }

    owner->processCsrPacket(pkt, false);
    sendPacket(pkt);
    return !isBlocked;
}

Tick
KuiSau::KuiSauCsrSidePort::recvAtomic(PacketPtr pkt)
{
    owner->processCsrPacket(pkt, true);
    return owner->csrAccessLatency();
}

Tick
KuiSau::KuiSauCsrSidePort::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &backdoor)
{
    backdoor = nullptr;
    return recvAtomic(pkt);
}

void
KuiSau::KuiSauCsrSidePort::recvFunctional(PacketPtr pkt)
{
    owner->processCsrPacket(pkt, false);
}

void
KuiSau::KuiSauCsrSidePort::recvMemBackdoorReq(const MemBackdoorReq &req, MemBackdoorPtr &backdoor)
{
    backdoor = nullptr;
}

void
KuiSau::KuiSauCsrSidePort::recvRespRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    sendPacket(pkt);
}

void
KuiSau::KuiSauCsrSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "CSR port already blocked");
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
        isBlocked = true;
        return;
    }
    isBlocked = false;
}

// Startup schedules first event
void
KuiSau::startup()
{
    if (!enableRandomTraffic) {
        inform("%s: random startup traffic disabled", name());
        return;
    }

    Tick first_tick = clockEdge();
    std::cout << "KuiSau startup tick=" << first_tick << " interval(cycles)=" << schedule_interval
              << " ticks=" << cyclesToTicks(Cycles(schedule_interval)) << std::endl;
    schedule(nextTickEvent, first_tick);
}

// Pack lower 8 accumulators of systolic into 128b value
static __uint128_t
pack_acc_lower8_to_128(KuiSauSystolic &s)
{
    __uint128_t v = 0;
    // 取第 0 行前 8 个累加器的低 8 位拼成 64 位（放在 128 位低部）
    for (int i = 0; i < 8; ++i) {
        uint32_t acc = static_cast<uint32_t>(s.readAcc(0, i)) & 0xFFFFFF; // 24-bit 有效
        v |= (__uint128_t(acc & 0xFF) << (i * 8));
    }
    return v;
}

// Main FSM tick
void
KuiSau::sendOneKuiPkt()
{
    std::cout << "[KuiSau] tick=" << curTick() << " state=";
    switch (flowState) {
        case FlowState::ReadA: std::cout << "ReadA"; break;
        case FlowState::ReadB: std::cout << "ReadB"; break;
        case FlowState::Compute: std::cout << "Compute"; break;
        case FlowState::Write: std::cout << "Write"; break;
    }
    std::cout << std::endl;

    if (maxStimulus > 0 && stimulusCount >= maxStimulus) {
        inform("%s: maxStimulus reached (%u)", name(), stimulusCount);
        return;
    }

    // Common constants
    const unsigned size = 16;
    const Request::Flags req_flags = Request::PHYSICAL;
    const RequestorID req_id = memoryRequestorId;

    if (port_KuiSau_sendto_mem.isBlocked) {
        // Just reschedule if still blocked
        schedule(nextTickEvent, curTick() + cyclesToTicks(Cycles(schedule_interval)));
        return;
    }

    if (randomTrafficRequestInFlight) {
        schedule(nextTickEvent,
                 curTick() + cyclesToTicks(Cycles(schedule_interval)));
        return;
    }

    switch (flowState) {
        case FlowState::ReadA: {
            addrA = addrDist(rng);
            addrOut = addrA; // for simplicity write back where A came from
            RequestPtr req = std::make_shared<Request>(addrA, size, req_flags, req_id);
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();
            std::cout << "[KuiSau] Send Timing ReadA @0x" << std::hex << addrA << std::dec << std::endl;
            randomTrafficRequestInFlight = true;
            port_KuiSau_sendto_mem.sendPacket(pkt);
            // State remains ReadA until response transitions it to ReadB
            break;
        }
        case FlowState::ReadB: {
            addrB = addrDist(rng);
            RequestPtr req = std::make_shared<Request>(addrB, size, req_flags, req_id);
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();
            std::cout << "[KuiSau] Send Timing ReadB @0x" << std::hex << addrB << std::dec << std::endl;
            randomTrafficRequestInFlight = true;
            port_KuiSau_sendto_mem.sendPacket(pkt);
            // State remains ReadB until response transitions it to Compute
            break;
        }
        case FlowState::Compute: {
            systolic.pulse();
            writeData128 = pack_acc_lower8_to_128(systolic);
            std::cout << "[KuiSau] Compute done writeData128 prepared" << std::endl;
            flowState = FlowState::Write;
            break;
        }
        case FlowState::Write: {
            RequestPtr req = std::make_shared<Request>(addrOut, size, req_flags, req_id);
            PacketPtr pkt = Packet::createWrite(req);
            pkt->allocate(); // allocate 16B
            uint8_t *buf = pkt->getPtr<uint8_t>();
            for (unsigned i = 0; i < size; ++i)
                buf[i] = (uint8_t)((writeData128 >> (i * 8)) & 0xFF);
            std::cout << "[KuiSau] Send Timing Write @0x" << std::hex << addrOut << std::dec << std::endl;
            randomTrafficRequestInFlight = true;
            port_KuiSau_sendto_mem.sendPacket(pkt);
            stimulusCount++;
            flowState = FlowState::ReadA;
            break;
        }
    }

    // Schedule next tick
    Tick next_tick = clockEdge(Cycles(schedule_interval));
    schedule(nextTickEvent, next_tick);
}

Port &
KuiSau::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port_KuiSau_sendto_mem")
        return port_KuiSau_sendto_mem;
    if (if_name == "port_KuiSau_getfrm_mem")
        return port_KuiSau_getfrm_mem;
    return SimObject::getPort(if_name, idx);
}

} // namespace gem5
