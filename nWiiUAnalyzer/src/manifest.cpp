#include "nwiiu/analyzer/manifest.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace nwiiu::analyzer {
namespace {
void write_escaped(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20) {
                out << "\\u00" << hex[byte >> 4] << hex[byte & 0x0F];
            } else {
                out.put(static_cast<char>(byte));
            }
        }
    }
}

void write_address(std::ostream& out, uint32_t address) {
    constexpr char hex[] = "0123456789ABCDEF";
    out << "\"0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        out.put(hex[(address >> shift) & 0x0F]);
    }
    out.put('"');
}

void write_string(std::ostream& out, std::string_view value) {
    out.put('"');
    write_escaped(out, value);
    out.put('"');
}

template <typename Integer>
void write_integer(std::ostream& out, Integer value) {
    std::array<char, 32> buffer{};
    const auto [end, error] =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) {
        throw std::runtime_error("manifest integer serialization failed");
    }
    out.write(buffer.data(), end - buffer.data());
}

void write_symbol(std::ostream& out, const Symbol& symbol) {
    out << "{\"index\":";
    write_integer(out, symbol.index);
    out << ",\"name\":";
    write_string(out, symbol.name);
    out << ",\"address\":";
    write_address(out, symbol.value);
    out << ",\"size\":";
    write_integer(out, symbol.size);
    out << ",\"info\":";
    write_integer(out, static_cast<uint32_t>(symbol.info));
    out << ",\"other\":";
    write_integer(out, static_cast<uint32_t>(symbol.other));
    out << ",\"section_index\":";
    write_integer(out, symbol.section_index);
    out.put('}');
}

std::string_view symbol_kind(const Symbol& symbol) {
    switch (symbol.info & 0x0F) {
    case 0:
        return "notype";
    case 1:
        return "object";
    case 2:
        return "function";
    case 3:
        return "section";
    case 4:
        return "file";
    case 5:
        return "common";
    case 6:
        return "tls";
    case 10:
    case 11:
    case 12:
        return "os_specific";
    case 13:
    case 14:
    case 15:
        return "processor_specific";
    default:
        return "unknown";
    }
}

void write_export(std::ostream& out, const Symbol& symbol) {
    out << "{\"index\":";
    write_integer(out, symbol.index);
    out << ",\"name\":";
    write_string(out, symbol.name);
    out << ",\"kind\":";
    write_string(out, symbol_kind(symbol));
    out << ",\"address\":";
    write_address(out, symbol.value);
    out << ",\"size\":";
    write_integer(out, symbol.size);
    out << ",\"info\":";
    write_integer(out, static_cast<uint32_t>(symbol.info));
    out << ",\"other\":";
    write_integer(out, static_cast<uint32_t>(symbol.other));
    out << ",\"section_index\":";
    write_integer(out, symbol.section_index);
    out.put('}');
}

template <typename Range, typename Writer>
void write_array(std::ostream& out, const Range& range, Writer write_value) {
    out.put('[');
    bool first = true;
    for (const auto& value : range) {
        if (!first) {
            out.put(',');
        }
        first = false;
        write_value(value);
    }
    out.put(']');
}

auto symbol_key(const Symbol& symbol) {
    return std::tuple{symbol.value, std::string_view(symbol.name), symbol.index,
                      symbol.size, symbol.info, symbol.other,
                      symbol.section_index};
}

std::vector<uint32_t>
sorted_symbol_indices(const RpxImage& image,
                      const std::vector<uint32_t>& indices) {
    auto sorted = indices;
    std::sort(sorted.begin(), sorted.end(), [&](uint32_t left, uint32_t right) {
        return symbol_key(image.symbols.at(left)) <
               symbol_key(image.symbols.at(right));
    });
    return sorted;
}

void write_symbol_indices(std::ostream& out, const RpxImage& image,
                          const std::vector<uint32_t>& indices) {
    write_array(out, indices, [&](uint32_t index) {
        write_symbol(out, image.symbols.at(index));
    });
}

