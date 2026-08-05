#pragma once

#include "runtime/ppc_semantics.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace nwii::runtime::ppc {
namespace {
constexpr uint32_t kXerSo = uint32_t{1} << 31;
constexpr uint32_t kXerCa = uint32_t{1} << 29;
constexpr uint32_t kXerOv = uint32_t{1} << 30;

// Espresso/Broadway frsqrte estimate table and algorithm, source-verified
// against Dolphin Common/FloatUtils.cpp (frsqrte_expected +
// ApproximateReciprocalSquareRoot). Bit-exact to the hardware estimate, not a
// full-precision reciprocal square root.
struct FrsqrteEntry {
    int base;
    int dec;
};
constexpr std::array<FrsqrteEntry, 32> kFrsqrteTable{{
    {0x1a7e800, -0x568}, {0x17cb800, -0x4f3}, {0x1552800, -0x48d},
    {0x130c000, -0x435}, {0x10f2000, -0x3e7}, {0x0eff000, -0x3a2},
    {0x0d2e000, -0x365}, {0x0b7c000, -0x32e}, {0x09e5000, -0x2fc},
    {0x0867000, -0x2d0}, {0x06ff000, -0x2a8}, {0x05ab800, -0x283},
    {0x046a000, -0x261}, {0x0339800, -0x243}, {0x0218800, -0x226},
    {0x0105800, -0x20b}, {0x3ffa000, -0x7a4}, {0x3c29000, -0x700},
    {0x38aa000, -0x670}, {0x3572000, -0x5f2}, {0x3279000, -0x584},
    {0x2fb7000, -0x524}, {0x2d26000, -0x4cc}, {0x2ac0000, -0x47e},
    {0x2881000, -0x43a}, {0x2665000, -0x3fa}, {0x2468000, -0x3c2},
    {0x2287000, -0x38e}, {0x20c1000, -0x35e}, {0x1f12000, -0x332},
    {0x1d79000, -0x30a}, {0x1bf4000, -0x2e6},
}};
constexpr std::array<FrsqrteEntry, 32> kFresTable{{
    {0x7ff800, 0x3e1}, {0x783800, 0x3a7}, {0x70ea00, 0x371},
    {0x6a0800, 0x340}, {0x638800, 0x313}, {0x5d6200, 0x2ea},
    {0x579000, 0x2c4}, {0x520800, 0x2a0}, {0x4cc800, 0x27f},
    {0x47ca00, 0x261}, {0x430800, 0x245}, {0x3e8000, 0x22a},
    {0x3a2c00, 0x212}, {0x360800, 0x1fb}, {0x321400, 0x1e5},
    {0x2e4a00, 0x1d1}, {0x2aa800, 0x1be}, {0x272c00, 0x1ac},
    {0x23d600, 0x19b}, {0x209e00, 0x18b}, {0x1d8800, 0x17c},
    {0x1a9000, 0x16e}, {0x17ae00, 0x15b}, {0x14f800, 0x15b},
    {0x124400, 0x143}, {0x0fbe00, 0x143}, {0x0d3800, 0x12d},
    {0x0ade00, 0x12d}, {0x088400, 0x11a}, {0x065000, 0x11a},
    {0x041c00, 0x108}, {0x020c00, 0x106},
}};

inline double approximate_reciprocal(double val) {
    int64_t integral = std::bit_cast<int64_t>(val);
    const int64_t mantissa = integral & ((int64_t{1} << 52) - 1);
    const int64_t sign =
        integral & std::bit_cast<int64_t>(uint64_t{1} << 63);
    int64_t exponent = integral & (int64_t{0x7FF} << 52);
    if (mantissa == 0 && exponent == 0) {
        return std::copysign(std::numeric_limits<double>::infinity(), val);
    }
    if (exponent == (int64_t{0x7FF} << 52)) {
        if (mantissa == 0) {
            return std::copysign(0.0, val);
        }
        return std::bit_cast<double>(integral | (int64_t{1} << 51));
    }
    if (exponent < (int64_t{895} << 52)) {
        return std::copysign(std::numeric_limits<float>::max(), val);
    }
    if (exponent >= (int64_t{1149} << 52)) {
        return std::copysign(0.0, val);
    }
    exponent = (int64_t{0x7FD} << 52) - exponent;
    const int index = static_cast<int>(mantissa >> 37);
    const FrsqrteEntry& entry = kFresTable[index / 1024];
    integral = sign | exponent;
    integral |= static_cast<int64_t>(
                    entry.base - (entry.dec * (index % 1024) + 1) / 2)
                << 29;
    return std::bit_cast<double>(integral);
}

inline double approximate_rsqrt(double val) {
    int64_t integral = std::bit_cast<int64_t>(val);
    int64_t mantissa = integral & ((int64_t{1} << 52) - 1);
    const int64_t sign =
        integral & std::bit_cast<int64_t>(uint64_t{1} << 63);
    int64_t exponent = integral & (int64_t{0x7FF} << 52);

    if (mantissa == 0 && exponent == 0) {
        return sign ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::infinity();
    }
    if (exponent == (int64_t{0x7FF} << 52)) {
        if (mantissa == 0) {
            return sign ? std::numeric_limits<double>::quiet_NaN() : 0.0;
        }
        return std::bit_cast<double>(integral | (int64_t{1} << 51));
    }
    if (sign) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (!exponent) {
        do {
            exponent -= int64_t{1} << 52;
            mantissa <<= 1;
        } while (!(mantissa & (int64_t{1} << 52)));
        mantissa &= (int64_t{1} << 52) - 1;
        exponent += int64_t{1} << 52;
    }
    const int64_t exponent_lsb = exponent & (int64_t{1} << 52);
    exponent = ((int64_t{0x3FF} << 52) -
                ((exponent - (int64_t{0x3FE} << 52)) / 2)) &
               (int64_t{0x7FF} << 52);
    integral = sign | exponent;
    const int index = static_cast<int>((exponent_lsb | mantissa) >> 37);
    const FrsqrteEntry& entry = kFrsqrteTable[index / 2048];
    integral |= static_cast<int64_t>(entry.base + entry.dec * (index % 2048))
                << 26;
    return std::bit_cast<double>(integral);
}
inline uint32_t reverse_bytes(uint32_t value) {
    return (value >> 24) | ((value >> 8) & 0x0000FF00) |
           ((value << 8) & 0x00FF0000) | (value << 24);
}

inline uint32_t effective_address(const CPUContext& cpu, uint32_t ra,
                           int32_t displacement) {
    const uint32_t base = ra == 0 ? 0 : cpu.gpr[ra];
    return base + static_cast<uint32_t>(displacement);
}

inline uint32_t indexed_address(const CPUContext& cpu, uint32_t ra, uint32_t rb) {
    return (ra == 0 ? 0 : cpu.gpr[ra]) + cpu.gpr[rb];
}

inline void require_aligned(const CPUContext& cpu, uint32_t address, uint32_t width,
                     MemoryAccess access) {
    if ((address & (width - 1)) != 0) {
        throw GuestFault("unaligned guest access", address, width, cpu.pc,
                         access);
    }
}

inline void require_aligned_target(const CPUContext& cpu, uint32_t target) {
    if ((target & 3) != 0) {
        throw GuestFault("unaligned branch target", target, 4, cpu.pc,
                         MemoryAccess::execute);
    }
}

inline void require_update_form(const CPUContext& cpu, uint32_t rt, uint32_t ra,
                         bool load) {
    if (ra == 0 || (load && ra == rt)) {
        throw GuestFault("reserved update form", cpu.pc, 4, cpu.pc,
                         MemoryAccess::execute);
    }
}

inline int32_t signed32(uint32_t value) { return std::bit_cast<int32_t>(value); }

inline double raw_double(const CPUContext& cpu, uint32_t reg) {
    return std::bit_cast<double>(cpu.fpr[reg][0]);
}

inline void set_double(CPUContext& cpu, uint32_t reg, double value) {
    cpu.fpr[reg][0] = std::bit_cast<uint64_t>(value);
}

// Single-precision scalar arithmetic on Gekko/Broadway/Espresso writes the
// result to BOTH paired-single lanes, not just ps0. Leaving ps1 stale is
// invisible until a later paired-single op reads it, at which point vector
// math silently consumes a value from an unrelated earlier computation.
inline void set_single(CPUContext& cpu, uint32_t reg, double value) {
    const uint64_t bits = std::bit_cast<uint64_t>(value);
    cpu.fpr[reg][0] = bits;
    cpu.fpr[reg][1] = bits;
}

inline void set_cr_field(CPUContext& cpu, uint32_t field, bool less, bool greater,
                  bool equal, bool low_bit) {
    cpu.cr[field] = static_cast<uint8_t>((less ? 8 : 0) |
                                         (greater ? 4 : 0) |
                                         (equal ? 2 : 0) |
                                         (low_bit ? 1 : 0));
}

inline void set_integer_cr(CPUContext& cpu, uint32_t field, uint32_t value) {
    set_cr_field(cpu, field, (value & 0x80000000) != 0,
                 value != 0 && (value & 0x80000000) == 0, value == 0,
                 (cpu.xer & kXerSo) != 0);
}

inline void set_ca(CPUContext& cpu, bool carry) {
    cpu.xer = (cpu.xer & ~kXerCa) | (carry ? kXerCa : 0);
}

inline void set_ov(CPUContext& cpu, bool overflow) {
    cpu.xer = (cpu.xer & ~kXerOv) | (overflow ? kXerOv : 0);
    if (overflow) {
        cpu.xer |= kXerSo;
    }
}

inline bool cr_bit(const CPUContext& cpu, uint32_t bi) {
    return ((cpu.cr[bi / 4] >> (3 - bi % 4)) & 1) != 0;
}

inline void conditional_branch(CPUContext& cpu, uint32_t target, uint32_t bo,
                        uint32_t bi, bool link, bool allow_ctr_decrement) {
    uint32_t next_ctr = cpu.ctr;
    bool ctr_ok = true;
    if ((bo & 0x04) == 0) {
        if (!allow_ctr_decrement) {
            throw GuestFault("bcctr cannot decrement CTR", cpu.pc, 4, cpu.pc,
                             MemoryAccess::execute);
        }
        --next_ctr;
        ctr_ok = (next_ctr != 0) != ((bo & 0x02) != 0);
    }
    bool condition_ok = true;
    if ((bo & 0x10) == 0) {
        condition_ok = cr_bit(cpu, bi) == ((bo & 0x08) != 0);
    }
    const bool take = ctr_ok && condition_ok;
    if (take) {
        require_aligned_target(cpu, target);
    }

    cpu.ctr = next_ctr;
    if (link) {
        cpu.lr = cpu.pc + 4;
    }
    cpu.pc = take ? target : cpu.pc + 4;
}
} // namespace


