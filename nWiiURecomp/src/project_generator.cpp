#include "nwiiu/recomp/project_generator.h"

#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/recomp/native_generator.h"
#include "runtime/execution_image.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

namespace nwiiu::recomp {
namespace {
constexpr size_t kBlocksPerShard = 128;

// Profile strings reach the generated sources as C++ literals. Nothing in a
// profile is trusted to be identifier-safe, so escape rather than assume.
std::string cpp_literal(std::string_view value) {
    std::string text;
    text.reserve(value.size() + 2);
    text.push_back('"');
    for (const char item : value) {
        if (item == '"' || item == '\\') {
            text.push_back('\\');
            text.push_back(item);
        } else if (item >= 0x20 && item != 0x7F) {
            text.push_back(item);
        } else {
            // Control characters have no business in a profile; drop them
            // rather than emit an escape that changes the string's length.
            text.push_back('?');
        }
    }
    text.push_back('"');
    return text;
}

// Title ids are written the way the eShop and Cemu write them: 16 hex digits,
// no prefix. An absent or malformed id becomes 0, which is what the module
// reported before profiles existed for every title but WWHD.
uint64_t title_id_value(std::string_view text) {
    if (text.size() != 16) {
        return 0;
    }
    uint64_t value = 0;
    for (const char item : text) {
        uint64_t digit = 0;
        if (item >= '0' && item <= '9') {
            digit = static_cast<uint64_t>(item - '0');
        } else if (item >= 'a' && item <= 'f') {
            digit = static_cast<uint64_t>(item - 'a' + 10);
        } else if (item >= 'A' && item <= 'F') {
            digit = static_cast<uint64_t>(item - 'A' + 10);
        } else {
            return 0;
        }
        value = (value << 4) | digit;
    }
    return value;
}

struct TranslatedBlock {
    analyzer::BasicBlock block;
    std::string symbol;
    uint32_t first_instruction;
    bool external_safe;
    bool external_check;
    // Instruction words inside this block that the loader relocates, paired
    // with the value the generated code was built from. An external host has
    // to agree on these before the block may run.
    std::vector<std::pair<uint32_t, uint32_t>> relocated_words;
    std::string source;
};

std::string hex32(uint32_t value) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << value;
    return output.str();
}

std::string symbol_name(uint32_t address) {
    std::ostringstream output;
    output << "recomp_" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << address;
    return output.str();
}

std::string shard_name(size_t index) {
    std::ostringstream output;
    output << "block_" << std::setw(4) << std::setfill('0') << index
           << ".cpp";
    return output.str();
}

std::string registration_name(size_t index) {
    std::ostringstream output;
    output << "register_recompiled_blocks_" << std::setw(4)
           << std::setfill('0') << index;
    return output.str();
}

std::string dispatch_name(size_t index) {
    std::ostringstream output;
    output << "dispatch_recompiled_blocks_" << std::setw(4)
           << std::setfill('0') << index;
    return output.str();
}

std::vector<analyzer::BasicBlock>
ordered_blocks(const analyzer::Analysis& analysis) {
    std::vector<analyzer::BasicBlock> blocks;
    for (const auto& [address, function] : analysis.functions) {
        (void)address;
        blocks.insert(blocks.end(), function.basic_blocks.begin(),
                      function.basic_blocks.end());
    }
    std::sort(blocks.begin(), blocks.end(),
              [](const auto& left, const auto& right) {
                  if (left.start != right.start) {
                      return left.start < right.start;
                  }
                  if (left.end != right.end) {
                      return left.end < right.end;
                  }
                  return left.instruction_count < right.instruction_count;
              });
    return blocks;
}

std::vector<uint32_t> extract_instructions(
    const analyzer::RpxImage& image, const nwii::runtime::GuestMemory& memory,
    const analyzer::BasicBlock& block) {
    const uint64_t expected_end = static_cast<uint64_t>(block.start) +
                                  static_cast<uint64_t>(block.instruction_count) *
                                      sizeof(uint32_t);
    if (block.instruction_count == 0) {
        throw std::runtime_error("block has no instructions");
    }
    if (expected_end > UINT32_MAX || block.end != expected_end) {
        throw std::runtime_error("block range does not match instruction count");
    }

    const auto* section = image.section_containing(block.start);
    if (section == nullptr || !section->analyzable_code()) {
        throw std::runtime_error("block is outside executable code");
    }
    const uint64_t offset = static_cast<uint64_t>(block.start) - section->address;
    const uint64_t byte_count =
        static_cast<uint64_t>(block.instruction_count) * sizeof(uint32_t);
    if (offset + byte_count > section->data.size()) {
        throw std::runtime_error("block extends beyond executable section data");
    }

    std::vector<uint32_t> instructions;
    instructions.reserve(block.instruction_count);
    for (uint64_t index = 0; index < byte_count; index += sizeof(uint32_t)) {
        instructions.push_back(
            memory.fetch32(block.start + static_cast<uint32_t>(index)));
    }
    return instructions;
}

std::map<uint32_t, uint32_t> block_overrides(
    const std::map<uint32_t, uint32_t>& overrides,
    const analyzer::BasicBlock& block) {
    std::map<uint32_t, uint32_t> result;
    for (auto entry = overrides.lower_bound(block.start);
         entry != overrides.end() && entry->first < block.end; ++entry) {
        result.insert(*entry);
    }
    return result;
}

std::string registry_source(std::span<const TranslatedBlock> blocks,
                            size_t shard_count) {
    std::ostringstream output;
    output << "#include \"runtime/executor.h\"\n\n"
              "#include <algorithm>\n"
              "#include <array>\n"
              "#include <vector>\n"
              "#include <cstdint>\n\n";
    for (size_t shard = 0; shard < shard_count; ++shard) {
        output << "extern \"C\" void " << dispatch_name(shard)
               << "(nwii::runtime::CPUContext&, "
                  "nwii::runtime::GuestMemory&);\n";
        output << "void " << registration_name(shard)
               << "(nwii::runtime::Executor& executor);\n";
    }
    output << "\nnamespace {\n"
              "struct RecompiledBlock {\n"
              "    uint32_t address;\n"
              "    nwii::runtime::NativeThunk thunk;\n"
              "};\n"
              "constexpr std::array<RecompiledBlock, "
           << blocks.size() << "> kRecompiledBlocks{{\n";
    for (size_t index = 0; index < blocks.size(); ++index) {
        output << "    {" << hex32(blocks[index].block.start) << ", &"
               << dispatch_name(index / kBlocksPerShard) << "},\n";
    }
    const uint32_t range_lo = blocks.empty() ? 0u : blocks.front().block.start;
    const uint32_t range_hi = blocks.empty() ? 0u : blocks.back().block.start + 4u;
    output << "}};\n"
              "constexpr uint32_t kBlockRangeLo = "
           << hex32(range_lo)
           << ";\nconstexpr uint32_t kBlockRangeHi = "
           << hex32(range_hi)
           << ";\n"
              "}\n\n"
              "// One indexed load rather than a binary search over every block.\n"
              "// The search was about nineteen dependent random accesses into a\n"
              "// table far larger than L2, and it runs once per recompiled\n"
              "// block: measured at 146ns against 13ns for this table, it was\n"
              "// the majority of the module's run time.\n"
              "nwii::runtime::NativeThunk find_recompiled_block("
              "uint32_t address) {\n"
              "    static const std::vector<nwii::runtime::NativeThunk> index = "
              "[] {\n"
              "        std::vector<nwii::runtime::NativeThunk> table(\n"
              "            (kBlockRangeHi - kBlockRangeLo) / 4, nullptr);\n"
              "        for (const auto& entry : kRecompiledBlocks) {\n"
              "            table[(entry.address - kBlockRangeLo) / 4] = "
              "entry.thunk;\n"
              "        }\n"
              "        return table;\n"
              "    }();\n"
              "    if (address < kBlockRangeLo || address >= kBlockRangeHi ||\n"
              "        (address & 3u) != 0u) {\n"
              "        return nullptr;\n"
              "    }\n"
              "    return index[(address - kBlockRangeLo) / 4];\n"
              "}\n\n"
              "void register_recompiled_blocks("
              "nwii::runtime::Executor& executor) {\n";
    for (size_t shard = 0; shard < shard_count; ++shard) {
        output << "    " << registration_name(shard) << "(executor);\n";
    }
    output << "}\n";
    return output.str();
}

// The profile, frozen into the generated runner: the built program carries the
// gates and hooks it was generated with and never has to find the .toml again.
std::string profile_source(const analyzer::GameConfig& config) {
    std::ostringstream output;
    output << "namespace {\n"
              "nwiiu::analyzer::Target profile_target() {\n"
              "    nwiiu::analyzer::Target target;\n"
              "    target.product_code = "
           << cpp_literal(config.target.product_code)
           << ";\n"
              "    target.title_id = "
           << cpp_literal(config.target.title_id)
           << ";\n"
              "    target.title_version = " << config.target.title_version
           << "u;\n"
              "    target.sha256 = "
           << cpp_literal(config.target.sha256)
           << ";\n"
              "    target.entry_point = 0x" << std::uppercase << std::hex
           << config.target.entry_point << std::dec << std::nouppercase
           << "u;\n"
              "    target.name = "
           << cpp_literal(config.target.name)
           << ";\n"
              "    return target;\n"
              "}\n\n"
              "std::map<uint32_t, std::string> profile_hooks() {\n"
              "    return {\n";
    for (const auto& [address, name] : config.hle_hooks) {
        output << "        {0x" << std::uppercase << std::hex << address
               << std::dec << std::nouppercase << "u, " << cpp_literal(name)
               << "},\n";
    }
    output << "    };\n"
              "}\n"
              "} // namespace\n\n";
    return output.str();
}

std::string runner_source(const analyzer::GameConfig& config) {
    std::ostringstream output;
    output << R"(#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "nwiiu/recomp/runner_cli.h"

#include "nwiiu/analyzer/rpx.h"
#include "nwiiu/analyzer/target.h"
#include "runtime/machine.h"
#include "runtime/wwhd_renderer.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

void register_recompiled_blocks(nwii::runtime::Executor& executor);

)" << profile_source(config)
           << R"(
int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        const auto options = nwiiu::recomp::parse_runner_options(arguments);
        auto rpx = nwiiu::analyzer::load_rpx(options.input, profile_target());
        auto image = nwii::runtime::make_execution_image(rpx);
        nwii::runtime::Machine machine(image, options.title_root,
                                       options.save_root, options.shared_font,
                                       profile_hooks());
        register_recompiled_blocks(machine.executor());
        machine.executor().set_trace_enabled(options.trace);

