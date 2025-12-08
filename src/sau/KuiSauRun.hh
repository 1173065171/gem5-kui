#ifndef __KUI_SAU_SYSTOLIC_HH__
#define __KUI_SAU_SYSTOLIC_HH__

#include <cstdint>
#include <vector>
#include <iostream>
#include <iomanip>

namespace gem5 {

/**
 * KuiSauSystolic
 *
 * 一个轻量的“脉动阵列”辅助类，用于演示性地保存和处理 128-bit 数据单元。
 * 设计目标是小而清晰：提供加载 128-bit 数据、脉动（pulse）计算、以及打印状态。
 */
class KuiSauSystolic {
  public:
    KuiSauSystolic(unsigned rows = 8, unsigned cols = 8);
    ~KuiSauSystolic() = default;

    // 从 128-bit 输入切分为 16 个 int8 元素，按行主序填充阵列
    void loadDataA(__uint128_t value);

    // 载入第二路 128-bit 输入（如权重或相邻行），同样切分为 int8
    void loadDataB(__uint128_t value);

    // 执行一次脉动：int8 x int8 -> int16，累加到 24-bit 带饱和累加器
    void pulse();

    // 读取某个单元（行/列）
    int8_t readA(unsigned r, unsigned c) const;
    int8_t readB(unsigned r, unsigned c) const;
    int32_t readAcc(unsigned r, unsigned c) const; // 低 24-bit 有效

    // 打印当前阵列内容（以 128-bit 十六进制展现）
    void display(std::ostream &os = std::cout) const;

    unsigned rows() const { return m_rows; }
    unsigned cols() const { return m_cols; }

  private:
    unsigned m_rows;
    unsigned m_cols;
    // A/B 输入阵列（int8）
    std::vector<int8_t> m_A;
    std::vector<int8_t> m_B;
    // 累加器阵列，使用 32-bit 存储，但仅 24-bit 有效，提供饱和逻辑
    std::vector<int32_t> m_ACC;

    // 辅助：饱和到 24-bit 有符号范围 [-2^23, 2^23-1]
    static inline int32_t saturate24(int32_t v);
};

} // namespace gem5

#endif // __KUI_SAU_RUN_HH__