inline void mulli(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate) {
    const int64_t result = static_cast<int64_t>(signed32(cpu.gpr[ra])) *
                           static_cast<int64_t>(immediate);
    cpu.gpr[rt] = static_cast<uint32_t>(static_cast<uint64_t>(result));
    cpu.pc += 4;
}
inline void subfic(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate) {
    const uint64_t result = static_cast<uint64_t>(~cpu.gpr[ra]) +
                            static_cast<uint32_t>(immediate) + 1;
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    cpu.pc += 4;
}


inline void cmpli(CPUContext& cpu, uint32_t bf, uint32_t ra, uint32_t immediate) {
    const uint32_t value = cpu.gpr[ra];
    set_cr_field(cpu, bf, value < immediate, value > immediate,
                 value == immediate, (cpu.xer & kXerSo) != 0);
    cpu.pc += 4;
}

inline void cmpi(CPUContext& cpu, uint32_t bf, uint32_t ra, int32_t immediate) {
    const int32_t value = signed32(cpu.gpr[ra]);
    set_cr_field(cpu, bf, value < immediate, value > immediate,
                 value == immediate, (cpu.xer & kXerSo) != 0);
    cpu.pc += 4;
}

inline void addic(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate,
           bool record) {
    const uint64_t result = static_cast<uint64_t>(cpu.gpr[ra]) +
                            static_cast<uint32_t>(immediate);
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (record) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void addi(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate) {
    cpu.gpr[rt] = (ra == 0 ? 0 : cpu.gpr[ra]) +
                  static_cast<uint32_t>(immediate);
    cpu.pc += 4;
}

inline void addis(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate) {
    cpu.gpr[rt] = (ra == 0 ? 0 : cpu.gpr[ra]) +
                  (static_cast<uint32_t>(immediate) << 16);
    cpu.pc += 4;
}

inline void rlwinm(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t sh,
            uint32_t mb, uint32_t me, bool rc) {
    const uint32_t rotated = std::rotl(cpu.gpr[rs], static_cast<int>(sh));
    const uint32_t left = std::numeric_limits<uint32_t>::max() >> mb;
    const uint32_t right = std::numeric_limits<uint32_t>::max() << (31 - me);
    const uint32_t mask = mb <= me ? left & right : left | right;
    cpu.gpr[ra] = rotated & mask;
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}
inline void rlwnm(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb,
           uint32_t mb, uint32_t me, bool rc) {
    const uint32_t rotated =
        std::rotl(cpu.gpr[rs], static_cast<int>(cpu.gpr[rb] & 31));
    const uint32_t left = std::numeric_limits<uint32_t>::max() >> mb;
    const uint32_t right = std::numeric_limits<uint32_t>::max() << (31 - me);
    const uint32_t mask = mb <= me ? left & right : left | right;
    cpu.gpr[ra] = rotated & mask;
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}

inline void rlwimi(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t sh,
            uint32_t mb, uint32_t me, bool rc) {
    const uint32_t rotated = std::rotl(cpu.gpr[rs], static_cast<int>(sh));
    const uint32_t left = std::numeric_limits<uint32_t>::max() >> mb;
    const uint32_t right = std::numeric_limits<uint32_t>::max() << (31 - me);
    const uint32_t mask = mb <= me ? left & right : left | right;
    cpu.gpr[ra] = (rotated & mask) | (cpu.gpr[ra] & ~mask);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}


inline void ori(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t immediate) {
    cpu.gpr[ra] = cpu.gpr[rs] | immediate;
    cpu.pc += 4;
}

inline void xori(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t immediate) {
    cpu.gpr[ra] = cpu.gpr[rs] ^ immediate;
    cpu.pc += 4;
}

inline void andi_dot(CPUContext& cpu, uint32_t ra, uint32_t rs,
              uint32_t immediate) {
    cpu.gpr[ra] = cpu.gpr[rs] & immediate;
    set_integer_cr(cpu, 0, cpu.gpr[ra]);
    cpu.pc += 4;
}

inline void cmp(CPUContext& cpu, uint32_t bf, uint32_t ra, uint32_t rb) {
    const int32_t a = signed32(cpu.gpr[ra]);
    const int32_t b = signed32(cpu.gpr[rb]);
    set_cr_field(cpu, bf, a < b, a > b, a == b,
                 (cpu.xer & kXerSo) != 0);
    cpu.pc += 4;
}

inline void cmpl(CPUContext& cpu, uint32_t bf, uint32_t ra, uint32_t rb) {
    const uint32_t a = cpu.gpr[ra];
    const uint32_t b = cpu.gpr[rb];
    set_cr_field(cpu, bf, a < b, a > b, a == b,
                 (cpu.xer & kXerSo) != 0);
    cpu.pc += 4;
}

inline void tw(CPUContext& cpu, uint32_t to, uint32_t ra, uint32_t rb) {
    const uint32_t ua = cpu.gpr[ra];
    const uint32_t ub = cpu.gpr[rb];
    const int32_t a = signed32(ua);
    const int32_t b = signed32(ub);
    const bool trap = ((to & 0x10) != 0 && a < b) ||
                      ((to & 0x08) != 0 && a > b) ||
                      ((to & 0x04) != 0 && a == b) ||
                      ((to & 0x02) != 0 && ua < ub) ||
                      ((to & 0x01) != 0 && ua > ub);
    if (trap) {
        throw GuestFault("tw condition met", cpu.pc, 4, cpu.pc,
                         MemoryAccess::execute);
    }
    cpu.pc += 4;
}
inline void mfcr(CPUContext& cpu, uint32_t rt) {
    uint32_t value = 0;
    for (const uint8_t field : cpu.cr) {
        value = (value << 4) | field;
    }
    cpu.gpr[rt] = value;
    cpu.pc += 4;
}


inline void subf(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    cpu.gpr[rt] = cpu.gpr[rb] - cpu.gpr[ra];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void subfc(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const uint64_t result = static_cast<uint64_t>(cpu.gpr[rb]) +
                            static_cast<uint64_t>(~cpu.gpr[ra]) + 1;
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void subfe(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const uint64_t result = static_cast<uint64_t>(cpu.gpr[rb]) +
                            static_cast<uint64_t>(~cpu.gpr[ra]) +
                            ((cpu.xer & kXerCa) != 0 ? 1 : 0);
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}
inline void subfze(CPUContext& cpu, uint32_t rt, uint32_t ra, bool oe, bool rc) {
    const uint32_t a = cpu.gpr[ra];
    const bool carry_in = (cpu.xer & kXerCa) != 0;
    const uint64_t result = static_cast<uint64_t>(~a) + carry_in;
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (oe) {
        set_ov(cpu, carry_in && a == 0x80000000);
    }
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}


inline void neg(CPUContext& cpu, uint32_t rt, uint32_t ra, bool rc) {
    cpu.gpr[rt] = 0U - cpu.gpr[ra];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void cntlzw(CPUContext& cpu, uint32_t ra, uint32_t rs, bool rc) {
    cpu.gpr[ra] = std::countl_zero(cpu.gpr[rs]);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}


inline void and_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    cpu.gpr[ra] = cpu.gpr[rs] & cpu.gpr[rb];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}
inline void andc(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    cpu.gpr[ra] = cpu.gpr[rs] & ~cpu.gpr[rb];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}

inline void nor_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    cpu.gpr[ra] = ~(cpu.gpr[rs] | cpu.gpr[rb]);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}

inline void mulhwu(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const uint64_t product = static_cast<uint64_t>(cpu.gpr[ra]) * cpu.gpr[rb];
    cpu.gpr[rt] = static_cast<uint32_t>(product >> 32);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}
inline void mulhw(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const int64_t product = static_cast<int64_t>(signed32(cpu.gpr[ra])) *
                            signed32(cpu.gpr[rb]);
    cpu.gpr[rt] =
        static_cast<uint32_t>(std::bit_cast<uint64_t>(product) >> 32);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void divwu(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const uint32_t divisor = cpu.gpr[rb];
    cpu.gpr[rt] = divisor == 0 ? 0 : cpu.gpr[ra] / divisor;
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}
inline void divw(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool oe,
          bool rc) {
    const int32_t dividend = static_cast<int32_t>(cpu.gpr[ra]);
    const int32_t divisor = static_cast<int32_t>(cpu.gpr[rb]);
    int32_t quotient = 0;
    if (divisor != 0) {
        quotient =
            dividend == std::numeric_limits<int32_t>::min() && divisor == -1
                ? dividend
                : dividend / divisor;
    }
    cpu.gpr[rt] = static_cast<uint32_t>(quotient);
    if (oe) {
        set_ov(cpu, divisor == 0 ||
                        (dividend == std::numeric_limits<int32_t>::min() &&
                         divisor == -1));
    }
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}


inline void mullw(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const uint64_t product = static_cast<uint64_t>(cpu.gpr[ra]) * cpu.gpr[rb];
    cpu.gpr[rt] = static_cast<uint32_t>(product);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void add(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    cpu.gpr[rt] = cpu.gpr[ra] + cpu.gpr[rb];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void addc(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc) {
    const uint64_t result =
        static_cast<uint64_t>(cpu.gpr[ra]) + cpu.gpr[rb];
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}
inline void addze(CPUContext& cpu, uint32_t rt, uint32_t ra, bool oe, bool rc) {
    const uint32_t a = cpu.gpr[ra];
    const bool carry_in = (cpu.xer & kXerCa) != 0;
    const uint64_t result = static_cast<uint64_t>(a) + carry_in;
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (oe) {
        set_ov(cpu, carry_in && a == 0x7FFFFFFF);
    }
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}
inline void adde(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool oe,
          bool rc) {
    const uint32_t a = cpu.gpr[ra];
    const uint32_t b = cpu.gpr[rb];
    const uint32_t carry = (cpu.xer & kXerCa) != 0;
    const uint64_t result =
        static_cast<uint64_t>(a) + static_cast<uint64_t>(b) + carry;
    cpu.gpr[rt] = static_cast<uint32_t>(result);
    set_ca(cpu, (result >> 32) != 0);
    if (oe) {
        const int64_t signed_result =
            static_cast<int64_t>(signed32(a)) + signed32(b) + carry;
        set_ov(cpu, signed_result < std::numeric_limits<int32_t>::min() ||
                        signed_result > std::numeric_limits<int32_t>::max());
    }
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[rt]);
    }
    cpu.pc += 4;
}

inline void slw(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    const uint32_t shift = cpu.gpr[rb] & 0x3F;
    cpu.gpr[ra] = shift >= 32 ? 0 : cpu.gpr[rs] << shift;
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}
inline void srw(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    const uint32_t shift = cpu.gpr[rb] & 0x3F;
    cpu.gpr[ra] = shift >= 32 ? 0 : cpu.gpr[rs] >> shift;
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}


inline void or_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    cpu.gpr[ra] = cpu.gpr[rs] | cpu.gpr[rb];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}
inline void orc(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    cpu.gpr[ra] = cpu.gpr[rs] | ~cpu.gpr[rb];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}

inline void xor_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    cpu.gpr[ra] = cpu.gpr[rs] ^ cpu.gpr[rb];
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}
inline void srawi(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t sh, bool rc) {
    const uint32_t value = cpu.gpr[rs];
    const bool negative = (value & 0x80000000) != 0;
    const uint32_t shifted =
        sh == 0 ? value
                : (value >> sh) |
                      (negative ? (~uint32_t{0} << (32 - sh)) : 0);
    const uint32_t discarded =
        sh == 0 ? 0 : value & ((uint32_t{1} << sh) - 1);
    cpu.gpr[ra] = shifted;
    set_ca(cpu, negative && discarded != 0);
    if (rc) {
        set_integer_cr(cpu, 0, shifted);
    }
    cpu.pc += 4;
}
inline void sraw(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc) {
    const uint32_t value = cpu.gpr[rs];
    const uint32_t shift = cpu.gpr[rb] & 0x3F;
    const bool negative = (value & 0x80000000) != 0;
    const uint32_t shifted =
        shift == 0
            ? value
            : shift >= 32
                  ? (negative ? std::numeric_limits<uint32_t>::max() : 0)
                  : (value >> shift) |
                        (negative ? (~uint32_t{0} << (32 - shift)) : 0);
    const uint32_t discarded =
        shift == 0
            ? 0
            : shift >= 32 ? value : value & ((uint32_t{1} << shift) - 1);
    cpu.gpr[ra] = shifted;
    set_ca(cpu, negative && discarded != 0);
    if (rc) {
        set_integer_cr(cpu, 0, shifted);
    }
    cpu.pc += 4;
}

inline void extsb(CPUContext& cpu, uint32_t ra, uint32_t rs, bool rc) {
    cpu.gpr[ra] = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int8_t>(cpu.gpr[rs])));
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}
inline void extsh(CPUContext& cpu, uint32_t ra, uint32_t rs, bool rc) {
    cpu.gpr[ra] = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int16_t>(cpu.gpr[rs])));
    if (rc) {
        set_integer_cr(cpu, 0, cpu.gpr[ra]);
    }
    cpu.pc += 4;
}


