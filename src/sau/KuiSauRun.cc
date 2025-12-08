#include "sau/KuiSauRun.hh"

#include <sstream>
#include <algorithm>

namespace gem5 {

static inline int8_t get_i8_from_128(__uint128_t v, unsigned idx)
{
    // idx: 0..15, 低位字节为 idx=0
    unsigned shift = idx * 8;
    uint64_t lo = (uint64_t)(v & (~(__uint128_t)0ULL));
    uint64_t hi = (uint64_t)(v >> 64);
    uint64_t word;
    if (shift < 64) {
        word = (lo >> shift) & 0xFFULL;
    } else {
        word = (hi >> (shift - 64)) & 0xFFULL;
    }
    return static_cast<int8_t>(static_cast<uint8_t>(word));
}

 KuiSauSystolic::KuiSauSystolic(unsigned rows, unsigned cols)
     : m_rows(rows), m_cols(cols), m_A(rows * cols, 0), m_B(rows * cols, 0), m_ACC(rows * cols, 0)
{
}

void KuiSauSystolic::loadDataA(__uint128_t value)
{
    // 将 128-bit 切分为 16 个 int8，并填充到 A（行主序）。如果阵列超过 16 个元素，则循环重复。
    const unsigned total = m_rows * m_cols;
    for (unsigned i = 0; i < total; ++i) {
        int8_t b = get_i8_from_128(value, i % 16);
        m_A[i] = b;
    }
}

void KuiSauSystolic::loadDataB(__uint128_t value)
{
    const unsigned total = m_rows * m_cols;
    for (unsigned i = 0; i < total; ++i) {
        int8_t b = get_i8_from_128(value, i % 16);
        m_B[i] = b;
    }
}

inline int32_t KuiSauSystolic::saturate24(int32_t v)
{
    const int32_t MAX24 = (1 << 23) - 1;   // 0x7FFFFF
    const int32_t MIN24 = -(1 << 23);      // -0x800000
    if (v > MAX24) return MAX24;
    if (v < MIN24) return MIN24;
    return v;
}

void KuiSauSystolic::pulse()
{
    // 对每个单元：int8 * int8 -> int16，然后累加到 24-bit 累加器（带饱和）
    const unsigned total = m_rows * m_cols;

	// 阵列在一个pulse并行执行
    for (unsigned i = 0; i < total; ++i) {
		// 计算元素乘积
        int16_t prod = static_cast<int16_t>(static_cast<int16_t>(m_A[i]) * static_cast<int16_t>(m_B[i]));
        
		// 累加元素索引
		int32_t acc = m_ACC[i];

		// 累加
        acc += static_cast<int32_t>(prod);

		// 截取位数
        m_ACC[i] = saturate24(acc);
    }
}

int8_t KuiSauSystolic::readA(unsigned r, unsigned c) const
{
    if (r >= m_rows || c >= m_cols) return 0;
    return m_A[r * m_cols + c];
}

int8_t KuiSauSystolic::readB(unsigned r, unsigned c) const
{
    if (r >= m_rows || c >= m_cols) return 0;
    return m_B[r * m_cols + c];
}

int32_t KuiSauSystolic::readAcc(unsigned r, unsigned c) const
{
    if (r >= m_rows || c >= m_cols) return 0;
    return m_ACC[r * m_cols + c];
}

void KuiSauSystolic::display(std::ostream &os) const
{
    os << "KuiSauSystolic(" << m_rows << "x" << m_cols << ") A (int8):\n";
    for (unsigned r = 0; r < m_rows; ++r) {
        os << "  ";
        for (unsigned c = 0; c < m_cols; ++c) {
            int v = static_cast<int>(readA(r, c));
            os << v;
            if (c + 1 < m_cols) os << ", ";
        }
        os << "\n";
    }
    os << "B (int8):\n";
    for (unsigned r = 0; r < m_rows; ++r) {
        os << "  ";
        for (unsigned c = 0; c < m_cols; ++c) {
            int v = static_cast<int>(readB(r, c));
            os << v;
            if (c + 1 < m_cols) os << ", ";
        }
        os << "\n";
    }
    os << "ACC (sat 24-bit signed):\n";
    for (unsigned r = 0; r < m_rows; ++r) {
        os << "  ";
        for (unsigned c = 0; c < m_cols; ++c) {
            int32_t v = readAcc(r, c);
            os << v;
            if (c + 1 < m_cols) os << ", ";
        }
        os << "\n";
    }
}

} // namespace gem5
