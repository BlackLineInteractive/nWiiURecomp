#include "nwiiu/recomp/native_generator.h"

#include "runtime/executor.h"
#include "test_support.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwiiu::recomp::generate_native_block;
using nwiiu::recomp::generate_native_function;

constexpr uint32_t kProgramAddress = 0x00001000;
constexpr uint32_t kDataAddress = 0x00002000;
constexpr uint32_t kDataSize = 0x100;

constexpr uint32_t d_form(uint32_t opcode, uint32_t rt, uint32_t ra,
                          uint32_t immediate) {
    return (opcode << 26) | (rt << 21) | (ra << 16) |
           (immediate & 0xFFFF);
}

constexpr uint32_t x_form(uint32_t opcode, uint32_t rt, uint32_t ra,
                          uint32_t rb, uint32_t xo, bool rc = false) {
    return (opcode << 26) | (rt << 21) | (ra << 16) | (rb << 11) |
           (xo << 1) | static_cast<uint32_t>(rc);
}

constexpr uint32_t a_form(uint32_t opcode, uint32_t frt, uint32_t fra,
                          uint32_t frb, uint32_t frc, uint32_t xo) {
    return (opcode << 26) | (frt << 21) | (fra << 16) | (frb << 11) |
           (frc << 6) | (xo << 1);
}

constexpr uint32_t xl_form(uint32_t bo, uint32_t bi, uint32_t xo,
                           bool link = false) {
    return (19U << 26) | (bo << 21) | (bi << 16) | (xo << 1) |
           static_cast<uint32_t>(link);
}

constexpr uint32_t rlwinm(uint32_t rs, uint32_t ra, uint32_t sh, uint32_t mb,
                          uint32_t me, bool rc = false) {
    return (21U << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) |
           (me << 1) | static_cast<uint32_t>(rc);
}

constexpr uint32_t rlwimi(uint32_t rs, uint32_t ra, uint32_t sh, uint32_t mb,
                          uint32_t me, bool rc = false) {
    return (20U << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) |
           (me << 1) | static_cast<uint32_t>(rc);
}

constexpr uint32_t spr_form(uint32_t rs_rt, uint32_t spr, uint32_t xo) {
    return (31U << 26) | (rs_rt << 21) | ((spr & 0x1F) << 16) |
           ((spr >> 5) << 11) | (xo << 1);
}
inline constexpr std::array<uint32_t, 5> kProgram{
    0x38600005, // addi r3,r0,5
    0x90610000, // stw  r3,0(r1)
    0x80810000, // lwz  r4,0(r1)
    0x38840007, // addi r4,r4,7
    0x4E800020, // blr
};

inline constexpr std::array kSurfaceProgram{
    x_form(4, 1, 2, 3, 592),
    d_form(7, 6, 4, 0xFFFE),
    d_form(10, 4, 4, 1),
    d_form(11, 8, 4, 0xFFFF),
    d_form(12, 6, 4, 1),
    d_form(13, 6, 4, 1),
    d_form(14, 6, 4, 1),
    d_form(15, 6, 4, 1),
    xl_form(0, 0, 150),
    rlwinm(4, 6, 8, 8, 23, true),
    d_form(24, 4, 6, 0x00F0),
    d_form(26, 4, 6, 0xFFFF),
    x_form(31, 4, 4, 5, 0),
    x_form(31, 0x04, 4, 5, 4),
    x_form(31, 6, 1, 2, 23),
    x_form(31, 8, 4, 5, 32),
    x_form(31, 6, 4, 5, 40, true),
    x_form(31, 4, 6, 5, 124, true),
    x_form(31, 6, 4, 5, 136, true),
    x_form(31, 4, 1, 2, 151),
    x_form(31, 6, 4, 5, 235, true),
    x_form(31, 6, 4, 5, 266, true),
    spr_form(6, 8, 339),
    x_form(31, 4, 6, 5, 444, true),
    spr_form(6, 9, 467),
    x_form(31, 4, 6, 4, 824, true),
    x_form(31, 3, 1, 2, 983),
    d_form(32, 6, 1, 8),
    d_form(34, 6, 1, 9),
    d_form(36, 4, 1, 0x10),
    d_form(38, 4, 1, 0x14),
    d_form(44, 4, 1, 0x16),
    d_form(46, 30, 1, 0x20),
    d_form(47, 30, 1, 0x28),
    d_form(48, 4, 1, 0x30),
    d_form(50, 5, 1, 0x38),
    d_form(52, 4, 1, 0x50),
    d_form(54, 5, 1, 0x58),
    x_form(59, 6, 2, 3, 20),
    x_form(63, 8, 2, 3, 0),
    x_form(63, 7, 0, 2, 12),
    x_form(63, 8, 0, 2, 15),
    x_form(63, 9, 2, 3, 20),
    x_form(63, 10, 0, 2, 72),
    d_form(33, 6, 1, 0x40),
    d_form(35, 6, 1, 4),
    d_form(37, 4, 1, 4),
    d_form(39, 4, 1, 1),
    d_form(27, 4, 14, 0x8000),
    x_form(31, 11, 1, 4, 597),
    x_form(31, 11, 1, 4, 725),
    x_form(31, 12, 14, 0, 954, true),
    d_form(45, 4, 20, 2),
    d_form(49, 19, 21, 4),
    a_form(59, 14, 2, 3, 0, 18),
    a_form(59, 15, 2, 3, 0, 21),
    a_form(59, 16, 2, 0, 3, 25),
    a_form(59, 17, 2, 3, 4, 29),
    x_form(63, 18, 0, 2, 40),
    d_form(8, 22, 4, 5),
    rlwimi(4, 22, 16, 8, 15, true),
    d_form(25, 4, 22, 0xFFFF),
    x_form(31, 4, 22, 5, 24, true),
    x_form(31, 23, 4, 5, 11, true),
    x_form(31, 24, 4, 5, 459, true),
    x_form(31, 4, 25, 5, 536, true),
    uint32_t{0x7C845814},
    uint32_t{0xFDA06B3A},
    uint32_t{0x7D6BFA78},
    uint32_t{0xFC200A10},
    uint32_t{0xFC1E02F2},
    uint32_t{0xFFC0F01C},
    uint32_t{0x7D7B42AE},
    uint32_t{0x7D2B0734},
    uint32_t{0xDDA904E8},
    uint32_t{0x7FA7DB2E},
    uint32_t{0xFD496A3C},
    uint32_t{0x7D6900AE},
    uint32_t{0xFC210024},
    uint32_t{0xFDAA682A},
    uint32_t{0x7C630194},
    x_form(31, 26, 26, 0, 714, true),
    uint32_t{0x7CE433D6},
    uint32_t{0x4E800020},
};

inline constexpr std::array<uint32_t, 2> kSubfzeProgram{
    uint32_t{0x7CC00191},
    uint32_t{0x4E800020},
};

inline constexpr std::array<uint32_t, 2> kSubfzeoProgram{
    x_form(31, 3, 3, 0, 712, true),
    uint32_t{0x4E800020},
};
inline constexpr std::array<uint32_t, 2> kDivwoProgram{
    uint32_t{0x7CE437D7},
    uint32_t{0x4E800020},
};
static_assert(x_form(31, 7, 4, 6, 1003, true) == kDivwoProgram[0]);


inline constexpr std::array<uint32_t, 1> kBcProgram{0x41820008};
inline constexpr std::array<uint32_t, 1> kBProgram{0x48000008};
inline constexpr std::array<uint32_t, 1> kBctrProgram{
    xl_form(20, 0, 528)};