inline void mfspr(CPUContext& cpu, uint32_t rt, uint32_t spr) {
    if (spr == 8) {
        cpu.gpr[rt] = cpu.lr;
    } else if (spr == 9) {
        cpu.gpr[rt] = cpu.ctr;
    } else {
        throw GuestFault("unsupported SPR", cpu.pc, 4, cpu.pc,
                         MemoryAccess::execute);
    }
    cpu.pc += 4;
}

inline void mtspr(CPUContext& cpu, uint32_t rs, uint32_t spr) {
    if (spr == 8) {
        cpu.lr = cpu.gpr[rs];
    } else if (spr == 9) {
        cpu.ctr = cpu.gpr[rs];
    } else if (spr < 898 || spr > 901) {
        throw GuestFault("unsupported SPR", cpu.pc, 4, cpu.pc,
                         MemoryAccess::execute);
    }
    // Reached Espresso implementation-control writes have no user-visible
    // effect in the unified-memory HLE runtime.
    cpu.pc += 4;
}

inline void lwz(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    cpu.gpr[rt] = memory.read32(address, cpu.pc);
    cpu.pc += 4;
}

inline void lwzu(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const uint32_t value = memory.read32(address, cpu.pc);
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}
inline void lhzu(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const uint32_t value = memory.read16(address, cpu.pc);
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void lhz(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    cpu.gpr[rt] = memory.read16(address, cpu.pc);
    cpu.pc += 4;
}

inline void lha(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    cpu.gpr[rt] = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int16_t>(
            memory.read16(address, cpu.pc))));
    cpu.pc += 4;
}
inline void lhau(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const uint32_t value = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int16_t>(
            memory.read16(address, cpu.pc))));
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void lbz(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement) {
    cpu.gpr[rt] = memory.read8(effective_address(cpu, ra, displacement), cpu.pc);
    cpu.pc += 4;
}

