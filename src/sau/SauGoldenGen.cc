#include "sau/SauGoldenGen.hh"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "mem/packet_access.hh"
#include "mem/request.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

namespace
{

constexpr Addr CsrBase = 0x2f000000;
constexpr Addr SauBase = 0x20000000;
constexpr Addr AOffset = 0x0000;
constexpr Addr BOffset = 0x1000;
constexpr Addr COffset = 0x2000;
constexpr Addr DOffset = 0x3000;
constexpr unsigned UnitSize = 16;
constexpr size_t MatrixBytes = UnitSize * UnitSize;
constexpr size_t ShiftMatrixBytes = MatrixBytes * 2;
constexpr size_t CBytes = UnitSize * 2;

std::vector<uint8_t>
makeZeroBytes(size_t size = MatrixBytes)
{
    return std::vector<uint8_t>(size, 0);
}

std::vector<uint8_t>
makeABytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        bytes[row * UnitSize + (UnitSize - 1 - row)] = 1;
    }
    return bytes;
}

std::vector<uint8_t>
makeBBytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            bytes[row * UnitSize + col] = col;
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeAllOnesABytes()
{
    return std::vector<uint8_t>(MatrixBytes, 1);
}

std::vector<uint8_t>
makeDequantBBytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            bytes[row * UnitSize + col] = col * 4;
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeDequantExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            const unsigned shifted = col * 16;
            bytes[row * UnitSize + col] = std::min(shifted, 127u);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeNegativeDequantBBytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            const int value = -static_cast<int>(col * 4);
            bytes[row * UnitSize + col] = static_cast<uint8_t>(value);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeNegativeDequantExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            const int shifted = -static_cast<int>(col * 16);
            const int saturated = std::max(shifted, -128);
            bytes[row * UnitSize + col] = static_cast<uint8_t>(saturated);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeMixedDequantBBytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            const int value = (col % 2 == 0) ?
                static_cast<int>(col * 4) :
                -static_cast<int>(col * 4);
            bytes[row * UnitSize + col] = static_cast<uint8_t>(value);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makePointwiseDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            const int dequantized = (col % 2 == 0) ?
                static_cast<int>(col) + 1 :
                1 - static_cast<int>(col);
            bytes[row * UnitSize + col] = static_cast<uint8_t>(dequantized);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeIncrementedBBytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            bytes[row * UnitSize + col] = col + 1;
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeOutputTransposeExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        const uint8_t value = UnitSize - 1 - row;
        for (unsigned col = 0; col < UnitSize; ++col) {
            bytes[row * UnitSize + col] = value;
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeRetainedGemmExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            bytes[row * UnitSize + col] = col * 2;
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvABytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    bytes[UnitSize - 1] = 1;
    return bytes;
}

std::vector<uint8_t>
makeConvStrideABytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    bytes[UnitSize + UnitSize - 1] = 1;
    return bytes;
}

std::vector<uint8_t>
makeRichConvABytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    auto set = [&bytes](unsigned row, unsigned col, int value) {
        bytes[row * UnitSize + col] = static_cast<uint8_t>(value);
    };

    set(0, 15, 1);
    set(0, 13, 2);
    set(1, 14, 3);
    set(1, 12, -1);
    set(2, 15, -2);
    set(2, 11, 1);
    set(3, 10, 2);
    set(3, 8, -1);
    set(4, 15, 1);
    set(4, 9, 2);
    set(5, 14, -1);
    set(5, 7, 1);

    return bytes;
}

