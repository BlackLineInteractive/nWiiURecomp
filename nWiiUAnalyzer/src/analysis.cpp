#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/analyzer/ppc_instruction.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>

namespace nwiiu::analyzer {
namespace {
const Section* code_section_containing(const RpxImage& image,
                                       uint32_t address) {
    if ((address & 3) != 0) {
        return nullptr;
    }
    for (const auto& section : image.sections) {
        if (!section.analyzable_code() || address < section.address) {
            continue;
        }
        const uint64_t offset =
            static_cast<uint64_t>(address) - section.address;
        if (offset + sizeof(uint32_t) <= section.data.size()) {
            return &section;
        }
    }
    return nullptr;
}

uint32_t read_instruction(const Section& section, uint32_t address) {
    const size_t offset = static_cast<size_t>(address - section.address);
    return static_cast<uint32_t>(section.data[offset]) << 24 |
           static_cast<uint32_t>(section.data[offset + 1]) << 16 |
           static_cast<uint32_t>(section.data[offset + 2]) << 8 |
           static_cast<uint32_t>(section.data[offset + 3]);
}

std::optional<uint32_t>
recover_jump_table_base(const std::array<uint32_t, 33>& instructions,
                        size_t instruction_count, size_t next_instruction) {
    if (instruction_count < 5) {
        return std::nullopt;
    }

    std::optional<uint32_t> table_register;
    bool found_load = false;
    std::optional<uint32_t> low;
    for (size_t scanned = 1; scanned < instruction_count; ++scanned) {
        const uint32_t word =
            instructions[(next_instruction + 32 - scanned) % 33];
        if (!table_register.has_value()) {
            if ((word & 0xFC1FFFFF) == 0x7C0903A6) {
                table_register = (word >> 21) & 0x1F;
            }
            continue;
        }

        const uint32_t reg = *table_register;
        if (!found_load) {
            if ((word & 0xFC0007FF) == 0x7C00002E &&
                ((word >> 21) & 0x1F) == reg &&
                ((word >> 16) & 0x1F) == reg) {
                found_load = true;
            }
            continue;
        }
        if (!low.has_value()) {
            if (word >> 26 == 14 && ((word >> 21) & 0x1F) == reg &&
                ((word >> 16) & 0x1F) == reg) {
                low = (word & 0x8000) != 0
                          ? (word & 0xFFFF) | 0xFFFF0000
                          : word & 0xFFFF;
            }
            continue;
        }

        if (word >> 26 == 15 && ((word >> 21) & 0x1F) == reg &&
            ((word >> 16) & 0x1F) == 0) {
            return ((word & 0xFFFF) << 16) + *low;
        }
    }
    return std::nullopt;
}

const Section* data_section_containing(const RpxImage& image,
                                       uint32_t address) {
    for (const auto& section : image.sections) {
        if ((section.flags & kShfAlloc) == 0 || section.executable() ||
            address < section.address) {
            continue;
        }
        const uint64_t offset =
            static_cast<uint64_t>(address) - section.address;
        if (offset < section.data.size()) {
            return &section;
        }
    }
    return nullptr;
}
}

Analysis analyze(const RpxImage& image) {
    Analysis analysis;
    std::map<uint32_t, uint32_t> rel24_targets;
    for (const auto& relocation : image.relocations) {
        if (relocation.type == kRppcRel24 &&
            relocation.target_address.has_value()) {
            rel24_targets.emplace(relocation.source_address,
                                  *relocation.target_address);
        }
    }
    std::map<uint32_t, std::set<std::string>> root_reasons;
    std::set<uint32_t> pending_roots;
    const auto add_root = [&](uint32_t address, std::string_view reason) {
        if (code_section_containing(image, address) == nullptr) {
            return false;
        }
        root_reasons[address].emplace(reason);
        const auto existing = analysis.functions.find(address);
        if (existing == analysis.functions.end()) {
            pending_roots.insert(address);
        } else {
            existing->second.discovery_reasons.emplace(reason);
        }
        return true;
    };

    add_root(image.entry_point, "entry_point");
    for (const auto& relocation : image.relocations) {
        if (relocation.target_address.has_value()) {
            add_root(*relocation.target_address, "relocation");
        }
    }

    size_t roots_before_pass;
    do {
        roots_before_pass = root_reasons.size();
        analysis.functions.clear();
        analysis.unresolved.clear();
        pending_roots.clear();
        for (const auto& root : root_reasons) {
            pending_roots.insert(root.first);
        }

        while (!pending_roots.empty()) {
            const uint32_t start = *pending_roots.begin();
            pending_roots.erase(pending_roots.begin());
            if (analysis.functions.contains(start)) {
                continue;
            }

            Function function;
            function.start = start;
            const Section* function_section =
                code_section_containing(image, start);
            uint64_t interval_end =
                static_cast<uint64_t>(function_section->address) +
                function_section->data.size();
            for (auto next = root_reasons.upper_bound(start);
                 next != root_reasons.end(); ++next) {
                if (code_section_containing(image, next->first) ==
                    function_section) {
                    interval_end = next->first;
                    break;
                }
            }
            const auto owns_address = [&](uint32_t address) {
                return address >= start &&
                       static_cast<uint64_t>(address) < interval_end &&
                       code_section_containing(image, address) ==
                           function_section;
            };
            std::set<uint32_t> blocks{start};
            std::set<uint32_t> decoded;
            while (!blocks.empty()) {
                uint32_t address = *blocks.begin();
                blocks.erase(blocks.begin());
                std::array<uint32_t, 33> block_instructions{};
                size_t instruction_count = 0;
                size_t next_instruction = 0;
                while (true) {
                    if (!owns_address(address) || decoded.contains(address)) {
                        break;
                    }

                    decoded.insert(address);
                    const uint32_t word =
                        read_instruction(*function_section, address);
                    block_instructions[next_instruction] = word;
                    next_instruction = (next_instruction + 1) % 33;
                    instruction_count =
                        std::min(instruction_count + 1,
                                 block_instructions.size());
                    const PpcInstruction instruction{word};
                    const uint32_t fallthrough = address + sizeof(uint32_t);

                    if (instruction.is_direct_branch()) {
                        const auto relocation_target =
                            rel24_targets.find(address);
                        const bool resolved_relocation =
                            relocation_target != rel24_targets.end();
                        const uint32_t target =
                            resolved_relocation
                                ? relocation_target->second
                                : instruction.branch_target(address);
                        if (instruction.link()) {
                            if (add_root(target, "direct_call") ||
                                resolved_relocation) {
                                function.callees.insert(target);
                            } else {
                                analysis.unresolved.push_back(
                                    {address, "function_boundary",
                                     "direct branch target outside root-owned "
                                     "interval"});
                            }
                            address = fallthrough;
                            continue;
                        }

                        if (instruction.opcode() == 16 &&
                            instruction.branch_option_is_conditional() &&
                            owns_address(fallthrough)) {
                            blocks.insert(fallthrough);
                        }
                        if (owns_address(target)) {
                            blocks.insert(target);
                        } else if (root_reasons.contains(target) ||
                                   resolved_relocation) {
                            function.callees.insert(target);
                        } else if (add_root(target, "direct_branch")) {
                            // A branch target is a statically known entry
                            // point even when it lands outside this function's
                            // interval, which is routine for the EABI register
                            // save/restore helpers that are entered at many
                            // different offsets. Dropping it left the address
                            // unrecompiled and the host had to take the call.
                            function.callees.insert(target);
                        } else {
                            analysis.unresolved.push_back(
                                {address, "function_boundary",
                                 "direct branch target outside root-owned "
                                 "interval"});
                        }
                        break;
                    }

                    if (instruction.is_branch_to_lr()) {
                        function.dynamic_transfers.push_back(
                            {address,
                             instruction.link() ? "indirect_call" : "return"});
                        if (instruction.link() ||
                            instruction.branch_option_is_conditional()) {
                            address = fallthrough;
                            continue;
                        }
                        break;
                    }

                    if (instruction.is_branch_to_ctr()) {
                        if (instruction.link()) {
                            function.dynamic_transfers.push_back(
                                {address, "indirect_call"});
                            address = fallthrough;
                            continue;
                        }
                        if (instruction.branch_option_is_conditional() &&
                            owns_address(fallthrough)) {
                            blocks.insert(fallthrough);
                        }
                        // Inline branch table: the compiler emits the table
                        // directly after the bctr as a run of unconditional
                        // branches, one per case. Each entry is reachable only
                        // through the bctr, so nothing points at it statically
                        // and it would otherwise be left unrecompiled.
                        {
                            uint32_t entry = fallthrough;
                            uint32_t recovered = 0;
                            while (true) {
                                const auto* entry_section =
                                    code_section_containing(image, entry);
                                if (entry_section == nullptr) {
                                    break;
                                }
                                const uint64_t entry_offset =
                                    static_cast<uint64_t>(entry) -
                                    entry_section->address;
                                if (entry_offset + sizeof(uint32_t) >
                                    entry_section->data.size()) {
                                    break;
                                }
                                const PpcInstruction candidate{
                                    read_instruction(*entry_section, entry)};
                                if (candidate.opcode() != 18 ||
                                    candidate.link()) {
                                    break;
                                }
                                add_root(entry, "branch_table");
                                function.jump_table_targets.insert(entry);
                                entry += sizeof(uint32_t);
                                ++recovered;
                            }
                            if (recovered != 0) {
                                break;
                            }
                        }
                        const auto table_base =
                            recover_jump_table_base(
                                block_instructions, instruction_count,
                                next_instruction);
                        if (!table_base.has_value()) {
                            analysis.unresolved.push_back(
                                {address, "indirect_branch",
                                 "jump table base not recovered"});
                            break;
                        }

                        const Section* table_section =
                            data_section_containing(image, *table_base);
                        if (table_section == nullptr) {
                            analysis.unresolved.push_back(
                                {address, "indirect_branch",
                                 "jump table data out of bounds"});
                            break;
                        }

                        const size_t table_offset =
                            static_cast<size_t>(*table_base -
                                                table_section->address);
                        bool found_target = false;
                        bool boundary_target = false;
                        std::string_view failure =
                            "jump table entry limit reached";
                        for (size_t entry = 0; entry < 4096; ++entry) {
                            const size_t offset = table_offset + entry * 4;
                            if (offset + sizeof(uint32_t) >
                                table_section->data.size()) {
                                failure = "jump table data out of bounds";
                                break;
                            }
                            const uint32_t target =
                                static_cast<uint32_t>(
                                    table_section->data[offset])
                                    << 24 |
                                static_cast<uint32_t>(
                                    table_section->data[offset + 1])
                                    << 16 |
                                static_cast<uint32_t>(
                                    table_section->data[offset + 2])
                                    << 8 |
                                static_cast<uint32_t>(
                                    table_section->data[offset + 3]);
                            if (code_section_containing(image, target) ==
                                nullptr) {
                                failure =
                                    found_target
                                        ? std::string_view{}
                                        : "jump table first entry is not an "
                                          "aligned executable address";
                                break;
                            }

                            if (!owns_address(target)) {
                                boundary_target = true;
                                failure = {};
                                break;
                            }
                            found_target = true;
                            function.jump_table_targets.insert(target);
                            blocks.insert(target);
                        }
                        if (found_target) {
                            function.dynamic_transfers.push_back(
                                {address, "jump_table"});
                        }
                        if (!failure.empty()) {
                            analysis.unresolved.push_back(
                                {address, "indirect_branch",
                                 std::string(failure)});
                        } else if (boundary_target) {
                            analysis.unresolved.push_back(
                                {address, "function_boundary",
                                 "jump table target outside root-owned "
                                 "interval"});
                        }
                        break;
                    }

                    address = fallthrough;
                }
            }

            std::set<uint32_t> boundaries{start};
            uint32_t previous_address = 0;
            bool has_previous = false;
            for (const uint32_t address : decoded) {
                if (has_previous &&
                    address != previous_address + sizeof(uint32_t)) {
                    boundaries.insert(address);
                }

                const PpcInstruction instruction{
                    read_instruction(*function_section, address)};
                const uint32_t fallthrough = address + sizeof(uint32_t);
                if (instruction.is_direct_branch()) {
                    const auto relocation_target =
                        rel24_targets.find(address);
                    const uint32_t target =
                        relocation_target != rel24_targets.end()
                            ? relocation_target->second
                            : instruction.branch_target(address);
                    if (decoded.contains(target)) {
                        boundaries.insert(target);
                    }
                    if ((instruction.link() ||
                         (instruction.opcode() == 16 &&
                          instruction.branch_option_is_conditional())) &&
                        decoded.contains(fallthrough)) {
                        boundaries.insert(fallthrough);
                    }
                } else if ((instruction.is_branch_to_lr() ||
                            instruction.is_branch_to_ctr()) &&
                           (instruction.link() ||
                            instruction.branch_option_is_conditional()) &&
                           decoded.contains(fallthrough)) {
                    boundaries.insert(fallthrough);
                }
                previous_address = address;
                has_previous = true;
            }
            for (const uint32_t target : function.jump_table_targets) {
                if (decoded.contains(target)) {
                    boundaries.insert(target);
                }
            }

            uint32_t block_start = 0;
            uint32_t block_instruction_count = 0;
            uint32_t covered_instruction_count = 0;
            previous_address = 0;
            const auto finish_block = [&](uint32_t end) {
                assert(block_instruction_count != 0);
                assert((block_start & 3) == 0 && (end & 3) == 0);
                assert(block_instruction_count ==
                       (end - block_start) / sizeof(uint32_t));
                function.basic_blocks.push_back(
                    {block_start, end, block_instruction_count});
                covered_instruction_count += block_instruction_count;
                block_instruction_count = 0;
            };
            for (const uint32_t address : decoded) {
                if (block_instruction_count != 0 &&
                    (address != previous_address + sizeof(uint32_t) ||
                     boundaries.contains(address))) {
                    finish_block(previous_address + sizeof(uint32_t));
                }
                if (block_instruction_count == 0) {
                    block_start = address;
                }
                ++block_instruction_count;

                const PpcInstruction instruction{
                    read_instruction(*function_section, address)};
                if (instruction.is_direct_branch() ||
                    instruction.is_branch_to_lr() ||
                    instruction.is_branch_to_ctr()) {
                    finish_block(address + sizeof(uint32_t));
                }
                previous_address = address;
            }
            if (block_instruction_count != 0) {
                finish_block(previous_address + sizeof(uint32_t));
            }
            assert(covered_instruction_count == decoded.size());

            function.discovery_reasons = root_reasons[start];
            function.instruction_count = static_cast<uint32_t>(decoded.size());
            function.end = decoded.empty() ? start : *decoded.rbegin() + 4;
            std::sort(
                function.dynamic_transfers.begin(),
                function.dynamic_transfers.end(),
                [](const DynamicTransfer& left, const DynamicTransfer& right) {
                    return left.address < right.address ||
                           (left.address == right.address &&
                            left.kind < right.kind);
                });
            analysis.functions.emplace(start, std::move(function));
        }
    } while (root_reasons.size() != roots_before_pass);

    for (auto& [caller_address, caller] : analysis.functions) {
        for (const uint32_t callee_address : caller.callees) {
            const auto callee = analysis.functions.find(callee_address);
            if (callee != analysis.functions.end()) {
                callee->second.callers.insert(caller_address);
            }
        }
    }
    std::sort(analysis.unresolved.begin(), analysis.unresolved.end(),
              [](const Unresolved& left, const Unresolved& right) {
                  if (left.address != right.address) {
                      return left.address < right.address;
                  }
                  if (left.category != right.category) {
                      return left.category < right.category;
                  }
                  return left.reason < right.reason;
              });
    return analysis;
}
}