inline void lbzu(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const uint32_t value = memory.read8(address, cpu.pc);
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void stw(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    if (std::getenv("NWIIU_MATRIX_TRACE") != nullptr &&
        cpu.pc >= 0x02877A8Cu && cpu.pc <= 0x02877AA4u) {
        std::fprintf(stderr,
                     "MATRIX-STW pc=%08X r9=%08X r11=%08X rs=%u ra=%u\n",
                     cpu.pc, cpu.gpr[9], cpu.gpr[11], rs, ra);
    }
    memory.write32(address, cpu.gpr[rs], cpu.pc);
    cpu.pc += 4;
}

inline void stwu(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rs, ra, false);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const uint32_t value = cpu.gpr[rs];
    memory.write32(address, value, cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void stb(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
         int32_t displacement) {
    memory.write8(effective_address(cpu, ra, displacement),
                  static_cast<uint8_t>(cpu.gpr[rs]), cpu.pc);
    cpu.pc += 4;
}

inline void stbu(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rs, ra, false);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const uint8_t value = static_cast<uint8_t>(cpu.gpr[rs]);
    memory.write8(address, value, cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void sth(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    memory.write16(address, static_cast<uint16_t>(cpu.gpr[rs]), cpu.pc);
    cpu.pc += 4;
}
inline void sthu(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement) {
    require_update_form(cpu, rs, ra, false);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    memory.write16(address, static_cast<uint16_t>(cpu.gpr[rs]), cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}


inline void lmw(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement) {
    uint32_t address = effective_address(cpu, ra, displacement);
    for (uint32_t reg = rt; reg < cpu.gpr.size(); ++reg, address += 4) {
        cpu.gpr[reg] = memory.read32(address, cpu.pc);
    }
    cpu.pc += 4;
}

inline void stmw(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement) {
    uint32_t address = effective_address(cpu, ra, displacement);
    for (uint32_t reg = rs; reg < cpu.gpr.size(); ++reg, address += 4) {
        memory.write32(address, cpu.gpr[reg], cpu.pc);
    }
    cpu.pc += 4;
}

inline void lwzx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    cpu.gpr[rt] = memory.read32(address, cpu.pc);
    cpu.pc += 4;
}
inline void lwzux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const uint32_t value = memory.read32(address, cpu.pc);
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}
inline void lwarx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    require_aligned(cpu, address, 4, MemoryAccess::read);
    const uint32_t reserved = memory.read32(address, cpu.pc);
    cpu.gpr[rt] = reserved;
    cpu.reservation_address = address;
    cpu.reservation_value = reserved;
    cpu.reservation_valid = true;
    cpu.pc += 4;
}

inline void lwbrx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    cpu.gpr[rt] = reverse_bytes(memory.read32(address, cpu.pc));
    cpu.pc += 4;
}

