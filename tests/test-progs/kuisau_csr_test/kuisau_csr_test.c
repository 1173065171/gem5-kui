#include <stdint.h>

// KuiSau CSR基地址
#define KUISAU_CSR_BASE 0x2F000000

// 简单的打印函数 - 直接写入 stdout (fd=1)
// 注：这个函数保留是为了未来扩展，当前不使用
__attribute__((unused))
static void print_str(const char *s)
{
    // 对于 gem5 的 SimpleSyscallDesc，我们可以尝试直接写内存
    // 或者使用 HLT 指令通知模拟器
    volatile int fd = 1;
    (void)fd;
    (void)s;
}

// 从CSR读取当前的控制寄存器值
static inline uint32_t kuisau_csr_read(void)
{
    uint32_t val;
    asm volatile(
        "li t0, %1\n"       // 将地址加载到t0
        "lw %0, 0(t0)\n"    // 从地址读取4字节
        : "=r" (val)
        : "i" (KUISAU_CSR_BASE)
        : "t0"
    );
    return val;
}

// 向CSR写入控制寄存器值
static inline void kuisau_csr_write(uint32_t val)
{
    asm volatile(
        "li t0, %0\n"       // 将地址加载到t0
        "sw %1, 0(t0)\n"    // 将val写入地址
        :
        : "i" (KUISAU_CSR_BASE), "r" (val)
        : "t0"
    );
}

// 测试程序入口
void _start(void)
{
    // 测试1: 写入测试值
    kuisau_csr_write(0xDEADBEEF);
    uint32_t readval = kuisau_csr_read();
    if (readval != 0xDEADBEEF) {
        asm volatile("ebreak"); // 失败，触发断点
    }

    // 测试2: 写入另一个测试值
    kuisau_csr_write(0x12345678);
    readval = kuisau_csr_read();
    if (readval != 0x12345678) {
        asm volatile("ebreak"); // 失败
    }

    // 测试3: 写入0
    kuisau_csr_write(0x00000000);
    readval = kuisau_csr_read();
    if (readval != 0x00000000) {
        asm volatile("ebreak"); // 失败
    }

    // 测试4: 多次写读循环
    uint32_t test_values[] = {0xAAAAAAAA, 0x55555555, 0xFFFFFFFF, 0x80000000};
    for (int i = 0; i < 4; i++) {
        kuisau_csr_write(test_values[i]);
        readval = kuisau_csr_read();
        if (readval != test_values[i]) {
            asm volatile("ebreak"); // 失败
        }
    }

    // 所有测试通过，退出
    // 使用 ECALL (environment call) 退出
    register long a0 asm("a0") = 0;  // exit code 0
    register long a7 asm("a7") = 93; // exit syscall number (rv32i/rv64i)
    asm volatile("ecall" : : "r"(a0), "r"(a7));

    // 备用：无限循环
    while(1) {
        asm volatile("wfi"); // wait for interrupt
    }
}
