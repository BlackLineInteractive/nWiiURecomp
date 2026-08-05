#pragma once
#include "nwiiu/analyzer/target.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nwiiu::analyzer {
inline constexpr uint32_t kShfWrite = 0x1;
inline constexpr uint32_t kShfAlloc = 0x2;
inline constexpr uint32_t kShfExec = 0x4;
inline constexpr uint32_t kShfRplZlib = 0x08000000;
inline constexpr uint32_t kShtProgbits = 1;
inline constexpr uint32_t kShtSymtab = 2;
inline constexpr uint32_t kShtStrtab = 3;
inline constexpr uint32_t kShtRela = 4;
inline constexpr uint32_t kShtNobits = 8;
inline constexpr uint32_t kShtRplExports = 0x80000001;
inline constexpr uint32_t kShtRplImports = 0x80000002;
inline constexpr uint32_t kShtRplCrcs = 0x80000003;
inline constexpr uint32_t kShtRplFileInfo = 0x80000004;
inline constexpr uint32_t kRppcAddr32 = 1;
inline constexpr uint32_t kRppcAddr16Lo = 4;
inline constexpr uint32_t kRppcAddr16Hi = 5;
inline constexpr uint32_t kRppcAddr16Ha = 6;
inline constexpr uint32_t kRppcRel24 = 10;

struct Section {
    uint32_t index{};
    std::string name;
    uint32_t type{};
    uint32_t flags{};
    uint32_t address{};
    uint32_t stored_size{};
    uint32_t decompressed_size{};
    uint32_t link{};
    uint32_t info{};
    uint32_t alignment{};
    uint32_t entry_size{};
    std::vector<uint8_t> data;
    [[nodiscard]] bool executable() const { return (flags & kShfExec) != 0; }
    [[nodiscard]] bool analyzable_code() const {
        return type == kShtProgbits && executable();
    }
};

struct Symbol {
    uint32_t index{};
    std::string name;
    uint32_t value{};
    uint32_t size{};
    uint8_t info{};
    uint8_t other{};
    uint16_t section_index{};
};

struct Relocation {
    uint32_t source_section_index{};
    uint32_t source_address{};
    uint32_t type{};
    uint32_t symbol_index{};
    std::string symbol_name;
    int32_t addend{};
    std::optional<uint32_t> target_address;
};

struct ImportModule {
    std::string name;
    std::vector<uint32_t> function_symbols;
    std::vector<uint32_t> data_symbols;
};

struct ExportSymbol {
    uint32_t symbol_index{};
};

struct RpxFileInfo {
    uint32_t magic_version{};
    uint32_t text_size{};
    uint32_t text_alignment{};
    uint32_t data_size{};
    uint32_t data_alignment{};
    uint32_t loader_info_size{};
    uint32_t loader_info_alignment{};
    uint32_t temp_size{};
    uint32_t trampoline_adjustment{};
    uint32_t sda_base{};
    uint32_t sda2_base{};
    uint32_t core_stack_size{};
    uint32_t source_filename_offset{};
    uint32_t flags{};
    uint32_t system_heap_size{};
    uint32_t tags_offset{};
};

struct RpxImage {
    Target target{};
    uint64_t input_size{};
    std::string sha256;
    uint32_t entry_point{};
    std::vector<Section> sections;
    std::optional<RpxFileInfo> file_info;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
    std::vector<ImportModule> imports;
    std::vector<ExportSymbol> exports;
    [[nodiscard]] const Section* section_containing(uint32_t address) const;
};

RpxImage load_rpx(const std::filesystem::path& path, Target target);
}