template <typename Range, typename Less>
auto sorted_view(const Range& range, Less less) {
    using Value = typename Range::value_type;
    std::vector<const Value*> view;
    view.reserve(range.size());
    for (const auto& value : range) {
        view.push_back(&value);
    }
    std::sort(view.begin(), view.end(),
              [&](const Value* left, const Value* right) {
                  return less(*left, *right);
              });
    return view;
}

struct ImportView {
    const ImportModule* module;
    std::vector<uint32_t> function_symbols;
    std::vector<uint32_t> data_symbols;
};

bool symbol_sequence_less(const RpxImage& image,
                          const std::vector<uint32_t>& left,
                          const std::vector<uint32_t>& right) {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [&](uint32_t left_index, uint32_t right_index) {
            return symbol_key(image.symbols.at(left_index)) <
                   symbol_key(image.symbols.at(right_index));
        });
}

std::vector<ImportView> sorted_imports(const RpxImage& image) {
    std::vector<ImportView> imports;
    imports.reserve(image.imports.size());
    for (const auto& module : image.imports) {
        imports.push_back({&module,
                           sorted_symbol_indices(image,
                                                 module.function_symbols),
                           sorted_symbol_indices(image, module.data_symbols)});
    }
    std::sort(imports.begin(), imports.end(),
              [&](const ImportView& left, const ImportView& right) {
                  if (left.module->name != right.module->name) {
                      return left.module->name < right.module->name;
                  }
                  if (symbol_sequence_less(image, left.function_symbols,
                                           right.function_symbols)) {
                      return true;
                  }
                  if (symbol_sequence_less(image, right.function_symbols,
                                           left.function_symbols)) {
                      return false;
                  }
                  return symbol_sequence_less(image, left.data_symbols,
                                              right.data_symbols);
              });
    return imports;
}

auto dynamic_transfer_key(const DynamicTransfer& transfer) {
    return std::tuple{transfer.address, std::string_view(transfer.kind)};
}

struct FunctionView {
    const Function* function;
    std::vector<const DynamicTransfer*> dynamic_transfers;
};

bool dynamic_transfer_sequence_less(
    const std::vector<const DynamicTransfer*>& left,
    const std::vector<const DynamicTransfer*>& right) {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const DynamicTransfer* left_transfer,
           const DynamicTransfer* right_transfer) {
            return dynamic_transfer_key(*left_transfer) <
                   dynamic_transfer_key(*right_transfer);
        });
}

bool function_less(const FunctionView& left, const FunctionView& right) {
    const Function& left_function = *left.function;
    const Function& right_function = *right.function;
    const auto left_key =
        std::tie(left_function.start, left_function.end,
                 left_function.instruction_count, left_function.callers,
                 left_function.callees, left_function.jump_table_targets,
                 left_function.discovery_reasons);
    const auto right_key =
        std::tie(right_function.start, right_function.end,
                 right_function.instruction_count, right_function.callers,
                 right_function.callees, right_function.jump_table_targets,
                 right_function.discovery_reasons);
    if (left_key != right_key) {
        return left_key < right_key;
    }
    return dynamic_transfer_sequence_less(left.dynamic_transfers,
                                          right.dynamic_transfers);
}

std::vector<FunctionView> sorted_functions(const Analysis& analysis) {
    std::vector<FunctionView> functions;
    functions.reserve(analysis.functions.size());
    for (const auto& [key, function] : analysis.functions) {
        (void)key;
        functions.push_back(
            {&function,
             sorted_view(function.dynamic_transfers,
                         [](const DynamicTransfer& left,
                            const DynamicTransfer& right) {
                             return dynamic_transfer_key(left) <
                                    dynamic_transfer_key(right);
                         })});
    }
    std::sort(functions.begin(), functions.end(), function_less);
    return functions;
}
void write_addresses(std::ostream& out, const std::set<uint32_t>& addresses) {
    write_array(out, addresses,
                [&](uint32_t address) { write_address(out, address); });
}

