#include "runtime/latte_shader.h"

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nwii::runtime {
namespace {
constexpr std::array<const char*, 4> kChannel{"x", "y", "z", "w"};

uint32_t word(std::span<const uint8_t> bytes, size_t offset,
              const char* stage) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error(std::string{"truncated Latte "} + stage +
                                 " shader at byte " +
                                 std::to_string(offset));
    }
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::string unsupported(const char* stage, size_t offset, uint32_t first,
                        uint32_t second) {
    std::ostringstream message;
    message << "unsupported Latte " << stage << " instruction at byte 0x"
            << std::hex << offset << " raw=0x" << std::setw(8)
            << std::setfill('0') << first << ":0x" << std::setw(8) << second;
    return message.str();
}

uint32_t components(uint32_t format) {
    switch (format) {
    case 13:
    case 14: return 1;
    case 29:
    case 30: return 2;
    case 47:
    case 48: return 3;
    case 34:
    case 35: return 4;
    default:
        throw std::runtime_error("unsupported Latte vertex format " +
                                 std::to_string(format));
    }
}

std::string vector_type(uint32_t count) {
    return count == 1 ? "float" : "vec" + std::to_string(count);
}

std::string selected(const std::string& value, uint32_t selection) {
    if (selection < 4) {
        return value + "." + kChannel[selection];
    }
    if (selection == 4 || selection == 7) {
        return "0.0";
    }
    if (selection == 5) {
        return "1.0";
    }
    throw std::runtime_error("unsupported Latte component selection " +
                             std::to_string(selection));
}

std::vector<LatteVertexInput>
parse_fetch(std::span<const uint8_t> program) {
    if (program.size() < 16) {
        throw std::runtime_error("truncated Latte fetch shader");
    }
    const uint32_t address = word(program, 0, "fetch");
    const uint32_t cf = word(program, 4, "fetch");
    if (((cf >> 23) & 0x7Fu) != 3) {
        throw std::runtime_error(unsupported("fetch CF", 0, address, cf));
    }
    const uint32_t count = ((cf >> 10) & 7u) +
                           (((cf >> 19) & 1u) << 3) + 1;
    std::vector<LatteVertexInput> inputs;
    inputs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t offset = static_cast<size_t>(address) * 8 + i * 16;
        const uint32_t w0 = word(program, offset, "fetch");
        const uint32_t w1 = word(program, offset + 4, "fetch");
        const uint32_t w2 = word(program, offset + 8, "fetch");
        if ((w0 & 0x1Fu) != 1) {
            throw std::runtime_error(
                unsupported("fetch VTX", offset, w0, w1));
        }
        const uint32_t resource = (w0 >> 8) & 0xFFu;
        if (resource < 0xA0u) {
            throw std::runtime_error("invalid Latte attribute resource");
        }
        inputs.push_back({
            w1 & 0xFFu,
            resource - 0xA0u,
            w2 & 0xFFFFu,
            (w1 >> 22) & 0x3Fu,
            (w1 >> 28) & 3u,
            ((w1 >> 30) & 1u) != 0,
            {(w1 >> 9) & 7u, (w1 >> 12) & 7u, (w1 >> 15) & 7u,
             (w1 >> 18) & 7u},
            (w2 >> 16) & 3u,
        });
        static_cast<void>(components(inputs.back().data_format));
    }
    return inputs;
}

struct AluInstruction {
    uint32_t w0{};
    uint32_t w1{};
};

uint32_t source_count(const AluInstruction& instruction) {
    if (((instruction.w1 >> 15) & 7u) != 0) {
        return 3;
    }
    switch ((instruction.w1 >> 7) & 0x7FFu) {
    case 0x19: return 1;
    case 0x50: return 2;
    default: return 2;
    }
}
bool is_transcendental(uint32_t opcode) {
    return (opcode >= 0x61 && opcode <= 0x6F) || opcode == 0x73 ||
           opcode == 0x75 || opcode == 0x79;
}