inline void lbzux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const uint32_t value = memory.read8(address, cpu.pc);
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void lhaux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const uint32_t value = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int16_t>(
            memory.read16(address, cpu.pc))));
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void lbzx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb) {
    cpu.gpr[rt] = memory.read8(indexed_address(cpu, ra, rb), cpu.pc);
    cpu.pc += 4;
}

inline void lhax(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    cpu.gpr[rt] = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int16_t>(
            memory.read16(address, cpu.pc))));
    cpu.pc += 4;
}
inline void lhzx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    cpu.gpr[rt] = memory.read16(address, cpu.pc);
    cpu.pc += 4;
}
inline void lhzux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rt, ra, true);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const uint32_t value = memory.read16(address, cpu.pc);
    cpu.gpr[rt] = value;
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}


inline void stwx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    memory.write32(address, cpu.gpr[rs], cpu.pc);
    cpu.pc += 4;
}
inline void stwux(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rs, ra, false);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const uint32_t value = cpu.gpr[rs];
    memory.write32(address, value, cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}
inline void stwcx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    require_aligned(cpu, address, 4, MemoryAccess::write);
    // Matches the host's reservation semantics: a conflicting write between
    // lwarx and stwcx. must break the reservation, not just a lost address.
    const bool stored = cpu.reservation_valid &&
                        cpu.reservation_address == address &&
                        memory.read32(address, cpu.pc) == cpu.reservation_value;
    if (stored) {
        memory.write32(address, cpu.gpr[rs], cpu.pc);
    }
    cpu.reservation_valid = false;
    cpu.cr[0] = static_cast<uint8_t>((stored ? 2 : 0) |
                                     ((cpu.xer & kXerSo) != 0 ? 1 : 0));
    cpu.pc += 4;
}

inline void stwbrx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
            uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    memory.write32(address, reverse_bytes(cpu.gpr[rs]), cpu.pc);
    cpu.pc += 4;
}

inline void dcbf(CPUContext& cpu) { cpu.pc += 4; }
inline void dcbst(CPUContext& cpu) { cpu.pc += 4; }
inline void dcbz(CPUContext& cpu, GuestMemory& memory, uint32_t ra, uint32_t rb) {
    constexpr uint32_t kCacheLineSize = 32;
    const uint32_t address =
        indexed_address(cpu, ra, rb) & ~(kCacheLineSize - 1);
    memory.validate_range(address, kCacheLineSize, cpu.pc,
                          MemoryAccess::write);
    for (uint32_t offset = 0; offset < kCacheLineSize; offset += 4) {
        memory.write32(address + offset, 0, cpu.pc);
    }
    cpu.pc += 4;
}


inline void stbx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          uint32_t rb) {
    memory.write8(indexed_address(cpu, ra, rb),
                  static_cast<uint8_t>(cpu.gpr[rs]), cpu.pc);
    cpu.pc += 4;
}
inline void stbux(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rs, ra, false);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    memory.write8(address, static_cast<uint8_t>(cpu.gpr[rs]), cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}
inline void sthx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    memory.write16(address, static_cast<uint16_t>(cpu.gpr[rs]), cpu.pc);
    cpu.pc += 4;
}
inline void sthux(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, rs, ra, false);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    memory.write16(address, static_cast<uint16_t>(cpu.gpr[rs]), cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}
inline void lswi(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t byte_count) {
    const uint32_t count = byte_count == 0 ? 32 : byte_count;
    const uint32_t address = ra == 0 ? 0 : cpu.gpr[ra];
    memory.validate(address, count, cpu.pc, MemoryAccess::read);
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t reg = (rt + index / 4) & 31;
        if ((index & 3) == 0) {
            cpu.gpr[reg] = 0;
        }
        cpu.gpr[reg] |= static_cast<uint32_t>(
                            memory.read8(address + index, cpu.pc))
                        << (24 - (index & 3) * 8);
    }
    cpu.pc += 4;
}
inline void stswi(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t byte_count) {
    const uint32_t count = byte_count == 0 ? 32 : byte_count;
    const uint32_t address = ra == 0 ? 0 : cpu.gpr[ra];
    memory.validate(address, count, cpu.pc, MemoryAccess::write);
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t reg = (rs + index / 4) & 31;
        memory.write8(address + index,
                      static_cast<uint8_t>(cpu.gpr[reg] >>
                                           (24 - (index & 3) * 8)),
                      cpu.pc);
    }
    cpu.pc += 4;
}



inline void stfiwx(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
            uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    memory.write32(address, static_cast<uint32_t>(cpu.fpr[frs][0]), cpu.pc);
    cpu.pc += 4;
}

