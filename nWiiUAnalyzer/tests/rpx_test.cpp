#include "nwiiu/analyzer/hash.h"
#include "nwiiu/analyzer/rpx.h"
#include "test_support.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <exception>
#include <string_view>
#include <vector>

namespace {
uint32_t be32(const std::vector<std::byte>& bytes, size_t offset) {
    return std::to_integer<uint32_t>(bytes.at(offset)) << 24 |
           std::to_integer<uint32_t>(bytes.at(offset + 1)) << 16 |
           std::to_integer<uint32_t>(bytes.at(offset + 2)) << 8 |
           std::to_integer<uint32_t>(bytes.at(offset + 3));
}

void expect_failure(test::TempDir& temp, std::vector<std::byte> bytes,
                    std::string_view expected, std::string_view filename,
                    uint32_t entry_point = 0x02000000) {
    const auto path = temp.path() / filename;
    test::write_bytes(path, bytes);
    const std::string hash = nwiiu::analyzer::sha256_file(path);
    const nwiiu::analyzer::Target target{"fixture", "fixture", 0, hash,
                                           entry_point};
    try {
        (void)nwiiu::analyzer::load_rpx(path, target);
    } catch (const std::exception& error) {
        test::require(error.what() == expected, expected);
        return;
    }
    test::require(false, expected);
}
}

