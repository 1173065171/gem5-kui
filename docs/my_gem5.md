# gem5 测试记录

## 临时开一个新的容器进入gem5的docker运行环境：

```sh
docker run -u $UID:$GID -v /home/zbn/code/gem5:/gem5 -it ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest bash
```

## 保持一个容器重复使用

创建容器

```sh
docker run -u $UID:$GID -v /home/zbn/code/gem5:/gem5 -it --name gem5-dev ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest bash
```

进入环境

```sh
docker start -ai gem5-dev
```

## 编译

按照RISCV编译

```sh
scons build/RISCV/gem5.opt -j$(nproc)
```

## 执行

```sh
./build/RISCV/gem5.opt ./configs/tutorial/part1/kui_system_mem.py > output.log
```