std::string source(const AluInstruction& instruction, uint32_t index,
                   const std::vector<uint32_t>& literals,
                   const std::array<uint32_t, 2>& kcache,
                   const std::array<uint32_t, 2>& kcache_banks,
                   std::vector<LatteUniformBlockRef>& block_refs) {
    uint32_t selector{};
    uint32_t channel{};
    bool negate{};
    bool absolute{};
    if (index == 0) {
        selector = instruction.w0 & 0x1FFu;
        channel = (instruction.w0 >> 10) & 3u;
        negate = ((instruction.w0 >> 12) & 1u) != 0;
        absolute = ((instruction.w1 >> 15) & 7u) == 0 &&
                   (instruction.w1 & 1u) != 0;
    } else if (index == 1) {
        selector = (instruction.w0 >> 13) & 0x1FFu;
        channel = (instruction.w0 >> 23) & 3u;
        negate = ((instruction.w0 >> 25) & 1u) != 0;
        absolute = ((instruction.w1 >> 15) & 7u) == 0 &&
                   ((instruction.w1 >> 1) & 1u) != 0;
    } else {
        selector = instruction.w1 & 0x1FFu;
        channel = (instruction.w1 >> 10) & 3u;
        negate = ((instruction.w1 >> 12) & 1u) != 0;
    }

    std::string value;
    if (selector < 128) {
        value = "R[" + std::to_string(selector) + "]." + kChannel[channel];
    } else if (selector < 192) {
        const uint32_t cache = selector < 160 ? 0 : 1;
        const LatteUniformBlockRef ref{
            kcache_banks[cache],
            kcache[cache] + selector - (cache == 0 ? 128 : 160)};
        auto found = std::find(block_refs.begin(), block_refs.end(), ref);
        if (found == block_refs.end()) {
            found = block_refs.insert(block_refs.end(), ref);
        }
        value = "b[" +
                std::to_string(
                    static_cast<size_t>(found - block_refs.begin())) +
                "]." + kChannel[channel];
    } else if (selector >= 256) {
        value = "c[" + std::to_string(selector - 256) + "]." +
                kChannel[channel];
    } else {
        switch (selector) {
        case 248: value = "0.0"; break;
        case 249: value = "1.0"; break;
        case 250: value = "uintBitsToFloat(1u)"; break;
        case 251: value = "uintBitsToFloat(0xFFFFFFFFu)"; break;
        case 252: value = "0.5"; break;
        case 253:
            if (channel >= literals.size()) {
                throw std::runtime_error("missing Latte ALU literal");
            }
            value = "uintBitsToFloat(" + std::to_string(literals[channel]) +
                    "u)";
            break;
        case 254: value = "PV." + std::string{kChannel[channel]}; break;
        case 255: value = "PS"; break;
        default:
            throw std::runtime_error("unsupported Latte ALU source " +
                                     std::to_string(selector));
        }
    }
    if (absolute) {
        value = "abs(" + value + ")";
    }
    if (negate) {
        value = "-(" + value + ")";
    }
    return value;
}

class Emitter {
public:
    Emitter(const char* stage, std::span<const uint8_t> program)
        : stage_(stage), program_(program) {}

    void emit(std::ostringstream& output) {
        for (size_t cf_offset = 0; cf_offset + 8 <= program_.size();
             cf_offset += 8) {
            const uint32_t w0 = word(program_, cf_offset, stage_);
            const uint32_t w1 = word(program_, cf_offset + 4, stage_);
            const uint32_t type = (w1 >> 28) & 3u;
            if (type == 2 || type == 3) {
                emit_alu(output, w0, w1);
            } else if (type == 1) {
                emit_export(output, w0, w1);
            } else {
                const uint32_t opcode = (w1 >> 23) & 0x7Fu;
                if (opcode == 0 || opcode == 0x0A || opcode == 0x13 ||
                    opcode == 0x14 || opcode == 0x19) {
                } else if (opcode == 0x0B || opcode == 0x0C) {
                    output << "  execStack[execStackIndex++] = execActive;\n";
                } else if (opcode == 0x0D) {
                    output << "  if (execStack[execStackIndex - 1]) "
                              "execActive = !execActive;\n";
                } else if (opcode == 0x0E) {
                    output << "  execStackIndex -= " << (w1 & 7u)
                           << "; execActive = execStack[execStackIndex];\n";
                } else if (opcode == 1) {
                    emit_texture(output, w0, w1);
                } else {
                    throw std::runtime_error(
                        unsupported(stage_, cf_offset, w0, w1));
                }
            }
            if (((type == 0 || type == 1) && ((w1 >> 21) & 1u) != 0) ||
                (((w1 >> 23) & 0x7Fu) == 0x14 && type == 0)) {
                break;
            }
        }
    }