// Reached crxor 6,6,6 (0x4CC63182): opcode 19, XO 193, clears CR bit 6.
inline constexpr std::array<uint32_t, 2> kCrxorProgram{
    uint32_t{0x4CC63182},
    uint32_t{0x4E800020},
};
// Exact reached fnmsubs f18,f13,f23,f18 (0xEE4D95FC): opcode 59, XO5 30,
// single-precision negative multiply-subtract with one fused rounding.
inline constexpr std::array<uint32_t, 2> kFnmsubsProgram{
    uint32_t{0xEE4D95FC},
    uint32_t{0x4E800020},
};
// Exact reached fmsubs f0,f9,f1,f10 (0xEC095078): opcode 59, XO5 28,
// single-precision multiply-subtract that fuses fC before the single rounding.
// The old emit else-branch assumed fdivs/fadds/fsubs and would silently emit
// fsubs here, dropping fC entirely.
inline constexpr std::array<uint32_t, 2> kFmsubsProgram{
    uint32_t{0xEC095078},
    uint32_t{0x4E800020},
};
static_assert(a_form(59, 0, 9, 10, 1, 28) == 0xEC095078);
// Reached paired-single ops: ps_muls0, ps_mul, ps_madd, ps_sum0, the exact
// reached ps_add f0,f7,f9 (0x1007482A), ps_sub, ps_msub, ps_merge11,
// ps_merge01, and ps_neg (distinct registers so earlier result assertions are
// not clobbered). Each has a natively-emitted sibling; the arithmetic forms
// were rejected before Task 8.
inline constexpr std::array<uint32_t, 11> kPairedProgram{
    a_form(4, 5, 6, 0, 7, 12),     // ps_muls0 f5,f6,f7
    a_form(4, 8, 9, 0, 10, 25),    // ps_mul   f8,f9,f10
    a_form(4, 11, 12, 14, 13, 29), // ps_madd  f11,f12,f13,f14 (frc=13,frb=14)
    a_form(4, 15, 16, 18, 17, 10), // ps_sum0  f15,f16,f17,f18 (frc=17,frb=18)
    uint32_t{0x1007482A},          // ps_add   f0,f7,f9 (ra=7,rb=9,frc=0)
    a_form(4, 19, 20, 21, 0, 20),  // ps_sub   f19,f20,f21 (frc=0)
    a_form(4, 22, 23, 24, 25, 28), // ps_msub  f22,f23,f25,f24 (frc=25,frb=24)
    x_form(4, 26, 27, 28, 624),    // ps_merge11 f26,f27,f28
    x_form(4, 30, 29, 31, 560),    // ps_merge01 f30,f29,f31
    x_form(4, 1, 0, 2, 40),        // ps_neg   f1,f2 (fra reserved 0)
    uint32_t{0x4E800020},
};
static_assert(kPairedProgram[0] == 0x10A601D8);
static_assert(kPairedProgram[2] == 0x116C737A);
static_assert(kPairedProgram[4] == a_form(4, 0, 7, 9, 0, 21));
inline constexpr std::array<uint32_t, 63> kReachedWords{
    0xA3FC0028, 0xA7DD0002, 0xA8050000, 0xD7A80008, 0x7C000034,
    0x7D800026, 0x7EAB01AE, 0x7C87422E, 0x7D800038, 0x7C09526E,
    0x7CECD42E, 0x7DAC5C6E, 0x7D296078, 0x7C034D2E, 0x7D4700D0,
    0x7D404096, 0xFC2D006E, 0xFC205034, 0x4CC63242, 0x5F84203E,
    0x70E000FD, 0x75491028, 0xACE8FDE8, 0x7C003FEC, 0x7D29256E,
    0x7D2A0630, 0x7D6CD86E, 0x7CBB516E, 0x7FE82010, 0xE1810024,
    0xF381005C, 0xFC006910, 0xFD6D6EF8, 0x110D3420, 0x11AB68DC,
    0x118249DE, 0x100001D6, 0x118500DA, 0x1085312E, 0x10073040,
    0xECA06030, 0x7FE7336E, 0x7D006028, 0x7C00F06C, 0x7D8A4114,
    0x7CE0552C, 0x7ED461EE, 0x7D0618EE, 0x7C00642C, 0x7C0C5AEE,
    0x7C1E60AC, 0x7D62E3A6, 0xE0FDB000, 0xF166B000, 0xE4C40004,
    0x10C04034, 0x10EC583E, 0x112561FC, 0x7D4A3338, 0x7D83E3A6,
    0xF5850004, 0x7C04E3A6, 0x7C05E3A6,
};


extern "C" void native_fixture(CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory);
extern "C" void native_surface_fixture(CPUContext& cpu,
                                       nwii::runtime::GuestMemory& memory);
extern "C" void native_subfze_fixture(CPUContext& cpu,
                                      nwii::runtime::GuestMemory& memory);
extern "C" void native_subfzeo_fixture(CPUContext& cpu,
                                       nwii::runtime::GuestMemory& memory);
extern "C" void native_divwo_fixture(CPUContext& cpu,
                                     nwii::runtime::GuestMemory& memory);

extern "C" void native_bc_fixture(CPUContext& cpu,
                                  nwii::runtime::GuestMemory& memory);
extern "C" void native_b_fixture(CPUContext& cpu,
                                 nwii::runtime::GuestMemory& memory);
extern "C" void native_bctr_fixture(CPUContext& cpu,
                                    nwii::runtime::GuestMemory& memory);
extern "C" void native_crxor_fixture(CPUContext& cpu,
                                     nwii::runtime::GuestMemory& memory);
extern "C" void native_fnmsubs_fixture(CPUContext& cpu,
                                       nwii::runtime::GuestMemory& memory);
extern "C" void native_fmsubs_fixture(CPUContext& cpu,
                                      nwii::runtime::GuestMemory& memory);
extern "C" void native_paired_fixture(CPUContext& cpu,
                                      nwii::runtime::GuestMemory& memory);
#define DECLARE_WWHD_FIXTURE(index)                                          \
    extern "C" void native_wwhd_##index(                                    \
        CPUContext&, nwii::runtime::GuestMemory&);
DECLARE_WWHD_FIXTURE(00)
DECLARE_WWHD_FIXTURE(01)
DECLARE_WWHD_FIXTURE(02)
DECLARE_WWHD_FIXTURE(03)
DECLARE_WWHD_FIXTURE(04)
DECLARE_WWHD_FIXTURE(05)
DECLARE_WWHD_FIXTURE(06)
DECLARE_WWHD_FIXTURE(07)
DECLARE_WWHD_FIXTURE(08)
DECLARE_WWHD_FIXTURE(09)
DECLARE_WWHD_FIXTURE(10)
DECLARE_WWHD_FIXTURE(11)
DECLARE_WWHD_FIXTURE(12)
DECLARE_WWHD_FIXTURE(13)
DECLARE_WWHD_FIXTURE(14)
DECLARE_WWHD_FIXTURE(15)
DECLARE_WWHD_FIXTURE(16)
DECLARE_WWHD_FIXTURE(17)
DECLARE_WWHD_FIXTURE(18)
DECLARE_WWHD_FIXTURE(19)
DECLARE_WWHD_FIXTURE(20)
DECLARE_WWHD_FIXTURE(21)
DECLARE_WWHD_FIXTURE(22)
DECLARE_WWHD_FIXTURE(23)
DECLARE_WWHD_FIXTURE(24)
DECLARE_WWHD_FIXTURE(25)
DECLARE_WWHD_FIXTURE(26)
DECLARE_WWHD_FIXTURE(27)
DECLARE_WWHD_FIXTURE(28)
DECLARE_WWHD_FIXTURE(29)
DECLARE_WWHD_FIXTURE(30)
DECLARE_WWHD_FIXTURE(31)
DECLARE_WWHD_FIXTURE(32)
DECLARE_WWHD_FIXTURE(33)
DECLARE_WWHD_FIXTURE(34)
DECLARE_WWHD_FIXTURE(35)
DECLARE_WWHD_FIXTURE(36)
DECLARE_WWHD_FIXTURE(37)
DECLARE_WWHD_FIXTURE(38)
DECLARE_WWHD_FIXTURE(39)
DECLARE_WWHD_FIXTURE(40)
DECLARE_WWHD_FIXTURE(41)
DECLARE_WWHD_FIXTURE(42)
DECLARE_WWHD_FIXTURE(43)
DECLARE_WWHD_FIXTURE(44)
DECLARE_WWHD_FIXTURE(45)
DECLARE_WWHD_FIXTURE(46)
DECLARE_WWHD_FIXTURE(47)
DECLARE_WWHD_FIXTURE(48)
DECLARE_WWHD_FIXTURE(49)
DECLARE_WWHD_FIXTURE(50)
DECLARE_WWHD_FIXTURE(51)
DECLARE_WWHD_FIXTURE(52)
DECLARE_WWHD_FIXTURE(53)
DECLARE_WWHD_FIXTURE(54)
DECLARE_WWHD_FIXTURE(55)
DECLARE_WWHD_FIXTURE(56)
DECLARE_WWHD_FIXTURE(57)
DECLARE_WWHD_FIXTURE(58)
DECLARE_WWHD_FIXTURE(59)
DECLARE_WWHD_FIXTURE(60)
DECLARE_WWHD_FIXTURE(61)
DECLARE_WWHD_FIXTURE(62)
#undef DECLARE_WWHD_FIXTURE

inline constexpr std::array kReachedFixtures{
    native_wwhd_00, native_wwhd_01, native_wwhd_02, native_wwhd_03,
    native_wwhd_04, native_wwhd_05, native_wwhd_06, native_wwhd_07,
    native_wwhd_08, native_wwhd_09, native_wwhd_10, native_wwhd_11,
    native_wwhd_12, native_wwhd_13, native_wwhd_14, native_wwhd_15,
    native_wwhd_16, native_wwhd_17, native_wwhd_18, native_wwhd_19,
    native_wwhd_20, native_wwhd_21, native_wwhd_22, native_wwhd_23,
    native_wwhd_24, native_wwhd_25, native_wwhd_26, native_wwhd_27,
    native_wwhd_28, native_wwhd_29, native_wwhd_30, native_wwhd_31,
    native_wwhd_32, native_wwhd_33, native_wwhd_34, native_wwhd_35,
    native_wwhd_36, native_wwhd_37, native_wwhd_38, native_wwhd_39,
    native_wwhd_40, native_wwhd_41, native_wwhd_42, native_wwhd_43,
    native_wwhd_44, native_wwhd_45, native_wwhd_46, native_wwhd_47,
    native_wwhd_48, native_wwhd_49, native_wwhd_50, native_wwhd_51,
    native_wwhd_52, native_wwhd_53, native_wwhd_54, native_wwhd_55,
    native_wwhd_56, native_wwhd_57, native_wwhd_58, native_wwhd_59,
    native_wwhd_60, native_wwhd_61, native_wwhd_62,
};

