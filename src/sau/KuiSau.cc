// Clean implementation of KuiSau timing FSM (ReadA -> ReadB -> Compute -> Write)
#include "KuiSau.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"
#include <random>
#include <cmath>

namespace gem5 {

// ============================================================================
// CSR 和配置处理
// ============================================================================

void
KuiSau::updateCsrFromRegisters()
{
    // 从CSR寄存器解码配置参数，对应SAU.py中的update_csr()逻辑
    
    // ins1
    config.transpose_mode = (csr.ins1_msb) & 0x03;
    config.cutbit = (csr.ins1_msb >> 2) & 0x1F;
    
    // ins2
    config.register_mode = (csr.ins1_lsb) & 0x03;
    config.conv_kernel = (csr.ins1_lsb >> 2) & 0x03;
    config.stride = (csr.ins1_lsb >> 4) & 0x01;
    config.shift_mode = (csr.ins1_lsb >> 5) & 0x01;
    
    // ins3
    config.B_address_chstep = (csr.ins2_msb) & 0xFF;
    config.A_address_chstep = (csr.ins2_msb >> 8) & 0xFF;
    config.D_address_chstep = (csr.ins2_msb >> 16) & 0xFF;
    
    // ins4
    config.B_address_xstep = (csr.ins2_lsb) & 0x7F;
    config.A_address_xstep = (csr.ins2_lsb >> 8) & 0x7F;
    config.D_address_xstep = (csr.ins2_lsb >> 16) & 0x7F;
    
    // ins5
    config.A_address = (csr.ins3_msb) & 0xFFFFFF;
    
    // ins6
    config.B_address = (csr.ins3_lsb) & 0xFFFFFF;
    
    // ins7
    config.C_address = (csr.ins4_msb) & 0xFFFFF;
    config.work_mode = (csr.ins4_msb >> 20) & 0x03;
    config.flow_mode = (csr.ins4_msb >> 22) & 0x03;
    
    if (config.shift_mode == 1) {
        config.flow_loop_times = ((csr.ins4_msb >> 24) & 0x3F) / 2;
    } else {
        config.flow_loop_times = (csr.ins4_msb >> 24) & 0x3F;
    }
    
    // ins8
    config.start = (csr.ins4_lsb) & 0x01;
    config.ins_id = (csr.ins4_lsb >> 1) & 0xFF;
    config.D_address = (csr.ins4_lsb >> 9) & 0xFFFFF;
    
    // 更新状态
    status.running_time = 0;
    status.running = config.start;
    status.D_address = config.D_address + baseAddr;
    status.D_step = config.D_address_chstep * config.D_address_xstep * unitSize;
    status.D_count = unitSize;
    
    inform("%s: CSR decoded - work_mode=%u, conv_kernel=%u, flow_loop_times=%u",
           name(), config.work_mode, config.conv_kernel, config.flow_loop_times);
}

void
KuiSau::updateStatusFromConfig()
{
    // 根据配置参数计算数据访问地址和步长
    if (config.conv_kernel > 1) {
        // 卷积操作模式
        if (config.register_mode == 0) {
            // 普通卷积
            status.A_address = config.A_address + baseAddr + 
                             status.flow_i * config.A_address_chstep * config.A_address_xstep * unitSize;
            status.A_step = config.A_address_xstep * unitSize;
            status.A_count = config.conv_kernel;
            
            status.B_address = config.B_address + baseAddr + 
                             status.flow_i * config.B_address_chstep * config.B_address_xstep * unitSize;
            status.B_step = config.B_address_xstep * unitSize;
            status.B_count = config.conv_kernel * config.conv_kernel;
            
            status.C_address = config.C_address + baseAddr;
        } else if (config.register_mode == 2) {
            // DW卷积（深度卷积）
            status.A_address = config.A_address + baseAddr + 
                             status.flow_i * (config.stride + 1) * config.A_address_xstep * unitSize;
            status.A_step = config.A_address_xstep * unitSize;
            status.A_count = config.conv_kernel;
            
            status.B_address = config.B_address + baseAddr + 
                             status.flow_i * config.B_address_chstep * config.B_address_xstep * unitSize;
            status.B_step = config.B_address_xstep * unitSize;
            status.B_count = config.conv_kernel * config.conv_kernel;
            
            status.C_address = config.C_address + baseAddr;
        }
        
        status.A_kernel = config.stride + 2;
        status.B_kernel = 1;
        status.D_kernel = 1;
        status.A_bytes = unitSize * (config.shift_mode == 0 ? 1 : 2);
        status.B_bytes = unitSize;
    } else {
        // 矩阵乘法模式
        if (config.A_address_chstep == 0) {
            status.A_address = config.A_address + baseAddr + 
                             status.flow_i * unitSize * (config.shift_mode + 1);
        } else {
            status.A_address = config.A_address + baseAddr + 
                             status.flow_i * config.A_address_chstep * config.A_address_xstep * unitSize;
        }
        status.A_step = config.A_address_xstep * unitSize;
        status.A_count = unitSize;
        
        status.B_address = config.B_address + baseAddr + 
                         status.flow_i * config.B_address_chstep * config.B_address_xstep * unitSize;
        status.B_step = config.B_address_xstep * unitSize;
        status.B_count = unitSize;
        
        status.C_address = config.C_address + baseAddr;
        
        status.A_kernel = 1;
        status.B_kernel = 1;
        status.D_kernel = 1;
        status.A_bytes = unitSize * (config.shift_mode == 0 ? 1 : 2);
        status.B_bytes = unitSize;
    }
    
    // 计算写使能
    status.D_wstrb.clear();
    for (unsigned i = 0; i < unitSize; ++i) {
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
    input_matrix1 = MatrixBuffer(inputData.size() / unitSize, unitSize);
    
    for (size_t i = 0; i < inputData.size(); ++i) {
        input_matrix1.matrix[i / unitSize][i % unitSize] = inputData[i];
    }
}

void
KuiSau::updateInputMatrix2(const std::vector<int32_t> &inputData)
{
    // 读取并转换矩阵B的数据
    input_matrix2 = MatrixBuffer(inputData.size() / unitSize, unitSize);
    
    for (size_t i = 0; i < inputData.size(); ++i) {
        input_matrix2.matrix[i / unitSize][i % unitSize] = inputData[i];
    }
}

void
KuiSau::updateInputMatrix3(const std::vector<int32_t> &inputData)
{
    // 读取向量C的数据
    input_matrix3 = inputData;
}

void
KuiSau::preprocess()
{
    // 数据预处理：转置、格式转换、数据重排
    if (config.conv_kernel > 1) {
        // 卷积模式
        // 需要进行矩阵移位和掩码处理
        A_matrix = std::vector<std::vector<int32_t>>(input_matrix1.matrix);
        B_matrix = std::vector<std::vector<int32_t>>(input_matrix2.matrix);
        C_matrix = input_matrix3;
        
        if (config.transpose_mode == 1) {
            // 转置A矩阵
            std::vector<std::vector<int32_t>> transposed(A_matrix[0].size(),
                                                         std::vector<int32_t>(A_matrix.size()));
            for (size_t i = 0; i < A_matrix.size(); ++i) {
                for (size_t j = 0; j < A_matrix[i].size(); ++j) {
                    transposed[j][i] = A_matrix[i][j];
                }
            }
            A_matrix = transposed;
        } else if (config.transpose_mode == 2) {
            // 转置B矩阵
            std::vector<std::vector<int32_t>> transposed(B_matrix[0].size(),
                                                         std::vector<int32_t>(B_matrix.size()));
            for (size_t i = 0; i < B_matrix.size(); ++i) {
                for (size_t j = 0; j < B_matrix[i].size(); ++j) {
                    transposed[j][i] = B_matrix[i][j];
                }
            }
            B_matrix = transposed;
        }
    } else {
        // 矩阵乘法模式
        A_matrix = std::vector<std::vector<int32_t>>(input_matrix1.matrix);
        B_matrix = std::vector<std::vector<int32_t>>(input_matrix2.matrix);
        C_matrix = input_matrix3;
        
        if (config.transpose_mode == 1) {
            std::vector<std::vector<int32_t>> transposed(A_matrix[0].size(),
                                                         std::vector<int32_t>(A_matrix.size()));
            for (size_t i = 0; i < A_matrix.size(); ++i) {
                for (size_t j = 0; j < A_matrix[i].size(); ++j) {
                    transposed[j][i] = A_matrix[i][j];
                }
            }
            A_matrix = transposed;
        } else if (config.transpose_mode == 2) {
            std::vector<std::vector<int32_t>> transposed(B_matrix[0].size(),
                                                         std::vector<int32_t>(B_matrix.size()));
            for (size_t i = 0; i < B_matrix.size(); ++i) {
                for (size_t j = 0; j < B_matrix[i].size(); ++j) {
                    transposed[j][i] = B_matrix[i][j];
                }
            }
            B_matrix = transposed;
        }
    }
}

void
KuiSau::systolicArrayExecute()
{
    // 脉动阵列计算
    if (config.work_mode == 0 || config.work_mode == 1 || config.work_mode == 2) {
        // 矩阵乘法
        D_matrix_tmp = std::vector<std::vector<int32_t>>(A_matrix.size(),
                                                         std::vector<int32_t>(B_matrix[0].size(), 0));
        
        for (size_t i = 0; i < A_matrix.size(); ++i) {
            for (size_t j = 0; j < B_matrix[0].size(); ++j) {
                int32_t sum = 0;
                for (size_t k = 0; k < A_matrix[0].size(); ++k) {
                    sum += A_matrix[i][k] * B_matrix[k][j];
                }
                D_matrix_tmp[i][j] = sum;
            }
        }
    } else if (config.work_mode == 3) {
        // 矩阵加法
        D_matrix_tmp = A_matrix;
        for (size_t i = 0; i < A_matrix.size(); ++i) {
            for (size_t j = 0; j < A_matrix[i].size(); ++j) {
                D_matrix_tmp[i][j] += B_matrix[i][j];
            }
        }
    }
}

void
KuiSau::accumulateResults()
{
    // flow间累加
    if (status.flow_i == 0) {
        // 第一次flow，初始化D_matrix
        D_matrix = D_matrix_tmp;
    } else if (status.flow_i < config.flow_loop_times - 1) {
        // 中间flow，累加
        for (size_t i = 0; i < D_matrix.size(); ++i) {
            for (size_t j = 0; j < D_matrix[i].size(); ++j) {
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
    // 截位去量化：24bit -> 16bit
    D_matrix_deq = D_matrix;
    for (size_t i = 0; i < D_matrix_deq.size(); ++i) {
        for (size_t j = 0; j < D_matrix_deq[i].size(); ++j) {
            D_matrix_deq[i][j] = D_matrix_deq[i][j] >> config.cutbit;
        }
    }
}

void
KuiSau::transposeOutput()
{
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
    // 更新输出矩阵并准备写回
    output_matrix = MatrixBuffer(D_matrix_deq.size(), D_matrix_deq[0].size());
    output_matrix.matrix = D_matrix_deq;
    
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
KuiSau::sendMemoryRead(Addr addr, size_t size)
{
    const Request::Flags req_flags = Request::PHYSICAL;
    const RequestorID req_id = 1;
    
    RequestPtr req = std::make_shared<Request>(addr, size, req_flags, req_id);
    PacketPtr pkt = Packet::createRead(req);
    pkt->allocate();
    
    port_KuiSau_sendto_mem.sendPacket(pkt);
}

void
KuiSau::sendMemoryWrite(Addr addr, const std::vector<int32_t> &data)
{
    const Request::Flags req_flags = Request::PHYSICAL;
    const RequestorID req_id = 1;
    size_t size = data.size() * sizeof(int32_t);
    
    RequestPtr req = std::make_shared<Request>(addr, size, req_flags, req_id);
    PacketPtr pkt = Packet::createWrite(req);
    pkt->allocate();
    
    uint8_t *buf = pkt->getPtr<uint8_t>();
    memcpy(buf, data.data(), size);
    
    port_KuiSau_sendto_mem.sendPacket(pkt);
}

// ============================================================================
// SAU执行流程控制
// ============================================================================

void
KuiSau::executeFlow()
{
    // SAU执行流程的主循环
    if (!status.running || status.flow_i >= config.flow_loop_times) {
        status.running = 0;
        status.flow_i = 0;
        return;
    }
    
    // 执行一个flow的完整流程
    updateStatusFromConfig();
    
    // 发送读A矩阵请求
    sendMemoryRead(status.A_address, status.A_count * status.A_step);
    
    // 发送读B矩阵请求
    sendMemoryRead(status.B_address, status.B_count * status.B_step);
    
    // 发送读C向量请求
    sendMemoryRead(status.C_address, status.A_count * sizeof(int32_t));
    
    // 数据处理和计算
    preprocess();
    systolicArrayExecute();
    accumulateResults();
    
    // 最后一个flow时进行后处理和写回
    if (status.flow_i == config.flow_loop_times - 1) {
        addBiasC();
        dequantize();
        transposeOutput();
        updateOutputMatrix();
        
        // 发送写回请求
        std::vector<int32_t> output_data;
        for (const auto &row : output_matrix.matrix) {
            output_data.insert(output_data.end(), row.begin(), row.end());
        }
        sendMemoryWrite(status.D_address, output_data);
    }
    
    status.flow_i++;
    
    // 继续调度下一个flow
    schedule(executeFlowEvent, clockEdge(Cycles(10)));
}

// ============================================================================
// 内存响应处理
// ============================================================================

void
KuiSau::handleResponse(PacketPtr pkt)
{
    // 处理来自内存的读响应
    if (!pkt->isRead()) {
        // 写响应，无需处理
        return;
    }
    
    const unsigned sz = pkt->getSize();
    const uint8_t *data = pkt->getConstPtr<uint8_t>();
    
    if (!data) {
        warn("%s: Response payload ptr is null", name());
        return;
    }
    
    // 将响应数据转换为int32_t向量
    std::vector<int32_t> response_data;
    for (unsigned i = 0; i < sz; i += sizeof(int32_t)) {
        uint32_t val = 0;
        for (unsigned j = 0; j < sizeof(int32_t) && (i + j) < sz; ++j) {
            val |= (data[i + j] << (j * 8));
        }
        response_data.push_back(static_cast<int32_t>(val));
    }
    
    // 根据当前地址判断这是哪个矩阵的数据
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
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    sendPacket(pkt);
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
    panic_if(blockedPacket != nullptr, "MemSidePort: already blocked!");
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        isBlocked = true;
        return;
    }
    isBlocked = false;
}

// CSR side port: accepts CPU CSR accesses and decodes SAU instructions
void
KuiSau::processCsrPacket(PacketPtr pkt, bool isAtomic)
{
    if (isAtomic)
        pkt->makeAtomicResponse();
    else
        pkt->makeResponse();

    const unsigned size = pkt->getSize();
    if (size != sizeof(uint32_t)) {
        warn("%s: CSR access size %u unexpected (expect 4)", name(), size);
    }

    if (pkt->isWrite()) {
        if (!pkt->hasData()) {
            warn("%s: CSR write without data", name());
            return;
        }
        
        uint32_t data = pkt->getLE<uint32_t>();
        Addr addr = pkt->getAddr();
        
        // 根据CSR地址解码指令
        // SAU有8个CSR寄存器，地址范围通常是csrAddrRange定义的
        // 这里我们假设偏移量映射到CSR寄存器
        
        unsigned offset = addr & 0xFF;  // 获取偏移量
        
        std::cout << "[KuiSau] CSR Write @0x" << std::hex << offset 
                  << " value=0x" << data << std::dec << std::endl;
        
        // 将数据写入对应的CSR寄存器
        switch (offset) {
            case 0x00: csr.ins1_msb = data; break;
            case 0x04: csr.ins1_lsb = data; break;
            case 0x08: csr.ins2_msb = data; break;
            case 0x0C: csr.ins2_lsb = data; break;
            case 0x10: csr.ins3_msb = data; break;
            case 0x14: csr.ins3_lsb = data; break;
            case 0x18: csr.ins4_msb = data; break;
            case 0x1C:
                csr.ins4_lsb = data;
                // 最后一个寄存器写入时，触发指令解码和执行
                inform("%s: All CSR registers written, triggering execution", name());
                updateCsrFromRegisters();
                
                // 初始化矩阵缓冲区
                D_matrix = std::vector<std::vector<int32_t>>(unitSize,
                                                             std::vector<int32_t>(unitSize, 0));
                
                // 调度第一个flow执行
                if (status.running) {
                    schedule(executeFlowEvent, clockEdge(Cycles(5)));
                }
                break;
            default:
                warn("%s: Unknown CSR offset 0x%x", name(), offset);
                break;
        }
        
        csrControlReg = data;
    } else if (pkt->isRead()) {
        // 读取CSR状态
        uint32_t readValue = 0;
        
        // 可以返回执行状态或其他信息
        if (status.running) {
            readValue |= (1 << 0);  // bit 0: running状态
        }
        readValue |= ((status.flow_i & 0xFF) << 1);  // bits 8:1: 当前flow索引
        
        pkt->setLE<uint32_t>(readValue);
        
        std::cout << "[KuiSau] CSR Read response=0x" << std::hex << readValue << std::dec << std::endl;
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
    const RequestorID req_id = 1;

    if (port_KuiSau_sendto_mem.isBlocked) {
        // Just reschedule if still blocked
        schedule(nextTickEvent, curTick() + cyclesToTicks(Cycles(schedule_interval)));
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
