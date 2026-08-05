#include "runtime/execution_image.h"
#include "test_support.h"

#include <array>
#include <cstdint>

namespace {
using nwiiu::analyzer::ImportModule;
using nwiiu::analyzer::Relocation;
using nwiiu::analyzer::RpxFileInfo;
using nwiiu::analyzer::RpxImage;
using nwiiu::analyzer::Section;
using nwiiu::analyzer::Symbol;

constexpr uint32_t kCode = 0x02000000;
constexpr uint32_t kData = 0x10000000;
constexpr uint32_t kBss = 0x10000100;
constexpr uint32_t kTarget = 0x12348000;
constexpr uint32_t kImport = 0xC0009BC8;
constexpr uint32_t kDataImport = 0xC000A900;
constexpr uint32_t kStackBase = 0x4FEFF000;
constexpr uint32_t kStackTop = 0x50000000;

RpxImage make_rpx() {
    RpxImage rpx;
    rpx.entry_point = kCode;

    rpx.sections.push_back(
        {0, ".text", nwiiu::analyzer::kShtProgbits,
         nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfExec, kCode, 12,
         12, 0, 0, 4, 0,
         {0x48, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00,
          0x60, 0x00, 0x00, 0x00}});
    rpx.sections.push_back(
        {1, ".data", nwiiu::analyzer::kShtProgbits,
         nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfWrite, kData, 12,
         12, 0, 0, 4, 0, std::vector<uint8_t>(12)});
    rpx.sections.push_back(
        {2, ".bss", nwiiu::analyzer::kShtNobits,
         nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfWrite, kBss, 0,
         32, 0, 0, 4, 0, {0xFF}});

    // These overlap mapped sections and therefore prove ineligible sections are
    // ignored rather than handed to GuestMemory.
    rpx.sections.push_back({3, ".debug", nwiiu::analyzer::kShtProgbits, 0,
                            kData, 4, 4, 0, 0, 1, 0,
                            {0xFF, 0xFF, 0xFF, 0xFF}});
    rpx.sections.push_back(
        {4, ".strings", nwiiu::analyzer::kShtStrtab,
         nwiiu::analyzer::kShfAlloc, kData, 4, 4, 0, 0, 1, 0,
         {0xFF, 0xFF, 0xFF, 0xFF}});
    rpx.sections.push_back({5, ".empty", nwiiu::analyzer::kShtProgbits,
                            nwiiu::analyzer::kShfAlloc, kData, 1, 0, 0, 0,
                            1, 0, {0xFF}});
    rpx.sections.push_back(
        {6, ".dimport_coreinit", nwiiu::analyzer::kShtRplImports,
         nwiiu::analyzer::kShfAlloc,
         kDataImport, 16, 16, 0, 0, 4, 0,
         {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x00}});

    RpxFileInfo info;
    info.sda_base = 0x104E22E0;
    info.sda2_base = 0x10080000;
    info.core_stack_size = 0x00101000;
    rpx.file_info = info;

    rpx.symbols.push_back(Symbol{});
    rpx.symbols.push_back(
        {1, "OSGetCurrentThread", kImport, 4, 0x12, 0, 0});
    rpx.symbols.push_back({2, "NotAFunction", kImport + 4, 4, 0x11, 0, 0});
    rpx.symbols.push_back(
        {3, "MEMAllocFromDefaultHeap", kDataImport + 8, 0, 0x11, 0, 6});
    rpx.imports.push_back(ImportModule{"coreinit", {0, 1, 2}, {3}});

    rpx.relocations.push_back(
        {1, kData, nwiiu::analyzer::kRppcAddr32, 1, "target", 0, kTarget});
    rpx.relocations.push_back({1, kData + 4,
                               nwiiu::analyzer::kRppcAddr16Lo, 1, "target", 0,
                               kTarget});
    rpx.relocations.push_back({1, kData + 6,
                               nwiiu::analyzer::kRppcAddr16Hi, 1, "target", 0,
                               kTarget});
    rpx.relocations.push_back({1, kData + 8,
                               nwiiu::analyzer::kRppcAddr16Ha, 1, "target", 0,
                               kTarget});
    rpx.relocations.push_back({0, kCode + 4, nwiiu::analyzer::kRppcRel24, 1,
                               "OSGetCurrentThread", 0, kImport});
    rpx.relocations.push_back({0, kCode, nwiiu::analyzer::kRppcRel24, 0,
                               "backward", -4, kCode - 4});
    return rpx;
}

void require_invalid(RpxImage rpx, const char* expected, const char* message) {
    test::require_throws(
        [&] { (void)nwii::runtime::make_execution_image(rpx); }, expected,
        message);
}
} // namespace