std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    test::require(input.is_open(), "open generated fixture");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

ExecutionImage make_image(
    std::span<const uint32_t> program = kProgram) {
    ExecutionImage image;
    std::vector<uint8_t> program_bytes;
    program_bytes.reserve(program.size() * sizeof(uint32_t));
    for (const uint32_t word : program) {
        program_bytes.push_back(static_cast<uint8_t>(word >> 24));
        program_bytes.push_back(static_cast<uint8_t>(word >> 16));
        program_bytes.push_back(static_cast<uint8_t>(word >> 8));
        program_bytes.push_back(static_cast<uint8_t>(word));
    }
    image.memory.map(kProgramAddress,
                     static_cast<uint32_t>(program_bytes.size()),
                     {true, false, true}, program_bytes);

    std::array<uint8_t, kDataSize> data{};
    for (size_t index = 0; index < data.size(); ++index) {
        data[index] = static_cast<uint8_t>(index ^ 0xA5);
    }
    image.memory.map(kDataAddress, data.size(), {true, true, false}, data);
    return image;
}

CPUContext make_cpu() {
    CPUContext cpu;
    for (size_t index = 0; index < cpu.gpr.size(); ++index) {
        cpu.gpr[index] = 0x10000000U + static_cast<uint32_t>(index);
        cpu.fpr[index] = {
            std::bit_cast<uint64_t>(static_cast<double>(index) + 0.25),
            std::bit_cast<uint64_t>(static_cast<double>(index) + 0.75)};
        cpu.pc_history[index] = 0x80000000U + static_cast<uint32_t>(index * 4);
    }
    for (size_t index = 0; index < cpu.cr.size(); ++index) {
        cpu.cr[index] = static_cast<uint8_t>(index);
    }
    cpu.gpr[1] = kDataAddress;
    cpu.xer = 0xA0000000;
    cpu.lr = 0x00003000;
    cpu.ctr = 0x4000;
    cpu.pc = kProgramAddress;
    cpu.fpscr = 0x12345678;
    cpu.reservation_address = 0x2200;
    cpu.reservation_valid = true;
    cpu.instruction_count = 7;
    cpu.history_size = 3;
    cpu.history_cursor = 3;
    return cpu;
}

void require_same_cpu(const CPUContext& expected, const CPUContext& actual) {
    test::require(expected.gpr == actual.gpr, "GPR parity");
    test::require(expected.fpr == actual.fpr, "FPR parity");
    test::require(expected.cr == actual.cr, "CR parity");
    test::require(expected.xer == actual.xer, "XER parity");
    test::require(expected.lr == actual.lr, "LR parity");
    test::require(expected.ctr == actual.ctr, "CTR parity");
    test::require(expected.pc == actual.pc, "PC parity");
    test::require(expected.fpscr == actual.fpscr, "FPSCR parity");
    test::require(expected.reservation_address == actual.reservation_address,
                  "reservation address parity");
    test::require(expected.reservation_valid == actual.reservation_valid,
                  "reservation validity parity");
    test::require(expected.instruction_count == actual.instruction_count,
                  "instruction count parity");
    // Recompiled blocks deliberately do not maintain the debug PC ring. It cost
    // a modulo and an optional store on every guest instruction for a report
    // only the headless runner reads, and the module has to keep pace with a
    // JIT. Architectural state and the instruction count are still compared.

    test::require(expected.running == actual.running, "running parity");
}

void require_same_memory(const ExecutionImage& expected,
                         const ExecutionImage& actual,
                         size_t program_words = kProgram.size()) {
    for (uint32_t offset = 0; offset < program_words * sizeof(uint32_t);
         ++offset) {
        test::require(expected.memory.read8(kProgramAddress + offset, 0) ==
                          actual.memory.read8(kProgramAddress + offset, 0),
                      "full program bytes parity");
    }
    for (uint32_t offset = 0; offset < kDataSize; ++offset) {
        test::require(expected.memory.read8(kDataAddress + offset, 0) ==
                          actual.memory.read8(kDataAddress + offset, 0),
                      "full data bytes parity");
    }
}

template <typename Function>
void require_rejected(Function&& function, std::string_view message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    test::require(rejected, message);
}

void test_exact_source() {
    std::string generated =
        generate_native_function("native_fixture", kProgramAddress, kProgram, {});
    generated += generate_native_function(
        "native_surface_fixture", kProgramAddress, kSurfaceProgram, {});
    generated += generate_native_function(
        "native_subfze_fixture", kProgramAddress, kSubfzeProgram, {});
    generated += generate_native_function(
        "native_subfzeo_fixture", kProgramAddress, kSubfzeoProgram, {});
    generated += generate_native_function(
        "native_divwo_fixture", kProgramAddress, kDivwoProgram, {});

    generated += generate_native_function(
        "native_bc_fixture", kProgramAddress, kBcProgram, {});
    generated += generate_native_function(
        "native_b_fixture", kProgramAddress, kBProgram, {});
    generated += generate_native_function(
        "native_bctr_fixture", kProgramAddress, kBctrProgram, {});
    generated += generate_native_function(
        "native_crxor_fixture", kProgramAddress, kCrxorProgram, {});
    generated += generate_native_function(
        "native_fnmsubs_fixture", kProgramAddress, kFnmsubsProgram, {});
    generated += generate_native_function(
        "native_fmsubs_fixture", kProgramAddress, kFmsubsProgram, {});
    generated += generate_native_function(
        "native_paired_fixture", kProgramAddress, kPairedProgram, {});
    for (size_t index = 0; index < kReachedWords.size(); ++index) {
        std::string name = "native_wwhd_";
        name.push_back(static_cast<char>('0' + index / 10));
        name.push_back(static_cast<char>('0' + index % 10));
        generated += generate_native_block(
            name, kProgramAddress,
            std::span<const uint32_t>(&kReachedWords[index], 1), {});
    }
    test::require(generated == read_file(NWIIU_NATIVE_FIXTURE_PATH),
                  "all compiled fixtures exactly match generator output");
}