std::vector<uint8_t>
makeRichConvBBytes()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned col = 0; col < UnitSize; ++col) {
            const int value =
                static_cast<int>(row + 1) * (static_cast<int>(col % 5) - 2);
            bytes[row * UnitSize + col] = static_cast<uint8_t>(value);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    for (unsigned col = 0; col < UnitSize; ++col) {
        bytes[col] = col;
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvRichExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    const uint8_t rows[][UnitSize] = {
        {10, 5, 0, 251, 246, 10, 5, 0, 251, 246, 10, 5, 0, 251, 246, 10},
        {248, 252, 0, 4, 8, 248, 252, 0, 4, 8, 248, 252, 0, 4, 8, 248},
        {252, 254, 0, 2, 4, 252, 254, 0, 2, 4, 252, 254, 0, 2, 4, 252},
        {248, 252, 0, 4, 8, 248, 252, 0, 4, 8, 248, 252, 0, 4, 8, 248},
        {250, 253, 0, 3, 6, 250, 253, 0, 3, 6, 250, 253, 0, 3, 6, 250},
    };
    for (unsigned row = 0; row < 5; ++row) {
        std::copy(rows[row], rows[row] + UnitSize,
                  bytes.begin() + row * UnitSize);
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvStrideExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    const unsigned row = UnitSize / 2;
    for (unsigned col = 0; col < UnitSize; ++col) {
        bytes[row * UnitSize + col] = col;
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvStrideShiftExpectedD()
{
    std::vector<uint8_t> bytes(ShiftMatrixBytes, 0);
    const unsigned first_row = UnitSize / 2;
    for (unsigned i = 0; i < UnitSize / 2; ++i) {
        bytes[first_row * UnitSize + 2 * i + 1] = i + UnitSize / 2;
    }
    for (unsigned value = 1; value < UnitSize / 2; ++value) {
        bytes[(first_row + 1) * UnitSize + 2 * value + 1] = value;
    }
    return bytes;
}

std::vector<uint8_t>
makeInt16UnitBiasExpectedD()
{
    std::vector<uint8_t> bytes(ShiftMatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize * 2; ++row) {
        for (unsigned col = 0; col < UnitSize; col += 2) {
            bytes[row * UnitSize + col] = 0;
            bytes[row * UnitSize + col + 1] = 1;
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvStrideShiftDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes = makeInt16UnitBiasExpectedD();
    const unsigned first_row = UnitSize / 2;
    const uint8_t row8[] = {
        0, 9, 255, 248, 0, 11, 255, 246,
        0, 13, 255, 244, 0, 15, 255, 242,
    };
    const uint8_t row9[] = {
        0, 1, 0, 0, 0, 3, 255, 254,
        0, 5, 255, 252, 0, 7, 255, 250,
    };
    std::copy(row8, row8 + sizeof(row8),
              bytes.begin() + first_row * UnitSize);
    std::copy(row9, row9 + sizeof(row9),
              bytes.begin() + (first_row + 1) * UnitSize);
    return bytes;
}

std::vector<uint8_t>
makeNormalConvShiftExpectedD()
{
    std::vector<uint8_t> bytes(ShiftMatrixBytes, 0);
    size_t idx = 0;
    for (unsigned value = 8; value < UnitSize; ++value) {
        bytes[idx++] = 0;
        bytes[idx++] = value;
    }
    for (unsigned value = 0; value < 8; ++value) {
        bytes[idx++] = 0;
        bytes[idx++] = value;
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 1);
    for (unsigned col = 0; col < UnitSize; ++col) {
        const int dequantized = (col % 2 == 0) ?
            static_cast<int>(col) + 1 :
            1 - static_cast<int>(col);
        bytes[col] = static_cast<uint8_t>(dequantized);
    }
    return bytes;
}

std::vector<uint8_t>
makeNormalConvStrideDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 1);
    const unsigned row = UnitSize / 2;
    for (unsigned col = 0; col < UnitSize; ++col) {
        const int dequantized = (col % 2 == 0) ?
            static_cast<int>(col) + 1 :
            1 - static_cast<int>(col);
        bytes[row * UnitSize + col] = static_cast<uint8_t>(dequantized);
    }
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    bytes[UnitSize - 1] = UnitSize - 1;
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvRichExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    const uint8_t row_values[] = {10, 248, 252, 248, 250};
    for (unsigned row = 0; row < sizeof(row_values) / sizeof(row_values[0]);
         ++row) {
        bytes[row * UnitSize + UnitSize - 1] = row_values[row];
    }
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvStrideExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 0);
    bytes[(UnitSize / 2) * UnitSize + UnitSize - 1] = UnitSize - 1;
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvStrideShiftExpectedD()
{
    std::vector<uint8_t> bytes(ShiftMatrixBytes, 0);
    bytes[(UnitSize / 2) * UnitSize + UnitSize - 1] = UnitSize - 1;
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvStrideShiftDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes = makeInt16UnitBiasExpectedD();
    bytes[(UnitSize / 2) * UnitSize + UnitSize - 2] = 255;
    bytes[(UnitSize / 2) * UnitSize + UnitSize - 1] =
        static_cast<uint8_t>(-14);
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvShiftExpectedD()
{
    std::vector<uint8_t> bytes(ShiftMatrixBytes, 0);
    bytes[UnitSize - 1] = UnitSize - 1;
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 1);
    bytes[UnitSize - 1] = static_cast<uint8_t>(-14);
    return bytes;
}

std::vector<uint8_t>
makeDepthwiseConvStrideDequantMixedExpectedD()
{
    std::vector<uint8_t> bytes(MatrixBytes, 1);
    bytes[(UnitSize / 2) * UnitSize + UnitSize - 1] =
        static_cast<uint8_t>(-14);
    return bytes;
}

std::vector<uint8_t>
makeUnitBiasBytes()
{
    std::vector<uint8_t> bytes(CBytes, 0);
    for (unsigned i = 0; i < UnitSize; ++i) {
        bytes[2 * i] = 0;
        bytes[2 * i + 1] = 1;
    }
    return bytes;
}

std::vector<uint8_t>
makeShiftABytes()
{
    std::vector<uint8_t> bytes(ShiftMatrixBytes, 0);
    for (unsigned row = 0; row < UnitSize; ++row) {
        const unsigned input_row = row < 8 ? 2 * row : 2 * row + 1;
        const unsigned col = row < 8 ? row : row - 8;
        bytes[input_row * UnitSize + (UnitSize - 1 - 2 * col)] = 1;
    }
    return bytes;
}

std::vector<uint8_t>
makeShiftExpectedD()
{
    std::vector<uint8_t> bytes;
    bytes.reserve(ShiftMatrixBytes);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned value = 8; value < UnitSize; ++value) {
            bytes.push_back(0);
            bytes.push_back(value);
        }
        for (unsigned value = 0; value < 8; ++value) {
            bytes.push_back(0);
            bytes.push_back(value);
        }
    }
    return bytes;
}

std::vector<uint8_t>
makeShiftDequantExpectedD()
{
    std::vector<uint8_t> bytes;
    bytes.reserve(ShiftMatrixBytes);
    for (unsigned row = 0; row < UnitSize; ++row) {
        for (unsigned value = 8; value < UnitSize; ++value) {
            const uint16_t shifted = value >> 2;
            bytes.push_back((shifted >> 8) & 0xFF);
            bytes.push_back(shifted & 0xFF);
        }
        for (unsigned value = 0; value < 8; ++value) {
            const uint16_t shifted = value >> 2;
            bytes.push_back((shifted >> 8) & 0xFF);
            bytes.push_back(shifted & 0xFF);
        }
    }
    return bytes;
}

} // anonymous namespace

void
SauGoldenGen::GenPort::recvReqRetry()
{
    panic_if(blockedPacket == nullptr, "%s: retry without blocked packet",
             name());
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    sendPacket(pkt);
}

bool
SauGoldenGen::GenPort::recvTimingResp(PacketPtr pkt)
{
    owner->handleResponse(role, pkt);
    delete pkt;
    return true;
}

void
SauGoldenGen::GenPort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "%s: port already blocked", name());
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