int main() {
    test::TempDir temp;
    const auto path = temp.path() / "fixture.rpx";
    constexpr size_t file_info_header = 0x40 + 8 * 40;
    auto bytes = test::build_test_rpx();
    bytes.push_back(std::byte{});
    test::put_be32(bytes, file_info_header + 20, 65);
    test::write_bytes(path, bytes);
    const std::string hash = nwiiu::analyzer::sha256_file(path);
    const nwiiu::analyzer::Target target{"fixture", "fixture", 0, hash,
                                           0x02000000};

    const auto image = nwiiu::analyzer::load_rpx(path, target);
    test::require(image.sections.size() == 10, "section count");
    test::require(image.input_size == bytes.size(), "input size");
    test::require(image.sha256 == hash, "input hash");
    test::require(image.entry_point == 0x02000000, "entry point");
    test::require(image.file_info.has_value(), "file-info present");
    test::require(image.file_info->magic_version == 0xCAFE0402,
                  "file-info magic/version");
    test::require(image.file_info->sda_base == 0x104E22E0,
                  "file-info SDA base");
    test::require(image.file_info->sda2_base == 0x10080000,
                  "file-info SDA2 base");
    test::require(image.file_info->core_stack_size == 0x00101000,
                  "file-info core stack size");
    test::require(image.file_info->flags == 2, "file-info flags");
    test::require(image.file_info->system_heap_size == 0x00080000,
                  "file-info system heap size");

    const auto& text = image.sections.at(1);
    test::require(text.name == ".text", "text name");
    test::require(text.data.size() == 24, "decompressed text size");
    test::require((text.flags & nwiiu::analyzer::kShfRplZlib) != 0,
                  "compressed flag retained");
    std::vector<std::byte> expected(text.data.size());
    for (size_t i = 0; i < test::kTestTextWords.size(); ++i) {
        test::put_be32(expected, i * sizeof(uint32_t), test::kTestTextWords[i]);
    }
    test::require(std::equal(
                      text.data.begin(), text.data.end(), expected.begin(),
                      [](uint8_t actual, std::byte wanted) {
                          return actual == std::to_integer<uint8_t>(wanted);
                      }),
                  "decompressed text bytes");
    test::require(image.section_containing(0x02000000) == &text,
                  "section containing start");
    test::require(image.section_containing(0x02000018) == nullptr,
                  "section end excluded");
    const auto& bss = image.sections.at(9);
    test::require(bss.name == ".bss", "NOBITS name");
    test::require(bss.stored_size == 0, "NOBITS has no file payload");
    test::require(bss.data.empty(), "NOBITS is not allocated");
    test::require(image.section_containing(0x03000000) == &bss,
                  "NOBITS section containing start");
    test::require(image.section_containing(0x0300001F) == &bss,
                  "NOBITS section containing last byte");
    test::require(image.section_containing(0x03000020) == nullptr,
                  "NOBITS section end excluded");

    test::require(image.symbols.size() == 3, "symbol count");
    test::require(image.imports.size() == 1, "import module count");
    test::require(image.imports[0].name == "coreinit", "import module name");
    test::require(image.imports[0].function_symbols.size() == 1,
                  "import function count");
    test::require(image.symbols[image.imports[0].function_symbols[0]].name ==
                      "OSReport",
                  "import symbol");
    test::require(image.relocations.size() == 2, "relocation count");
    test::require(image.relocations[0].target_address == 0x02000010,
                  "local REL24 target");
    test::require(image.relocations[1].target_address == 0xC0001000,
                  "import REL24 target");

    auto short_file_info = test::build_test_rpx();
    test::put_be32(short_file_info, file_info_header + 20, 60);
    expect_failure(temp, std::move(short_file_info), "RPX validation error",
                   "short-file-info.rpx");

    auto duplicate_file_info = test::build_test_rpx();
    constexpr size_t crc_header = 0x40 + 7 * 40;
    test::put_be32(duplicate_file_info, crc_header + 4,
                   nwiiu::analyzer::kShtRplFileInfo);
    test::put_be32(duplicate_file_info, crc_header + 20, 64);
    expect_failure(temp, std::move(duplicate_file_info),
                   "RPX validation error", "duplicate-file-info.rpx");

    auto unaligned_entry = test::build_test_rpx();
    test::put_be32(unaligned_entry, 24, 0x02000002);
    expect_failure(temp, std::move(unaligned_entry), "RPX validation error",
                   "unaligned-entry.rpx", 0x02000002);

    auto section_end_entry = test::build_test_rpx();
    test::put_be32(section_end_entry, 24, 0x02000018);
    expect_failure(temp, std::move(section_end_entry), "RPX validation error",
                   "section-end-entry.rpx", 0x02000018);

    auto truncated_entry = test::build_test_rpx();
    const size_t text_header = 0x40 + 40;
    test::put_be32(truncated_entry, 24, 0x02000018);
    test::put_be32(truncated_entry, text_header + 8, 0x6);
    test::put_be32(truncated_entry, text_header + 20, 26);
    const uint32_t truncated_crc_offset =
        be32(truncated_entry, 0x40 + 7 * 40 + 16);
    test::put_be32(truncated_entry, truncated_crc_offset + 4, 0);
    expect_failure(temp, std::move(truncated_entry), "RPX validation error",
                   "truncated-entry.rpx", 0x02000018);

    auto malformed_bounds = test::build_test_rpx();
    test::put_be32(malformed_bounds, 0x40 + 40 + 16,
                   malformed_bounds.size());
    expect_failure(temp, std::move(malformed_bounds), "RPX bounds error",
                   "bounds.rpx");

    auto malformed_length = test::build_test_rpx();
    const uint32_t text_offset = be32(malformed_length, 0x40 + 40 + 16);
    test::put_be32(malformed_length, text_offset, 25);
    expect_failure(temp, std::move(malformed_length),
                   "RPX decompression error", "length.rpx");

    auto malformed_crc = test::build_test_rpx();
    const uint32_t crc_offset = be32(malformed_crc, 0x40 + 7 * 40 + 16);
    malformed_crc.at(crc_offset + 4) ^= std::byte{1};
    expect_failure(temp, std::move(malformed_crc), "RPX CRC error", "crc.rpx");
    auto unsupported_relocation = test::build_test_rpx();
    const uint32_t relocation_offset =
        be32(unsupported_relocation, 0x40 + 2 * 40 + 16);
    unsupported_relocation.at(relocation_offset + 7) = std::byte{255};
    expect_failure(temp, std::move(unsupported_relocation),
                   "unsupported PowerPC relocation: 255",
                   "unsupported-relocation.rpx");

    auto out_of_section_relocation = test::build_test_rpx();
    const uint32_t out_of_section_offset =
        be32(out_of_section_relocation, 0x40 + 2 * 40 + 16);
    test::put_be32(out_of_section_relocation, out_of_section_offset,
                   0x01FFFFFC);
    expect_failure(temp, std::move(out_of_section_relocation),
                   "RPX validation error",
                   "out-of-section-relocation.rpx");

    auto truncated_relocation = test::build_test_rpx();
    const uint32_t truncated_offset =
        be32(truncated_relocation, 0x40 + 2 * 40 + 16);
    test::put_be32(truncated_relocation, truncated_offset, 0x02000016);
    expect_failure(temp, std::move(truncated_relocation),
                   "RPX validation error", "truncated-relocation.rpx");
}
