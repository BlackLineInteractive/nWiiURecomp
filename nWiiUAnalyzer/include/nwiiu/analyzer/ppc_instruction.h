#pragma once
#include <cstdint>

namespace nwiiu::analyzer {
// Branch decoding ported from BlackLineInteractive/NWiiRecomp
// revision 40f75790f42d903bfe728f2278460c903cdd6017.
class PpcInstruction {
public:
    explicit constexpr PpcInstruction(uint32_t value) : value_(value) {}

    [[nodiscard]] constexpr uint32_t raw() const { return value_; }
    [[nodiscard]] constexpr uint32_t rt() const {
        return (value_ >> 21) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t rs() const { return rt(); }
    [[nodiscard]] constexpr uint32_t ra() const {
        return (value_ >> 16) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t rb() const {
        return (value_ >> 11) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t frc() const {
        return (value_ >> 6) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t bo() const { return rt(); }
    [[nodiscard]] constexpr uint32_t bi() const { return ra(); }
    [[nodiscard]] constexpr int32_t simm() const {
        return sign_extend(value_ & 0xFFFF, 16);
    }
    [[nodiscard]] constexpr uint32_t uimm() const { return value_ & 0xFFFF; }
    [[nodiscard]] constexpr uint32_t bf() const { return rt() >> 2; }
    [[nodiscard]] constexpr uint32_t sh() const { return rb(); }
    [[nodiscard]] constexpr uint32_t mb() const {
        return (value_ >> 6) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t me() const {
        return (value_ >> 1) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t spr() const {
        return ((value_ >> 16) & 0x1F) | ((value_ >> 6) & 0x3E0);
    }

    [[nodiscard]] constexpr uint32_t opcode() const {
        return value_ >> 26;
    }

    [[nodiscard]] constexpr uint32_t extended_opcode() const {
        return (value_ >> 1) & 0x3FF;
    }
    [[nodiscard]] constexpr uint32_t xo5() const {
        return (value_ >> 1) & 0x1F;
    }
    [[nodiscard]] constexpr uint32_t ps_i() const {
        return (value_ >> 12) & 0x7;
    }
    [[nodiscard]] constexpr bool ps_w() const {
        return ((value_ >> 15) & 0x1) != 0;
    }
    [[nodiscard]] constexpr int32_t ps_displacement() const {
        return sign_extend(value_ & 0xFFF, 12);
    }

    [[nodiscard]] constexpr bool link() const {
        return (value_ & 1) != 0;
    }

    [[nodiscard]] constexpr bool rc() const { return (value_ & 1) != 0; }

    [[nodiscard]] constexpr bool absolute_address() const {
        return (value_ & 2) != 0;
    }

    [[nodiscard]] constexpr bool is_direct_branch() const {
        return opcode() == 16 || opcode() == 18;
    }

    [[nodiscard]] constexpr bool branch_option_is_conditional() const {
        return (bo() & 0x14) != 0x14;
    }

    [[nodiscard]] constexpr bool is_branch_to_lr() const {
        return opcode() == 19 && extended_opcode() == 16;
    }

    [[nodiscard]] constexpr bool is_branch_to_ctr() const {
        return opcode() == 19 && extended_opcode() == 528;
    }

    [[nodiscard]] constexpr uint32_t branch_target(uint32_t pc) const {
        int32_t displacement = 0;
        if (opcode() == 18) {
            displacement = sign_extend((value_ >> 2) & 0xFFFFFF, 24) * 4;
        } else if (opcode() == 16) {
            displacement = sign_extend((value_ >> 2) & 0x3FFF, 14) * 4;
        } else {
            return 0;
        }
        const uint32_t offset = static_cast<uint32_t>(displacement);
        return absolute_address() ? offset : pc + offset;
    }

private:

    [[nodiscard]] static constexpr int32_t sign_extend(uint32_t value,
                                                        uint32_t bits) {
        const uint32_t range = uint32_t{1} << bits;
        return (value & (range >> 1)) != 0
                   ? static_cast<int32_t>(value) - static_cast<int32_t>(range)
                   : static_cast<int32_t>(value);
    }

    uint32_t value_;
};
}