        nwii::runtime::ExecutionStop stop;
        bool host_closed = false;
        if (!options.window) {
            stop = machine.run(options.max_instructions,
                               nwii::runtime::kSchedulerQuantum);
        } else {
            nwii::runtime::WwhdRenderer display(image,
                                                machine.cafe_runtime());
            uint64_t executed = 0;
            while (executed < options.max_instructions && !host_closed) {
                const uint64_t budget =
                    std::min<uint64_t>(1'000'000,
                                       options.max_instructions - executed);
                stop = machine.run(budget,
                                   nwii::runtime::kSchedulerQuantum);
                executed += stop.instruction_count;
                host_closed = !display.pump_events();
                if (stop.category !=
                    nwii::runtime::StopCategory::instruction_budget) {
                    break;
                }
            }
            stop.instruction_count = executed;
        }

        std::cout << nwiiu::recomp::format_stop(stop);
        std::cout << "Native dispatches: "
                  << machine.executor().native_dispatch_count() << '\n';
        std::cout << "Native fallbacks: "
                  << machine.executor().native_fallback_count() << '\n';
        if (options.trace) {
            std::cerr << nwiiu::recomp::format_trace(stop);
        }
        return host_closed ||
                       stop.category == nwii::runtime::StopCategory::guest_exit
                   ? 0
                   : 3;
    } catch (const std::exception& error) {
        std::cerr << "INPUT ERROR: " << error.what() << '\n';
        return 2;
    }
}
)";
    return output.str();
}

