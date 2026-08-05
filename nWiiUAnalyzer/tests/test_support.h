#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <zlib.h>

namespace test {
inline void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("nwiiu-analyzer-test-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

inline void write_bytes(const std::filesystem::path& path,
                        std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary);
    require(output.is_open(), "open test output");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(output.good(), "write test output");
}

inline void put_be16(std::vector<std::byte>& bytes, size_t offset,
                     uint16_t value) {
    bytes.at(offset) = static_cast<std::byte>(value >> 8);
    bytes.at(offset + 1) = static_cast<std::byte>(value);
}

inline void put_be32(std::vector<std::byte>& bytes, size_t offset,
                     uint32_t value) {
    bytes.at(offset) = static_cast<std::byte>(value >> 24);
    bytes.at(offset + 1) = static_cast<std::byte>(value >> 16);
    bytes.at(offset + 2) = static_cast<std::byte>(value >> 8);
    bytes.at(offset + 3) = static_cast<std::byte>(value);
}

// uint8_t overloads for the shader extractor tests. New overloads, so the
// std::byte versions above and their existing callers are untouched.
inline void put_be16(std::vector<uint8_t>& bytes, size_t offset,
                     uint16_t value) {
    bytes.at(offset + 0) = static_cast<uint8_t>(value >> 8);
    bytes.at(offset + 1) = static_cast<uint8_t>(value);
}

inline void put_be32(std::vector<uint8_t>& bytes, size_t offset,
                     uint32_t value) {
    bytes.at(offset + 0) = static_cast<uint8_t>(value >> 24);
    bytes.at(offset + 1) = static_cast<uint8_t>(value >> 16);
    bytes.at(offset + 2) = static_cast<uint8_t>(value >> 8);
    bytes.at(offset + 3) = static_cast<uint8_t>(value);
}

inline void put_le32(std::vector<uint8_t>& bytes, size_t offset,
                     uint32_t value) {
    bytes.at(offset + 0) = static_cast<uint8_t>(value);
    bytes.at(offset + 1) = static_cast<uint8_t>(value >> 8);
    bytes.at(offset + 2) = static_cast<uint8_t>(value >> 16);
    bytes.at(offset + 3) = static_cast<uint8_t>(value >> 24);
}

inline constexpr std::array<uint32_t, 6> kTestTextWords{
    0x48000011, 0x49FFFFFD, 0x41800008,
    0x4E800020, 0x4E800421, 0x4E800020,
};

inline std::vector<std::byte> build_test_rpx() {
    constexpr size_t section_headers = 0x40;
    constexpr size_t section_header_size = 40;
    constexpr size_t section_count = 10;

    std::vector<std::byte> text(kTestTextWords.size() * sizeof(uint32_t));
    for (size_t i = 0; i < kTestTextWords.size(); ++i) {
        put_be32(text, i * sizeof(uint32_t), kTestTextWords[i]);
    }

    uLongf compressed_size = compressBound(text.size());
    std::vector<std::byte> compressed(sizeof(uint32_t) + compressed_size);
    put_be32(compressed, 0, text.size());
    require(compress2(reinterpret_cast<Bytef*>(compressed.data() + sizeof(uint32_t)),
                      &compressed_size,
                      reinterpret_cast<const Bytef*>(text.data()), text.size(),
                      Z_BEST_COMPRESSION) == Z_OK,
            "compress test text");
    compressed.resize(sizeof(uint32_t) + compressed_size);

    std::vector<std::byte> relocations(24);
    put_be32(relocations, 0, 0x02000000);
    put_be32(relocations, 4, 1U << 8 | 10);
    put_be32(relocations, 8, 0x10);
    put_be32(relocations, 12, 0x02000004);
    put_be32(relocations, 16, 2U << 8 | 10);
    const std::array<std::byte, 8> imports{};
    std::vector<std::byte> symbols(48);
    put_be32(symbols, 16 + 4, 0x02000000);
    put_be32(symbols, 16 + 8, text.size());
    symbols[16 + 12] = std::byte{3};
    put_be16(symbols, 16 + 14, 1);
    put_be32(symbols, 32, 1);
    put_be32(symbols, 32 + 4, 0xC0001000);
    symbols[32 + 12] = std::byte{0x12};
    put_be16(symbols, 32 + 14, 3);
    constexpr std::string_view symbol_strings{
        "\0OSReport\0", sizeof("\0OSReport\0") - 1};
    constexpr std::string_view section_strings{
        "\0.text\0.rela.text\0.fimport_coreinit\0.symtab\0.strtab\0.shstrtab\0.bss\0",
        sizeof("\0.text\0.rela.text\0.fimport_coreinit\0.symtab\0.strtab\0.shstrtab\0.bss\0") -
            1};
    std::array<std::byte, section_count * sizeof(uint32_t)> crcs{};
    const auto text_crc = crc32(
        0, reinterpret_cast<const Bytef*>(text.data()), text.size());
    crcs[4] = static_cast<std::byte>(text_crc >> 24);
    crcs[5] = static_cast<std::byte>(text_crc >> 16);
    crcs[6] = static_cast<std::byte>(text_crc >> 8);
    crcs[7] = static_cast<std::byte>(text_crc);
    std::vector<std::byte> file_info(64);
    put_be32(file_info, 0, 0xCAFE0402);
    put_be32(file_info, 36, 0x104E22E0); // mSDABase
    put_be32(file_info, 40, 0x10080000); // mSDA2Base
    put_be32(file_info, 44, 0x00101000); // mSizeCoreStacks
    put_be32(file_info, 52, 0x00000002); // mFlags
    put_be32(file_info, 56, 0x00080000); // mSysHeapBytes

    std::vector<std::byte> bytes(section_headers +
                                 section_count * section_header_size);
    const auto append = [&bytes](const auto& payload) {
        const uint32_t offset = bytes.size();
        const auto* first = reinterpret_cast<const std::byte*>(payload.data());
        bytes.insert(bytes.end(), first, first + payload.size());
        return offset;
    };
    const uint32_t text_offset = append(compressed);
    const uint32_t relocation_offset = append(relocations);
    const uint32_t imports_offset = append(imports);
    const uint32_t symbols_offset = append(symbols);
    const uint32_t symbol_strings_offset = append(symbol_strings);
    const uint32_t section_strings_offset = append(section_strings);
    const uint32_t crcs_offset = append(crcs);
    const uint32_t file_info_offset = append(file_info);

    bytes[0] = std::byte{0x7F};
    bytes[1] = std::byte{0x45};
    bytes[2] = std::byte{0x4C};
    bytes[3] = std::byte{0x46};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{2};
    bytes[6] = std::byte{1};
    bytes[7] = std::byte{0xCA};
    bytes[8] = std::byte{0xFE};
    put_be16(bytes, 16, 0xFE01);
    put_be16(bytes, 18, 0x0014);
    put_be32(bytes, 20, 1);
    put_be32(bytes, 24, 0x02000000);
    put_be32(bytes, 32, section_headers);
    put_be16(bytes, 40, 52);
    put_be16(bytes, 46, section_header_size);
    put_be16(bytes, 48, section_count);
    put_be16(bytes, 50, 6);

    const auto section = [&bytes](size_t index, uint32_t name, uint32_t type,
                                  uint32_t flags, uint32_t address,
                                  uint32_t offset, uint32_t size,
                                  uint32_t alignment, uint32_t entry_size) {
        const size_t header = section_headers + index * section_header_size;
        put_be32(bytes, header, name);
        put_be32(bytes, header + 4, type);
        put_be32(bytes, header + 8, flags);
        put_be32(bytes, header + 12, address);
        put_be32(bytes, header + 16, offset);
        put_be32(bytes, header + 20, size);
        put_be32(bytes, header + 32, alignment);
        put_be32(bytes, header + 36, entry_size);
    };
    section(1, 1, 1, 0x08000006, 0x02000000, text_offset, compressed.size(), 4,
            0);
    section(2, 7, 4, 0, 0, relocation_offset, relocations.size(), 4, 12);
    put_be32(bytes, section_headers + 2 * section_header_size + 24, 4);
    put_be32(bytes, section_headers + 2 * section_header_size + 28, 1);
    section(3, 18, 0x80000002, 0x6, 0xC0001000, imports_offset, imports.size(),
            4, 4);
    section(4, 36, 2, 0x2, 0xC0000000, symbols_offset, symbols.size(), 4, 16);
    put_be32(bytes, section_headers + 4 * section_header_size + 24, 5);
    section(5, 44, 3, 0x2, 0xC0000100, symbol_strings_offset,
            symbol_strings.size(), 1, 0);
    section(6, 52, 3, 0x2, 0xC0000200, section_strings_offset,
            section_strings.size(), 1, 0);
    section(7, 0, 0x80000003, 0, 0, crcs_offset, crcs.size(), 4, 4);
    section(8, 0, 0x80000004, 0, 0, file_info_offset, file_info.size(), 4, 0);
    section(9, 62, 8, 0x3, 0x03000000, 0, 0x20, 4, 0);
    return bytes;
}

template <typename Function>
void require_throws(Function&& function, std::string_view expected,
                    std::string_view message) {
    try {
        function();
    } catch (const std::exception& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos,
                message);
        return;
    }
    require(false, message);
}
}