void test_rejections() {
    require_rejected(
        [] { generate_native_function("", kProgramAddress, kProgram, {}); },
        "empty identifier rejected");
    require_rejected(
        [] { generate_native_function("9fixture", kProgramAddress, kProgram, {}); },
        "leading digit rejected");
    require_rejected(
        [] {
            generate_native_function("native-fixture", kProgramAddress, kProgram,
                                     {});
        },
        "punctuated identifier rejected");
    require_rejected(
        [] { generate_native_function("for", kProgramAddress, kProgram, {}); },
        "language keyword rejected");
    require_rejected(
        [] { generate_native_function("main", kProgramAddress, kProgram, {}); },
        "special main name rejected");
    require_rejected(
        [] {
            generate_native_function("__asm__", kProgramAddress, kProgram, {});
        },
        "implementation-reserved identifier rejected");
    require_rejected(
        [] {
            generate_native_function("native__fixture", kProgramAddress, kProgram,
                                     {});
        },
        "interior double underscore rejected");
    require_rejected(
        [] {
            constexpr std::array<uint32_t, 0> empty{};
            generate_native_function("empty", kProgramAddress, empty, {});
        },
        "empty function rejected");
    require_rejected(
        [] {
            constexpr std::array<uint32_t, 1> branchless{0x38600005};
            generate_native_function("branchless", kProgramAddress, branchless,
                                     {});
        },
        "branchless function rejected");
    require_rejected(
        [] {
            constexpr std::array<uint32_t, 1> unsupported{0xFFFFFFFF};
            generate_native_function("unsupported", kProgramAddress, unsupported,
                                     {});
        },
        "unsupported instruction rejected");
    require_rejected(
        [] {
            constexpr std::array<uint32_t, 2> early_branch{0x48000008,
                                                           0x4E800020};
            generate_native_function("early_branch", kProgramAddress,
                                     early_branch, {});
        },
        "early direct branch rejected");
    require_rejected(
        [] {
            constexpr std::array<uint32_t, 2> early_blr{0x4E800020,
                                                        0x4E800020};
            generate_native_function("early_blr", kProgramAddress, early_blr,
                                     {});
        },
        "early LR branch rejected");
    require_rejected(
        [] {
            generate_native_function("outside_override", kProgramAddress,
                                     kProgram, {{0x00000FFC, 0x4000}});
        },
        "non-local branch override rejected");
    require_rejected(
        [] {
            generate_native_function("non_branch_override", kProgramAddress,
                                     kProgram, {{kProgramAddress, 0x4000}});
        },
        "override on non-branch rejected");
    struct ReservedCase {
        uint32_t word;
        bool terminator;
    };
    constexpr std::array reserved_encodings{
        ReservedCase{d_form(10, 5, 3, 1), false},
        ReservedCase{d_form(10, 6, 3, 1), false},
        ReservedCase{d_form(11, 5, 3, 1), false},
        ReservedCase{d_form(11, 6, 3, 1), false},
        ReservedCase{x_form(31, 5, 3, 5, 0), false},
        ReservedCase{x_form(31, 6, 3, 5, 0), false},
        ReservedCase{x_form(31, 5, 3, 5, 32), false},
        ReservedCase{x_form(31, 6, 3, 5, 32), false},
        ReservedCase{x_form(19, 1, 0, 0, 150), false},
        ReservedCase{x_form(19, 0, 1, 0, 150), false},
        ReservedCase{x_form(19, 0, 0, 1, 150), false},
        ReservedCase{x_form(19, 20, 0, 4, 16), true},
        ReservedCase{x_form(19, 20, 0, 8, 16), true},
        ReservedCase{x_form(19, 20, 0, 16, 16), true},
        ReservedCase{x_form(19, 20, 0, 4, 528), true},
        ReservedCase{x_form(19, 20, 0, 8, 528), true},
        ReservedCase{x_form(19, 20, 0, 16, 528), true},
        ReservedCase{x_form(63, 3, 1, 5, 12), false},
        ReservedCase{x_form(63, 3, 1, 5, 15), false},
        ReservedCase{x_form(63, 3, 1, 5, 72), false},
        ReservedCase{x_form(63, 5, 3, 5, 0), false},
        ReservedCase{x_form(63, 6, 3, 5, 0), false},
        ReservedCase{x_form(63, 4, 3, 5, 0, true), false},
        ReservedCase{x_form(59, 3, 4, 5, 20, true), false},
        ReservedCase{x_form(63, 3, 0, 5, 12, true), false},
        ReservedCase{x_form(63, 3, 0, 5, 15, true), false},
        ReservedCase{x_form(63, 3, 4, 5, 20, true), false},
        ReservedCase{x_form(63, 3, 0, 5, 72, true), false},
        ReservedCase{x_form(4, 1, 2, 3, 592, true), false},
        ReservedCase{x_form(4, 26, 27, 28, 624, true), false},
        ReservedCase{x_form(4, 30, 29, 31, 560, true), false},
        ReservedCase{x_form(4, 1, 0, 2, 40, true), false},
        ReservedCase{x_form(4, 1, 3, 2, 40), false},
        ReservedCase{a_form(59, 3, 4, 5, 6, 18), false},
        ReservedCase{a_form(59, 3, 4, 5, 6, 20), false},
        ReservedCase{a_form(59, 3, 4, 5, 6, 21), false},
        ReservedCase{a_form(59, 0, 9, 10, 1, 28) | 1u, false},
        ReservedCase{a_form(4, 5, 6, 0, 7, 12) | 1u, false},
        ReservedCase{a_form(4, 8, 9, 0, 10, 25) | 1u, false},
        ReservedCase{a_form(4, 5, 6, 3, 7, 12), false},
        ReservedCase{a_form(4, 8, 9, 3, 10, 25), false},
        ReservedCase{a_form(4, 0, 7, 9, 0, 21) | 1u, false},
        ReservedCase{a_form(4, 0, 7, 9, 3, 21), false},
        ReservedCase{a_form(4, 19, 20, 21, 0, 20) | 1u, false},
        ReservedCase{a_form(4, 19, 20, 21, 3, 20), false},
        ReservedCase{a_form(4, 22, 23, 24, 25, 28) | 1u, false},
        ReservedCase{x_form(31, 0, 0, 4, 597), false},
        ReservedCase{x_form(31, 8, 9, 8, 597), false},
        ReservedCase{x_form(31, 31, 1, 12, 597), false},
        ReservedCase{x_form(31, 4, 4, 11, 522), false},
        ReservedCase{x_form(31, 3, 3, 1, 202), false},
        ReservedCase{x_form(31, 3, 3, 1, 714), false},
        ReservedCase{x_form(31, 6, 0, 1, 200, true), false},
        ReservedCase{x_form(31, 3, 3, 1, 712, true), false},
        ReservedCase{uint32_t{0x7CE437D4}, false},

        ReservedCase{uint32_t{0xFDA06B3B}, false},
        ReservedCase{x_form(63, 30, 1, 30, 14), false},
        ReservedCase{uint32_t{0xFFC0F01D}, false},
        ReservedCase{x_form(63, 1, 1, 1, 264), false},
        ReservedCase{uint32_t{0xFC200A11}, false},
        ReservedCase{a_form(63, 0, 30, 1, 11, 25), false},
        ReservedCase{uint32_t{0xFC1E02F3}, false},
        ReservedCase{a_form(63, 13, 10, 13, 1, 21), false},
        ReservedCase{uint32_t{0xFDAA682B}, false},
        ReservedCase{a_form(63, 1, 1, 0, 1, 18), false},
        ReservedCase{uint32_t{0xFC210025}, false},
        ReservedCase{uint32_t{0x7D6900AF}, false},
        ReservedCase{uint32_t{0xFD496A3D}, false},
        ReservedCase{uint32_t{0xEE4D95FD}, false},
        ReservedCase{x_form(31, 11, 27, 8, 343, true), false},
        ReservedCase{x_form(31, 9, 11, 1, 922), false},
        ReservedCase{d_form(55, 13, 0, 8), false},
        ReservedCase{x_form(31, 29, 7, 27, 407, true), false},
    };
    for (const auto test_case : reserved_encodings) {
        require_rejected(
            [test_case] {
                std::vector<uint32_t> program{test_case.word};
                if (!test_case.terminator) {
                    program.push_back(0x4E800020);
                }
                generate_native_function("reserved", kProgramAddress, program,
                                         {});
            },
            "reserved Task 2 encoding rejected");
    }
}

void test_native_block_generation() {
    constexpr std::array<uint32_t, 2> straight_line{0x38600005, 0x38840007};
    const auto fallthrough = generate_native_block(
        "straight_line", kProgramAddress, straight_line, {});
    test::require(
        fallthrough.find(
            "cpu.gpr[3] = 0 + static_cast<uint32_t>(5);") !=
            std::string::npos,
        "block inlines first addi");
    test::require(
        fallthrough.find(
            "cpu.gpr[4] = cpu.gpr[4] + static_cast<uint32_t>(7);") !=
            std::string::npos,
        "block inlines second addi");
    test::require(fallthrough.find("    cpu.pc = 0x00001008;\n") !=
                      std::string::npos,
                  "straight-line block assigns fallthrough PC");

    constexpr std::array<uint32_t, 7> hot_instructions{
        d_form(15, 3, 4, 1),
        d_form(24, 5, 3, 7),
        d_form(26, 6, 5, 3),
        d_form(36, 6, 1, 8),
        d_form(32, 7, 1, 8),
        x_form(31, 8, 7, 6, 444),
        0x4E800020,
    };
    const auto hot = generate_native_function(
        "hot_instructions", kProgramAddress, hot_instructions, {});
    for (const auto helper :
         {"::addis(", "::ori(", "::xori(", "::stw(", "::lwz(", "::or_("}) {
        test::require(hot.find(helper) == std::string::npos,
                      "hot instruction is emitted inline");
    }

    constexpr std::array<uint32_t, 2> branch_terminated{0x38600005,
                                                        0x48000008};
    const auto branch = generate_native_block(
        "branch_terminated", kProgramAddress, branch_terminated, {});
    test::require(branch.find(
                      "nwii::runtime::ppc::b(cpu, 0x0000100C, false);") !=
                      std::string::npos,
                  "branch-terminated block emits branch semantics");
    test::require(branch.find("    cpu.pc = ") == std::string::npos,
                  "branch-terminated block omits fallthrough PC");

    require_rejected(
        [] {
            constexpr std::array<uint32_t, 2> early_branch{0x48000008,
                                                           0x38600005};
            generate_native_block("early_branch", kProgramAddress, early_branch,
                                  {});
        },
        "non-final block branch rejected");
}

void test_final_direct_branch() {
    constexpr std::array<uint32_t, 1> direct_branch{0x48000008};
    const auto generated = generate_native_function(
        "direct_branch", kProgramAddress, direct_branch,
        {{kProgramAddress, 0xC0001000}});
    test::require(
        generated.find("nwii::runtime::ppc::relocated_branch_target("
                       "memory, 0x00001000, 0x48000008, 0xC0001000)") !=
            std::string::npos,
        "local branch override follows host relocation");
}

void test_native_parity() {
    auto interpreted_image = make_image();
    auto native_image = make_image();
    auto interpreted_cpu = make_cpu();
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_fixture(native_cpu, native_image.memory);

    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image);
}

