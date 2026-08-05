#include "nwiiu/analyzer/rpx.h"

#include "nwiiu/analyzer/hash.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <zlib.h>

namespace nwiiu::analyzer {
namespace {
class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    uint8_t u8(size_t off) const { return slice(off, 1)[0]; }

    uint16_t be16(size_t off) const {
        const auto bytes = slice(off, 2);
        return static_cast<uint16_t>(bytes[0]) << 8 | bytes[1];
    }

    uint32_t be32(size_t off) const {
        const auto bytes = slice(off, 4);
        return static_cast<uint32_t>(bytes[0]) << 24 |
               static_cast<uint32_t>(bytes[1]) << 16 |
               static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
    }

    std::span<const uint8_t> slice(size_t off, size_t size) const {
        if (off > bytes_.size() || size > bytes_.size() - off) {
            throw std::runtime_error("RPX bounds error");
        }
        return bytes_.subspan(off, size);
    }

private:
    std::span<const uint8_t> bytes_;
};


[[noreturn]] void validation_error() {
    throw std::runtime_error("RPX validation error");
}

std::string read_string(const Section& table, uint32_t offset) {
    if (offset >= table.data.size()) {
        throw std::runtime_error("RPX bounds error");
    }
    const auto first = table.data.begin() + offset;
    const auto end = std::find(first, table.data.end(), uint8_t{});
    if (end == table.data.end()) {
        validation_error();
    }
    return {first, end};
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open input: " + path.string());
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot read input: " + path.string());
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error("cannot read input: " + path.string());
    }
    return bytes;
}
}

const Section* RpxImage::section_containing(uint32_t address) const {
    const auto found = std::find_if(
        sections.begin(), sections.end(), [address](const Section& section) {
            return address >= section.address &&
                   address - section.address < section.decompressed_size;
        });
    return found == sections.end() ? nullptr : &*found;
}