inline void stfsx(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
           uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    const float value = static_cast<float>(raw_double(cpu, frs));
    memory.write32(address, std::bit_cast<uint32_t>(value), cpu.pc);
    cpu.pc += 4;
}
inline void lfsx(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
          uint32_t rb) {
    const uint32_t address = indexed_address(cpu, ra, rb);
    const float value = std::bit_cast<float>(memory.read32(address, cpu.pc));
    set_single(cpu, frt, static_cast<double>(value));
    cpu.pc += 4;
}
inline void lfsux(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
           uint32_t rb) {
    require_update_form(cpu, frt, ra, false);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const float value = std::bit_cast<float>(memory.read32(address, cpu.pc));
    set_single(cpu, frt, static_cast<double>(value));
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}
inline void stfsux(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
            uint32_t rb) {
    require_update_form(cpu, frs, ra, false);
    const uint32_t address = cpu.gpr[ra] + cpu.gpr[rb];
    const float value = static_cast<float>(raw_double(cpu, frs));
    memory.write32(address, std::bit_cast<uint32_t>(value), cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}



inline void lfs(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    const float value = std::bit_cast<float>(memory.read32(address, cpu.pc));
    set_single(cpu, frt, static_cast<double>(value));
    cpu.pc += 4;
}
inline void lfsu(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
           int32_t displacement) {
    require_update_form(cpu, frt, ra, false);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const float value = std::bit_cast<float>(memory.read32(address, cpu.pc));
    set_single(cpu, frt, static_cast<double>(value));
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}


inline void lfd(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
         int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    cpu.fpr[frt][0] = memory.read64(address, cpu.pc);
    cpu.pc += 4;
}

inline void stfs(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
          int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    const float value = static_cast<float>(raw_double(cpu, frs));
    memory.write32(address, std::bit_cast<uint32_t>(value), cpu.pc);
    cpu.pc += 4;
}

inline void stfsu(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
           int32_t displacement) {
    require_update_form(cpu, frs, ra, false);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    const float value = static_cast<float>(raw_double(cpu, frs));
    memory.write32(address, std::bit_cast<uint32_t>(value), cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void stfd(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
          int32_t displacement) {
    const uint32_t address = effective_address(cpu, ra, displacement);
    memory.write64(address, cpu.fpr[frs][0], cpu.pc);
    cpu.pc += 4;
}
inline void stfdu(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
           int32_t displacement) {
    require_update_form(cpu, frs, ra, false);
    const uint32_t address = cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    memory.write64(address, cpu.fpr[frs][0], cpu.pc);
    cpu.gpr[ra] = address;
    cpu.pc += 4;
}

inline void fdivs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    set_single(cpu, frt, static_cast<double>(
                             static_cast<float>(raw_double(cpu, fra) /
                                                raw_double(cpu, frb))));
    cpu.pc += 4;
}
inline void fres(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    // Espresso estimate, source-verified against Dolphin FloatUtils.
    set_single(cpu, frt, approximate_reciprocal(raw_double(cpu, frb)));
    cpu.pc += 4;
}
inline void fadds(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    set_single(cpu, frt, static_cast<double>(
                             static_cast<float>(raw_double(cpu, fra) +
                                                raw_double(cpu, frb))));
    cpu.pc += 4;
}
inline void fmuls(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc) {
    set_single(cpu, frt, static_cast<double>(
                             static_cast<float>(raw_double(cpu, fra) *
                                                raw_double(cpu, frc))));
    cpu.pc += 4;
}
inline void fmul(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc) {
    set_double(cpu, frt, raw_double(cpu, fra) * raw_double(cpu, frc));
    cpu.pc += 4;
}

inline void fmadds(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
            uint32_t frc) {
    set_single(cpu, frt,
               static_cast<double>(static_cast<float>(
                   std::fma(raw_double(cpu, fra), raw_double(cpu, frc),
                            raw_double(cpu, frb)))));
    cpu.pc += 4;
}
inline void fmadd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
           uint32_t frc) {
    set_double(cpu, frt,
               std::fma(raw_double(cpu, fra), raw_double(cpu, frc),
                        raw_double(cpu, frb)));
    cpu.pc += 4;
}
inline void fnmsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
            uint32_t frc) {
    set_double(cpu, frt,
               -std::fma(raw_double(cpu, fra), raw_double(cpu, frc),
                         -raw_double(cpu, frb)));
    cpu.pc += 4;
}
inline void fmsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
           uint32_t frc) {
    set_double(cpu, frt,
               std::fma(raw_double(cpu, fra), raw_double(cpu, frc),
                        -raw_double(cpu, frb)));
    cpu.pc += 4;
}

inline void fnmsubs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
             uint32_t frc) {
    set_single(cpu, frt,
               static_cast<double>(static_cast<float>(
                   -std::fma(raw_double(cpu, fra), raw_double(cpu, frc),
                             -raw_double(cpu, frb)))));
    cpu.pc += 4;
}

inline void fmsubs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
            uint32_t frc) {
    set_single(cpu, frt,
               static_cast<double>(static_cast<float>(
                   std::fma(raw_double(cpu, fra), raw_double(cpu, frc),
                            -raw_double(cpu, frb)))));
    cpu.pc += 4;
}




inline void fsubs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    set_single(cpu, frt, static_cast<double>(
                             static_cast<float>(raw_double(cpu, fra) -
                                                raw_double(cpu, frb))));
    cpu.pc += 4;
}

inline void fsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    set_double(cpu, frt, raw_double(cpu, fra) - raw_double(cpu, frb));
    cpu.pc += 4;
}
inline void fadd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    set_double(cpu, frt, raw_double(cpu, fra) + raw_double(cpu, frb));
    cpu.pc += 4;
}
inline void fdiv(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    set_double(cpu, frt, raw_double(cpu, fra) / raw_double(cpu, frb));
    cpu.pc += 4;
}



inline void frsp(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    set_single(cpu, frt,
               static_cast<double>(static_cast<float>(raw_double(cpu, frb))));
    cpu.pc += 4;
}

inline void fctiw(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    const double value = raw_double(cpu, frb);
    uint32_t result = 0x80000000;
    if (std::isfinite(value)) {
        double rounded{};
        switch (cpu.fpscr & 3) {
        case 0: {
            const double lower = std::floor(value);
            const double fraction = value - lower;
            rounded =
                fraction < 0.5
                    ? lower
                    : fraction > 0.5 || std::fmod(lower, 2.0) != 0
                          ? lower + 1
                          : lower;
            break;
        }
        case 1:
            rounded = std::trunc(value);
            break;
        case 2:
            rounded = std::ceil(value);
            break;
        default:
            rounded = std::floor(value);
            break;
        }
        if (rounded >=
                static_cast<double>(std::numeric_limits<int32_t>::min()) &&
            rounded < 2147483648.0) {
            result = std::bit_cast<uint32_t>(static_cast<int32_t>(rounded));
        }
    }
    cpu.fpr[frt][0] = result;
    cpu.pc += 4;
}

inline void fctiwz(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    const double value = raw_double(cpu, frb);
    uint32_t result = 0x80000000;
    if (std::isfinite(value) &&
        value >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
        value < 2147483648.0) {
        result = std::bit_cast<uint32_t>(
            static_cast<int32_t>(std::trunc(value)));
    }
    cpu.fpr[frt][0] = result;
    cpu.pc += 4;
}

inline void fmr(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    cpu.fpr[frt][0] = cpu.fpr[frb][0];
    cpu.pc += 4;
}
inline void fneg(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    cpu.fpr[frt][0] = cpu.fpr[frb][0] ^ (uint64_t{1} << 63);
    cpu.pc += 4;
}
inline void fabs(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    cpu.fpr[frt][0] = cpu.fpr[frb][0] & ~(uint64_t{1} << 63);
    cpu.pc += 4;
}
inline void fnabs(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    cpu.fpr[frt][0] = cpu.fpr[frb][0] | (uint64_t{1} << 63);
    cpu.pc += 4;
}

inline void frsqrte(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    set_double(cpu, frt, approximate_rsqrt(raw_double(cpu, frb)));
    cpu.pc += 4;
}

inline void fcmpu(CPUContext& cpu, uint32_t bf, uint32_t fra, uint32_t frb) {
    const double a = raw_double(cpu, fra);
    const double b = raw_double(cpu, frb);
    const bool unordered = std::isnan(a) || std::isnan(b);
    set_cr_field(cpu, bf, !unordered && a < b, !unordered && a > b,
                 !unordered && a == b, unordered);
    cpu.pc += 4;
}

inline void fsel(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
          uint32_t frb) {
    const double a = raw_double(cpu, fra);
    cpu.fpr[frt][0] = a >= 0.0 ? cpu.fpr[frc][0] : cpu.fpr[frb][0];
    cpu.pc += 4;
}

inline void ps_merge00(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    const uint64_t lane0 = cpu.fpr[fra][0];
    const uint64_t lane1 = cpu.fpr[frb][0];
    cpu.fpr[frt][0] = lane0;
    cpu.fpr[frt][1] = lane1;
    cpu.pc += 4;
}