SauGoldenGen::SauGoldenGen(const SauGoldenGenParams &params)
    : SimObject(params),
      memPort(params.name + ".mem_port", this, PortRole::Mem),
      csrPort(params.name + ".csr_port", this, PortRole::Csr),
      stepEvent([this] { issueNext(); }, name()),
      clockDomain(params.clk_domain),
      system(params.system),
      memRequestorId(params.system->getRequestorId(this, "mem")),
      csrRequestorId(params.system->getRequestorId(this, "csr")),
      interval(params.interval),
      pollInterval(params.poll_interval),
      maxBusyPolls(params.max_busy_polls),
      testCase(params.test_case)
{
    fatal_if(!clockDomain, "%s: ClockDomain must be set", name());
    fatal_if(!system, "%s: System must be set", name());
    buildScript();

    inform("%s created:", name());
    inform("  Clock period: %lld ticks", getClockPeriod());
    inform("  Scripted actions: %llu",
           static_cast<unsigned long long>(actions.size()));
    inform("  Test case: %s", testCase.c_str());
}

Tick
SauGoldenGen::clockEdge(Cycles cycles) const
{
    Tick tick = curTick();
    const Tick period = getClockPeriod();
    const Tick tickInCycle = tick % period;
    if (tickInCycle != 0) {
        tick += period - tickInCycle;
    }
    return tick + cyclesToTicks(cycles);
}