std::string module_source(const analyzer::GameConfig& config) {
    std::ostringstream output;
    output << R"(#include "nwiiu/static_module.h"

#include "runtime/cpu_context.h"
#include "runtime/executor.h"
#include "runtime/memory.h"
#include "runtime/patch_guard.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>

nwii::runtime::NativeThunk find_recompiled_block(uint32_t address);

namespace {
nwii::runtime::CPUContext to_runtime(const nwiiu_static_cpu_state& source,
                                     uint64_t instruction_limit) {
    nwii::runtime::CPUContext result;
    std::copy_n(source.gpr, 32, result.gpr.begin());
    for (size_t index = 0; index < result.fpr.size(); ++index) {
        result.fpr[index][0] = source.fpr[index][0];
        result.fpr[index][1] = source.fpr[index][1];
    }
    std::copy_n(source.cr, 8, result.cr.begin());
    result.xer = source.xer;
    result.lr = source.lr;
    result.ctr = source.ctr;
    result.pc = source.pc;
    result.fpscr = source.fpscr;
    result.reservation_address = source.reservation_address;
    result.reservation_value = source.reservation_value;
    result.reservation_valid = source.reservation_valid != 0;
    result.instruction_count = source.instruction_count;
    result.native_instruction_endpoint =
        instruction_limit >
                std::numeric_limits<uint64_t>::max() - source.instruction_count
            ? std::numeric_limits<uint64_t>::max()
            : source.instruction_count + instruction_limit;
    return result;
}

void from_runtime(const nwii::runtime::CPUContext& source,
                  nwiiu_static_cpu_state& result) {
    std::copy(source.gpr.begin(), source.gpr.end(), result.gpr);
    for (size_t index = 0; index < source.fpr.size(); ++index) {
        result.fpr[index][0] = source.fpr[index][0];
        result.fpr[index][1] = source.fpr[index][1];
    }
    std::copy(source.cr.begin(), source.cr.end(), result.cr);
    result.xer = source.xer;
    result.lr = source.lr;
    result.ctr = source.ctr;
    result.pc = source.pc;
    result.fpscr = source.fpscr;
    result.reservation_address = source.reservation_address;
    result.reservation_value = source.reservation_value;
    result.reservation_valid = source.reservation_valid;
    result.instruction_count = source.instruction_count;
}

uint32_t run(nwiiu_static_cpu_state* cpu,
             const nwiiu_static_memory* memory,
             uint64_t instruction_limit) {
    if (cpu == nullptr || memory == nullptr || instruction_limit == 0) {
        return NWIIU_STATIC_RESULT_MISS;
    }
    const auto thunk = find_recompiled_block(cpu->pc);
    if (thunk == nullptr) {
        return NWIIU_STATIC_RESULT_MISS;
    }
    auto runtime_cpu = to_runtime(*cpu, instruction_limit);
    try {
        nwii::runtime::GuestMemory runtime_memory(
            nwii::runtime::GuestMemoryCallbacks{
                memory->context,     memory->read8,       memory->read16,
                memory->read32,      memory->read64,      memory->write8,
                memory->write16,     memory->write32,     memory->write64,
                memory->read_bytes, memory->write_bytes,
                static_cast<const uint8_t*>(memory->flat_base),
                memory->flat_size,
            });
        nwii::runtime::set_patched_addresses(memory->patched_addresses,
                                             memory->patched_count);
        // Chain blocks inside the module. Returning after each one made the
        // host marshal the whole CPU state four times per block, twice on its
        // side and twice here, for an average of about thirty guest
        // instructions of work. Staying in until a miss or the budget runs out
        // amortises all four copies.
        const auto entry_count = runtime_cpu.instruction_count;
        auto next = thunk;
        while (next != nullptr) {
            const auto before = runtime_cpu.instruction_count;
            next(runtime_cpu, runtime_memory);
            if (runtime_cpu.instruction_count == before) {
                break;  // the block refused: a guard fired or the budget is out
            }
            if (runtime_cpu.instruction_count >= instruction_limit) {
                break;
            }
            next = find_recompiled_block(runtime_cpu.pc);
        }
        if (runtime_cpu.instruction_count == entry_count) {
            return NWIIU_STATIC_RESULT_MISS;
        }
        from_runtime(runtime_cpu, *cpu);
        return NWIIU_STATIC_RESULT_EXECUTED;
    } catch (const std::exception&) {
        from_runtime(runtime_cpu, *cpu);
        return NWIIU_STATIC_RESULT_FAULT;
    }
}

constexpr nwiiu_static_module kModule{
    NWIIU_STATIC_MODULE_ABI_VERSION,
    sizeof(nwiiu_static_module),
)";
    // The host matches this against the title it loaded, so a profile without
    // a title id yields 0 and the host's own check decides what that means.
    output << "    0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(16) << title_id_value(config.target.title_id)
           << std::dec << std::nouppercase << "ULL,\n";
    output << R"(    0u,
    0u,
    &run,
};
}