inline void ps_merge10(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    const uint64_t lane0 = cpu.fpr[fra][1];
    const uint64_t lane1 = cpu.fpr[frb][0];
    cpu.fpr[frt][0] = lane0;
    cpu.fpr[frt][1] = lane1;
    cpu.pc += 4;
}

inline void ps_merge01(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    const uint64_t lane0 = cpu.fpr[fra][0];
    const uint64_t lane1 = cpu.fpr[frb][1];
    cpu.fpr[frt][0] = lane0;
    cpu.fpr[frt][1] = lane1;
    cpu.pc += 4;
}

inline void ps_merge11(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    const uint64_t lane0 = cpu.fpr[fra][1];
    const uint64_t lane1 = cpu.fpr[frb][1];
    cpu.fpr[frt][0] = lane0;
    cpu.fpr[frt][1] = lane1;
    cpu.pc += 4;
}

inline void ps_neg(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    cpu.fpr[frt][0] = cpu.fpr[frb][0] ^ (uint64_t{1} << 63);
    cpu.fpr[frt][1] = cpu.fpr[frb][1] ^ (uint64_t{1} << 63);
    cpu.pc += 4;
}

inline void ps_muls0(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc) {
    const float scalar =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frc][0]));
    const float lane0 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][0])) * scalar;
    const float lane1 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][1])) * scalar;
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_muls1(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc) {
    const double scalar = std::bit_cast<double>(cpu.fpr[frc][1]);
    const float lane0 = static_cast<float>(
        std::bit_cast<double>(cpu.fpr[fra][0]) * scalar);
    const float lane1 = static_cast<float>(
        std::bit_cast<double>(cpu.fpr[fra][1]) * scalar);
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_mul(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc) {
    const float lane0 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][0])) *
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frc][0]));
    const float lane1 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][1])) *
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frc][1]));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_madd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb) {
    const float lane0 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][0]),
                 std::bit_cast<double>(cpu.fpr[frc][0]),
                 std::bit_cast<double>(cpu.fpr[frb][0])));
    const float lane1 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][1]),
                 std::bit_cast<double>(cpu.fpr[frc][1]),
                 std::bit_cast<double>(cpu.fpr[frb][1])));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_madds0(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
               uint32_t frb) {
    const double scalar = std::bit_cast<double>(cpu.fpr[frc][0]);
    const float lane0 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][0]), scalar,
                 std::bit_cast<double>(cpu.fpr[frb][0])));
    const float lane1 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][1]), scalar,
                 std::bit_cast<double>(cpu.fpr[frb][1])));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_madds1(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
               uint32_t frb) {
    const double scalar = std::bit_cast<double>(cpu.fpr[frc][1]);
    const float lane0 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][0]), scalar,
                 std::bit_cast<double>(cpu.fpr[frb][0])));
    const float lane1 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][1]), scalar,
                 std::bit_cast<double>(cpu.fpr[frb][1])));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_sum0(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb) {
    const float a0 = static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][0]));
    const float b1 = static_cast<float>(std::bit_cast<double>(cpu.fpr[frb][1]));
    const uint64_t lane1 = cpu.fpr[frc][1];
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(a0 + b1));
    cpu.fpr[frt][1] = lane1;
    cpu.pc += 4;
}

inline void ps_sum1(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb) {
    const float lane0 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frc][0]));
    const float lane1 = static_cast<float>(
        std::bit_cast<double>(cpu.fpr[fra][0]) +
        std::bit_cast<double>(cpu.fpr[frb][1]));
    cpu.fpr[frt][0] =
        std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] =
        std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_add(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    const float lane0 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][0])) +
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frb][0]));
    const float lane1 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][1])) +
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frb][1]));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_sub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb) {
    const float lane0 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][0])) -
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frb][0]));
    const float lane1 =
        static_cast<float>(std::bit_cast<double>(cpu.fpr[fra][1])) -
        static_cast<float>(std::bit_cast<double>(cpu.fpr[frb][1]));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_msub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb) {
    const float lane0 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][0]),
                 std::bit_cast<double>(cpu.fpr[frc][0]),
                 -std::bit_cast<double>(cpu.fpr[frb][0])));
    const float lane1 = static_cast<float>(
        std::fma(std::bit_cast<double>(cpu.fpr[fra][1]),
                 std::bit_cast<double>(cpu.fpr[frc][1]),
                 -std::bit_cast<double>(cpu.fpr[frb][1])));
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(static_cast<double>(lane0));
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(static_cast<double>(lane1));
    cpu.pc += 4;
}

inline void ps_sel(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
            uint32_t frb) {
    const double a0 = std::bit_cast<double>(cpu.fpr[fra][0]);
    const double a1 = std::bit_cast<double>(cpu.fpr[fra][1]);
    const uint64_t lane0 = a0 >= 0.0 ? cpu.fpr[frc][0] : cpu.fpr[frb][0];
    const uint64_t lane1 = a1 >= 0.0 ? cpu.fpr[frc][1] : cpu.fpr[frb][1];
    cpu.fpr[frt][0] = lane0;
    cpu.fpr[frt][1] = lane1;
    cpu.pc += 4;
}

inline void ps_cmpo0(CPUContext& cpu, uint32_t bf, uint32_t fra, uint32_t frb) {
    const double a = std::bit_cast<double>(cpu.fpr[fra][0]);
    const double b = std::bit_cast<double>(cpu.fpr[frb][0]);
    const bool unordered = std::isnan(a) || std::isnan(b);
    set_cr_field(cpu, bf, !unordered && a < b, !unordered && a > b,
                 !unordered && a == b, unordered);
    cpu.pc += 4;
}
inline void ps_rsqrte(CPUContext& cpu, uint32_t frt, uint32_t frb) {
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const float result = static_cast<float>(
            approximate_rsqrt(std::bit_cast<double>(cpu.fpr[frb][lane])));
        cpu.fpr[frt][lane] =
            std::bit_cast<uint64_t>(static_cast<double>(result));
    }
    cpu.pc += 4;
}

inline void ps_nmsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
              uint32_t frb) {
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const float result = static_cast<float>(
            -std::fma(std::bit_cast<double>(cpu.fpr[fra][lane]),
                      std::bit_cast<double>(cpu.fpr[frc][lane]),
                      -std::bit_cast<double>(cpu.fpr[frb][lane])));
        cpu.fpr[frt][lane] =
            std::bit_cast<uint64_t>(static_cast<double>(result));
    }
    cpu.pc += 4;
}

inline void ps_nmadd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
              uint32_t frb) {
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const float result = static_cast<float>(
            -std::fma(std::bit_cast<double>(cpu.fpr[fra][lane]),
                      std::bit_cast<double>(cpu.fpr[frc][lane]),
                      std::bit_cast<double>(cpu.fpr[frb][lane])));
        cpu.fpr[frt][lane] =
            std::bit_cast<uint64_t>(static_cast<double>(result));
    }
    cpu.pc += 4;
}