RpxImage load_rpx(const std::filesystem::path& path, Target target) {
    const std::vector<uint8_t> bytes = read_file(path);
    const std::string hash = sha256(bytes);
    const Reader reader(bytes);

    if (reader.u8(0) != 0x7F || reader.u8(1) != 'E' ||
        reader.u8(2) != 'L' || reader.u8(3) != 'F' || reader.u8(4) != 1 ||
        reader.u8(5) != 2 || reader.u8(7) != 0xCA ||
        reader.u8(8) != 0xFE || reader.be16(16) != 0xFE01 ||
        reader.be16(18) != 0x14 || reader.be16(40) != 52 ||
        reader.be16(46) != 40) {
        validation_error();
    }

    const uint32_t entry_point = reader.be32(24);
    const uint32_t section_header_offset = reader.be32(32);
    const uint16_t section_count = reader.be16(48);
    const uint16_t string_table_index = reader.be16(50);
    // A profile without a digest or an entry point authenticates nothing; the
    // structural checks above and the entry-point-lands-in-code check below
    // still have to pass, so a corrupt image is rejected either way.
    if (section_count == 0 || string_table_index >= section_count ||
        (target.verifies_hash() && hash != target.sha256) ||
        (target.pins_entry_point() && entry_point != target.entry_point)) {
        validation_error();
    }

    RpxImage image;
    image.target = std::move(target);
    image.input_size = bytes.size();
    image.sha256 = hash;
    image.entry_point = entry_point;
    image.sections.reserve(section_count);
    std::vector<uint32_t> name_offsets;
    name_offsets.reserve(section_count);

    for (uint32_t index = 0; index < section_count; ++index) {
        const size_t header =
            static_cast<size_t>(section_header_offset) + index * 40;
        reader.slice(header, 40);
        const uint32_t name_offset = reader.be32(header);
        const uint32_t type = reader.be32(header + 4);
        const uint32_t flags = reader.be32(header + 8);
        const uint32_t address = reader.be32(header + 12);
        const uint32_t stored_offset = reader.be32(header + 16);
        const uint32_t payload_size = reader.be32(header + 20);

        Section section;
        section.index = index;
        section.type = type;
        section.flags = flags;
        section.address = address;
        section.stored_size = type == kShtNobits ? 0 : payload_size;
        section.link = reader.be32(header + 24);
        section.info = reader.be32(header + 28);
        section.alignment = reader.be32(header + 32);
        section.entry_size = reader.be32(header + 36);
        if (type == kShtNobits) {
            section.decompressed_size = payload_size;
        } else {
            const auto stored = reader.slice(stored_offset, payload_size);
            if ((flags & kShfRplZlib) != 0) {
                if (stored.size() < sizeof(uint32_t)) {
                    throw std::runtime_error("RPX decompression error");
                }
                const uint32_t decompressed_size = Reader(stored).be32(0);
                section.data.resize(decompressed_size);
                uLongf output_size = section.data.size();
                const int status = uncompress(
                    section.data.data(), &output_size,
                    stored.data() + sizeof(uint32_t),
                    stored.size() - sizeof(uint32_t));
                if (status != Z_OK || output_size != section.data.size()) {
                    throw std::runtime_error("RPX decompression error");
                }
            } else {
                section.data.assign(stored.begin(), stored.end());
            }
            section.decompressed_size =
                static_cast<uint32_t>(section.data.size());
        }
        name_offsets.push_back(name_offset);
        image.sections.push_back(std::move(section));
    }
    const bool valid_entry =
        (entry_point & 3) == 0 &&
        std::any_of(image.sections.begin(), image.sections.end(),
                    [entry_point](const Section& section) {
                        if (!section.analyzable_code() ||
                            entry_point < section.address) {
                            return false;
                        }
                        const uint32_t offset = entry_point - section.address;
                        return section.decompressed_size >= sizeof(uint32_t) &&
                               offset <= section.decompressed_size -
                                             sizeof(uint32_t) &&
                               section.data.size() >= sizeof(uint32_t) &&
                               offset <= section.data.size() - sizeof(uint32_t);
                    });
    if (!valid_entry) {
        validation_error();
    }


    if (image.sections[string_table_index].type != kShtStrtab) {
        validation_error();
    }
    const auto& section_names = image.sections[string_table_index].data;
    for (size_t index = 0; index < image.sections.size(); ++index) {
        const size_t name_offset = name_offsets[index];
        if (name_offset >= section_names.size()) {
            if (name_offset == 0 && section_names.empty()) {
                continue;
            }
            throw std::runtime_error("RPX bounds error");
        }
        const auto first = section_names.begin() + name_offset;
        const auto end = std::find(first, section_names.end(), uint8_t{});
        if (end == section_names.end()) {
            validation_error();
        }
        image.sections[index].name.assign(first, end);
    }

    for (const Section& section : image.sections) {
        if (section.type != kShtRplFileInfo) {
            continue;
        }
        if (image.file_info.has_value() || section.data.size() < 64) {
            validation_error();
        }
        const Reader info(section.data);
        image.file_info = RpxFileInfo{
            info.be32(0),  info.be32(4),  info.be32(8),  info.be32(12),
            info.be32(16), info.be32(20), info.be32(24), info.be32(28),
            info.be32(32), info.be32(36), info.be32(40), info.be32(44),
            info.be32(48), info.be32(52), info.be32(56), info.be32(60),
        };
    }
    if (!image.file_info.has_value() ||
        image.file_info->magic_version != 0xCAFE0402) {
        validation_error();
    }

    const auto crc_section = std::find_if(
        image.sections.begin(), image.sections.end(),
        [](const Section& section) { return section.type == kShtRplCrcs; });
    if (crc_section != image.sections.end()) {
        const Reader crcs(crc_section->data);
        for (size_t index = 0; index < image.sections.size(); ++index) {
            const uint32_t expected = crcs.be32(index * sizeof(uint32_t));
            const auto& data = image.sections[index].data;
            const uint32_t actual = crc32(0, data.data(), data.size());
            if (expected != 0 && actual != expected) {
                throw std::runtime_error("RPX CRC error");
            }
        }
    }

    std::vector<size_t> symbol_table_bases(image.sections.size());
    std::vector<size_t> symbol_table_sizes(image.sections.size());
    for (const Section& section : image.sections) {
        if (section.type != kShtSymtab) {
            continue;
        }
        if (section.entry_size != 16 || section.link >= image.sections.size() ||
            image.sections[section.link].type != kShtStrtab ||
            section.data.size() % 16 != 0) {
            validation_error();
        }

        symbol_table_bases[section.index] = image.symbols.size();
        symbol_table_sizes[section.index] = section.data.size() / 16;
        const Reader symbols(section.data);
        const Section& strings = image.sections[section.link];
        for (size_t offset = 0; offset < section.data.size(); offset += 16) {
            Symbol symbol;
            symbol.index = image.symbols.size();
            symbol.name = read_string(strings, symbols.be32(offset));
            symbol.value = symbols.be32(offset + 4);
            symbol.size = symbols.be32(offset + 8);
            symbol.info = symbols.u8(offset + 12);
            symbol.other = symbols.u8(offset + 13);
            symbol.section_index = symbols.be16(offset + 14);
            image.symbols.push_back(std::move(symbol));
        }
    }

    for (const Symbol& symbol : image.symbols) {
        if (symbol.section_index >= image.sections.size()) {
            continue;
        }
        const Section& section = image.sections[symbol.section_index];
        std::string_view module_name;
        std::vector<uint32_t> ImportModule::*kind = nullptr;
        if (section.name.starts_with(".fimport_")) {
            module_name = std::string_view(section.name).substr(9);
            kind = &ImportModule::function_symbols;
        } else if (section.name.starts_with(".dimport_")) {
            module_name = std::string_view(section.name).substr(9);
            kind = &ImportModule::data_symbols;
        } else if (section.type == kShtRplExports) {
            image.exports.push_back({symbol.index});
            continue;
        } else {
            continue;
        }
        if (module_name.ends_with(".rpl")) {
            module_name.remove_suffix(4);
        }
        auto module = std::find_if(
            image.imports.begin(), image.imports.end(),
            [module_name](const ImportModule& import) {
                return import.name == module_name;
            });
        if (module == image.imports.end()) {
            image.imports.emplace_back();
            image.imports.back().name = module_name;
            module = std::prev(image.imports.end());
        }
        ((*module).*kind).push_back(symbol.index);
    }

    const auto symbol_less = [&image](uint32_t lhs, uint32_t rhs) {
        const Symbol& left = image.symbols[lhs];
        const Symbol& right = image.symbols[rhs];
        if (left.value != right.value) {
            return left.value < right.value;
        }
        if (left.name != right.name) {
            return left.name < right.name;
        }
        return lhs < rhs;
    };
    std::sort(image.imports.begin(), image.imports.end(),
              [](const ImportModule& lhs, const ImportModule& rhs) {
                  return lhs.name < rhs.name;
              });
    for (ImportModule& module : image.imports) {
        std::sort(module.function_symbols.begin(), module.function_symbols.end(),
                  symbol_less);
        std::sort(module.data_symbols.begin(), module.data_symbols.end(),
                  symbol_less);
    }
    std::sort(image.exports.begin(), image.exports.end(),
              [&symbol_less](const ExportSymbol& lhs, const ExportSymbol& rhs) {
                  return symbol_less(lhs.symbol_index, rhs.symbol_index);
              });

    for (const Section& section : image.sections) {
        if (section.type != kShtRela) {
            continue;
        }
        if (section.entry_size != 12 || section.info >= image.sections.size() ||
            section.link >= image.sections.size() ||
            image.sections[section.link].type != kShtSymtab ||
            section.data.size() % 12 != 0) {
            validation_error();
        }

        const Reader relocations(section.data);
        for (size_t offset = 0; offset < section.data.size(); offset += 12) {
            const uint32_t info = relocations.be32(offset + 4);
            const uint32_t table_symbol_index = info >> 8;
            const uint32_t type = info & 0xFF;
            uint32_t width;
            switch (type) {
            case kRppcAddr32:
            case kRppcRel24:
                width = 4;
                break;
            case kRppcAddr16Lo:
            case kRppcAddr16Hi:
            case kRppcAddr16Ha:
                width = 2;
                break;
            default:
                throw std::runtime_error("unsupported PowerPC relocation: " +
                                         std::to_string(type));
            }
            const uint32_t source_address = relocations.be32(offset);
            const Section& source_section = image.sections[section.info];
            if (source_address < source_section.address ||
                width > source_section.decompressed_size ||
                source_address - source_section.address >
                    source_section.decompressed_size - width) {
                validation_error();
            }
            if (table_symbol_index >= symbol_table_sizes[section.link]) {
                validation_error();
            }
            const uint32_t symbol_index =
                symbol_table_bases[section.link] + table_symbol_index;
            const Symbol& symbol = image.symbols[symbol_index];
            const int32_t addend =
                std::bit_cast<int32_t>(relocations.be32(offset + 8));
            const int64_t target =
                static_cast<int64_t>(symbol.value) + addend;
            if (target < 0 || target > 0xFFFFFFFFLL) {
                validation_error();
            }
            image.relocations.push_back(
                {section.info, source_address, type, symbol_index, symbol.name,
                 addend, static_cast<uint32_t>(target)});
        }
    }
    return image;
}
}
