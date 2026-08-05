#include "nwiiu/recomp/native_generator.h"

#include "nwiiu/analyzer/ppc_instruction.h"
#include "runtime/ppc_semantics.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace nwiiu::recomp {
namespace {
using nwiiu::analyzer::PpcInstruction;
constexpr std::string_view kKeywords[]{
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
    "bitor", "bool", "break", "case", "catch", "char", "char8_t",
    "char16_t", "char32_t", "class", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "continue",
    "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline",
    "int", "long", "mutable", "namespace", "new", "noexcept", "not",
    "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
    "public", "register", "reinterpret_cast", "requires", "return", "short",
    "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try",
    "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
};

bool is_identifier(std::string_view name) {
    const auto is_alpha = [](char value) {
        return (value >= 'A' && value <= 'Z') ||
               (value >= 'a' && value <= 'z') || value == '_';
    };
    const auto is_alnum = [&is_alpha](char value) {
        return is_alpha(value) || (value >= '0' && value <= '9');
    };
    if (name.empty() || name == "main" || name.front() == '_' ||
        name.find("__") != std::string_view::npos ||
        !is_alpha(name.front())) {
        return false;
    }
    for (const char value : name.substr(1)) {
        if (!is_alnum(value)) {
            return false;
        }
    }
    for (const auto keyword : kKeywords) {
        if (name == keyword) {
            return false;
        }
    }
    return true;
}

std::string hex32(uint32_t value) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(8) << value;
    return output.str();
}

const char* boolean(bool value) { return value ? "true" : "false"; }

std::string base_gpr(uint32_t reg) {
    return reg == 0 ? "0" : "cpu.gpr[" + std::to_string(reg) + "]";
}

void validate_overrides(
    uint32_t start_address, std::span<const uint32_t> instructions,
    const std::map<uint32_t, uint32_t>& branch_overrides) {
    const uint64_t end_address = static_cast<uint64_t>(start_address) +
                                 instructions.size() * sizeof(uint32_t);
    if (end_address >
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1) {
        throw std::invalid_argument("instruction range wraps guest address space");
    }
    for (const auto& [address, target] : branch_overrides) {
        (void)target;
        if (address < start_address || address >= end_address ||
            (address - start_address) % sizeof(uint32_t) != 0) {
            throw std::invalid_argument("branch override is outside function");
        }
        const size_t index = (address - start_address) / sizeof(uint32_t);
        if (!PpcInstruction{instructions[index]}.is_direct_branch()) {
            throw std::invalid_argument("branch override does not name a direct branch");
        }
    }
}