static uint32_t psq_width(const CPUContext& cpu, uint32_t gqr,
                          MemoryAccess access) {
    if (gqr == 0) {
        return 4;
    }
    if (gqr == 2 || gqr == 4) {
        return 1;
    }
    if (gqr == 3 || gqr == 5) {
        return 2;
    }
    throw GuestFault("unsupported paired-single GQR index", cpu.pc, 4, cpu.pc,
                     access);
}

static double psq_read(const CPUContext& cpu, GuestMemory& memory,
                       uint32_t address, uint32_t gqr) {
    if (gqr == 0) {
        return static_cast<double>(
            std::bit_cast<float>(memory.read32(address, cpu.pc)));
    }
    if (gqr == 2) {
        return memory.read8(address, cpu.pc);
    }
    if (gqr == 3) {
        return memory.read16(address, cpu.pc);
    }
    if (gqr == 4) {
        return static_cast<int8_t>(memory.read8(address, cpu.pc));
    }
    return static_cast<int16_t>(memory.read16(address, cpu.pc));
}

static void psq_write(const CPUContext& cpu, GuestMemory& memory,
                      uint32_t address, uint32_t gqr, double value) {
    if (gqr == 0) {
        const float converted = static_cast<float>(value);
        memory.write32(address, std::bit_cast<uint32_t>(converted), cpu.pc);
        return;
    }
    value = static_cast<float>(value);
    value = std::isnan(value) ? 0.0 : std::trunc(value);
    if (gqr == 2) {
        memory.write8(address, static_cast<uint8_t>(
                                   std::clamp(value, 0.0, 255.0)), cpu.pc);
    } else if (gqr == 3) {
        memory.write16(address, static_cast<uint16_t>(
                                    std::clamp(value, 0.0, 65535.0)), cpu.pc);
    } else if (gqr == 4) {
        const auto converted = static_cast<int8_t>(
            std::clamp(value, -128.0, 127.0));
        memory.write8(address, static_cast<uint8_t>(converted), cpu.pc);
    } else {
        const auto converted = static_cast<int16_t>(
            std::clamp(value, -32768.0, 32767.0));
        memory.write16(address, static_cast<uint16_t>(converted), cpu.pc);
    }
}

inline void psq_l(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
           bool w, uint32_t gqr, int32_t displacement) {
    // Cafe OS fixes GQR2..5 to U8/U16/S8/S16 with scale zero.
    const uint32_t width = psq_width(cpu, gqr, MemoryAccess::read);
    const uint32_t address = effective_address(cpu, ra, displacement);
    memory.validate_range(address, width * (w ? 1 : 2), cpu.pc,
                          MemoryAccess::read);
    const double ps0 = psq_read(cpu, memory, address, gqr);
    const double ps1 = w ? 1.0 : psq_read(cpu, memory, address + width, gqr);
    cpu.fpr[frt][0] = std::bit_cast<uint64_t>(ps0);
    cpu.fpr[frt][1] = std::bit_cast<uint64_t>(ps1);
    cpu.pc += 4;
}

inline void psq_lu(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
            bool w, uint32_t gqr, int32_t displacement) {
    if (ra == 0) {
        throw GuestFault("reserved update form", cpu.pc, 4, cpu.pc,
                         MemoryAccess::execute);
    }
    const uint32_t address =
        cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    psq_l(cpu, memory, frt, ra, w, gqr, displacement);
    cpu.gpr[ra] = address;
}

inline void psq_st(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
            bool w, uint32_t gqr, int32_t displacement) {
    const uint32_t width = psq_width(cpu, gqr, MemoryAccess::write);
    const uint32_t address = effective_address(cpu, ra, displacement);
    memory.validate_range(address, width * (w ? 1 : 2), cpu.pc,
                          MemoryAccess::write);
    psq_write(cpu, memory, address, gqr,
              std::bit_cast<double>(cpu.fpr[frs][0]));
    if (!w) {
        psq_write(cpu, memory, address + width, gqr,
                  std::bit_cast<double>(cpu.fpr[frs][1]));
    }
    cpu.pc += 4;
}
inline void psq_stu(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
             bool w, uint32_t gqr, int32_t displacement) {
    if (ra == 0) {
        throw GuestFault("reserved update form", cpu.pc, 4, cpu.pc,
                         MemoryAccess::execute);
    }
    const uint32_t address =
        cpu.gpr[ra] + static_cast<uint32_t>(displacement);
    psq_st(cpu, memory, frs, ra, w, gqr, displacement);
    cpu.gpr[ra] = address;
}

inline uint32_t relocated_branch_target(const GuestMemory& memory, uint32_t address,
                                 uint32_t original, uint32_t fallback) {
    const uint32_t current = memory.read32(address, address);
    if (current == original) {
        return fallback;
    }

    const uint32_t opcode = original >> 26;
    const uint32_t mask = opcode == 16 ? 0x0000FFFC : 0x03FFFFFC;
    if ((opcode != 16 && opcode != 18) ||
        (current & ~mask) != (original & ~mask)) {
        return fallback;
    }

    uint32_t displacement = current & mask;
    const uint32_t sign = opcode == 16 ? 0x00008000 : 0x02000000;
    if ((displacement & sign) != 0) {
        displacement |= ~mask & ~uint32_t{3};
    }
    return (current & 2) != 0 ? displacement : address + displacement;
}

inline void b(CPUContext& cpu, uint32_t target, bool link) {
    require_aligned_target(cpu, target);
    if (link) {
        cpu.lr = cpu.pc + 4;
    }
    cpu.pc = target;
}

inline void bc(CPUContext& cpu, uint32_t target, uint32_t bo, uint32_t bi,
        bool link) {
    conditional_branch(cpu, target, bo, bi, link, true);
}

inline void bclr(CPUContext& cpu, uint32_t bo, uint32_t bi, bool link) {
    conditional_branch(cpu, cpu.lr & ~uint32_t{3}, bo, bi, link, true);
}

inline void bcctr(CPUContext& cpu, uint32_t bo, uint32_t bi, bool link) {
    conditional_branch(cpu, cpu.ctr & ~uint32_t{3}, bo, bi, link, false);
}

inline void isync(CPUContext& cpu) { cpu.pc += 4; }

inline void crxor(CPUContext& cpu, uint32_t bt, uint32_t ba, uint32_t bb) {
    const bool result = cr_bit(cpu, ba) != cr_bit(cpu, bb);
    const uint8_t mask = static_cast<uint8_t>(1u << (3 - bt % 4));
    uint8_t& field = cpu.cr[bt / 4];
    field = static_cast<uint8_t>(result ? (field | mask)
                                        : (field & ~mask));
    cpu.pc += 4;
}
inline void creqv(CPUContext& cpu, uint32_t bt, uint32_t ba, uint32_t bb) {
    const bool result = cr_bit(cpu, ba) == cr_bit(cpu, bb);
    const uint8_t mask = static_cast<uint8_t>(1u << (3 - bt % 4));
    uint8_t& field = cpu.cr[bt / 4];
    field = static_cast<uint8_t>(result ? (field | mask)
                                        : (field & ~mask));
    cpu.pc += 4;
}
} // namespace nwii::runtime::ppc
