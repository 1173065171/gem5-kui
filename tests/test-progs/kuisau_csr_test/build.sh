#!/bin/bash

# KuiSau CSR 测试编译脚本
# 支持 RISC-V 交叉编译器工具链

set -e

# POSIX 兼容的脚本目录获取
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/build"

# 自动查找 RISC-V 工具链
find_riscv_toolchain() {
    # 1. 检查环境变量指定的工具链
    if [ -n "$RISCV_PREFIX" ] && command -v "${RISCV_PREFIX}gcc" >/dev/null 2>&1; then
        echo "$RISCV_PREFIX"
        return 0
    fi
    
    # 2. 尝试常见的系统安装路径
    for prefix in "riscv64-unknown-elf-" "riscv64-linux-gnu-" "riscv32-unknown-linux-gnu-"; do
        if command -v "${prefix}gcc" >/dev/null 2>&1; then
            echo "$prefix"
            return 0
        fi
    done
    
    # 3. 搜索常见的本地安装路径
    for basedir in "$HOME/software" "$HOME/riscv" "/opt/riscv" "/usr/local/riscv"; do
        for prefix in "riscv64-unknown-elf" "riscv64-linux-gnu" "riscv32-unknown-linux-gnu"; do
            if [ -x "$basedir/$prefix/bin/${prefix}-gcc" ]; then
                echo "$basedir/$prefix/bin/${prefix}-"
                return 0
            fi
            if [ -x "$basedir/bin/${prefix}-gcc" ]; then
                echo "$basedir/bin/${prefix}-"
                return 0
            fi
        done
    done
    
    return 1
}

# 获取工具链
RISCV_PREFIX=$(find_riscv_toolchain)
if [ -z "$RISCV_PREFIX" ]; then
    echo "Error: RISC-V compiler not found"
    echo ""
    echo "Found toolchain at:"
    echo "  /home/zbn/software/riscv32-glibc/bin/riscv32-unknown-linux-gnu-gcc"
    echo ""
    echo "Please use one of these methods:"
    echo "  Method 1: Set RISCV_PREFIX environment variable"
    echo "    export RISCV_PREFIX=/home/zbn/software/riscv32-glibc/bin/riscv32-unknown-linux-gnu-"
    echo "    $0"
    echo ""
    echo "  Method 2: Add toolchain to PATH"
    echo "    export PATH=/home/zbn/software/riscv32-glibc/bin:\$PATH"
    echo "    $0"
    echo ""
    echo "  Method 3: Use Docker"
    echo "    docker run --rm -it --user \$(id -u):\$(id -g) -v \$(pwd):/gem5 -w /gem5 \\"
    echo "      ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest bash"
    echo ""
    exit 1
fi

CC="${RISCV_PREFIX}gcc"
OBJCOPY="${RISCV_PREFIX}objcopy"

mkdir -p "$OUTPUT_DIR"

# 根据工具链类型自动选择编译标志
if echo "$RISCV_PREFIX" | grep -q "riscv32"; then
    # 32 位编译标志
    CFLAGS="-march=rv32i -mabi=ilp32 -nostdlib -static -Wall"
    LDFLAGS="-Wl,-Ttext=0x80000000 -Wl,--no-dynamic-linker"
    echo "Detected: 32-bit RISC-V toolchain"
elif echo "$RISCV_PREFIX" | grep -q "riscv64"; then
    # 64 位编译标志
    CFLAGS="-march=rv64i -mabi=lp64 -nostdlib -static -mcmodel=medany -Wall"
    LDFLAGS="-Wl,-Ttext=0x80000000"
    echo "Detected: 64-bit RISC-V toolchain"
else
    # 默认使用 32 位
    CFLAGS="-march=rv32i -mabi=ilp32 -nostdlib -static -Wall"
    LDFLAGS="-Wl,-Ttext=0x80000000 -Wl,--no-dynamic-linker"
    echo "Detected: Generic RISC-V toolchain (using 32-bit flags)"
fi

echo "=========================================="
echo "Compiling RISC-V CSR test program"
echo "=========================================="
echo "CC:       $CC"
echo "CFLAGS:   $CFLAGS"
echo "LDFLAGS:  $LDFLAGS"
echo "Source:   $SCRIPT_DIR/kuisau_csr_test.c"
echo "Output:   $OUTPUT_DIR/kuisau_csr_test.riscv"
echo "=========================================="

# 编译源文件
"$CC" $CFLAGS $LDFLAGS -o "$OUTPUT_DIR/kuisau_csr_test.riscv" \
    "$SCRIPT_DIR/kuisau_csr_test.c"

echo "✓ Compilation successful!"

# 创建内存转储用的二进制文件
"$OBJCOPY" -O binary "$OUTPUT_DIR/kuisau_csr_test.riscv" \
    "$OUTPUT_DIR/kuisau_csr_test.bin"

echo "✓ Binary file created: $OUTPUT_DIR/kuisau_csr_test.bin"

# 显示文件信息
echo ""
echo "File information:"
file "$OUTPUT_DIR/kuisau_csr_test.riscv"
echo ""
echo "=========================================="
echo "Build completed successfully!"
echo "=========================================="