void validate_instruction(const PpcInstruction& instruction) {
    const uint32_t opcode = instruction.opcode();
    const uint32_t xo = instruction.extended_opcode();
    switch (opcode) {
    case 4:
        if (instruction.rc()) {
            break;
        }
        if (xo == 528 || xo == 560 || xo == 592 || xo == 624) {
            return;
        }
        if ((instruction.xo5() == 12 || instruction.xo5() == 13 ||
             instruction.xo5() == 25) &&
            instruction.rb() == 0) {
            return;
        }
        if (instruction.xo5() == 10 || instruction.xo5() == 11 ||
            instruction.xo5() == 14 || instruction.xo5() == 15 ||
            instruction.xo5() == 23 || instruction.xo5() == 28 ||
            instruction.xo5() == 29 || instruction.xo5() == 30 ||
            instruction.xo5() == 31) {
            return;
        }
        if ((instruction.xo5() == 20 || instruction.xo5() == 21) &&
            instruction.frc() == 0) {
            return;
        }
        if (xo == 26 && instruction.ra() == 0) {
            return;
        }
        if (xo == 32 && (instruction.rt() & 3) == 0) {
            return;
        }
        if (xo == 40 && instruction.ra() == 0) {
            return;
        }
        break;
    case 7:
        return;
    case 8:
        return;
    case 10:
    case 11:
        if ((instruction.rt() & 0x3) == 0) {
            return;
        }
        break;
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 18:
    case 20:
    case 21:
    case 24:
    case 25:
    case 26:
    case 27:
    case 23:
    case 28:
    case 29:
    case 32:
    case 34:
    case 36:
    case 38:
    case 44:
    case 40:
    case 42:
    case 46:
    case 47:
    case 48:
    case 50:
    case 52:
    case 54:
        return;
    case 33:
    case 35:
    case 41:
    case 43:
        if (instruction.ra() != 0 && instruction.ra() != instruction.rt()) {
            return;
        }
        throw std::invalid_argument("reserved update load form");
    case 37:
    case 39:
    case 45:
    case 53:
    case 55:
        if (instruction.ra() != 0) {
            return;
        }
        throw std::invalid_argument("reserved update store form");
    case 49:
        if (instruction.ra() != 0) {
            return;
        }
        throw std::invalid_argument("reserved update load form");
    case 57:
        if (instruction.ra() != 0 && instruction.ps_i() == 0) {
            return;
        }
        break;
    case 56:
    case 60:
        if (instruction.ps_i() != 1 && instruction.ps_i() <= 5) {
            return;
        }
        break;
    case 61:
        if (instruction.ra() != 0 && instruction.ps_i() == 0) {
            return;
        }
        break;
    case 19:
        if (xo == 16 || xo == 528) {
            if ((instruction.rb() & 0x1C) != 0) {
                break;
            }
            if (xo == 528 && (instruction.bo() & 0x04) == 0) {
                throw std::invalid_argument("bcctr cannot decrement CTR");
            }
            return;
        }
        if (xo == 150 && instruction.bo() == 0 &&
            instruction.bi() == 0 && instruction.rb() == 0 &&
            !instruction.link()) {
            return;
        }
        if ((xo == 193 || xo == 289) && !instruction.link()) {
            return;
        }
        break;
    case 31:
        switch (xo) {
        case 0:
        case 4:
        case 23:
        case 87:
        case 32:
        case 343:
        case 151:
        case 407:
        case 983:
        case 725:
        case 215:
        case 279:
        case 535:
        case 663:
        case 20:
        case 534:
        case 662:
            if (!instruction.rc() &&
                ((xo != 0 && xo != 32) ||
                 (instruction.rt() & 0x3) == 0)) {
                return;
            }
            break;
        case 10:
        case 11:
        case 8:
        case 792:
        case 28:
        case 60:
        case 75:
        case 24:
        case 40:
        case 124:
        case 136:
        case 235:
        case 266:
        case 316:
        case 444:
        case 459:
        case 491:
        case 1003:
        case 536:
        case 824:
            return;
        case 26:
        case 104:
            if (instruction.rb() == 0) {
                return;
            }
            break;
        case 412:
        case 138:
            return;
        case 150:
            if (instruction.rc()) {
                return;
            }
            break;
        case 200:
        case 712:
        case 202:
        case 714:
            if (instruction.rb() == 0) {
                return;
            }
            break;
        case 922:
        case 954:
            if (instruction.rb() == 0) {
                return;
            }
            break;
        case 311:
            if (!instruction.rc() && instruction.ra() != 0 &&
                instruction.ra() != instruction.rt()) {
                return;
            }
            break;
        case 567:
            if (!instruction.rc() && instruction.ra() != 0) {
                return;
            }
            break;
        case 55:
            if (!instruction.rc() && instruction.ra() != 0 &&
                instruction.ra() != instruction.rt()) {
                return;
            }
            break;
        case 183:
        case 695:
            if (!instruction.rc() && instruction.ra() != 0) {
                return;
            }
            break;
        case 119:
        case 375:
            if (!instruction.rc() && instruction.ra() != 0 &&
                instruction.ra() != instruction.rt()) {
                return;
            }
            break;
        case 247:
        case 439:
            if (!instruction.rc() && instruction.ra() != 0) {
                return;
            }
            break;
        case 54:
        case 86:
            if (!instruction.rc() && instruction.rt() == 0) {
                return;
            }
            break;
        case 1014:
            if (!instruction.rc() && instruction.rt() == 0) {
                return;
            }
            break;
        case 597:
            if (!instruction.rc() &&
                nwii::runtime::ppc::valid_lswi_form(
                    instruction.rt(), instruction.ra(), instruction.rb())) {
                return;
            }
            break;
        case 339:
            if (!instruction.rc() &&
                (instruction.spr() == 8 || instruction.spr() == 9)) {
                return;
            }
            break;
        case 467:
            if (!instruction.rc() &&
                (instruction.spr() == 8 || instruction.spr() == 9 ||
                 (instruction.spr() >= 898 && instruction.spr() <= 901))) {
                return;
            }
            break;
        case 19:
            if (!instruction.rc() && instruction.ra() == 0 &&
                instruction.rb() == 0) {
                return;
            }
            break;
        }
        break;
    case 59:
        if (instruction.rc()) {
            break;
        }
        if ((instruction.xo5() == 20 ||
             instruction.xo5() == 21) &&
            instruction.frc() == 0) {
            return;
        }
        if (instruction.xo5() == 25 && instruction.rb() == 0) {
            return;
        }
        if (instruction.xo5() == 28 || instruction.xo5() == 29 ||
            instruction.xo5() == 30) {
            return;
        }
        if (xo == 24 && instruction.ra() == 0) {
            return;
        }
        break;
    case 63:
        if (instruction.rc()) {
            break;
        }
        if (instruction.xo5() == 18 && instruction.frc() == 0) {
            return;
        }
        if (instruction.xo5() == 20) {
            return;
        }
        if (instruction.xo5() == 21 && instruction.frc() == 0) {
            return;
        }
        if (instruction.xo5() == 25 && instruction.rb() == 0) {
            return;
        }
        if (instruction.xo5() == 29) {
            return;
        }
        if (instruction.xo5() == 30) {
            return;
        }
        if (instruction.xo5() == 28) {
            return;
        }
        if (instruction.xo5() == 23) {
            return;
        }
        if (xo == 0 && (instruction.rt() & 0x3) == 0) {
            return;
        }
        if ((xo == 136 || xo == 264) && instruction.ra() == 0) {
            return;
        }
        if (xo == 14 && instruction.ra() == 0) {
            return;
        }
        if (xo == 26 && instruction.ra() == 0) {
            return;
        }
        if ((xo == 12 || xo == 15 || xo == 40 || xo == 72) &&
            instruction.ra() == 0) {
            return;
        }
        break;
    }
    throw std::invalid_argument("unsupported PowerPC instruction");
}

