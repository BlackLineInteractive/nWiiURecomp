#include "nwiiu/recomp/project_generator.h"
#include "nwiiu/static_module.h"

#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/analyzer/rpx.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using nwiiu::analyzer::Analysis;
using nwiiu::analyzer::BasicBlock;
using nwiiu::analyzer::Function;
using nwiiu::analyzer::Relocation;
using nwiiu::analyzer::RpxFileInfo;
using nwiiu::analyzer::RpxImage;
using nwiiu::analyzer::Section;
using nwiiu::recomp::generate_native_project;

constexpr uint32_t kTextAddress = 0x02000000;

void append_be32(std::vector<uint8_t>& bytes, uint32_t word) {
    bytes.push_back(static_cast<uint8_t>(word >> 24));
    bytes.push_back(static_cast<uint8_t>(word >> 16));
    bytes.push_back(static_cast<uint8_t>(word >> 8));
    bytes.push_back(static_cast<uint8_t>(word));
}

RpxImage make_image(std::initializer_list<uint32_t> words) {
    RpxImage image;
    image.entry_point = kTextAddress;
    image.file_info = RpxFileInfo{};
    image.file_info->core_stack_size = 0x1000;

    Section text;
    text.index = 0;
    text.name = ".text";
    text.type = nwiiu::analyzer::kShtProgbits;
    text.flags = nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfExec;
    text.address = kTextAddress;
    for (const uint32_t word : words) {
        append_be32(text.data, word);
    }
    text.stored_size = static_cast<uint32_t>(text.data.size());
    text.decompressed_size = text.stored_size;
    image.sections.push_back(std::move(text));
    return image;
}

