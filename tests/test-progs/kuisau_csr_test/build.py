#!/usr/bin/env python3

"""
KuiSau CSR Test Compiler
编译 RISC-V CSR 测试程序的 Python 脚本
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path


def find_riscv_compiler():
    """查找可用的 RISC-V 编译器"""
    
    # 1. 检查环境变量 RISCV_PREFIX
    riscv_prefix = os.environ.get('RISCV_PREFIX', '')
    if riscv_prefix:
        cc = f"{riscv_prefix}gcc"
        if shutil.which(cc):
            return cc
    
    # 2. 检查常见的编译器
    common_prefixes = [
        "riscv64-unknown-elf-",
        "riscv64-linux-gnu-",
        "riscv64-",
        "riscv32-unknown-elf-",
    ]
    
    for prefix in common_prefixes:
        cc = f"{prefix}gcc"
        if shutil.which(cc):
            return cc
    
    # 3. 检查常见的安装路径
    common_paths = [
        "/opt/riscv/bin/riscv64-unknown-elf-gcc",
        "/usr/local/riscv/bin/riscv64-unknown-elf-gcc",
        os.path.expanduser("~/riscv/bin/riscv64-unknown-elf-gcc"),
        os.path.expanduser("~/.local/riscv/bin/riscv64-unknown-elf-gcc"),
    ]
    
    for path in common_paths:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    
    return None


def get_objcopy_from_gcc(gcc_path):
    """从 gcc 路径推导 objcopy 路径"""
    gcc_dir = os.path.dirname(gcc_path)
    gcc_name = os.path.basename(gcc_path)
    
    # 替换 gcc -> objcopy
    objcopy_name = gcc_name.replace('gcc', 'objcopy')
    objcopy_path = os.path.join(gcc_dir, objcopy_name)
    
    if os.path.isfile(objcopy_path):
        return objcopy_path
    
    # 尝试标准路径
    standard_objcopy = os.path.join(gcc_dir, 'riscv64-unknown-elf-objcopy')
    if os.path.isfile(standard_objcopy):
        return standard_objcopy
    
    return None


def compile_test():
    """编译测试程序"""
    
    # 获取脚本目录
    script_dir = Path(__file__).parent.absolute()
    output_dir = script_dir / "build"
    source_file = script_dir / "kuisau_csr_test.c"
    output_file = output_dir / "kuisau_csr_test.riscv"
    binary_file = output_dir / "kuisau_csr_test.bin"
    
    # 检查源文件是否存在
    if not source_file.exists():
        print(f"Error: Source file not found: {source_file}")
        return False
    
    # 查找编译器
    cc = find_riscv_compiler()
    if not cc:
        print("=" * 60)
        print("Error: RISC-V compiler not found")
        print("=" * 60)
        print()
        print("Installation options:")
        print()
        print("1. Ubuntu/Debian:")
        print("   sudo apt install gcc-riscv64-unknown-elf")
        print()
        print("2. Fedora/RHEL:")
        print("   sudo dnf install riscv64-unknown-elf-gcc")
        print()
        print("3. macOS (Homebrew):")
        print("   brew install riscv-gnu-toolchain")
        print()
        print("4. Manual installation:")
        print("   Visit: https://github.com/riscv-collab/riscv-gnu-toolchain")
        print()
        print("5. Using gem5 Docker container:")
        print("   docker run -it ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest bash")
        print()
        return False
    
    objcopy = get_objcopy_from_gcc(cc)
    
    # 创建输出目录
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # 编译选项
    cflags = [
        "-march=rv64i",
        "-mabi=lp64",
        "-static",
        "-mcmodel=medany",
        "-Wall",
        "-Werror",
    ]
    
    ldflags = [
        "-nostdlib",
        "-Wl,-Ttext=0x80000000",
    ]
    
    # 打印编译信息
    print("=" * 60)
    print("Compiling RISC-V CSR test program")
    print("=" * 60)
    print(f"Compiler:   {cc}")
    if objcopy:
        print(f"Objcopy:    {objcopy}")
    print(f"Source:     {source_file}")
    print(f"Output:     {output_file}")
    print(f"CFLAGS:     {' '.join(cflags)}")
    print(f"LDFLAGS:    {' '.join(ldflags)}")
    print("=" * 60)
    
    # 构建编译命令
    cmd = [cc] + cflags + ldflags + ["-o", str(output_file), str(source_file)]
    
    # 编译
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print("✓ Compilation successful!")
    except subprocess.CalledProcessError as e:
        print("✗ Compilation failed!")
        print()
        print("stdout:")
        print(e.stdout)
        print()
        print("stderr:")
        print(e.stderr)
        return False
    
    # 创建二进制文件
    if objcopy:
        try:
            cmd_objcopy = [objcopy, "-O", "binary", str(output_file), str(binary_file)]
            subprocess.run(cmd_objcopy, check=True, capture_output=True)
            print(f"✓ Binary file created: {binary_file}")
        except subprocess.CalledProcessError as e:
            print(f"Warning: Failed to create binary file")
            print(e.stderr)
    
    # 显示文件信息
    print()
    print("File information:")
    try:
        file_result = subprocess.run(
            ["file", str(output_file)],
            capture_output=True,
            text=True
        )
        print(file_result.stdout.strip())
    except:
        pass
    
    # 显示文件大小
    if output_file.exists():
        size = output_file.stat().st_size
        print(f"File size: {size} bytes")
    
    print()
    print("=" * 60)
    print("Build completed successfully!")
    print("=" * 60)
    
    return True


if __name__ == "__main__":
    success = compile_test()
    sys.exit(0 if success else 1)