int main() {
    auto image = nwii::runtime::make_execution_image(make_rpx());
    test::require(image.memory.read32(kData, 0) == kTarget,
                  "ADDR32 materialized");
    test::require(image.memory.read16(kData + 4, 0) == 0x8000,
                  "ADDR16_LO materialized");
    test::require(image.memory.read16(kData + 6, 0) == 0x1234,
                  "ADDR16_HI materialized");
    test::require(image.memory.read16(kData + 8, 0) == 0x1235,
                  "ADDR16_HA materialized");
    test::require(image.branch_overrides.at(kCode + 4) == kImport,
                  "external REL24 semantic target");
    test::require(image.branch_overrides.at(kCode) == kCode - 4,
                  "signed backward REL24 semantic target");
    test::require(image.memory.fetch32(kCode) == 0x48000000 &&
                      image.memory.fetch32(kCode + 4) == 0x48000000,
                  "REL24 leaves encoded instructions unchanged");
    test::require(image.imports.size() == 2 &&
                      image.imports.at(kImport).module == "coreinit" &&
                      image.imports.at(kImport).symbol == "OSGetCurrentThread",
                  "function import lookup excludes non-functions");
    test::require(
        image.imports.at(kDataImport + 8).module == "coreinit" &&
            image.imports.at(kDataImport + 8).symbol ==
                "MEMAllocFromDefaultHeap",
        "data-import pointer target is dispatchable");
    test::require(image.memory.read32(kDataImport + 8, 0) ==
                      kDataImport + 8,
                  "data-import slot resolves to its HLE target");
    image.memory.write32(kDataImport + 8, 0xCAFEBABE, 0);
    test::require(image.memory.read32(kDataImport + 8, 0) == 0xCAFEBABE,
                  "writable data-import slot preserves permissions");

    test::require(image.memory.read64(kBss, 0) == 0 &&
                      image.memory.read64(kBss + 24, 0) == 0,
                  "NOBITS is zero-filled for its logical size");
    test::require(image.entry_point == kCode && image.stack_base == kStackBase &&
                      image.stack_top == kStackTop,
                  "entry point and fixed stack range");
    image.memory.write32(kStackTop - 4, 0xCAFEBABE, 0);
    test::require(image.memory.read32(kStackTop - 4, 0) == 0xCAFEBABE,
                  "stack is writable");
    test::require_throws([&] { image.memory.write32(kCode + 8, 0, 0); },
                         "permission", "text is guest read-only");
    test::require_throws([&] { (void)image.memory.fetch32(kData); },
                         "permission", "data is not executable");

    nwii::runtime::CPUContext cpu;
    cpu.gpr.fill(0xFFFFFFFF);
    cpu.fpr.fill({1, 1});
    cpu.pc = 0xFFFFFFFF;
    cpu.reservation_valid = true;
    cpu.running = false;
    nwii::runtime::initialize_cpu(image, cpu);
    test::require(cpu.pc == kCode && cpu.gpr[1] == kStackTop - 0x20 &&
                      cpu.gpr[2] == 0x10080000 &&
                      cpu.gpr[13] == 0x104E22E0 && cpu.gpr[3] == 0 &&
                      cpu.gpr[4] == 0,
                  "Cafe ABI registers initialized");
    test::require(cpu.gpr[0] == 0 &&
                      cpu.fpr ==
                          std::array<std::array<uint64_t, 2>, 32>{} &&
                      cpu.reservation_address == 0xFFFFFFFF &&
                      !cpu.reservation_valid && cpu.running,
                  "CPU context reset before ABI initialization");

    auto readonly_patch = make_rpx();
    readonly_patch.relocations.front().source_section_index = 0;
    readonly_patch.relocations.front().source_address = kCode + 8;
    const auto patched =
        nwii::runtime::make_execution_image(readonly_patch);
    test::require(patched.memory.fetch32(kCode + 8) == kTarget,
                  "loader patch bypasses guest write permission");

    auto missing_info = make_rpx();
    missing_info.file_info.reset();
    require_invalid(std::move(missing_info), "file-info",
                    "absent file-info rejected");

    auto zero_stack = make_rpx();
    zero_stack.file_info->core_stack_size = 0;
    require_invalid(std::move(zero_stack), "stack",
                    "zero-sized stack rejected");
    auto unaligned_stack = make_rpx();
    unaligned_stack.file_info->core_stack_size = 0x1001;
    require_invalid(std::move(unaligned_stack), "stack",
                    "unaligned stack rejected");
    auto oversized_stack = make_rpx();
    oversized_stack.file_info->core_stack_size = kStackTop + 0x1000;
    require_invalid(std::move(oversized_stack), "stack",
                    "stack subtraction underflow rejected");

    auto stack_overlap = make_rpx();
    stack_overlap.sections.push_back(
        {6, ".stack_collision", nwiiu::analyzer::kShtNobits,
         nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfWrite, kStackBase,
         0, 0x1000, 0, 0, 0x1000, 0, {}});
    require_invalid(std::move(stack_overlap), "overlap",
                    "stack overlap rejected");

    auto section_overlap = make_rpx();
    section_overlap.sections.push_back(
        {6, ".collision", nwiiu::analyzer::kShtNobits,
         nwiiu::analyzer::kShfAlloc, kData + 8, 0, 8, 0, 0, 1, 0, {}});
    require_invalid(std::move(section_overlap), "overlap",
                    "allocated section overlap rejected");

    auto oversized_initial = make_rpx();
    oversized_initial.sections[1].decompressed_size = 8;
    require_invalid(std::move(oversized_initial), "initial",
                    "PROGBITS bytes beyond logical size rejected");

    auto undersized_initial = make_rpx();
    undersized_initial.sections[1].data.pop_back();
    require_invalid(std::move(undersized_initial), "PROGBITS",
                    "PROGBITS bytes short of logical size rejected");

    auto wrapping_section = make_rpx();
    wrapping_section.sections.push_back(
        {6, ".wrapping", nwiiu::analyzer::kShtNobits,
         nwiiu::analyzer::kShfAlloc, 0xFFFFFFF0, 0, 0x20, 0, 0, 1, 0, {}});
    require_invalid(std::move(wrapping_section), "wrap",
                    "wrapping section mapping rejected");

    auto duplicate_source = make_rpx();
    duplicate_source.relocations.push_back(
        duplicate_source.relocations.front());
    require_invalid(std::move(duplicate_source), "duplicate relocation",
                    "duplicate relocation source rejected");

    auto unaligned_rel24 = make_rpx();
    unaligned_rel24.relocations[4].source_address = kCode + 2;
    require_invalid(std::move(unaligned_rel24), "aligned",
                    "unaligned REL24 rejected");

    auto unaligned_rel24_target = make_rpx();
    unaligned_rel24_target.relocations[4].target_address = kImport + 2;
    require_invalid(std::move(unaligned_rel24_target), "target",
                    "unaligned REL24 target rejected");

    auto non_branch_rel24 = make_rpx();
    non_branch_rel24.sections[0].data[4] = 0x60;
    require_invalid(std::move(non_branch_rel24), "direct branch",
                    "REL24 on non-branch source rejected");

    auto absent_target = make_rpx();
    absent_target.relocations.front().target_address.reset();
    require_invalid(std::move(absent_target), "target",
                    "relocation without resolved target rejected");

    auto unmapped_relocation = make_rpx();
    unmapped_relocation.relocations.front().source_address = 0x20000000;
    require_invalid(std::move(unmapped_relocation), "source section",
                    "relocation into unmapped memory rejected");

    auto mismatched_source_section = make_rpx();
    mismatched_source_section.relocations.front().source_address = kCode + 8;
    require_invalid(std::move(mismatched_source_section), "source section",
                    "relocation outside its declared section rejected");

    auto stack_source = make_rpx();
    stack_source.relocations.front().source_address = kStackBase;
    require_invalid(std::move(stack_source), "source section",
                    "relocation into stack rejected");

    auto out_of_range_source_section = make_rpx();
    out_of_range_source_section.relocations.front().source_section_index = 99;
    require_invalid(std::move(out_of_range_source_section), "source section",
                    "out-of-range relocation source section rejected");

    auto ineligible_source_section = make_rpx();
    ineligible_source_section.relocations.front().source_section_index = 3;
    require_invalid(std::move(ineligible_source_section), "source section",
                    "ineligible relocation source section rejected");

    auto truncated_source_field = make_rpx();
    truncated_source_field.relocations.front().source_address = kData + 10;
    require_invalid(std::move(truncated_source_field), "source section",
                    "relocation field crossing its section end rejected");

    auto duplicate_import = make_rpx();
    duplicate_import.symbols.push_back(
        {4, "Duplicate", kImport, 4, 0x12, 0, 0});
    duplicate_import.imports.push_back(ImportModule{"other", {4}, {}});
    require_invalid(std::move(duplicate_import), "duplicate import",
                    "duplicate import address rejected");
}