void test_reached_subfze_native_parity() {
    auto interpreted_image = make_image(kSubfzeProgram);
    auto native_image = make_image(kSubfzeProgram);
    auto interpreted_cpu = make_cpu();
    interpreted_cpu.gpr[0] = 0x12345678;
    interpreted_cpu.xer = 0x20000000;
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kSubfzeProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_subfze_fixture(native_cpu, native_image.memory);

    test::require(interpreted_cpu.gpr[6] == 0xEDCBA988 &&
                      interpreted_cpu.xer == 0 &&
                      interpreted_cpu.cr[0] == 0x8,
                  "exact reached subfze retains decoded semantics");
    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image,
                        kSubfzeProgram.size());
}

void test_subfzeo_native_parity() {
    struct Case {
        uint32_t input;
        uint32_t xer;
        uint32_t result;
        uint32_t expected_xer;
        uint8_t cr;
    };
    constexpr std::array cases{
        Case{0x80000000, 0x21234567, 0x80000000, 0xC1234567, 0x9},
        Case{0, 0xE1234567, 0, 0xA1234567, 0x3},
    };

    for (const auto& test_case : cases) {
        auto interpreted_image = make_image(kSubfzeoProgram);
        auto native_image = make_image(kSubfzeoProgram);
        auto interpreted_cpu = make_cpu();
        interpreted_cpu.gpr[3] = test_case.input;
        interpreted_cpu.xer = test_case.xer;
        interpreted_cpu.cr[0] = 0x6;
        auto native_cpu = interpreted_cpu;

        Executor executor(interpreted_image);
        for (size_t index = 0; index < kSubfzeoProgram.size(); ++index) {
            executor.step(interpreted_cpu);
        }
        native_subfzeo_fixture(native_cpu, native_image.memory);

        test::require(interpreted_cpu.gpr[3] == test_case.result &&
                          interpreted_cpu.xer == test_case.expected_xer &&
                          interpreted_cpu.cr[0] == test_case.cr,
                      "subfzeo interpreter covers overflow and sticky SO");
        require_same_cpu(interpreted_cpu, native_cpu);
        require_same_memory(interpreted_image, native_image,
                            kSubfzeoProgram.size());
    }
}

void test_reached_crxor_native_parity() {
    auto interpreted_image = make_image(kCrxorProgram);
    auto native_image = make_image(kCrxorProgram);
    auto interpreted_cpu = make_cpu();
    // CR bit 6 lives in field 1, nibble bit 0x2. Seed it set so the reached
    // crxor 6,6,6 must clear it (BA == BB yields 0).
    interpreted_cpu.cr[1] = 0xF;
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kCrxorProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_crxor_fixture(native_cpu, native_image.memory);

    test::require(interpreted_cpu.cr[1] == 0xD,
                  "exact reached crxor 6,6,6 clears CR bit 6");
    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image, kCrxorProgram.size());
}
void test_reached_fnmsubs_native_parity() {
    auto interpreted_image = make_image(kFnmsubsProgram);
    auto native_image = make_image(kFnmsubsProgram);
    auto interpreted_cpu = make_cpu();
    // fnmsubs f18,f13,f23,f18 = round_single(-(fA*fC - fB)). Seed operands so
    // the fused product-difference is exactly 2^-54, which a non-fused double
    // multiply would flush to zero.
    interpreted_cpu.fpr[13][0] = std::bit_cast<uint64_t>(1.0 + 0x1p-27);
    interpreted_cpu.fpr[23][0] = std::bit_cast<uint64_t>(1.0 - 0x1p-27);
    interpreted_cpu.fpr[18][0] = std::bit_cast<uint64_t>(1.0);
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kFnmsubsProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_fnmsubs_fixture(native_cpu, native_image.memory);

    test::require(std::bit_cast<double>(interpreted_cpu.fpr[18][0]) == 0x1p-54,
                  "exact reached fnmsubs fuses before single rounding");
    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image,
                        kFnmsubsProgram.size());
}
void test_reached_fmsubs_native_parity() {
    auto interpreted_image = make_image(kFmsubsProgram);
    auto native_image = make_image(kFmsubsProgram);
    auto interpreted_cpu = make_cpu();
    // fmsubs f0,f9,f1,f10 = round_single(fA*fC - fB). Seed operands so the
    // fused product-difference is exactly -2^-54, which a non-fused double
    // multiply flushes to zero and a naive fsubs (fA - fB) turns into 2^-27.
    interpreted_cpu.fpr[9][0] = std::bit_cast<uint64_t>(1.0 + 0x1p-27);
    interpreted_cpu.fpr[1][0] = std::bit_cast<uint64_t>(1.0 - 0x1p-27);
    interpreted_cpu.fpr[10][0] = std::bit_cast<uint64_t>(1.0);
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kFmsubsProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_fmsubs_fixture(native_cpu, native_image.memory);

    test::require(std::bit_cast<double>(interpreted_cpu.fpr[0][0]) == -0x1p-54,
                  "exact reached fmsubs fuses fC before single rounding");
    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image,
                        kFmsubsProgram.size());
}
void test_reached_paired_native_parity() {
    auto interpreted_image = make_image(kPairedProgram);
    auto native_image = make_image(kPairedProgram);
    auto interpreted_cpu = make_cpu();
    // ps_msub f22,f23,f25,f24 fuses fA*fC-fB. Seed lane0 so the fused
    // product-difference is exactly -2^-54, which an unfused double multiply
    // flushes to zero -- the exactly-representable defaults could not tell fma
    // apart from a plain multiply-then-subtract.
    interpreted_cpu.fpr[23][0] = std::bit_cast<uint64_t>(1.0 + 0x1p-27);
    interpreted_cpu.fpr[25][0] = std::bit_cast<uint64_t>(1.0 - 0x1p-27);
    interpreted_cpu.fpr[24][0] = std::bit_cast<uint64_t>(1.0);
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kPairedProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_paired_fixture(native_cpu, native_image.memory);

    // ps_madd f11 = single(fma(fA, fC, fB)) per lane, with fA=f12, fC=f13,
    // fB=f14. Distinct fC/fB values make a swapped emit argument order diverge,
    // so this value check also guards the frc/frb mapping.
    test::require(
        std::bit_cast<double>(interpreted_cpu.fpr[11][0]) ==
            static_cast<double>(static_cast<float>(
                std::fma(12.0 + 0.25, 13.0 + 0.25, 14.0 + 0.25))),
        "exact reached ps_madd fuses lane0 with decoded fC/fB operands");
    // ps_add f0,f7,f9 adds both single lanes independently: fA=f7={7.25,7.75},
    // fB=f9={9.25,9.75} from make_cpu, so f0={16.5,17.5}.
    test::require(
        std::bit_cast<double>(interpreted_cpu.fpr[0][0]) == 16.5 &&
            std::bit_cast<double>(interpreted_cpu.fpr[0][1]) == 17.5,
        "exact reached ps_add sums paired-single lanes independently");
    // ps_sub f19,f20,f21 subtracts both single lanes: fA=f20={20.25,20.75},
    // fB=f21={21.25,21.75} from make_cpu, so f19={-1.0,-1.0}.
    test::require(
        std::bit_cast<double>(interpreted_cpu.fpr[19][0]) == -1.0 &&
            std::bit_cast<double>(interpreted_cpu.fpr[19][1]) == -1.0,
        "exact reached ps_sub subtracts paired-single lanes independently");
    // ps_msub f22,f23,f25,f24 fuses A*C-B per lane: fA=f23, fC=f25, fB=f24.
    // Lane0 was seeded so the fused result is exactly -2^-54; an unfused
    // multiply-then-subtract flushes to zero, and a swapped fC/fB emit order
    // diverges from this value too.
    test::require(
        std::bit_cast<double>(interpreted_cpu.fpr[22][0]) == -0x1p-54,
        "reached ps_msub fuses lane0 before single rounding");
    // ps_merge11 f26,f27,f28 copies the raw high lanes: f26[0]=f27[1],
    // f26[1]=f28[1]. From make_cpu f27={27.25,27.75}, f28={28.25,28.75}.
    test::require(
        interpreted_cpu.fpr[26][0] == std::bit_cast<uint64_t>(27.75) &&
            interpreted_cpu.fpr[26][1] == std::bit_cast<uint64_t>(28.75),
        "exact reached ps_merge11 copies frA/frB high lanes");
    // ps_merge01 f30,f29,f31 copies f30[0]=f29[0], f30[1]=f31[1]. From
    // make_cpu f29={29.25,29.75}, f31={31.25,31.75}.
    test::require(
        interpreted_cpu.fpr[30][0] == std::bit_cast<uint64_t>(29.25) &&
            interpreted_cpu.fpr[30][1] == std::bit_cast<uint64_t>(31.75),
        "exact reached ps_merge01 copies frA low lane and frB high lane");
    // ps_neg f1,f2 toggles the sign bit of both lanes. From make_cpu
    // f2={2.25,2.75}, so f1={-2.25,-2.75}.
    test::require(
        std::bit_cast<double>(interpreted_cpu.fpr[1][0]) == -2.25 &&
            std::bit_cast<double>(interpreted_cpu.fpr[1][1]) == -2.75,
        "exact reached ps_neg negates both paired-single lanes");
    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image,
                        kPairedProgram.size());
}
void test_divwo_native_parity() {
    struct Case {
        uint32_t dividend;
        uint32_t divisor;
        uint32_t xer;
        uint32_t result;
        uint32_t expected_xer;
        uint8_t expected_cr;
    };
    constexpr std::array cases{
        Case{9, 3, 0x41234567, 3, 0x01234567, 0x4},
        Case{10, 0, 0x21234567, 0, 0xE1234567, 0x3},
        Case{0x80000000, 0xFFFFFFFF, 0x01234567, 0x80000000,
             0xC1234567, 0x9},
        Case{9, 3, 0xC1234567, 3, 0x81234567, 0x5},
    };

    for (const auto& test_case : cases) {
        auto interpreted_image = make_image(kDivwoProgram);
        auto native_image = make_image(kDivwoProgram);
        auto interpreted_cpu = make_cpu();
        interpreted_cpu.gpr[4] = test_case.dividend;
        interpreted_cpu.gpr[6] = test_case.divisor;
        interpreted_cpu.gpr[7] = 0xA5A5A5A5;
        interpreted_cpu.xer = test_case.xer;
        interpreted_cpu.cr[0] = 0x6;
        auto native_cpu = interpreted_cpu;

        Executor executor(interpreted_image);
        for (size_t index = 0; index < kDivwoProgram.size(); ++index) {
            executor.step(interpreted_cpu);
        }
        native_divwo_fixture(native_cpu, native_image.memory);

        test::require(interpreted_cpu.gpr[7] == test_case.result &&
                          interpreted_cpu.xer == test_case.expected_xer &&
                          interpreted_cpu.cr[0] == test_case.expected_cr,
                      "divwo interpreter covers overflow, sticky SO, and Rc");
        require_same_cpu(interpreted_cpu, native_cpu);
        require_same_memory(interpreted_image, native_image,
                            kDivwoProgram.size());
    }
}