void add_block(Analysis& analysis, uint32_t address,
               uint32_t instruction_count = 1) {
    Function function;
    function.start = address;
    function.end = address + instruction_count * 4;
    function.instruction_count = instruction_count;
    function.basic_blocks.push_back(BasicBlock{
        address, address + instruction_count * 4, instruction_count});
    analysis.functions.emplace(address, std::move(function));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    test::require(static_cast<bool>(input), "generated file opens");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void require_contains(std::string_view text, std::string_view expected,
                      std::string_view message) {
    test::require(text.find(expected) != std::string_view::npos, message);
}

void test_deterministic_project() {
    auto image = make_image(
        {0x60000000, 0x4E800020, 0x48000000, 0x3D800290, 0x818CF408});
    image.relocations.push_back(Relocation{
        0, kTextAddress + 8, nwiiu::analyzer::kRppcRel24, 0, {}, 0,
        0x03000000});
    image.relocations.push_back(Relocation{
        0, kTextAddress + 14, nwiiu::analyzer::kRppcAddr16Ha, 0, {}, 0,
        0xC000A908});
    image.relocations.push_back(Relocation{
        0, kTextAddress + 18, nwiiu::analyzer::kRppcAddr16Lo, 0, {}, 0,
        0xC000A908});

    Analysis analysis;
    add_block(analysis, kTextAddress);
    add_block(analysis, kTextAddress + 4);
    add_block(analysis, kTextAddress + 12, 2);

    const auto root = std::filesystem::temp_directory_path() /
                      "nwiiu_project_generator_test";
    const auto first = root / "first";
    const auto second = root / "second";
    std::filesystem::remove_all(root);

    const auto first_summary = generate_native_project(image, analysis, first);
    const auto second_summary =
        generate_native_project(image, analysis, second);

    const std::vector<std::filesystem::path> expected_files{
        "block_0000.cpp", "registry.cpp", "main.cpp", "module.cpp",
        "program.cmake"};
    test::require(first_summary.block_count == 3, "summary block count");
    test::require(first_summary.instruction_count == 4,
                  "summary instruction count");
    test::require(first_summary.shard_count == 1, "summary shard count");
    test::require(first_summary.emitted_files == expected_files,
                  "stable emitted file list");
    test::require(second_summary.emitted_files == expected_files,
                  "second emitted file list");

    for (const auto& file : expected_files) {
        test::require(read_file(first / file) == read_file(second / file),
                      "generated files are byte-identical");
    }

    const auto shard = read_file(first / "block_0000.cpp");
    require_contains(shard, "recomp_02000000", "stable first symbol");
    require_contains(shard, "0x02000000: 0x60000000",
                     "big-endian fallthrough extraction");
    require_contains(shard, "recomp_02000004", "stable second symbol");
    require_contains(shard, "0x02000004: 0x4E800020",
                     "big-endian blr extraction");
    require_contains(shard, "0x0200000C: 0x3D80C001",
                     "ADDR16_HA relocation reaches native instruction");
    require_contains(shard, "0x02000010: 0x818CA908",
                     "ADDR16_LO relocation reaches native instruction");
    require_contains(shard, "switch (cpu.pc)",
                     "shard dispatches local blocks without executor lookup");
    require_contains(shard, "cpu.native_executor->is_patched(cpu.pc)",
                     "shard yields to runtime patches between blocks");
    require_contains(shard,
                     "memory.read32(0x0200000C, 0x0200000C) != 0x3D80C001",
                     "external host verifies a relocated word before running "
                     "the block that contains it");
    require_contains(shard,
                     "memory.read32(0x02000010, 0x02000010) != 0x818CA908",
                     "every relocated word is verified, not just the first "
                     "instruction");
    require_contains(shard, "nwii::runtime::range_has_patch(0x02000000, 1)",
                     "every block refuses to run if the host patched any word "
                     "inside it, which a first-instruction check cannot see");
    require_contains(
        shard,
        "memory.read32(0x02000000, 0x02000000) != 0x60000000",
        "external host defers blocks modified after recompilation");

    require_contains(
        shard,
        "executor.register_native(0x02000000, 1, "
        "&dispatch_recompiled_blocks_0000);",
        "first block registers the shard dispatcher");
    require_contains(
        shard,
        "executor.register_native(0x02000004, 1, "
        "&dispatch_recompiled_blocks_0000);",
        "second block registers the shard dispatcher");

    const auto registry = read_file(first / "registry.cpp");
    require_contains(registry, "register_recompiled_blocks_0000(executor);",
                     "registry dispatches bounded shard registration");
    require_contains(registry, "void register_recompiled_blocks(",
                     "registry interface");
    require_contains(registry, "find_recompiled_block",
                     "registry exposes static block lookup");

    const auto runner = read_file(first / "main.cpp");
    require_contains(runner, "parse_runner_options", "runner parses CLI");
    require_contains(runner, "load_rpx", "runner loads authenticated RPX");
    require_contains(runner, "nwii::runtime::Machine machine",
                     "runner constructs machine");
    require_contains(runner,
                     "register_recompiled_blocks(machine.executor());",
                     "runner installs native blocks");
    require_contains(runner, "nwii::runtime::kSchedulerQuantum",
                     "runner preempts guest threads");
    require_contains(runner, "format_stop", "runner formats stop");
    require_contains(runner, "Native dispatches: ",
                     "runner reports native dispatch count");
    require_contains(runner, "Native fallbacks: ",
                     "runner reports native fallback count");
    require_contains(runner, "nwii::runtime::WwhdRenderer display",
                     "window mode starts the GX2 event renderer");

    test::require(sizeof(((nwiiu_static_module*)nullptr)->title_version) ==
                          sizeof(uint32_t) &&
                      offsetof(nwiiu_static_module, title_version) == 16 &&
                      offsetof(nwiiu_static_module, reserved) == 20 &&
                      offsetof(nwiiu_static_module, run) == 24,
                  "module metadata has stable C ABI layout");
    const auto module = read_file(first / "module.cpp");
    require_contains(module, "0x0005000010143600ULL,\n    0u,\n    0u,\n",
                     "module identifies WWHD EU title version 0");
    const auto runtime_cpu =
        module.find("    auto runtime_cpu = to_runtime(*cpu, instruction_limit);");
    const auto try_block = module.find("    try {");
    test::require(runtime_cpu != std::string::npos &&
                      try_block != std::string::npos &&
                      runtime_cpu < try_block,
                  "module keeps runtime CPU state available to fault handler");
    require_contains(
        module,
        "} catch (const std::exception&) {\n"
        "        from_runtime(runtime_cpu, *cpu);\n"
        "        return NWIIU_STATIC_RESULT_FAULT;\n",
        "module fault exports partial CPU progress");
    require_contains(module, "std::copy(source.gpr.begin(), source.gpr.end(), "
                             "result.gpr);",
                     "module exports registers");
    require_contains(
        module,
        "result.pc = source.pc;\n"
        "    result.fpscr = source.fpscr;\n"
        "    result.reservation_address = source.reservation_address;\n"
        "    result.reservation_value = source.reservation_value;\n"
        "    result.reservation_valid = source.reservation_valid;\n"
        "    result.instruction_count = source.instruction_count;",
        "module exports PC, reservation, and instruction count");

    const auto cmake = read_file(first / "program.cmake");
    require_contains(cmake, "add_executable(wwhd-native",
                     "cmake defines the generated build-hook target");
    require_contains(cmake, "${CMAKE_CURRENT_LIST_DIR}/block_0000.cpp",
                     "cmake locates shard beside fragment");
    require_contains(cmake, "${CMAKE_CURRENT_LIST_DIR}/registry.cpp",
                     "cmake locates registry beside fragment");
    require_contains(cmake, "${CMAKE_CURRENT_LIST_DIR}/main.cpp",
                     "cmake locates runner beside fragment");
    require_contains(cmake, "add_library(wwhd-module SHARED",
                     "cmake defines the loadable static module");
    require_contains(cmake, "OUTPUT_NAME wwhd-module",
                     "cmake gives the loadable module its package filename");
    require_contains(cmake, "${CMAKE_CURRENT_LIST_DIR}/module.cpp",
                     "cmake locates module ABI wrapper");
    require_contains(cmake, "SDL3::SDL3",
                     "generated native runner links SDL3");

    std::filesystem::remove_all(root);
}

void test_branch_override_is_sliced_into_own_block() {
    auto image = make_image({0x48000000, 0x60000000});
    image.relocations.push_back(Relocation{
        0, kTextAddress, nwiiu::analyzer::kRppcRel24, 0, {}, 0,
        0x03000000});
    Analysis analysis;
    add_block(analysis, kTextAddress);
    add_block(analysis, kTextAddress + 4);

    const auto output = std::filesystem::temp_directory_path() /
                        "nwiiu_project_generator_override_test";
    std::filesystem::remove_all(output);
    generate_native_project(image, analysis, output);
    const auto shard = read_file(output / "block_0000.cpp");
    require_contains(
        shard,
        "relocated_branch_target(memory, 0x02000000, 0x48000000, "
        "0x03000000)",
        "branch relocation override reaches its block");
    require_contains(shard, "0x02000004: 0x60000000",
                     "unrelated block translated without override");
    std::filesystem::remove_all(output);
}

void test_strict_diagnostics_preserve_existing_output() {
    const auto output = std::filesystem::temp_directory_path() /
                        "nwiiu_project_generator_failure_test";
    std::filesystem::remove_all(output);
    std::filesystem::create_directories(output);
    {
        std::ofstream sentinel(output / "sentinel.txt", std::ios::binary);
        sentinel << "keep me";
    }

    const auto image = make_image({0x00000000, 0x04000000});
    Analysis analysis;
    add_block(analysis, kTextAddress);
    add_block(analysis, kTextAddress + 4);

    std::string diagnostic;
    try {
        generate_native_project(image, analysis, output);
    } catch (const std::runtime_error& error) {
        diagnostic = error.what();
    }
    test::require(!diagnostic.empty(), "unsupported blocks reject project");
    require_contains(diagnostic, "0x02000000",
                     "diagnostic names first unsupported block");
    require_contains(diagnostic, "0x02000004",
                     "diagnostic aggregates second unsupported block");
    require_contains(diagnostic, "unsupported PowerPC instruction",
                     "diagnostic includes translation reason");
    require_contains(
        diagnostic,
        "instruction 0x02000000 (0x00000000): unsupported PowerPC instruction",
        "diagnostic identifies first guest instruction and raw word");
    require_contains(
        diagnostic,
        "instruction 0x02000004 (0x04000000): unsupported PowerPC instruction",
        "diagnostic identifies second guest instruction and raw word");
    test::require(read_file(output / "sentinel.txt") == "keep me",
                  "failed generation preserves existing directory");
    test::require(std::distance(std::filesystem::directory_iterator(output),
                                std::filesystem::directory_iterator{}) == 1,
                  "failed generation does not partially replace output");
    std::filesystem::remove_all(output);
}
} // namespace

int main() {
    test_deterministic_project();
    test_branch_override_is_sliced_into_own_block();
    test_strict_diagnostics_preserve_existing_output();
    return 0;
}