void
SauGoldenGen::startup()
{
    scheduleStep(Cycles(0));
}

void
SauGoldenGen::buildScript()
{
    std::vector<uint8_t> aBytes;
    const std::vector<uint8_t> bBytes = makeBBytes();
    std::vector<uint8_t> cBytes;
    size_t dBytes = MatrixBytes;
    std::vector<uint8_t> zeros = makeZeroBytes();
    std::vector<uint8_t> expectedD;
    std::vector<uint8_t> secondExpectedD;
    std::vector<uint8_t> activeBBytes = bBytes;
    uint32_t ins4Msb = 0x01000000;
    uint32_t ins1Msb = 0x00000000;
    uint32_t ins1Lsb = 0x00000000;
    uint32_t ins2Lsb = 0x00010101;
    bool writeC = false;
    bool retainSequence = false;
    bool traceReplayOnly = false;

    if (testCase == "gemm") {
        // This vector is intentionally simple but still exercises the SAU.py
        // data path: A memory fliplr -> identity, systolic transposes A ->
        // identity, B memory fliplr -> B_matrix, and output fliplr restores
        // the B bytes.
        aBytes = makeABytes();
        expectedD = bBytes;
    } else if (testCase == "gemm_out_transpose") {
        // flow_mode=1 transposes the dequantized output before byte packing.
        // With identity A and the fixed B row pattern, packed D becomes rows
        // filled with 15, 14, ..., 0.
        aBytes = makeABytes();
        expectedD = makeOutputTransposeExpectedD();
        ins4Msb |= 1u << 22;
    } else if (testCase == "gemm_retain") {
        // The first instruction uses flow_mode=2, so D_matrix must survive
        // after output. A second normal GEMM then accumulates another copy of
        // the same output and finally clears it.
        aBytes = makeABytes();
        expectedD = bBytes;
        secondExpectedD = makeRetainedGemmExpectedD();
        ins4Msb |= 2u << 22;
        retainSequence = true;
    } else if (testCase == "gemm_transpose_retain") {
        // flow_mode=3 combines output transpose with retain. The first output
        // should match the transpose vector, then the second normal GEMM proves
        // the untransposed accumulated D state survived and can be cleared.
        aBytes = makeABytes();
        expectedD = makeOutputTransposeExpectedD();
        secondExpectedD = makeRetainedGemmExpectedD();
        ins4Msb |= 3u << 22;
        retainSequence = true;
    } else if (testCase == "gemm_dequant") {
        // All-one A rows accumulate 16 copies of each B column. With B memory
        // bytes 0,4,8,...,60 and cutbit=2, output bytes become
        // 0,16,32,...,112,127,...,127, proving right-shift plus int8
        // saturation.
        aBytes = makeAllOnesABytes();
        activeBBytes = makeDequantBBytes();
        expectedD = makeDequantExpectedD();
        ins1Msb = 2u << 2;
    } else if (testCase == "gemm_dequant_negative") {
        // Same cutbit=2 structure as gemm_dequant, but B memory bytes encode
        // 0,-4,-8,...,-60. The expected bytes prove arithmetic right shift
        // and saturation to -128 before uint8 output packing.
        aBytes = makeAllOnesABytes();
        activeBBytes = makeNegativeDequantBBytes();
        expectedD = makeNegativeDequantExpectedD();
        ins1Msb = 2u << 2;
    } else if (testCase == "normal_conv") {
        // conv_kernel=2 with register_mode=0 exercises normal convolution
        // staging. A contains one nonzero lane that survives convMatShift into
        // A_matrix[0][0], so the first output row follows B row 0 and the
        // remaining rows stay zero after SAU.py-style output packing.
        aBytes = makeNormalConvABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeNormalConvExpectedD();
        writeC = true;
        ins1Lsb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (1u << 24);
    } else if (testCase == "normal_conv_rich") {
        // Keep the base normal-conv mode, but use multiple signed A lanes across
        // the six conv read rows and distinct signed B kernel rows. This proves
        // the convMatShift row merge and int8 signed matmul beyond a one-hot lane.
        aBytes = makeRichConvABytes();
        activeBBytes = makeRichConvBBytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeNormalConvRichExpectedD();
        writeC = true;
        ins1Lsb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (1u << 24);
    } else if (testCase == "normal_conv_stride") {
        // stride=1 expands the conv A window to three rows. Placing the
        // one-hot lane in input row 1 proves the stride merge moves the output
        // to D row 8 instead of matching the base normal_conv row-0 vector.
        aBytes = makeConvStrideABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeNormalConvStrideExpectedD();
        writeC = true;
        ins1Lsb = (2u << 2) | (1u << 4);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (1u << 24);
    } else if (testCase == "normal_conv_stride_shift") {
        // Combine stride=1 and shift_mode=1. The row-1 one-hot lane drives the
        // normal-conv int16 packing path into output rows 8/9, proving the
        // stride merge composes with the 512-byte D layout.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeConvStrideABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeNormalConvStrideShiftExpectedD();
        writeC = true;
        ins1Lsb = (2u << 2) | (1u << 4) | (1u << 5);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 24);
    } else if (testCase == "normal_conv_stride_shift_dequant_mixed") {
        // This composes stride=1, shift_mode=1, and work_mode=2/cutbit=2.
        // The row-1 one-hot lane moves mixed signed B data into output rows
        // 8/9 while inactive int16 lanes retain the all-one C bias.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeConvStrideABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeNormalConvStrideShiftDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = (2u << 2) | (1u << 4) | (1u << 5);
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 20) | (2u << 24);
    } else if (testCase == "normal_conv_shift") {
        // This keeps the normal-conv one-hot A setup but enables
        // shift_mode=1. The A tile enters the int16 convMatShift path and
        // produces only the first int16 output row after SAU.py packing.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeNormalConvABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeNormalConvShiftExpectedD();
        writeC = true;
        ins1Lsb = (2u << 2) | (1u << 5);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 24);
    } else if (testCase == "normal_conv_dequant_mixed") {
        // This extends the normal-conv staging vector with work_mode=2 and
        // cutbit=2. The one-hot A selects B row 0 for the first output row,
        // while all-one C bias is shifted into accumulator scale before
        // signed dequantization; inactive output rows therefore become 1.
        aBytes = makeNormalConvABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeNormalConvDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = 2u << 2;
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 20) | (1u << 24);
    } else if (testCase == "normal_conv_stride_dequant_mixed") {
        // Combine stride=1 with work_mode=2/cutbit=2. The row-1 one-hot lane
        // moves the signed mixed B row to output row 8 while all inactive rows
        // remain the all-one C bias after dequantization.
        aBytes = makeConvStrideABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeNormalConvStrideDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = (2u << 2) | (1u << 4);
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 20) | (1u << 24);
    } else if (testCase == "depthwise_conv") {
        // register_mode=2 applies the DW convolution B mask. For flow_k=0,
        // only the first preprocessed B column survives; with the one-hot A
        // layout the packed D has only the last byte of row 0 set to 15.
        aBytes = makeNormalConvABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeDepthwiseConvExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (1u << 24);
    } else if (testCase == "depthwise_conv_rich") {
        // Reuse the rich normal-conv A/B data under register_mode=2. The DW mask
        // keeps only the selected B column, so multiple output rows should survive
        // while all other packed columns remain zero.
        aBytes = makeRichConvABytes();
        activeBBytes = makeRichConvBBytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeDepthwiseConvRichExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (1u << 24);
    } else if (testCase == "depthwise_conv_stride") {
        // The same stride=1 A placement as normal_conv_stride, combined with
        // the DW B-column mask, leaves only the last byte of output row 8 set.
        aBytes = makeConvStrideABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeDepthwiseConvStrideExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2) | (1u << 4);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (1u << 24);
    } else if (testCase == "depthwise_conv_stride_shift") {
        // The combined stride=1 plus shift_mode=1 path should still preserve
        // the DW B-column mask, leaving only byte 15 of packed output row 8.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeConvStrideABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeDepthwiseConvStrideShiftExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2) | (1u << 4) | (1u << 5);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 24);
    } else if (testCase == "depthwise_conv_stride_shift_dequant_mixed") {
        // Compose stride=1, shift_mode=1, work_mode=2/cutbit=2, and the DW
        // B mask. All lanes keep the int16 C bias except row 8 byte pair 14/15.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeConvStrideABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeDepthwiseConvStrideShiftDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2) | (1u << 4) | (1u << 5);
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 20) | (2u << 24);
    } else if (testCase == "depthwise_conv_shift") {
        // register_mode=2 applies the DW B-column mask while shift_mode=1
        // routes A through the int16 convMatShift path and writes 512 D bytes.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeNormalConvABytes();
        cBytes = makeZeroBytes(CBytes);
        expectedD = makeDepthwiseConvShiftExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2) | (1u << 5);
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 24);
    } else if (testCase == "depthwise_conv_dequant_mixed") {
        // This extends the DW mask vector with work_mode=2 and cutbit=2. The
        // retained B column contains -60 for the active lane, while all-one C
        // bias shifts into accumulator scale before signed dequantization.
        aBytes = makeNormalConvABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeDepthwiseConvDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2);
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 20) | (1u << 24);
    } else if (testCase == "depthwise_conv_stride_dequant_mixed") {
        // Combine stride=1 with work_mode=2/cutbit=2 under the DW mask. The
        // only non-bias output byte is row 8 byte 15, which packs -14.
        aBytes = makeConvStrideABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeDepthwiseConvStrideDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = 2u | (2u << 2) | (1u << 4);
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = COffset | (2u << 20) | (1u << 24);
    } else if (testCase == "add") {
        // Matrix-add mode still uses the same preprocess/output byte order.
        // Zero A makes the expected output equal to B after the final fliplr.
        aBytes = zeros;
        expectedD = bBytes;
        ins4Msb |= 3u << 20;
    } else if (testCase == "pointwise") {
        // Pointwise convolution uses conv_kernel=1 and enables C reads. With
        // A as identity and C as an all-ones int16 bias vector, the output
        // should be the B byte pattern incremented by one after packing.
        aBytes = makeABytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makeIncrementedBBytes();
        writeC = true;
        ins1Lsb = 1u << 2;
        ins4Msb = COffset | (1u << 20) | (1u << 24);
    } else if (testCase == "pointwise_dequant_mixed") {
        // work_mode=2 shifts C into accumulator scale before dequantization.
        // With cutbit=2, all-one C bias, identity A, and alternating signed B
        // columns, the packed output proves pointwise C+bias and signed
        // dequantization compose correctly.
        aBytes = makeABytes();
        activeBBytes = makeMixedDequantBBytes();
        cBytes = makeUnitBiasBytes();
        expectedD = makePointwiseDequantMixedExpectedD();
        writeC = true;
        ins1Lsb = 1u << 2;
        ins1Msb = 2u << 2;
        ins4Msb = COffset | (2u << 20) | (1u << 24);
    } else if (testCase == "gemm_shift") {
        // shift_mode=1 reads two A segments per logical row. This A layout
        // becomes a 16x16 int16 identity matrix after SAU.py's fliplr/view/
        // reshape path, so the expected D is B packed as int16 output bytes.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeShiftABytes();
        expectedD = makeShiftExpectedD();
        ins1Lsb = 1u << 5;
        ins2Lsb = 0x00010201;
        ins4Msb = 0x02000000;
    } else if (testCase == "gemm_dequant_int16") {
        // This reuses the shift-mode int16 identity layout and adds cutbit=2.
        // The expected D keeps the same int16 output byte order as gemm_shift,
        // but each value is arithmetically shifted before packing.
        dBytes = ShiftMatrixBytes;
        zeros = makeZeroBytes(dBytes);
        aBytes = makeShiftABytes();
        expectedD = makeShiftDequantExpectedD();
        ins1Lsb = 1u << 5;
        ins1Msb = 2u << 2;
        ins2Lsb = 0x00010201;
        ins4Msb = 0x02000000;
    } else if (testCase == "lkssfull_sau_stdconv_10_trace") {
        // Trace-only driver for the external RTL smoke case. This emits the
        // 16 normconv-like CSR starts observed in firmware; KuiSau's trace
        // replay mode turns each start into one captured RTL request flow.
        traceReplayOnly = true;
    } else {
        fatal("%s: unsupported golden test case '%s'", name(),
              testCase.c_str());
    }

    if (!traceReplayOnly) {
        actions.push_back({ActionType::MemWrite, SauBase + AOffset, aBytes});
        actions.push_back({ActionType::MemWrite, SauBase + BOffset,
                           activeBBytes});
        if (writeC) {
            actions.push_back(
                {ActionType::MemWrite, SauBase + COffset, cBytes});
        }
        actions.push_back({ActionType::MemWrite, SauBase + DOffset, zeros});
    }

    auto appendInstructionWords = [this, dBytes](uint32_t ins1_lsb,
                                                 uint32_t ins1_msb,
                                                 uint32_t ins2_lsb,
                                                 uint32_t ins2_msb,
                                                 uint32_t ins3_lsb,
                                                 uint32_t ins3_msb,
                                                 uint32_t ins4_lsb,
                                                 uint32_t ins4_msb,
                                                 bool readD) {
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x200, {}, ins1_lsb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x201, {}, ins1_msb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x202, {}, ins2_lsb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x203, {}, ins2_msb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x204, {}, ins3_lsb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x205, {}, ins3_msb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x207, {}, ins4_msb});
        actions.push_back(
            {ActionType::CsrWrite, CsrBase + 0x206, {}, ins4_lsb});
        actions.push_back(
            {ActionType::CsrReadBusy, CsrBase + 0x206, {}, 0, 0});
        if (readD) {
            const Addr d_addr = SauBase + ((ins4_lsb >> 9) & 0xFFFFF);
            actions.push_back(
                {ActionType::MemReadD, d_addr, {}, 0, dBytes});
        }
    };

    if (traceReplayOnly) {
        for (uint32_t flow = 0; flow < 16; ++flow) {
            const uint32_t d_offset = 0x22c20 + flow * 0x20;
            appendInstructionWords(
                0x0000002c, 0x00000088, 0x00020401, 0x00101209,
                0x00025030, 0x00024c30 + flow * 0x40,
                (d_offset << 9) | 0x1, 0x0e525010, false);
        }
        actions.push_back({ActionType::Exit, 0, {}});
    } else {
        appendInstructionWords(
            ins1Lsb, ins1Msb, ins2Lsb, 0x00010001,
            BOffset, AOffset, (DOffset << 9) | 0x1, ins4Msb, true);
        expectedDReads.push_back(expectedD);
    }
    if (retainSequence) {
        appendInstructionWords(
            0x00000000, 0x00000000, 0x00010101, 0x00010001,
            BOffset, AOffset, (DOffset << 9) | 0x1, 0x01000000, true);
        expectedDReads.push_back(secondExpectedD);
    }
}