void test_startup_surface_native_parity() {
    auto interpreted_image = make_image(kSurfaceProgram);
    auto native_image = make_image(kSurfaceProgram);
    auto interpreted_cpu = make_cpu();
    interpreted_cpu.gpr[0] = 0;
    interpreted_cpu.gpr[2] = 4;
    interpreted_cpu.gpr[4] = 0x01020304;
    interpreted_cpu.gpr[5] = 8;
    interpreted_cpu.gpr[20] = kDataAddress + 0x70;
    interpreted_cpu.gpr[21] = kDataAddress + 0x74;
    interpreted_cpu.gpr[7] = 0x82;
    interpreted_cpu.gpr[8] = 2;
    interpreted_cpu.gpr[27] = kDataAddress;
    interpreted_cpu.gpr[29] = 0xA1B2C3D4;
    interpreted_cpu.gpr[9] = kDataAddress - 0x468;
    interpreted_cpu.fpr[13][0] = 0x0123456789ABCDEF;
    auto native_cpu = interpreted_cpu;

    Executor executor(interpreted_image);
    for (size_t index = 0; index < kSurfaceProgram.size(); ++index) {
        executor.step(interpreted_cpu);
    }
    native_surface_fixture(native_cpu, native_image.memory);

    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image,
                        kSurfaceProgram.size());
}

using NativeFixture = void (*)(CPUContext&, nwii::runtime::GuestMemory&);

void require_branch_parity(std::span<const uint32_t> program,
                           NativeFixture fixture, CPUContext initial) {
    auto interpreted_image = make_image(program);
    auto native_image = make_image(program);
    auto interpreted_cpu = initial;
    auto native_cpu = initial;

    Executor(interpreted_image).step(interpreted_cpu);
    fixture(native_cpu, native_image.memory);

    require_same_cpu(interpreted_cpu, native_cpu);
    require_same_memory(interpreted_image, native_image, program.size());
}

void test_reached_words_native_parity() {
    constexpr std::array indexed_memory_xos{
        uint32_t{215}, uint32_t{279}, uint32_t{311}, uint32_t{535},
        uint32_t{567}, uint32_t{663}, uint32_t{695},
        uint32_t{55},  uint32_t{119}, uint32_t{183}, uint32_t{20},
        uint32_t{54},
        uint32_t{662}, uint32_t{247}, uint32_t{375}, uint32_t{534},
        uint32_t{790}, uint32_t{439}, uint32_t{1014}, uint32_t{86},
    };
    for (size_t index = 0; index < kReachedWords.size(); ++index) {
        const uint32_t word = kReachedWords[index];
        const uint32_t opcode = word >> 26;
        const uint32_t ra = (word >> 16) & 31;
        const uint32_t rb = (word >> 11) & 31;
        const uint32_t xo = (word >> 1) & 0x3FF;
        auto interpreted_image =
            make_image(std::span<const uint32_t>(&word, 1));
        auto native_image = make_image(std::span<const uint32_t>(&word, 1));
        auto interpreted_cpu = make_cpu();
        for (size_t reg = 0; reg < interpreted_cpu.gpr.size(); ++reg) {
            interpreted_cpu.gpr[reg] =
                0x01020408U * static_cast<uint32_t>(reg + 1);
        }

        const bool indexed_memory =
            opcode == 31 &&
            std::ranges::find(indexed_memory_xos, xo) !=
                indexed_memory_xos.end();
        if (indexed_memory) {
            if (ra == 0) {
                interpreted_cpu.gpr[rb] = kDataAddress + 0x40;
            } else {
                interpreted_cpu.gpr[ra] = kDataAddress + 0x40;
                interpreted_cpu.gpr[rb] = 0;
            }
        } else if ((opcode >= 40 && opcode <= 43) || opcode == 53) {
            const int32_t displacement =
                static_cast<int16_t>(word & 0xFFFF);
            interpreted_cpu.gpr[ra] =
                kDataAddress + 0x40 - static_cast<uint32_t>(displacement);
        } else if (opcode >= 56 && opcode <= 61) {
            int32_t displacement = static_cast<int32_t>(word & 0xFFF);
            if ((displacement & 0x800) != 0) {
                displacement -= 0x1000;
            }
            interpreted_cpu.gpr[ra] =
                kDataAddress + 0x40 - static_cast<uint32_t>(displacement);
        }
        if (index == 44) { // exact adde r12,r10,r8
            interpreted_cpu.gpr[10] = 0xFFFFFFFF;
            interpreted_cpu.gpr[8] = 0;
            interpreted_cpu.xer = 0x20000000;
        }
        if (index == 45) { // exact stwbrx r7,0,r10
            interpreted_cpu.gpr[7] = 0x11223344;
        }
        if (index == 48 || index == 42) {
            interpreted_image.memory.write32(kDataAddress + 0x40,
                                              0x11223344, 0);
            native_image.memory.write32(kDataAddress + 0x40, 0x11223344, 0);
        }
        if (index == 52) {
            interpreted_image.memory.write16(kDataAddress + 0x40, 0xBEEF, 0);
            native_image.memory.write16(kDataAddress + 0x40, 0xBEEF, 0);
        } else if (index == 53) {
            interpreted_cpu.fpr[11][0] =
                std::bit_cast<uint64_t>(1.0 - 0x1p-26);
        }
        auto native_cpu = interpreted_cpu;

        bool interpreted_fault = false;
        bool native_fault = false;
        std::string interpreted_reason;
        std::string native_reason;
        try {
            Executor(interpreted_image).step(interpreted_cpu);
        } catch (const nwii::runtime::GuestFault& fault) {
            interpreted_fault = true;
            interpreted_reason = fault.what();
        }
        try {
            kReachedFixtures[index](native_cpu, native_image.memory);
        } catch (const nwii::runtime::GuestFault& fault) {
            native_fault = true;
            native_reason = fault.what();
        }

        test::require(
            !interpreted_fault && !native_fault,
            "accepted exact reached word must execute without fault: " +
                std::to_string(index) + " interpreter=" + interpreted_reason +
                " native=" + native_reason);
        test::require(interpreted_cpu.pc == kProgramAddress + 4 &&
                          interpreted_cpu.current_instruction == word,
                      "exact reached word has observable PC/trace semantics");
        require_same_cpu(interpreted_cpu, native_cpu);
        require_same_memory(interpreted_image, native_image, 1);

        if (index == 44) {
            test::require(interpreted_cpu.gpr[12] == 0 &&
                              (interpreted_cpu.xer & 0x20000000) != 0,
                          "exact adde consumes and produces carry");
        } else if (index == 45) {
            test::require(
                interpreted_image.memory.read32(kDataAddress + 0x40, 0) ==
                    0x44332211,
                "exact stwbrx reverses guest byte order");
        } else if (index == 48) {
            test::require(interpreted_cpu.gpr[0] == 0x44332211,
                          "exact lwbrx reverses guest byte order");
        } else if (index == 42) {
            test::require(interpreted_cpu.gpr[8] == 0x11223344 &&
                              interpreted_cpu.reservation_valid &&
                              interpreted_cpu.reservation_address ==
                                  kDataAddress + 0x40,
                          "exact lwarx loads and establishes reservation");
        } else if (index == 52) {
            test::require(
                std::bit_cast<double>(interpreted_cpu.fpr[7][0]) == 48879.0 &&
                    std::bit_cast<double>(interpreted_cpu.fpr[7][1]) == 1.0,
                "exact GQR3 psq_l selects U16 width and W=1 lane fill");
        } else if (index == 53) {
            test::require(
                interpreted_image.memory.read8(kDataAddress + 0x40, 0) ==
                        0x00 &&
                    interpreted_image.memory.read8(kDataAddress + 0x41, 0) ==
                        0x01 &&
                    interpreted_image.memory.read8(kDataAddress + 0x42, 0) ==
                        (uint8_t{0x42} ^ uint8_t{0xA5}),
                "exact GQR3 psq_st float-rounds then writes U16 bytes");
        }
    }
}