void emit_instruction(std::ostringstream& output,
                      const PpcInstruction& instruction, uint32_t address,
                      bool final,
                      const std::map<uint32_t, uint32_t>& branch_overrides) {
    validate_instruction(instruction);
    output << "    // " << hex32(address) << ": " << hex32(instruction.raw())
           << '\n';
    // Recompiled blocks only need the instruction count, which the host uses
    // to charge the quantum. trace_instruction also maintains a 32-entry PC
    // ring with a modulo and an optional store on every guest instruction,
    // which only the headless runner ever reads back; on this path it is pure
    // overhead and the module has to keep pace with a JIT.
    output << "    ++cpu.instruction_count;\n";

    const uint32_t opcode = instruction.opcode();
    const uint32_t xo = instruction.extended_opcode();
    if ((opcode == 16 || opcode == 18 ||
         (opcode == 19 && (xo == 16 || xo == 528))) &&
        !final) {
        throw std::invalid_argument("branch must terminate function");
    }

    switch (opcode) {
    case 4:
        if (xo == 528 || xo == 560 || xo == 592 || xo == 624) {
            const char* name = xo == 528   ? "ps_merge00"
                               : xo == 560 ? "ps_merge01"
                               : xo == 592 ? "ps_merge10"
                                           : "ps_merge11";
            output << "    nwii::runtime::ppc::" << name << "(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (xo == 40) {
            output << "    nwii::runtime::ppc::ps_neg(cpu, "
                   << instruction.rt() << ", " << instruction.rb() << ");\n";
        } else if (xo == 32) {
            output << "    nwii::runtime::ppc::ps_cmpo0(cpu, "
                   << instruction.bf() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (xo == 26) {
            output << "    nwii::runtime::ppc::ps_rsqrte(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        } else if (instruction.xo5() == 12 ||
                   instruction.xo5() == 13 ||
                   instruction.xo5() == 25) {
            const char* name = instruction.xo5() == 12   ? "ps_muls0"
                               : instruction.xo5() == 13 ? "ps_muls1"
                                                        : "ps_mul";
            output << "    nwii::runtime::ppc::" << name << "(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.frc() << ");\n";
        } else if (instruction.xo5() == 20 ||
                   instruction.xo5() == 21) {
            output << "    nwii::runtime::ppc::"
                   << (instruction.xo5() == 20 ? "ps_sub" : "ps_add")
                   << "(cpu, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
        } else {
            const char* name = instruction.xo5() == 10   ? "ps_sum0"
                               : instruction.xo5() == 11 ? "ps_sum1"
                               : instruction.xo5() == 14 ? "ps_madds0"
                               : instruction.xo5() == 15 ? "ps_madds1"
                               : instruction.xo5() == 23 ? "ps_sel"
                               : instruction.xo5() == 28 ? "ps_msub"
                               : instruction.xo5() == 30 ? "ps_nmsub"
                               : instruction.xo5() == 31 ? "ps_nmadd"
                                                        : "ps_madd";
            output << "    nwii::runtime::ppc::" << name << "(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.frc() << ", " << instruction.rb()
                   << ");\n";
        }
        return;
    case 7:
        output << "    nwii::runtime::ppc::mulli(cpu, " << instruction.rt()
               << ", " << instruction.ra() << ", " << instruction.simm()
               << ");\n";
        return;
    case 8:
        output << "    nwii::runtime::ppc::subfic(cpu, " << instruction.rt()
               << ", " << instruction.ra() << ", " << instruction.simm()
               << ");\n";
        return;
    case 10:
        output << "    nwii::runtime::ppc::cmpli(cpu, " << instruction.bf()
               << ", " << instruction.ra() << ", " << instruction.uimm()
               << ");\n";
        return;
    case 11:
        output << "    nwii::runtime::ppc::cmpi(cpu, " << instruction.bf()
               << ", " << instruction.ra() << ", " << instruction.simm()
               << ");\n";
        return;
    case 12:
    case 13:
        output << "    nwii::runtime::ppc::addic(cpu, " << instruction.rt()
               << ", " << instruction.ra() << ", " << instruction.simm()
               << ", " << boolean(opcode == 13) << ");\n";
        return;
    case 14:
        output << "    cpu.gpr[" << instruction.rt() << "] = "
               << base_gpr(instruction.ra())
               << " + static_cast<uint32_t>(" << instruction.simm()
               << ");\n"
                  "    cpu.pc += 4;\n";
        return;
    case 15:
        output << "    cpu.gpr[" << instruction.rt() << "] = "
               << base_gpr(instruction.ra())
               << " + (static_cast<uint32_t>(" << instruction.simm()
               << ") << 16);\n"
                  "    cpu.pc += 4;\n";
        return;
    case 16: {
        const auto override = branch_overrides.find(address);
        output << "    nwii::runtime::ppc::bc(cpu, ";
        if (override == branch_overrides.end()) {
            output << hex32(instruction.branch_target(address));
        } else {
            output << "nwii::runtime::ppc::relocated_branch_target(memory, "
                   << hex32(address) << ", " << hex32(instruction.raw()) << ", "
                   << hex32(override->second) << ')';
        }
        output << ", " << instruction.bo() << ", " << instruction.bi() << ", "
               << boolean(instruction.link()) << ");\n";
        return;
    }
    case 18: {
        const auto override = branch_overrides.find(address);
        output << "    nwii::runtime::ppc::b(cpu, ";
        if (override == branch_overrides.end()) {
            output << hex32(instruction.branch_target(address));
        } else {
            output << "nwii::runtime::ppc::relocated_branch_target(memory, "
                   << hex32(address) << ", " << hex32(instruction.raw()) << ", "
                   << hex32(override->second) << ')';
        }
        output << ", " << boolean(instruction.link()) << ");\n";
        return;
    }
    case 19:
        if (xo == 16 || xo == 528) {
            output << "    nwii::runtime::ppc::"
                   << (xo == 16 ? "bclr" : "bcctr") << "(cpu, 0x"
                   << std::uppercase << std::hex << instruction.bo()
                   << std::dec << ", " << instruction.bi() << ", "
                   << boolean(instruction.link()) << ");\n";
        } else if (xo == 193) {
            output << "    nwii::runtime::ppc::crxor(cpu, "
                   << instruction.bo() << ", " << instruction.bi() << ", "
                   << instruction.rb() << ");\n";
        } else if (xo == 289) {
            output << "    nwii::runtime::ppc::creqv(cpu, "
                   << instruction.bo() << ", " << instruction.bi() << ", "
                   << instruction.rb() << ");\n";
        } else {
            output << "    nwii::runtime::ppc::isync(cpu);\n";
        }
        return;
    case 20:
        output << "    nwii::runtime::ppc::rlwimi(cpu, " << instruction.ra()
               << ", " << instruction.rs() << ", " << instruction.sh()
               << ", " << instruction.mb() << ", " << instruction.me()
               << ", " << boolean(instruction.rc()) << ");\n";
        return;
    case 21:
        output << "    nwii::runtime::ppc::rlwinm(cpu, " << instruction.ra()
               << ", " << instruction.rs() << ", " << instruction.sh()
               << ", " << instruction.mb() << ", " << instruction.me()
               << ", " << boolean(instruction.rc()) << ");\n";
        return;
    case 23:
        output << "    nwii::runtime::ppc::rlwnm(cpu, " << instruction.ra()
               << ", " << instruction.rs() << ", " << instruction.rb()
               << ", " << instruction.mb() << ", " << instruction.me()
               << ", " << boolean(instruction.rc()) << ");\n";
        return;
    case 24:
    case 25:
    case 26:
    case 27: {
        const char op = opcode == 24 || opcode == 25 ? '|' : '^';
        const uint32_t immediate =
            opcode == 25 || opcode == 27 ? instruction.uimm() << 16
                                         : instruction.uimm();
        output << "    cpu.gpr[" << instruction.ra() << "] = cpu.gpr["
               << instruction.rs() << "] " << op << " " << immediate
               << ";\n"
                  "    cpu.pc += 4;\n";
        return;
    }
    case 28:
    case 29:
        output << "    nwii::runtime::ppc::andi_dot(cpu, "
               << instruction.ra() << ", " << instruction.rs() << ", "
               << (opcode == 29 ? instruction.uimm() << 16
                                : instruction.uimm())
               << ");\n";
        return;
    case 31:
        switch (xo) {
        case 0:
        case 32:
            output << "    nwii::runtime::ppc::"
                   << (xo == 0 ? "cmp" : "cmpl") << "(cpu, "
                   << instruction.bf() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
            return;
        case 4:
            output << "    nwii::runtime::ppc::tw(cpu, " << instruction.rt()
                   << ", " << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 19:
            output << "    nwii::runtime::ppc::mfcr(cpu, "
                   << instruction.rt() << ");\n";
            return;
        case 87:
            output << "    nwii::runtime::ppc::lbzx(cpu, memory, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
            return;
        case 279:
        case 311:
            output << "    nwii::runtime::ppc::"
                   << (xo == 279 ? "lhzx" : "lhzux")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 535:
        case 567:
            output << "    nwii::runtime::ppc::"
                   << (xo == 535 ? "lfsx" : "lfsux")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 20:
        case 119:
        case 375:
        case 534:
            output << "    nwii::runtime::ppc::"
                   << (xo == 20    ? "lwarx"
                       : xo == 119 ? "lbzux"
                       : xo == 375 ? "lhaux"
                                   : "lwbrx")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 55:
        case 183:
        case 695:
            output << "    nwii::runtime::ppc::"
                   << (xo == 55 ? "lwzux" : xo == 183 ? "stwux" : "stfsux")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 150:
        case 247:
        case 439:
        case 662:
            output << "    nwii::runtime::ppc::"
                   << (xo == 150   ? "stwcx"
                       : xo == 247 ? "stbux"
                       : xo == 439 ? "sthux"
                                   : "stwbrx")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 1014:
            output << "    nwii::runtime::ppc::dcbz(cpu, memory, "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 54:
        case 86:
            output << "    nwii::runtime::ppc::"
                   << (xo == 54 ? "dcbst" : "dcbf") << "(cpu);\n";
            return;
        case 23:
        case 343:
        case 151:
        case 983:
        case 407:
        case 215:
        case 663:
            output << "    nwii::runtime::ppc::"
                   << (xo == 23    ? "lwzx"
                       : xo == 343 ? "lhax"
                       : xo == 151 ? "stwx"
                       : xo == 215 ? "stbx"
                       : xo == 407 ? "sthx"
                       : xo == 663 ? "stfsx"
                                   : "stfiwx")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 200:
        case 712:
            output << "    nwii::runtime::ppc::subfze(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << boolean(xo == 712) << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 202:
        case 714:
            output << "    nwii::runtime::ppc::addze(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << boolean(xo == 714) << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 138:
            output << "    nwii::runtime::ppc::adde(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", false, "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 491:
        case 1003:
            output << "    nwii::runtime::ppc::divw(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << boolean(xo == 1003) << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 26:
            output << "    nwii::runtime::ppc::cntlzw(cpu, "
                   << instruction.ra() << ", " << instruction.rs() << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 104:
            output << "    nwii::runtime::ppc::neg(cpu, " << instruction.rt()
                   << ", " << instruction.ra() << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 8:
        case 10:
        case 11:
        case 75:
        case 459:
        case 40:
        case 136:
        case 235:
        case 266: {
            const char* name = xo == 8     ? "subfc"
                               : xo == 10  ? "addc"
                               : xo == 11  ? "mulhwu"
                               : xo == 40  ? "subf"
                               : xo == 75  ? "mulhw"
                               : xo == 136 ? "subfe"
                               : xo == 235 ? "mullw"
                               : xo == 459 ? "divwu"
                                           : "add";
            output << "    nwii::runtime::ppc::" << name
                   << "(cpu, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb() << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        }
        case 24:
        case 536:
        case 124:
        case 316:
        case 28:
        case 60:
        case 412:
        case 444: {
            if (xo == 444 && !instruction.rc()) {
                output << "    cpu.gpr[" << instruction.ra()
                       << "] = cpu.gpr[" << instruction.rs()
                       << "] | cpu.gpr[" << instruction.rb()
                       << "];\n"
                          "    cpu.pc += 4;\n";
                return;
            }
            const char* name = xo == 24    ? "slw"
                               : xo == 28  ? "and_"
                               : xo == 60  ? "andc"
                               : xo == 124 ? "nor_"
                               : xo == 316 ? "xor_"
                               : xo == 444 ? "or_"
                               : xo == 412 ? "orc"
                                           : "srw";
            output << "    nwii::runtime::ppc::" << name << "(cpu, "
                   << instruction.ra() << ", " << instruction.rs() << ", "
                   << instruction.rb() << ", " << boolean(instruction.rc())
                   << ");\n";
            return;
        }
        case 339:
        case 467:
            output << "    nwii::runtime::ppc::"
                   << (xo == 339 ? "mfspr" : "mtspr") << "(cpu, "
                   << instruction.rt() << ", " << instruction.spr()
                   << ");\n";
            return;
        case 597:
        case 725:
            output << "    nwii::runtime::ppc::"
                   << (xo == 597 ? "lswi" : "stswi")
                   << "(cpu, memory, " << instruction.rt() << ", "
                   << instruction.ra() << ", " << instruction.rb()
                   << ");\n";
            return;
        case 824:
            output << "    nwii::runtime::ppc::srawi(cpu, "
                   << instruction.ra() << ", " << instruction.rs() << ", "
                   << instruction.sh() << ", " << boolean(instruction.rc())
                   << ");\n";
            return;
        case 792:
            output << "    nwii::runtime::ppc::sraw(cpu, "
                   << instruction.ra() << ", " << instruction.rs() << ", "
                   << instruction.rb() << ", " << boolean(instruction.rc())
                   << ");\n";
            return;
        case 922:
            output << "    nwii::runtime::ppc::extsh(cpu, "
                   << instruction.ra() << ", " << instruction.rs() << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        case 954:
            output << "    nwii::runtime::ppc::extsb(cpu, "
                   << instruction.ra() << ", " << instruction.rs() << ", "
                   << boolean(instruction.rc()) << ");\n";
            return;
        }
        break;
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47: {
        if (opcode == 32) {
            output << "    cpu.gpr[" << instruction.rt()
                   << "] = memory.read32("
                   << base_gpr(instruction.ra())
                   << " + static_cast<uint32_t>(" << instruction.simm()
                   << "), cpu.pc);\n"
                      "    cpu.pc += 4;\n";
            return;
        }
        if (opcode == 36) {
            output << "    memory.write32(" << base_gpr(instruction.ra())
                   << " + static_cast<uint32_t>(" << instruction.simm()
                   << "), cpu.gpr[" << instruction.rt()
                   << "], cpu.pc);\n"
                      "    cpu.pc += 4;\n";
            return;
        }
        constexpr const char* names[]{"lwz", "lwzu", "lbz", "lbzu",
                                      "stw", "stwu", "stb", "stbu",
                                      "lhz", "lhzu", "lha", "lhau",
                                      "sth", "sthu", "lmw", "stmw"};
        output << "    nwii::runtime::ppc::" << names[opcode - 32]
               << "(cpu, memory, " << instruction.rt() << ", "
               << instruction.ra() << ", " << instruction.simm()
               << ");\n";
        return;
    }
    case 48:
    case 49:
    case 50:
    case 52:
    case 53:
    case 54:
    case 55: {
        const char* name = opcode == 48   ? "lfs"
                           : opcode == 49 ? "lfsu"
                           : opcode == 50 ? "lfd"
                           : opcode == 52 ? "stfs"
                           : opcode == 53 ? "stfsu"
                           : opcode == 54 ? "stfd"
                                          : "stfdu";
        output << "    nwii::runtime::ppc::" << name << "(cpu, memory, "
               << instruction.rt() << ", " << instruction.ra() << ", "
               << instruction.simm() << ");\n";
        return;
    }
    case 56:
        output << "    nwii::runtime::ppc::psq_l(cpu, memory, "
               << instruction.rt() << ", " << instruction.ra() << ", "
               << boolean(instruction.ps_w()) << ", " << instruction.ps_i()
               << ", " << instruction.ps_displacement() << ");\n";
        return;
    case 57:
        output << "    nwii::runtime::ppc::psq_lu(cpu, memory, "
               << instruction.rt() << ", " << instruction.ra() << ", "
               << boolean(instruction.ps_w()) << ", " << instruction.ps_i()
               << ", " << instruction.ps_displacement() << ");\n";
        return;
    case 60:
        output << "    nwii::runtime::ppc::psq_st(cpu, memory, "
               << instruction.rt() << ", " << instruction.ra() << ", "
               << boolean(instruction.ps_w()) << ", " << instruction.ps_i()
               << ", " << instruction.ps_displacement() << ");\n";
        return;
    case 61:
        output << "    nwii::runtime::ppc::psq_stu(cpu, memory, "
               << instruction.rt() << ", " << instruction.ra() << ", "
               << boolean(instruction.ps_w()) << ", " << instruction.ps_i()
               << ", " << instruction.ps_displacement() << ");\n";
        return;
    case 59:
        if (xo == 24) {
            output << "    nwii::runtime::ppc::fres(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        } else if (instruction.xo5() == 18) {
            output << "    nwii::runtime::ppc::fdivs(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (instruction.xo5() == 20) {
            output << "    nwii::runtime::ppc::fsubs(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (instruction.xo5() == 21) {
            output << "    nwii::runtime::ppc::fadds(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (instruction.xo5() == 25) {
            output << "    nwii::runtime::ppc::fmuls(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.frc() << ");\n";
        } else if (instruction.xo5() == 28) {
            output << "    nwii::runtime::ppc::fmsubs(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << instruction.frc()
                   << ");\n";
        } else if (instruction.xo5() == 29) {
            output << "    nwii::runtime::ppc::fmadds(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << instruction.frc()
                   << ");\n";
        } else if (instruction.xo5() == 30) {
            output << "    nwii::runtime::ppc::fnmsubs(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << instruction.frc()
                   << ");\n";
        } else {
            throw std::invalid_argument(
                "unsupported single-precision floating-point instruction");
        }
        return;
    case 63:
        if (instruction.xo5() == 18) {
            output << "    nwii::runtime::ppc::fdiv(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (instruction.xo5() == 20) {
            output << "    nwii::runtime::ppc::fsub(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (instruction.xo5() == 21) {
            output << "    nwii::runtime::ppc::fadd(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (instruction.xo5() == 25) {
            output << "    nwii::runtime::ppc::fmul(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.frc() << ");\n";
        } else if (instruction.xo5() == 29) {
            output << "    nwii::runtime::ppc::fmadd(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << instruction.frc()
                   << ");\n";
        } else if (instruction.xo5() == 30) {
            output << "    nwii::runtime::ppc::fnmsub(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << instruction.frc()
                   << ");\n";
        } else if (instruction.xo5() == 28) {
            output << "    nwii::runtime::ppc::fmsub(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ", " << instruction.frc()
                   << ");\n";
        } else if (instruction.xo5() == 23) {
            output << "    nwii::runtime::ppc::fsel(cpu, "
                   << instruction.rt() << ", " << instruction.ra() << ", "
                   << instruction.frc() << ", " << instruction.rb()
                   << ");\n";
        } else if (xo == 0) {
            output << "    nwii::runtime::ppc::fcmpu(cpu, "
                   << instruction.bf() << ", " << instruction.ra() << ", "
                   << instruction.rb() << ");\n";
        } else if (xo == 264) {
            output << "    nwii::runtime::ppc::fabs(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        } else if (xo == 136) {
            output << "    nwii::runtime::ppc::fnabs(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        } else if (xo == 14) {
            output << "    nwii::runtime::ppc::fctiw(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        } else if (xo == 26) {
            output << "    nwii::runtime::ppc::frsqrte(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        } else if (xo == 12 || xo == 15 || xo == 40 || xo == 72) {
            const char* name = xo == 12   ? "frsp"
                               : xo == 15 ? "fctiwz"
                               : xo == 40 ? "fneg"
                                          : "fmr";
            output << "    nwii::runtime::ppc::" << name << "(cpu, "
                   << instruction.rt() << ", " << instruction.rb()
                   << ");\n";
        }
        return;
    }
    throw std::invalid_argument("unsupported PowerPC instruction");
}
std::string generate_native(
    std::string_view name, uint32_t start_address,
    std::span<const uint32_t> instructions,
    const std::map<uint32_t, uint32_t>& branch_overrides,
    bool require_terminator) {
    if (!is_identifier(name)) {
        throw std::invalid_argument("invalid native function identifier");
    }
    if (instructions.empty()) {
        throw std::invalid_argument("native function has no instructions");
    }
    const PpcInstruction terminator{instructions.back()};
    const bool valid_terminator =
        terminator.opcode() == 16 || terminator.opcode() == 18 ||
        (terminator.opcode() == 19 &&
         (terminator.extended_opcode() == 16 ||
          terminator.extended_opcode() == 528));
    if (require_terminator && !valid_terminator) {
        throw std::invalid_argument(
            "native function must end in a direct or indirect branch");
    }
    validate_overrides(start_address, instructions, branch_overrides);

    std::ostringstream output;
    output << "#include \"runtime/cpu_context.h\"\n"
              "#include \"runtime/memory.h\"\n"
              "#include \"runtime/ppc_semantics.h\"\n\n"
              "extern \"C\" void "
           << name
           << "(nwii::runtime::CPUContext& cpu,\n"
              "                               nwii::runtime::GuestMemory& memory) {\n"
              "    (void)memory;\n";
    for (size_t index = 0; index < instructions.size(); ++index) {
        const uint32_t address =
            start_address + static_cast<uint32_t>(index * sizeof(uint32_t));
        try {
            emit_instruction(output, PpcInstruction{instructions[index]},
                             address, index + 1 == instructions.size(),
                             branch_overrides);
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("instruction " + hex32(address) +
                                        " (" + hex32(instructions[index]) +
                                        "): " + error.what());
        }
    }
    if (!valid_terminator) {
        output << "    cpu.pc = "
               << hex32(start_address + static_cast<uint32_t>(
                                            instructions.size() *
                                            sizeof(uint32_t)))
               << ";\n";
    }
    output << "}\n";
    return output.str();
}
} // namespace

std::string generate_native_block(
    std::string_view name, uint32_t start_address,
    std::span<const uint32_t> instructions,
    const std::map<uint32_t, uint32_t>& branch_overrides) {
    return generate_native(name, start_address, instructions, branch_overrides,
                           false);
}

std::string generate_native_function(
    std::string_view name, uint32_t start_address,
    std::span<const uint32_t> instructions,
    const std::map<uint32_t, uint32_t>& branch_overrides) {
    return generate_native(name, start_address, instructions, branch_overrides,
                           true);
}
} // namespace nwiiu::recomp