void
SauGoldenGen::scheduleStep(Cycles delay)
{
    if (!stepEvent.scheduled()) {
        schedule(stepEvent, clockEdge(delay));
    }
}

void
SauGoldenGen::issueNext()
{
    if (requestInFlight) {
        return;
    }

    if (actions.empty()) {
        return;
    }

    const Action &action = actions.front();
    if (action.type == ActionType::Exit) {
        actions.pop_front();
        exitSimLoop("SAU golden " + testCase + " trace replay done", 0);
        return;
    }

    requestInFlight = true;

    switch (action.type) {
      case ActionType::MemWrite:
        sendMemWrite(action.addr, action.data);
        break;
      case ActionType::MemReadD:
        sendMemRead(action.addr, action.size);
        break;
      case ActionType::CsrWrite:
        sendCsrWrite(action.addr, action.value);
        break;
      case ActionType::CsrReadBusy:
        sendCsrRead(action.addr);
        break;
      case ActionType::Exit:
        break;
    }
}

void
SauGoldenGen::sendMemWrite(Addr addr, const std::vector<uint8_t> &data)
{
    RequestPtr req = std::make_shared<Request>(
        addr, data.size(), Request::PHYSICAL, memRequestorId);
    PacketPtr pkt = Packet::createWrite(req);
    pkt->allocate();
    std::memcpy(pkt->getPtr<uint8_t>(), data.data(), data.size());

    std::cout << "[SauGoldenGen] MemWrite @0x" << std::hex << addr
              << " size=" << std::dec << data.size() << std::endl;
    memPort.sendPacket(pkt);
}