void write_strings(std::ostream& out, const std::set<std::string>& strings) {
    write_array(out, strings,
                [&](const std::string& value) { write_string(out, value); });
}
}

void write_manifest(std::ostream& out, const RpxImage& image,
                    const Analysis& analysis) {
    out.imbue(std::locale::classic());
    out << "{\"target\":{\"product_code\":";
    write_string(out, image.target.product_code);
    out << ",\"title_id\":";
    write_string(out, image.target.title_id);
    out << ",\"title_version\":";
    write_integer(out, image.target.title_version);
    out << ",\"executable_format\":\"RPX\""
           ",\"architecture\":\"PowerPC\""
           ",\"elf_class\":\"ELF32\""
           ",\"endianness\":\"big\""
           ",\"abi\":\"Cafe OS\"";
    out << ",\"sha256\":";
    write_string(out, image.sha256);
    out << ",\"entry_point\":";
    write_address(out, image.entry_point);
    out << ",\"input_size\":";
    write_integer(out, image.input_size);

    out << "},\"sections\":";
    const auto sections =
        sorted_view(image.sections, [](const Section& left,
                                       const Section& right) {
            return std::tuple{left.address,
                              std::string_view(left.name),
                              left.index,
                              left.type,
                              left.flags,
                              left.stored_size,
                              left.decompressed_size,
                              left.link,
                              left.info,
                              left.alignment,
                              left.entry_size} <
                   std::tuple{right.address,
                              std::string_view(right.name),
                              right.index,
                              right.type,
                              right.flags,
                              right.stored_size,
                              right.decompressed_size,
                              right.link,
                              right.info,
                              right.alignment,
                              right.entry_size};
        });
    write_array(out, sections, [&](const Section* section_pointer) {
        const Section& section = *section_pointer;
        out << "{\"index\":";
        write_integer(out, section.index);
        out << ",\"name\":";
        write_string(out, section.name);
        out << ",\"type\":";
        write_integer(out, section.type);
        out << ",\"flags\":";
        write_integer(out, section.flags);
        out << ",\"address\":";
        write_address(out, section.address);
        out << ",\"stored_size\":";
        write_integer(out, section.stored_size);
        out << ",\"decompressed_size\":";
        write_integer(out, section.decompressed_size);
        out << ",\"link\":";
        write_integer(out, section.link);
        out << ",\"info\":";
        write_integer(out, section.info);
        out << ",\"alignment\":";
        write_integer(out, section.alignment);
        out << ",\"entry_size\":";
        write_integer(out, section.entry_size);
        out.put('}');
    });

    out << ",\"imports\":";
    const auto imports = sorted_imports(image);
    write_array(out, imports, [&](const ImportView& import) {
        const ImportModule& module = *import.module;
        out << "{\"name\":";
        write_string(out, module.name);
        out << ",\"functions\":";
        write_symbol_indices(out, image, import.function_symbols);
        out << ",\"data\":";
        write_symbol_indices(out, image, import.data_symbols);
        out.put('}');
    });

    out << ",\"exports\":";
    const auto exports =
        sorted_view(image.exports, [&](const ExportSymbol& left,
                                       const ExportSymbol& right) {
            return symbol_key(image.symbols.at(left.symbol_index)) <
                   symbol_key(image.symbols.at(right.symbol_index));
        });
    write_array(out, exports, [&](const ExportSymbol* export_pointer) {
        write_export(out, image.symbols.at(export_pointer->symbol_index));
    });

    out << ",\"relocations\":";
    const auto relocations =
        sorted_view(image.relocations, [&](const Relocation& left,
                                           const Relocation& right) {
            const Symbol& left_symbol =
                image.symbols.at(left.symbol_index);
            const Symbol& right_symbol =
                image.symbols.at(right.symbol_index);
            return std::tuple{
                       left.source_address, std::string_view(left_symbol.name),
                       left.type, left.source_section_index,
                       symbol_key(left_symbol), left.addend,
                       left.target_address} <
                   std::tuple{right.source_address,
                              std::string_view(right_symbol.name), right.type,
                              right.source_section_index,
                              symbol_key(right_symbol), right.addend,
                              right.target_address};
        });
    write_array(out, relocations, [&](const Relocation* relocation_pointer) {
        const Relocation& relocation = *relocation_pointer;
        out << "{\"source_section_index\":";
        write_integer(out, relocation.source_section_index);
        out << ",\"source_address\":";
        write_address(out, relocation.source_address);
        out << ",\"type\":";
        write_integer(out, relocation.type);
        out << ",\"symbol\":";
        write_symbol(out, image.symbols.at(relocation.symbol_index));
        out << ",\"addend\":";
        write_integer(out, relocation.addend);
        out << ",\"target_address\":";
        if (relocation.target_address.has_value()) {
            write_address(out, *relocation.target_address);
        } else {
            out << "null";
        }
        out.put('}');
    });

    out << ",\"functions\":";
    const auto functions = sorted_functions(analysis);
    write_array(out, functions, [&](const FunctionView& function_view) {
        const Function& function = *function_view.function;
        out << "{\"start\":";
        write_address(out, function.start);
        out << ",\"end\":";
        write_address(out, function.end);
        out << ",\"instruction_count\":";
        write_integer(out, function.instruction_count);
        out << ",\"callers\":";
        write_addresses(out, function.callers);
        out << ",\"callees\":";
        write_addresses(out, function.callees);
        out << ",\"jump_table_targets\":";
        write_addresses(out, function.jump_table_targets);
        out << ",\"discovery_reasons\":";
        write_strings(out, function.discovery_reasons);
        out << ",\"dynamic_transfers\":";
        write_array(out, function_view.dynamic_transfers,
                    [&](const DynamicTransfer* transfer_pointer) {
                        const DynamicTransfer& transfer = *transfer_pointer;
                        out << "{\"address\":";
                        write_address(out, transfer.address);
                        out << ",\"kind\":";
                        write_string(out, transfer.kind);
                        out.put('}');
                    });
        out.put('}');
    });

    out << ",\"unresolved\":";
    const auto unresolved_records =
        sorted_view(analysis.unresolved,
                    [](const Unresolved& left, const Unresolved& right) {
                        return std::tuple{left.address,
                                          std::string_view(left.category),
                                          std::string_view(left.reason)} <
                               std::tuple{right.address,
                                          std::string_view(right.category),
                                          std::string_view(right.reason)};
                    });
    write_array(out, unresolved_records,
                [&](const Unresolved* unresolved_pointer) {
        const Unresolved& unresolved = *unresolved_pointer;
        out << "{\"address\":";
        write_address(out, unresolved.address);
        out << ",\"category\":";
        write_string(out, unresolved.category);
        out << ",\"reason\":";
        write_string(out, unresolved.reason);
        out.put('}');
    });

    out << ",\"summary\":{\"section_count\":";
    write_integer(out, image.sections.size());
    out << ",\"import_count\":";
    write_integer(out, image.imports.size());
    out << ",\"export_count\":";
    write_integer(out, image.exports.size());
    out << ",\"relocation_count\":";
    write_integer(out, image.relocations.size());
    out << ",\"function_count\":";
    write_integer(out, analysis.functions.size());
    out << ",\"unresolved_count\":";
    write_integer(out, analysis.unresolved.size());
    out << "}}\n";
}

void write_manifest_file(const std::filesystem::path& output,
                         const RpxImage& image, const Analysis& analysis) {
    auto temporary = output;
    temporary += ".tmp";
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            throw std::runtime_error("cannot open manifest temporary file");
        }
        write_manifest(stream, image, analysis);
        stream.flush();
        if (!stream) {
            throw std::runtime_error("cannot write manifest temporary file");
        }
        stream.close();
        if (!stream) {
            throw std::runtime_error("cannot close manifest temporary file");
        }
        std::filesystem::rename(temporary, output);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
}