    const std::vector<uint32_t>& samplers() const { return samplers_; }
    const std::vector<LatteUniformBlockRef>& uniform_blocks() const {
        return uniform_blocks_;
    }

private:
    void emit_alu(std::ostringstream& output, uint32_t cf0, uint32_t cf1) {
        const size_t start = static_cast<size_t>(cf0 & 0x3FFFFFu) * 8;
        const uint32_t opcode = (cf1 >> 26) & 0xFu;
        if (opcode != 0x08 && opcode != 0x09 && opcode != 0x0A &&
            opcode != 0x0B && opcode != 0x0F) {
            throw std::runtime_error(unsupported(stage_, start, cf0, cf1));
        }
        if (opcode == 0x09) {
            output << "  execStack[execStackIndex++] = execActive;\n";
        }
        output << "  if (execActive) {\n";
        const uint32_t slots = ((cf1 >> 18) & 0x7Fu) + 1;
        kcache_[0] = ((cf1 >> 2) & 0xFFu) * 16u;
        kcache_[1] = ((cf1 >> 10) & 0xFFu) * 16u;
        kcache_banks_[0] = (cf0 >> 22) & 0xFu;
        kcache_banks_[1] = (cf0 >> 26) & 0xFu;
        size_t slot = 0;
        while (slot < slots) {
            std::vector<AluInstruction> group;
            for (uint32_t unit = 0; unit < 5 && slot < slots; ++unit, ++slot) {
                const size_t offset = start + slot * 8;
                AluInstruction instruction{word(program_, offset, stage_),
                                           word(program_, offset + 4, stage_)};
                group.push_back(instruction);
                if ((instruction.w0 >> 31) != 0) {
                    ++slot;
                    break;
                }
            }
            uint32_t literal_count = 0;
            for (const auto& instruction : group) {
                const uint32_t count = source_count(instruction);
                if (count > 0 && (instruction.w0 & 0x1FFu) == 253) {
                    literal_count = std::max(literal_count,
                                             ((instruction.w0 >> 10) & 3u) + 1);
                }
                if (count > 1 && ((instruction.w0 >> 13) & 0x1FFu) == 253) {
                    literal_count = std::max(literal_count,
                                             ((instruction.w0 >> 23) & 3u) + 1);
                }
                if (count > 2 && (instruction.w1 & 0x1FFu) == 253) {
                    literal_count = std::max(
                        literal_count, ((instruction.w1 >> 10) & 3u) + 1);
                }
            }
            std::vector<uint32_t> literals;
            literals.reserve(literal_count);
            for (uint32_t i = 0; i < literal_count; ++i) {
                literals.push_back(word(program_, start + slot * 8 + i * 4,
                                        stage_));
            }
            slot += (literal_count + 1) / 2;
            emit_group(output, group, literals);
        }
        output << "  }\n";
        if (opcode == 0x0A) {
            output << "  execActive = execStack[--execStackIndex];\n";
        } else if (opcode == 0x0B) {
            output << "  execStackIndex -= 2; "
                      "execActive = execStack[execStackIndex];\n";
        } else if (opcode == 0x0F) {
            output << "  if (execStack[execStackIndex - 1]) "
                      "execActive = !execActive;\n";
        }
    }