void test_startup_branch_native_parity() {
    auto cpu = make_cpu();
    cpu.cr[0] = 0x2;
    require_branch_parity(kBcProgram, native_bc_fixture, cpu);
    require_branch_parity(kBProgram, native_b_fixture, cpu);
    cpu.ctr = 0x00003003;
    require_branch_parity(kBctrProgram, native_bctr_fixture, cpu);
}

void test_native_budget_fallback() {
    auto image = make_image();
    CPUContext cpu;
    cpu.pc = kProgramAddress;
    cpu.gpr[1] = kDataAddress;
    const uint32_t initial_data = image.memory.read32(kDataAddress, 0);

    Executor executor(image);
    executor.register_native(kProgramAddress, kProgram.size(), native_fixture);
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category ==
                          nwii::runtime::StopCategory::instruction_budget &&
                      stop.instruction_count == 1 &&
                      cpu.pc == kProgramAddress + 4 && cpu.gpr[3] == 5,
                  "oversized native thunk falls back to one interpreted step");
    test::require(image.memory.read32(kDataAddress, 0) == initial_data,
                  "native thunk does not execute past remaining budget");
}

void test_native_fault_context() {
    auto image = make_image();
    CPUContext cpu;
    cpu.pc = kProgramAddress;
    cpu.gpr[1] = 0x30000000;

    Executor executor(image);
    executor.register_native(kProgramAddress, kProgram.size(), native_fixture);
    const auto stop = executor.run(cpu, kProgram.size());

    test::require(stop.category == nwii::runtime::StopCategory::guest_fault &&
                      stop.fault_address == 0x30000000 &&
                      stop.fault_width == 4 &&
                      stop.fault_access ==
                          nwii::runtime::MemoryAccess::write &&
                      stop.raw_instruction.has_value() &&
                      *stop.raw_instruction == 0x90610000 &&
                      stop.instruction_count == 2,
                  "native fault retains current instruction and access context");
}
void test_startup_surface_generation() {
    struct Case {
        uint32_t word;
        std::string_view helper;
        bool terminator;
    };
    constexpr std::array cases{
        Case{x_form(4, 1, 2, 3, 592), "::ps_merge10(", false},
        Case{x_form(4, 26, 27, 28, 624), "::ps_merge11(", false},
        Case{x_form(4, 30, 29, 31, 560), "::ps_merge01(", false},
        Case{x_form(4, 1, 0, 2, 40), "::ps_neg(", false},
        Case{d_form(7, 3, 4, 0xFFFE), "::mulli(", false},
        Case{d_form(8, 3, 4, 1), "::subfic(", false},
        Case{d_form(10, 4, 3, 1), "::cmpli(", false},
        Case{d_form(11, 4, 3, 1), "::cmpi(", false},
        Case{d_form(12, 3, 4, 1), "::addic(", false},
        Case{d_form(13, 3, 4, 1), "::addic(", false},
        Case{d_form(14, 3, 4, 1), "cpu.gpr[3] =", false},
        Case{d_form(15, 3, 4, 1), "static_cast<uint32_t>(1) << 16", false},
        Case{0x41820008, "::bc(", true},
        Case{0x48000008, "::b(", true},
        Case{xl_form(20, 0, 16), "::bclr(", true},
        Case{xl_form(0, 0, 150), "::isync(", false},
        Case{xl_form(20, 0, 528), "::bcctr(", true},
        Case{rlwimi(4, 3, 8, 8, 23), "::rlwimi(", false},
        Case{rlwinm(4, 3, 8, 8, 23), "::rlwinm(", false},
        Case{d_form(24, 4, 3, 1), " | 1;", false},
        Case{d_form(25, 4, 3, 1), " | 65536;", false},
        Case{d_form(26, 4, 3, 1), " ^ 1;", false},
        Case{d_form(27, 4, 3, 1), " ^ 65536;", false},
        Case{x_form(31, 4, 3, 5, 0), "::cmp(", false},
        Case{x_form(31, 4, 3, 5, 4), "::tw(", false},
        Case{x_form(31, 3, 4, 5, 23), "::lwzx(", false},
        Case{x_form(31, 4, 3, 5, 32), "::cmpl(", false},
        Case{x_form(31, 4, 3, 5, 24), "::slw(", false},
        Case{x_form(31, 3, 4, 5, 40), "::subf(", false},
        Case{x_form(31, 4, 3, 5, 124), "::nor_(", false},
        Case{x_form(31, 3, 4, 5, 136), "::subfe(", false},
        Case{x_form(31, 3, 4, 5, 151), "::stwx(", false},
        Case{uint32_t{0x7FA7DB2E}, "::sthx(", false},
        Case{x_form(31, 3, 4, 5, 11), "::mulhwu(", false},
        Case{x_form(31, 3, 4, 5, 235), "::mullw(", false},
        Case{x_form(31, 3, 4, 5, 266), "::add(", false},
        Case{x_form(31, 3, 4, 5, 459), "::divwu(", false},
        Case{uint32_t{0x7CE433D6}, "::divw(", false},
        Case{uint32_t{0x7CE437D6}, "::divw(", false},

        Case{uint32_t{0x7C845814}, "::addc(", false},
        Case{x_form(31, 4, 4, 11, 10, true), "::addc(", false},
        Case{uint32_t{0x7C630194}, "::addze(", false},
        Case{x_form(31, 3, 3, 0, 714, true), "::addze(", false},
        Case{uint32_t{0x4CC63182}, "::crxor(", false},
        Case{x_form(31, 4, 3, 5, 536), "::srw(", false},
        Case{spr_form(3, 8, 339), "::mfspr(", false},
        Case{uint32_t{0x7D6BFA78}, "::xor_(", false},
        Case{x_form(31, 11, 11, 31, 316, true), "::xor_(", false},
        Case{x_form(31, 4, 3, 5, 444), " | cpu.gpr[5]", false},
        Case{spr_form(3, 8, 467), "::mtspr(", false},
        Case{x_form(31, 8, 3, 4, 597), "::lswi(", false},
        Case{x_form(31, 8, 3, 4, 725), "::stswi(", false},
        Case{x_form(31, 4, 3, 4, 824), "::srawi(", false},
        Case{uint32_t{0x7D2B0734}, "::extsh(", false},
        Case{uint32_t{0x7D7B42AE}, "::lhax(", false},
        Case{x_form(31, 4, 3, 0, 954), "::extsb(", false},
        Case{x_form(31, 3, 4, 5, 983), "::stfiwx(", false},
        Case{d_form(32, 3, 4, 0), "memory.read32(", false},
        Case{d_form(33, 3, 4, 0), "::lwzu(", false},
        Case{d_form(34, 3, 4, 0), "::lbz(", false},
        Case{d_form(35, 3, 4, 0), "::lbzu(", false},
        Case{d_form(36, 3, 4, 0), "memory.write32(", false},
        Case{d_form(37, 3, 4, 0), "::stwu(", false},
        Case{d_form(38, 3, 4, 0), "::stb(", false},
        Case{d_form(39, 3, 4, 0), "::stbu(", false},
        Case{d_form(44, 3, 4, 0), "::sth(", false},
        Case{d_form(45, 3, 4, 0), "::sthu(", false},
        Case{d_form(46, 30, 4, 0), "::lmw(", false},
        Case{d_form(47, 30, 4, 0), "::stmw(", false},
        Case{d_form(48, 3, 4, 0), "::lfs(", false},
        Case{d_form(49, 3, 4, 0), "::lfsu(", false},
        Case{d_form(50, 3, 4, 0), "::lfd(", false},
        Case{d_form(52, 3, 4, 0), "::stfs(", false},
        Case{d_form(54, 3, 4, 0), "::stfd(", false},
        Case{uint32_t{0xDDA904E8}, "::stfdu(", false},
        Case{x_form(59, 3, 4, 5, 20), "::fsubs(", false},
        Case{a_form(59, 3, 4, 5, 0, 18), "::fdivs(", false},
        Case{a_form(59, 3, 4, 5, 0, 21), "::fadds(", false},
        Case{a_form(59, 3, 4, 0, 5, 25), "::fmuls(", false},
        Case{a_form(59, 3, 4, 5, 6, 29), "::fmadds(", false},
        Case{uint32_t{0xEE4D95FC}, "::fnmsubs(", false},
        Case{uint32_t{0xEC095078}, "::fmsubs(", false},
        Case{a_form(4, 5, 6, 0, 7, 12), "::ps_muls0(", false},
        Case{a_form(4, 8, 9, 0, 10, 25), "::ps_mul(", false},
        Case{a_form(4, 11, 12, 14, 13, 29), "::ps_madd(", false},
        Case{a_form(4, 15, 16, 18, 17, 10), "::ps_sum0(", false},
        Case{uint32_t{0x1007482A}, "::ps_add(", false},
        Case{a_form(4, 19, 20, 21, 0, 20), "::ps_sub(", false},
        Case{a_form(4, 22, 23, 24, 25, 28), "::ps_msub(", false},
        Case{uint32_t{0xFDA06B3A}, "::fmadd(", false},
        Case{uint32_t{0xFD496A3C}, "::fnmsub(", false},
        Case{uint32_t{0xFC200A10}, "::fabs(", false},
        Case{uint32_t{0xFC1E02F2}, "::fmul(", false},
        Case{uint32_t{0xFDAA682A}, "::fadd(", false},
        Case{uint32_t{0xFC210024}, "::fdiv(", false},
        Case{uint32_t{0x7D6900AE}, "::lbzx(", false},
        Case{uint32_t{0xFFC0F01C}, "::fctiw(", false},
        Case{x_form(63, 4, 3, 5, 0), "::fcmpu(", false},
        Case{x_form(63, 3, 0, 5, 12), "::frsp(", false},
        Case{x_form(63, 3, 0, 5, 15), "::fctiwz(", false},
        Case{x_form(63, 3, 4, 5, 20), "::fsub(", false},
        Case{x_form(63, 3, 0, 5, 72), "::fmr(", false},
        Case{x_form(63, 3, 0, 5, 40), "::fneg(", false},
        // Exact first reached WWHD words for every class rejected by the
        // whole-title strict generation pass.
        Case{uint32_t{0xA3FC0028}, "::lhz(", false},
        Case{uint32_t{0xA7DD0002}, "::lhzu(", false},
        Case{uint32_t{0xA8050000}, "::lha(", false},
        Case{uint32_t{0xD7A80008}, "::stfsu(", false},
        Case{uint32_t{0x7C000034}, "::cntlzw(", false},
        Case{uint32_t{0x7D800026}, "::mfcr(", false},
        Case{uint32_t{0x7EAB01AE}, "::stbx(", false},
        Case{uint32_t{0x7C87422E}, "::lhzx(", false},
        Case{uint32_t{0x7D800038}, "::and_(", false},
        Case{uint32_t{0x7C09526E}, "::lhzux(", false},
        Case{uint32_t{0x7CECD42E}, "::lfsx(", false},
        Case{uint32_t{0x7DAC5C6E}, "::lfsux(", false},
        Case{uint32_t{0x7D296078}, "::andc(", false},
        Case{uint32_t{0x7C034D2E}, "::stfsx(", false},
        Case{uint32_t{0x7D4700D0}, "::neg(", false},
        Case{uint32_t{0x7D404096}, "::mulhw(", false},
        Case{uint32_t{0xFC2D006E}, "::fsel(", false},
        Case{uint32_t{0xFC205034}, "::frsqrte(", false},
        Case{uint32_t{0x4CC63242}, "::creqv(", false},
        Case{uint32_t{0x5F84203E}, "::rlwnm(", false},
        Case{uint32_t{0x70E000FD}, "::andi_dot(", false},
        Case{uint32_t{0x75491028}, "::andi_dot(", false},
        Case{uint32_t{0xACE8FDE8}, "::lhau(", false},
        Case{uint32_t{0x7C003FEC}, "::dcbz(", false},
        Case{uint32_t{0x7D29256E}, "::stfsux(", false},
        Case{uint32_t{0x7D2A0630}, "::sraw(", false},
        Case{uint32_t{0x7D6CD86E}, "::lwzux(", false},
        Case{uint32_t{0x7CBB516E}, "::stwux(", false},
        Case{uint32_t{0x7FE82010}, "::subfc(", false},
        Case{uint32_t{0xE1810024}, "::psq_l(", false},
        Case{uint32_t{0xF381005C}, "::psq_st(", false},
        Case{uint32_t{0xFC006910}, "::fnabs(", false},
        Case{uint32_t{0xFD6D6EF8}, "::fmsub(", false},
        Case{uint32_t{0x110D3420}, "::ps_merge00(", false},
        Case{uint32_t{0x11AB68DC}, "::ps_madds0(", false},
        Case{uint32_t{0x118249DE}, "::ps_madds1(", false},
        Case{uint32_t{0x100001D6}, "::ps_sum1(", false},
        Case{uint32_t{0x118500DA}, "::ps_muls1(", false},
        Case{uint32_t{0x1085312E}, "::ps_sel(", false},
        Case{uint32_t{0x10073040}, "::ps_cmpo0(", false},
        Case{uint32_t{0xECA06030}, "::fres(", false},
        Case{uint32_t{0x7FE7336E}, "::sthux(", false},
        Case{uint32_t{0x7D006028}, "::lwarx(", false},
        Case{uint32_t{0x7C00F06C}, "::dcbst(", false},
        Case{uint32_t{0x7D8A4114}, "::adde(", false},
        Case{uint32_t{0x7CE0552C}, "::stwbrx(", false},
        Case{uint32_t{0x7ED461EE}, "::stbux(", false},
        Case{uint32_t{0x7D0618EE}, "::lbzux(", false},
        Case{uint32_t{0x7C00642C}, "::lwbrx(", false},
        Case{uint32_t{0x7C0C5AEE}, "::lhaux(", false},
        Case{uint32_t{0x7C1E60AC}, "::dcbf(", false},
        Case{uint32_t{0x7D62E3A6}, "::mtspr(", false},
        Case{uint32_t{0xE0FDB000}, "::psq_l(", false},
        Case{uint32_t{0xF166B000}, "::psq_st(", false},
        Case{uint32_t{0xE4C40004}, "::psq_lu(", false},
        Case{uint32_t{0x10C04034}, "::ps_rsqrte(", false},
        Case{uint32_t{0x10EC583E}, "::ps_nmadd(", false},
        Case{uint32_t{0x112561FC}, "::ps_nmsub(", false},
        Case{uint32_t{0x7D4A3338}, "::orc(", false},
        Case{uint32_t{0x7D83E3A6}, "::mtspr(", false},
        Case{uint32_t{0xF5850004}, "::psq_stu(", false},
        Case{uint32_t{0x7C04E3A6}, "::mtspr(", false},
        Case{uint32_t{0x7C05E3A6}, "::mtspr(", false},
    };

    for (const auto& test_case : cases) {
        std::vector<uint32_t> program{test_case.word};
        if (!test_case.terminator) {
            program.push_back(0x4E800020);
        }
        const auto generated =
            generate_native_function("surface", kProgramAddress, program, {});
        test::require(generated.find(test_case.helper) != std::string::npos,
                      "listed startup form emits native semantics");
    }

    const auto conditional = generate_native_function(
        "conditional_override", kProgramAddress,
        std::array<uint32_t, 1>{0x41820008},
        {{kProgramAddress, 0xC0002000}});
    test::require(
        conditional.find("nwii::runtime::ppc::relocated_branch_target("
                         "memory, 0x00001000, 0x41820008, 0xC0002000)") !=
            std::string::npos,
        "opcode-16 relocation override follows host relocation");

    require_rejected(
        [] {
            generate_native_function(
                "extsb_reserved_rb", kProgramAddress,
                std::array<uint32_t, 2>{x_form(31, 3, 4, 1, 954),
                                        0x4E800020},
                {});
        },
        "extsb reserved RB rejected");
    require_rejected(
        [] {
            generate_native_block(
                "mfspr_898_reserved", kProgramAddress,
                std::array<uint32_t, 1>{spr_form(3, 898, 339)}, {});
        },
        "mfspr 898 remains unsupported");
    require_rejected(
        [] {
            generate_native_block(
                "cntlzw_reserved_rb", kProgramAddress,
                std::array<uint32_t, 1>{x_form(31, 3, 4, 1, 26)}, {});
        },
        "cntlzw reserved RB rejected");
    require_rejected(
        [] {
            generate_native_block(
                "neg_reserved_rb", kProgramAddress,
                std::array<uint32_t, 1>{x_form(31, 3, 4, 1, 104)}, {});
        },
        "neg reserved RB rejected");
}
} // namespace

int main() {
    test_exact_source();
    test_rejections();
    test_final_direct_branch();
    test_native_block_generation();
    test_native_parity();
    test_reached_subfze_native_parity();
    test_subfzeo_native_parity();
    test_divwo_native_parity();
    test_reached_crxor_native_parity();
    test_reached_fnmsubs_native_parity();
    test_reached_fmsubs_native_parity();
    test_reached_paired_native_parity();
    test_startup_surface_native_parity();
    test_reached_words_native_parity();
    test_startup_branch_native_parity();
    test_native_budget_fallback();
    test_native_fault_context();
    test_startup_surface_generation();
    return 0;
}
