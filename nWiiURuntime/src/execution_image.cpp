#include "runtime/execution_image.h"

#include "nwiiu/analyzer/ppc_instruction.h"

#include <set>
#include <stdexcept>

namespace nwii::runtime {
namespace {
constexpr uint32_t kStackTop = 0x50000000;
constexpr uint32_t kPageSize = 0x1000;

bool is_mapped_section(const nwiiu::analyzer::Section& section) {
    return (section.flags & nwiiu::analyzer::kShfAlloc) != 0 &&
           section.decompressed_size != 0 &&
           (section.type == nwiiu::analyzer::kShtProgbits ||
            section.type == nwiiu::analyzer::kShtNobits ||
            section.type == nwiiu::analyzer::kShtRplImports);
}
} // namespace

ExecutionImage make_execution_image(const nwiiu::analyzer::RpxImage& rpx) {
    if (!rpx.file_info.has_value()) {
        throw std::runtime_error("RPX file-info is required");
    }
    const auto& file_info = *rpx.file_info;
    if (file_info.core_stack_size == 0 ||
        file_info.core_stack_size % kPageSize != 0 ||
        file_info.core_stack_size > kStackTop) {
        throw std::runtime_error("invalid core stack size");
    }

    ExecutionImage image;
    image.entry_point = rpx.entry_point;
    image.stack_base = kStackTop - file_info.core_stack_size;
    image.stack_top = kStackTop;
    image.sda_base = file_info.sda_base;
    image.sda2_base = file_info.sda2_base;

    for (const auto& section : rpx.sections) {
        if (!is_mapped_section(section)) {
            continue;
        }
        const MemoryPermissions permissions{
            true,
            section.type == nwiiu::analyzer::kShtRplImports ||
                (section.flags & nwiiu::analyzer::kShfWrite) != 0,
            (section.flags & nwiiu::analyzer::kShfExec) != 0};
        if (section.type == nwiiu::analyzer::kShtProgbits) {
            if (section.data.size() != section.decompressed_size) {
                throw std::runtime_error(
                    "PROGBITS initial data size mismatch");
            }
            image.memory.map(section.address, section.decompressed_size,
                             permissions, section.data);
        } else {
            image.memory.map(section.address, section.decompressed_size,
                             permissions);
        }
    }
    image.memory.map(image.stack_base, file_info.core_stack_size,
                     {true, true, false});

    std::set<uint32_t> relocation_sources;
    for (const auto& relocation : rpx.relocations) {
        uint32_t width;
        switch (relocation.type) {
        case nwiiu::analyzer::kRppcAddr32:
        case nwiiu::analyzer::kRppcRel24:
            width = 4;
            break;
        case nwiiu::analyzer::kRppcAddr16Lo:
        case nwiiu::analyzer::kRppcAddr16Hi:
        case nwiiu::analyzer::kRppcAddr16Ha:
            width = 2;
            break;
        default:
            throw std::runtime_error("unsupported PowerPC relocation: " +
                                     std::to_string(relocation.type));
        }
        if (relocation.source_section_index >= rpx.sections.size()) {
            throw std::runtime_error("invalid relocation source section");
        }
        const auto& source_section =
            rpx.sections[relocation.source_section_index];
        if (!is_mapped_section(source_section)) {
            throw std::runtime_error("ineligible relocation source section");
        }
        if (relocation.source_address < source_section.address ||
            width > source_section.decompressed_size ||
            relocation.source_address - source_section.address >
                source_section.decompressed_size - width) {
            throw std::runtime_error(
                "relocation source outside declared source section");
        }
        if (!relocation_sources.insert(relocation.source_address).second) {
            throw std::runtime_error("duplicate relocation source");
        }
        if (!relocation.target_address.has_value()) {
            throw std::runtime_error("relocation target is absent");
        }
        const uint32_t target = *relocation.target_address;
        switch (relocation.type) {
        case nwiiu::analyzer::kRppcAddr32:
            image.memory.patch32(relocation.source_address, target);
            break;
        case nwiiu::analyzer::kRppcAddr16Lo:
            image.memory.patch16(relocation.source_address,
                                 static_cast<uint16_t>(target));
            break;
        case nwiiu::analyzer::kRppcAddr16Hi:
            image.memory.patch16(relocation.source_address,
                                 static_cast<uint16_t>(target >> 16));
            break;
        case nwiiu::analyzer::kRppcAddr16Ha:
            image.memory.patch16(
                relocation.source_address,
                static_cast<uint16_t>((static_cast<uint64_t>(target) + 0x8000) >>
                                      16));
            break;
        case nwiiu::analyzer::kRppcRel24: {
            if (relocation.source_address % 4 != 0) {
                throw std::runtime_error("REL24 source must be aligned");
            }
            if (target % 4 != 0) {
                throw std::runtime_error("REL24 target must be aligned");
            }
            const nwiiu::analyzer::PpcInstruction instruction{
                image.memory.fetch32(relocation.source_address)};
            if (instruction.opcode() != 18) {
                throw std::runtime_error(
                    "REL24 source must be a direct branch");
            }
            if (!image.branch_overrides
                     .emplace(relocation.source_address, target)
                     .second) {
                throw std::runtime_error("duplicate branch override");
            }
            break;
        }
        default:
            throw std::runtime_error("unsupported PowerPC relocation: " +
                                     std::to_string(relocation.type));
        }
    }

    for (const auto& module : rpx.imports) {
        for (const uint32_t symbol_index : module.function_symbols) {
            if (symbol_index >= rpx.symbols.size()) {
                throw std::runtime_error("invalid import symbol index");
            }
            const auto& symbol = rpx.symbols[symbol_index];
            if ((symbol.info & 0x0F) != 2) {
                continue;
            }
            if (!image.imports
                     .emplace(symbol.value,
                              ImportTarget{module.name, symbol.name})
                     .second) {
                throw std::runtime_error("duplicate import address");
            }
        }
        for (const uint32_t symbol_index : module.data_symbols) {
            if (symbol_index >= rpx.symbols.size()) {
                throw std::runtime_error("invalid import symbol index");
            }
            const auto& symbol = rpx.symbols[symbol_index];
            if ((symbol.info & 0x0F) != 1) {
                continue;
            }
            image.memory.patch32(symbol.value, symbol.value);
            if (!image.imports
                     .emplace(symbol.value,
                              ImportTarget{module.name, symbol.name})
                     .second) {
                throw std::runtime_error("duplicate import address");
            }
        }
    }
    return image;
}

void initialize_cpu(const ExecutionImage& image, CPUContext& cpu) {
    cpu = {};
    cpu.pc = image.entry_point;
    cpu.gpr[1] = image.stack_top - 0x20;
    cpu.gpr[2] = image.sda2_base;
    cpu.gpr[13] = image.sda_base;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0;
}
} // namespace nwii::runtime