    void emit_group(std::ostringstream& output,
                    const std::vector<AluInstruction>& group,
                    const std::vector<uint32_t>& literals) {
        const uint32_t group_id = group_index_++;
        std::ostringstream writes;
        size_t first_instruction = 0;
        if (!group.empty() &&
            ((group.front().w1 >> 7) & 0x7FFu) == 0x50) {
            if (group.size() < 4) {
                throw std::runtime_error(
                    "Latte DOT4 group must contain four lanes");
            }
            output << "  float dotResult" << group_id << " = ";
            for (size_t i = 0; i < 4; ++i) {
                if (i != 0) {
                    output << " + ";
                }
                output << "("
                       << source(group[i], 0, literals, kcache_,
                                 kcache_banks_, uniform_blocks_)
                       << " * "
                       << source(group[i], 1, literals, kcache_,
                                 kcache_banks_, uniform_blocks_)
                       << ")";
            }
            output << ";\n";
            for (size_t i = 0; i < 4; ++i) {
                const auto& instruction = group[i];
                if (((instruction.w1 >> 4) & 1u) != 0) {
                    writes << "  R[" << ((instruction.w1 >> 21) & 0x7Fu)
                           << "]." << kChannel[(instruction.w1 >> 29) & 3u]
                           << " = dotResult" << group_id << ";\n";
                }
            }
            writes << "  PV = vec4(dotResult" << group_id << ");\n";
            first_instruction = 4;
        }

        std::array<bool, 4> used{};
        used.fill(first_instruction != 0);
        for (size_t i = first_instruction; i < group.size(); ++i) {
            const auto& instruction = group[i];
            const uint32_t encoding = (instruction.w1 >> 15) & 7u;
            const uint32_t opcode =
                encoding == 0 ? (instruction.w1 >> 7) & 0x7FFu
                              : (instruction.w1 >> 13) & 0x1Fu;
            if (encoding == 0 && opcode == 0x1A) {
                continue;
            }

            std::string predicate;
            std::string predicate_name;
            const auto first = source(instruction, 0, literals, kcache_,
                                      kcache_banks_, uniform_blocks_);
            std::string expression;
            if (encoding != 0) {
                const auto second =
                    source(instruction, 1, literals, kcache_, kcache_banks_,
                           uniform_blocks_);
                const auto third =
                    source(instruction, 2, literals, kcache_, kcache_banks_,
                           uniform_blocks_);
                if (opcode == 0x18) {
                    expression = "(" + first + " == 0.0 ? " + second +
                                 " : " + third + ")";
                } else if (opcode == 0x19) {
                    expression = "(" + first + " > 0.0 ? " + second +
                                 " : " + third + ")";
                } else if (opcode == 0x1A) {
                    expression = "(" + first + " >= 0.0 ? " + second +
                                 " : " + third + ")";
                } else if (opcode == 0x1C) {
                    expression = "(floatBitsToInt(" + first + ") == 0 ? " +
                                 second + " : " + third + ")";
                } else if (opcode == 0x1D) {
                    expression = "(floatBitsToInt(" + first + ") > 0 ? " +
                                 second + " : " + third + ")";
                } else if (opcode == 0x1E) {
                    expression = "(floatBitsToInt(" + first + ") >= 0 ? " +
                                 second + " : " + third + ")";
                } else {
                    if (opcode != 0x07 &&
                        (opcode < 0x10 || opcode > 0x17)) {
                        throw std::runtime_error(unsupported(
                            stage_, i * 8, instruction.w0, instruction.w1));
                    }
                    expression = first + " * " + second + " + " + third;
                    const uint32_t modifier =
                        opcode == 0x07 ? 0 : (opcode - 0x10) & 3u;
                    if (modifier == 1) {
                        expression = "2.0 * (" + expression + ")";
                    } else if (modifier == 2) {
                        expression = "4.0 * (" + expression + ")";
                    } else if (modifier == 3) {
                        expression = "0.5 * (" + expression + ")";
                    }
                }
            } else {
                const auto second =
                    source_count(instruction) > 1
                        ? source(instruction, 1, literals, kcache_,
                                 kcache_banks_, uniform_blocks_)
                        : std::string{};
                switch (opcode) {
                case 0x00: expression = first + " + " + second; break;
                case 0x01:
                case 0x02: expression = first + " * " + second; break;
                case 0x03:
                case 0x05:
                    expression = "max(" + first + ", " + second + ")";
                    break;
                case 0x04:
                case 0x06:
                    expression = "min(" + first + ", " + second + ")";
                    break;
                case 0x08:
                    expression =
                        "(" + first + " == " + second + " ? 1.0 : 0.0)";
                    break;
                case 0x09:
                    expression =
                        "(" + first + " > " + second + " ? 1.0 : 0.0)";
                    break;
                case 0x0D:
                    expression = "intBitsToFloat((" + first + " > " + second +
                                 " ? -1 : 0))";
                    break;
                case 0x0A:
                    expression =
                        "(" + first + " >= " + second + " ? 1.0 : 0.0)";
                    break;
                case 0x0B:
                    expression =
                        "(" + first + " != " + second + " ? 1.0 : 0.0)";
                    break;
                case 0x20: predicate = first + " == " + second; break;
                case 0x21: predicate = first + " > " + second; break;
                case 0x22: predicate = first + " >= " + second; break;
                case 0x23: predicate = first + " != " + second; break;
                case 0x42:
                    predicate = "floatBitsToInt(" + first +
                                ") == floatBitsToInt(" + second + ")";
                    break;
                case 0x43:
                    predicate = "floatBitsToInt(" + first +
                                ") > floatBitsToInt(" + second + ")";
                    break;
                case 0x44:
                    predicate = "floatBitsToInt(" + first +
                                ") >= floatBitsToInt(" + second + ")";
                    break;
                case 0x45:
                    predicate = "floatBitsToInt(" + first +
                                ") != floatBitsToInt(" + second + ")";
                    break;
                case 0x3A:
                    expression =
                        "uintBitsToFloat(floatBitsToInt(" + first +
                        ") == floatBitsToInt(" + second +
                        ") ? 0xFFFFFFFFu : 0u)";
                    break;
                case 0x3B:
                    expression =
                        "uintBitsToFloat(floatBitsToInt(" + first +
                        ") > floatBitsToInt(" + second +
                        ") ? 0xFFFFFFFFu : 0u)";
                    break;
                case 0x3C:
                    expression =
                        "uintBitsToFloat(floatBitsToInt(" + first +
                        ") >= floatBitsToInt(" + second +
                        ") ? 0xFFFFFFFFu : 0u)";
                    break;
                case 0x3D:
                    expression =
                        "uintBitsToFloat(floatBitsToInt(" + first +
                        ") != floatBitsToInt(" + second +
                        ") ? 0xFFFFFFFFu : 0u)";
                    break;
                case 0x3E:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") > floatBitsToUint(" + second +
                        ") ? 0xFFFFFFFFu : 0u)";
                    break;
                case 0x3F:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") >= floatBitsToUint(" + second +
                        ") ? 0xFFFFFFFFu : 0u)";
                    break;
                case 0x10: expression = "fract(" + first + ")"; break;
                case 0x14: expression = "floor(" + first + ")"; break;
                case 0x19: expression = first; break;
                case 0x13: expression = "roundEven(" + first + ")"; break;
                case 0x61: expression = "exp2(" + first + ")"; break;
                case 0x62:
                    expression = "log2(max(0.0, " + first + "))";
                    break;
                case 0x6A: expression = "sqrt(" + first + ")"; break;
                case 0x64:
                case 0x65:
                case 0x66: expression = "(1.0 / " + first + ")"; break;
                case 0x69: expression = "inversesqrt(" + first + ")"; break;
                case 0x6B:
                    expression = "intBitsToFloat(int(" + first + "))";
                    break;
                case 0x6C:
                    expression = "float(floatBitsToInt(" + first + "))";
                    break;
                case 0x6D:
                    expression = "float(floatBitsToUint(" + first + "))";
                    break;
                case 0x70:
                    expression =
                        "intBitsToFloat(floatBitsToInt(" + first +
                        ") >> (floatBitsToUint(" + second + ") & 31u))";
                    break;
                case 0x71:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") >> (floatBitsToUint(" + second + ") & 31u))";
                    break;
                case 0x72:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") << (floatBitsToUint(" + second + ") & 31u))";
                    break;
                case 0x73:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") * floatBitsToUint(" + second + "))";
                    break;
                case 0x79:
                    expression = "uintBitsToFloat(uint(" + first + "))";
                    break;
                case 0x46:
                    if (std::string_view(stage_) != "pixel") {
                        throw std::runtime_error("KILLE_INT outside pixel shader");
                    }
                    output << "  if (floatBitsToInt(" << first
                           << ") == floatBitsToInt(" << second
                           << ")) discard;\n";
                    continue;
                case 0x34:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") + floatBitsToUint(" + second + "))";
                    break;
                case 0x30:
                    expression =
                        "uintBitsToFloat(floatBitsToUint(" + first +
                        ") & floatBitsToUint(" + second + "))";
                    break;
                default:
                    throw std::runtime_error(unsupported(
                        stage_, i * 8, instruction.w0, instruction.w1));
                }
                if (!predicate.empty()) {
                    predicate_name = "pred" + std::to_string(group_id) + "_" +
                                     std::to_string(i);
                    output << "  bool " << predicate_name << " = " << predicate
                           << ";\n";
                    expression =
                        opcode >= 0x42
                            ? "uintBitsToFloat(" + predicate_name +
                                  " ? 0xFFFFFFFFu : 0u)"
                            : "(" + predicate_name + " ? 1.0 : 0.0)";
                }
                switch ((instruction.w1 >> 5) & 3u) {
                case 1: expression = "2.0 * (" + expression + ")"; break;
                case 2: expression = "4.0 * (" + expression + ")"; break;
                case 3: expression = "0.5 * (" + expression + ")"; break;
                default: break;
                }
            }
            if ((instruction.w1 >> 31) != 0) {
                expression = "clamp(" + expression + ", 0.0, 1.0)";
            }

            const uint32_t channel = (instruction.w1 >> 29) & 3u;
            const bool scalar =
                (encoding == 0 && is_transcendental(opcode)) || used[channel];
            used[channel] = true;
            output << "  float alu" << group_id << "_" << i << " = "
                   << expression << ";\n";
            if (encoding != 0 || ((instruction.w1 >> 4) & 1u) != 0) {
                writes << "  R[" << ((instruction.w1 >> 21) & 0x7Fu) << "]."
                       << kChannel[channel] << " = alu" << group_id << "_" << i
                       << ";\n";
            }
            if (predicate_name.empty()) {
                if (scalar) {
                    writes << "  PS = alu" << group_id << "_" << i << ";\n";
                } else {
                    writes << "  PV." << kChannel[channel] << " = alu"
                           << group_id << "_" << i << ";\n";
                }
            }
            if (encoding == 0 && ((instruction.w1 >> 2) & 3u) != 0) {
                if (predicate_name.empty()) {
                    throw std::runtime_error(
                        "unsupported Latte predicate mask update");
                }
                if (((instruction.w1 >> 3) & 1u) != 0) {
                    writes << "  predicateState = " << predicate_name << ";\n";
                }
                if (((instruction.w1 >> 2) & 1u) != 0) {
                    writes << "  execActive = " << predicate_name << ";\n";
                }
            }
        }
        output << writes.str();
    }