extern "C" const nwiiu_static_module* nwiiu_static_module_v1() {
    return &kModule;
}
)";
    return output.str();
}

std::string program_cmake_source(size_t shard_count, const std::string& prefix) {
    const std::string objects = prefix + "-recompiled";
    const std::string native = prefix + "-native";
    const std::string module = prefix + "-module";
    std::ostringstream output;
    output << "add_library(" << objects << " OBJECT\n"
           << "    ${CMAKE_CURRENT_LIST_DIR}/registry.cpp\n";
    for (size_t index = 0; index < shard_count; ++index) {
        output << "    ${CMAKE_CURRENT_LIST_DIR}/" << shard_name(index) << '\n';
    }
    output << ")\n"
           << "set_target_properties(" << objects
           << " PROPERTIES POSITION_INDEPENDENT_CODE ON)\n"
           << "target_link_libraries(" << objects
           << " PRIVATE nwiiu_recomp)\n"
           << "add_executable(" << native << "\n"
           << "    ${CMAKE_CURRENT_LIST_DIR}/main.cpp\n"
           << "    $<TARGET_OBJECTS:" << objects << ">\n"
           << ")\n"
           << "find_package(SDL3 3.2 REQUIRED CONFIG)\n"
           << "target_link_libraries(" << native
           << " PRIVATE nwiiu_recomp SDL3::SDL3)\n"
           << "add_library(" << module << " SHARED\n"
           << "    ${CMAKE_CURRENT_LIST_DIR}/module.cpp\n"
           << "    $<TARGET_OBJECTS:" << objects << ">\n"
           << ")\n"
           << "set_target_properties(" << module << " PROPERTIES OUTPUT_NAME "
           << module << ")\n"
           << "target_link_libraries(" << module
           << " PRIVATE nwiiu_recomp)\n";
    return output.str();
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create generated file: " +
                                 path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("cannot write generated file: " +
                                 path.string());
    }
}