void
SauGoldenGen::sendMemRead(Addr addr, size_t size)
{
    RequestPtr req = std::make_shared<Request>(
        addr, size, Request::PHYSICAL, memRequestorId);
    PacketPtr pkt = Packet::createRead(req);
    pkt->allocate();

    std::cout << "[SauGoldenGen] MemRead D @0x" << std::hex << addr
              << " size=" << std::dec << size << std::endl;
    memPort.sendPacket(pkt);
}

void
SauGoldenGen::sendCsrWrite(Addr addr, uint32_t value)
{
    RequestPtr req = std::make_shared<Request>(
        addr, sizeof(uint32_t), Request::PHYSICAL, csrRequestorId);
    PacketPtr pkt = Packet::createWrite(req);
    pkt->allocate();
    pkt->setLE<uint32_t>(value);

    std::cout << "[SauGoldenGen] CsrWrite @0x" << std::hex << addr
              << " value=0x" << value << std::dec << std::endl;
    csrPort.sendPacket(pkt);
}

void
SauGoldenGen::sendCsrRead(Addr addr)
{
    RequestPtr req = std::make_shared<Request>(
        addr, sizeof(uint32_t), Request::PHYSICAL, csrRequestorId);
    PacketPtr pkt = Packet::createRead(req);
    pkt->allocate();

    csrPort.sendPacket(pkt);
}