    void emit_texture(std::ostringstream& output, uint32_t address,
                      uint32_t cf) {
        const uint32_t count = ((cf >> 10) & 7u) +
                               (((cf >> 19) & 1u) << 3) + 1;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t offset = static_cast<size_t>(address) * 8 + i * 16;
            const uint32_t w0 = word(program_, offset, stage_);
            const uint32_t w1 = word(program_, offset + 4, stage_);
            const uint32_t w2 = word(program_, offset + 8, stage_);
            const uint32_t opcode = w0 & 0x1Fu;
            if (opcode != 0x0F && opcode != 0x10 && opcode != 0x11 &&
                opcode != 0x13) {
                throw std::runtime_error(
                    unsupported("pixel TEX", offset, w0, w1));
            }
            const uint32_t resource = (w0 >> 8) & 0xFFu;
            const uint32_t source_gpr = (w0 >> 16) & 0x7Fu;
            const uint32_t destination = w1 & 0x7Fu;
            const uint32_t sampler = (w2 >> 15) & 0x1Fu;
            if (resource != sampler) {
                throw std::runtime_error("separate Latte resource/sampler slots unsupported");
            }
            if (std::find(samplers_.begin(), samplers_.end(), resource) ==
                samplers_.end()) {
                samplers_.push_back(resource);
            }
            output << "  R[" << destination << "] = "
                   << (opcode == 0x11 || opcode == 0x13 ? "textureLod(tex"
                                                       : "texture(tex")
                   << resource << ", vec2("
                   << selected("R[" + std::to_string(source_gpr) + "]",
                               (w2 >> 20) & 7u)
                   << ", "
                   << selected("R[" + std::to_string(source_gpr) + "]",
                               (w2 >> 23) & 7u)
                   << ")";
            if (opcode == 0x11) {
                output << ", R[" << source_gpr << "].w";
            } else if (opcode == 0x13) {
                output << ", 0.0";
            }
            output << ");\n";
        }
    }

    void emit_export(std::ostringstream& output, uint32_t w0, uint32_t w1) {
        const uint32_t base = w0 & 0x1FFFu;
        const uint32_t type = (w0 >> 13) & 3u;
        const uint32_t gpr = (w0 >> 15) & 0x7Fu;
        std::string value = "vec4(";
        for (uint32_t i = 0; i < 4; ++i) {
            if (i != 0) {
                value += ", ";
            }
            value += selected("R[" + std::to_string(gpr) + "]",
                              (w1 >> (i * 3)) & 7u);
        }
        value += ")";
        if (std::string{stage_} == "vertex") {
            if (type == 1 && base == 60) {
                output << "  gl_Position = " << value << ";\n";
            } else if (type == 2 && base < 32) {
                output << "  v" << base << " = " << value << ";\n";
            } else {
                throw std::runtime_error("unsupported Latte vertex export");
            }
        } else if (type == 0 && base < 8) {
            output << "  outColor" << base << " = " << value << ";\n";
        } else if (type == 1 && base == 61) {
            output << "  gl_FragDepth = R[" << gpr << "].x;\n";
        } else {
            throw std::runtime_error(
                "unsupported Latte pixel export type=" +
                std::to_string(type) + " base=" + std::to_string(base));
        }
    }

    const char* stage_;
    std::span<const uint8_t> program_;
    std::vector<uint32_t> samplers_;
    std::array<uint32_t, 2> kcache_{};
    std::array<uint32_t, 2> kcache_banks_{};
    std::vector<LatteUniformBlockRef> uniform_blocks_;
    uint32_t group_index_{};
};