std::filesystem::path available_sibling(const std::filesystem::path& output,
                                        std::string_view suffix) {
    const auto parent = output.parent_path();
    const auto base = output.filename().string() + std::string(suffix);
    for (uint32_t index = 0;; ++index) {
        const auto candidate = parent / (base + std::to_string(index));
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
}

void replace_directory(const std::filesystem::path& temporary,
                       const std::filesystem::path& output) {
    if (!std::filesystem::exists(output)) {
        std::filesystem::rename(temporary, output);
        return;
    }

    const auto backup = available_sibling(output, ".nwiiu-backup-");
    std::filesystem::rename(output, backup);
    try {
        std::filesystem::rename(temporary, output);
    } catch (...) {
        std::filesystem::rename(backup, output);
        throw;
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(backup, cleanup_error);
}
} // namespace

ProjectSummary generate_native_project(
    const analyzer::RpxImage& image, const analyzer::Analysis& analysis,
    const std::filesystem::path& output_directory,
    const analyzer::GameConfig& config) {
    if (output_directory.empty() || output_directory.filename().empty()) {
        throw std::invalid_argument("output directory must name a directory");
    }
    const std::string prefix = config.target_prefix();

    const auto execution_image = nwii::runtime::make_execution_image(image);
    const auto blocks = ordered_blocks(analysis);
    std::vector<TranslatedBlock> translated;
    translated.reserve(blocks.size());
    std::vector<std::pair<uint32_t, std::string>> diagnostics;
    uint64_t instruction_count = 0;
    std::set<uint32_t> external_unsafe_relocations;
    for (const auto& relocation : image.relocations) {
        if (relocation.type != analyzer::kRppcRel24) {
            external_unsafe_relocations.insert(relocation.source_address);
        }
    }

    for (const auto& block : blocks) {
        try {
            auto instructions =
                extract_instructions(image, execution_image.memory, block);
            auto overrides =
                block_overrides(execution_image.branch_overrides, block);
            const auto symbol = symbol_name(block.start);
            const auto unsafe =
                external_unsafe_relocations.lower_bound(block.start);
            const bool external_safe =
                unsafe == external_unsafe_relocations.end() ||
                *unsafe >= block.end;
            // The execution image already applied these relocations, so the
            // generated code is correct as long as the host's loaded image
            // matches. Verify the exact words instead of refusing the block.
            std::vector<std::pair<uint32_t, uint32_t>> relocated_words;
            for (auto it = external_unsafe_relocations.lower_bound(block.start);
                 it != external_unsafe_relocations.end() && *it < block.end;
                 ++it) {
                const uint32_t word_address = *it & ~uint32_t{3};
                if (!relocated_words.empty() &&
                    relocated_words.back().first == word_address) {
                    continue;
                }
                relocated_words.emplace_back(
                    word_address,
                    execution_image.memory.fetch32(word_address));
            }
            translated.push_back(
                {block, symbol, instructions.front(), external_safe,
                 external_safe && !overrides.contains(block.start),
                 std::move(relocated_words),
                 generate_native_block(symbol, block.start, instructions,
                                       overrides)});
            instruction_count += block.instruction_count;
        } catch (const std::exception& error) {
            diagnostics.emplace_back(block.start, error.what());
        }
    }

    if (!diagnostics.empty()) {
        std::ostringstream message;
        message << "unsupported blocks:";
        for (const auto& [address, reason] : diagnostics) {
            message << "\n  " << hex32(address) << ": " << reason;
        }
        throw std::runtime_error(message.str());
    }

    const size_t shard_count =
        (translated.size() + kBlocksPerShard - 1) / kBlocksPerShard;
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    files.reserve(shard_count + 4);
    for (size_t shard = 0; shard < shard_count; ++shard) {
        std::string source;
        const size_t begin = shard * kBlocksPerShard;
        const size_t end = std::min(begin + kBlocksPerShard, translated.size());
        for (size_t index = begin; index < end; ++index) {
            if (!source.empty()) {
                source.push_back('\n');
            }
            source += translated[index].source;
        }
        source.insert(0, "#include \"runtime/executor.h\"\n"
                      "#include \"runtime/patch_guard.h\"\n\n");
        const std::string dispatcher = dispatch_name(shard);
        source += "\nextern \"C\" void " + dispatcher +
                  "(nwii::runtime::CPUContext& cpu, "
                  "nwii::runtime::GuestMemory& memory) {\n"
                  "    while (cpu.instruction_count < "
                  "cpu.native_instruction_endpoint) {\n"
                  "        if (cpu.native_executor != nullptr &&\n"
                  "            cpu.native_executor->is_patched(cpu.pc)) return;\n"
                  "        switch (cpu.pc) {\n";
        for (size_t index = begin; index < end; ++index) {
            const auto& translated_block = translated[index];
            source += "        case " +
                      hex32(translated_block.block.start) + ":\n"
                      "            if (nwii::runtime::range_has_patch(" +
                      hex32(translated_block.block.start) + ", " +
                      std::to_string(translated_block.block.instruction_count) +
                      ")) return;\n";
            if (!translated_block.external_safe) {
                // These blocks hold loader-relocated words. The execution image
                // already applied those relocations, so the baked code is
                // correct as long as the host loaded the same image; verify each
                // relocated word rather than refusing the block outright.
                for (const auto& [word_address, expected] :
                     translated_block.relocated_words) {
                    source +=
                        "            if (cpu.native_executor == nullptr &&\n"
                        "                memory.read32(" +
                        hex32(word_address) + ", " + hex32(word_address) +
                        ") != " + hex32(expected) + ") return;\n";
                }
            }
            if (translated_block.external_check) {
                source +=
                    "            // ponytail: one host read per block; add a "
                    "code-patch generation callback if profiling warrants it.\n"
                    "            if (cpu.native_executor == nullptr &&\n"
                    "                memory.read32(" +
                    hex32(translated_block.block.start) + ", " +
                    hex32(translated_block.block.start) + ") != " +
                    hex32(translated_block.first_instruction) + ") return;\n";
            }
            source +=
                "            if (" +
                std::to_string(translated_block.block.instruction_count) +
                " > cpu.native_instruction_endpoint - "
                "cpu.instruction_count) return;\n"
                "            " +
                translated_block.symbol + "(cpu, memory);\n"
                "            break;\n";
        }
        source += "        default:\n"
                  "            return;\n"
                  "        }\n"
                  "    }\n"
                  "}\n";

        source += "\nvoid " + registration_name(shard) +
                  "(nwii::runtime::Executor& executor) {\n";
        for (size_t index = begin; index < end; ++index) {
            source += "    executor.register_native(" +
                      hex32(translated[index].block.start) + ", " +
                      std::to_string(translated[index].block.instruction_count) +
                      ", &" + dispatcher + ");\n";
        }
        source += "}\n";
        files.emplace_back(shard_name(shard), std::move(source));
    }
    files.emplace_back("registry.cpp", registry_source(translated, shard_count));
    files.emplace_back("main.cpp", runner_source(config));
    files.emplace_back("module.cpp", module_source(config));
    files.emplace_back("program.cmake",
                       program_cmake_source(shard_count, prefix));

    std::filesystem::create_directories(output_directory.parent_path().empty()
                                            ? std::filesystem::path{"."}
                                            : output_directory.parent_path());
    const auto temporary =
        available_sibling(output_directory, ".nwiiu-tmp-");
    std::filesystem::create_directory(temporary);
    try {
        for (const auto& [path, contents] : files) {
            write_file(temporary / path, contents);
        }
        replace_directory(temporary, output_directory);
    } catch (...) {
        std::filesystem::remove_all(temporary);
        throw;
    }

    ProjectSummary summary;
    summary.block_count = translated.size();
    summary.instruction_count = instruction_count;
    summary.shard_count = shard_count;
    for (const auto& [path, contents] : files) {
        (void)contents;
        summary.emitted_files.push_back(path);
    }
    return summary;
}
} // namespace nwiiu::recomp