void
SauGoldenGen::handleResponse(PortRole role, PacketPtr pkt)
{
    panic_if(actions.empty(), "%s: unexpected response", name());
    const Action &action = actions.front();

    if (role == PortRole::Csr && action.type == ActionType::CsrReadBusy) {
        handleBusyRead(pkt->getLE<uint32_t>());
        return;
    }

    if (role == PortRole::Mem && action.type == ActionType::MemReadD) {
        checkD(pkt->getConstPtr<uint8_t>(), pkt->getSize());
        actions.pop_front();
        requestInFlight = false;
        if (actions.empty()) {
            exitSimLoop("SAU golden " + testCase + " test passed", 0);
        } else {
            scheduleStep(interval);
        }
        return;
    }

    actions.pop_front();
    requestInFlight = false;
    scheduleStep(interval);
}

void
SauGoldenGen::handleBusyRead(uint32_t value)
{
    const bool busy = (value & (1u << 31)) != 0;
    std::cout << "[SauGoldenGen] Busy poll value=0x" << std::hex << value
              << " busy=" << busy << std::dec << std::endl;

    requestInFlight = false;

    if (busy) {
        busyPolls++;
        panic_if(busyPolls > maxBusyPolls,
                 "%s: SAU remained busy after %u polls", name(), busyPolls);
        scheduleStep(pollInterval);
        return;
    }

    actions.pop_front();
    busyPolls = 0;
    scheduleStep(interval);
}

void
SauGoldenGen::checkD(const uint8_t *data, size_t size)
{
    panic_if(expectedDReads.empty(), "%s: no expected D vector queued", name());
    const std::vector<uint8_t> &expectedD = expectedDReads.front();

    panic_if(size != expectedD.size(),
             "%s: D read size mismatch got %llu expected %llu",
             name(), static_cast<unsigned long long>(size),
             static_cast<unsigned long long>(expectedD.size()));
    panic_if(data == nullptr, "%s: D read returned null payload", name());

    for (size_t i = 0; i < expectedD.size(); ++i) {
        panic_if(data[i] != expectedD[i],
                 "%s: D mismatch at byte %llu got 0x%x expected 0x%x",
                 name(), static_cast<unsigned long long>(i), data[i],
                 expectedD[i]);
    }

    std::cout << "[SauGoldenGen] D output matches SAU.py-derived "
              << testCase << " vector" << std::endl;
    expectedDReads.pop_front();
}

Port &
SauGoldenGen::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_port") {
        return memPort;
    }
    if (if_name == "csr_port") {
        return csrPort;
    }
    return SimObject::getPort(if_name, idx);
}

} // namespace gem5