std::vector<uint32_t> compile(const std::string& source,
                              shaderc_shader_kind kind, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_0);
    options.SetTargetSpirv(shaderc_spirv_version_1_0);
    options.SetWarningsAsErrors();
    const auto result = compiler.CompileGlslToSpv(source, kind, name, options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error(std::string{name} + ": " +
                                 result.GetErrorMessage() + "\n" + source);
    }
    return {result.cbegin(), result.cend()};
}
} // namespace

uint32_t decode_latte_vertex_word(uint32_t word, uint32_t endian) {
    switch (endian) {
    case 0:
        return ((word & 0x000000FFu) << 24) |
               ((word & 0x0000FF00u) << 8) |
               ((word & 0x00FF0000u) >> 8) |
               ((word & 0xFF000000u) >> 24);
    case 1:
        return (word << 16) | (word >> 16);
    case 2:
        return word;
    default:
        throw std::runtime_error("invalid Latte vertex endian mode");
    }
}

LatteTranslation translate_latte(const LatteShaderInput& input) {
    LatteTranslation result;
    result.vertex_inputs = parse_fetch(input.fetch_program);
    if (input.vs_regs.size() > 17) {
        const size_t semantic_count =
            std::min<size_t>({input.vs_regs[16], 32,
                              input.vs_regs.size() - 17});
        const auto semantics = input.vs_regs.subspan(17, semantic_count);
        std::erase_if(result.vertex_inputs, [&](const LatteVertexInput& value) {
            return std::none_of(
                semantics.begin(), semantics.end(), [&](uint32_t semantic) {
                    return (semantic & 0xFFu) == value.semantic;
                });
        });
    }

    Emitter vertex_emitter("vertex", input.vs_program);
    std::ostringstream vertex_body;
    vertex_emitter.emit(vertex_body);
    result.vertex_uniform_blocks = vertex_emitter.uniform_blocks();

    std::ostringstream vertex;
    vertex << "#version 450\n";
    for (const auto& value : result.vertex_inputs) {
        vertex << "layout(location=" << value.semantic << ") in "
               << vector_type(components(value.data_format)) << " a"
               << value.semantic << ";\n";
    }
    for (uint32_t location = 0; location < 30; ++location) {
        vertex << "layout(location=" << location << ") out vec4 v"
               << location << ";\n";
    }
    vertex << "layout(std140, set=1, binding=0) uniform VsRegs { "
              "vec4 c[256]; vec4 b["
           << std::max<size_t>(1, result.vertex_uniform_blocks.size())
           << "]; };\n"
              "void main() {\n"
              "  vec4 R[128]; vec4 PV=vec4(0.0); float PS=0.0;\n"
              "  bool execActive=true, predicateState=false; "
              "bool execStack[4]; int execStackIndex=0;\n";
    vertex << "  for (int i = 0; i < 128; ++i) R[i] = vec4(0.0);\n"
              "  R[0] = vec4(intBitsToFloat(gl_VertexIndex), 0.0, 0.0, "
              "intBitsToFloat(gl_InstanceIndex));\n";
    for (const auto& value : result.vertex_inputs) {
        uint32_t gpr = value.semantic + 1;
        if (input.vs_regs.size() > 17) {
            const size_t semantic_count =
                std::min<size_t>({input.vs_regs[16], 32,
                                  input.vs_regs.size() - 17});
            bool found = false;
            for (size_t index = 0; index < semantic_count; ++index) {
                if ((input.vs_regs[17 + index] & 0xFFu) == value.semantic) {
                    gpr = static_cast<uint32_t>(index + 1);
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("unmapped Latte vertex semantic " +
                                         std::to_string(value.semantic));
            }
        }
        const uint32_t count = components(value.data_format);
        vertex << "  R[" << gpr << "] = vec4(";
        for (uint32_t i = 0; i < 4; ++i) {
            if (i != 0) {
                vertex << ", ";
            }
            vertex << selected("a" + std::to_string(value.semantic),
                               value.dst_sel[i]);
        }
        vertex << ");\n";
        static_cast<void>(count);
    }
    vertex << vertex_body.str();
    if (std::getenv("NWIIU_GPU_DEBUG_SOLID") != nullptr ||
        std::getenv("NWIIU_GPU_DEBUG_VERTEX") != nullptr) {
        vertex << "  const vec2 debugPositions[4] = vec2[4]("
                  "vec2(-1.0,-1.0),vec2(1.0,-1.0),"
                  "vec2(-1.0,1.0),vec2(1.0,1.0));\n"
                  "  gl_Position = vec4(debugPositions[gl_VertexIndex & 3],"
                  "0.0,1.0);\n";
    }
    vertex << "}\n";
    result.vertex_glsl = vertex.str();
    Emitter pixel_emitter("pixel", input.ps_program);
    std::ostringstream pixel_body;
    pixel_emitter.emit(pixel_body);
    result.ps_sampler_slots = pixel_emitter.samplers();
    result.fragment_uniform_blocks = pixel_emitter.uniform_blocks();

    std::ostringstream fragment;
    fragment << "#version 450\n";
    for (uint32_t location = 0; location < 30; ++location) {
        fragment << "layout(location=" << location << ") in vec4 v"
                 << location << ";\n";
    }
    fragment << "layout(location=0) out vec4 outColor0;\n"
                "layout(std140, set=3, binding=0) uniform PsRegs { "
                "vec4 c[256]; vec4 b["
             << std::max<size_t>(1, result.fragment_uniform_blocks.size())
             << "]; };\n";
    for (size_t i = 0; i < result.ps_sampler_slots.size(); ++i) {
        fragment << "layout(set=2, binding=" << i
                 << ") uniform sampler2D tex" << result.ps_sampler_slots[i]
                 << ";\n";
    }
    fragment << "void main() {\n";
    if (std::getenv("NWIIU_GPU_DEBUG_SAMPLE") != nullptr &&
        !result.ps_sampler_slots.empty()) {
        fragment << "  outColor0 = texelFetch(tex"
                 << result.ps_sampler_slots.front() << ", ivec2(0), 0);\n";
    } else {
        fragment << "  vec4 R[128]; vec4 PV=vec4(0.0); float PS=0.0;\n"
                    "  bool execActive=true, predicateState=false; "
                    "bool execStack[4]; int execStackIndex=0;\n"
                    "  for (int i = 0; i < 128; ++i) R[i] = vec4(0.0);\n";
        for (uint32_t location = 0; location < 30; ++location) {
            fragment << "  R[" << location << "] = v" << location << ";\n";
        }
        fragment << pixel_body.str();
        if (std::getenv("NWIIU_GPU_DEBUG_SOLID") != nullptr ||
            std::getenv("NWIIU_GPU_DEBUG_FRAGMENT") != nullptr) {
            fragment << "  outColor0 = vec4(1.0,0.0,0.0,1.0);\n";
        }
    }
    fragment << "}\n";
    result.fragment_glsl = fragment.str();
    result.vertex_spirv = compile(result.vertex_glsl,
                                  shaderc_glsl_vertex_shader, "Latte vertex");
    result.fragment_spirv = compile(result.fragment_glsl,
                                    shaderc_glsl_fragment_shader, "Latte pixel");
    return result;
}

} // namespace nwii::runtime
