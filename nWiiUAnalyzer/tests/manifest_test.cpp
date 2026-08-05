#include "nwiiu/analyzer/manifest.h"
#include "test_support.h"
#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace {
std::string escaped_name(std::string_view prefix) {
    std::string value(prefix);
    value += "\"\\\n\t";
    value.push_back('\x01');
    return value;
}

nwiiu::analyzer::RpxImage make_image() {
    nwiiu::analyzer::RpxImage image;
    static const std::string product = escaped_name("product");
    image.target = {product, "0005000010143600", 0,
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    0x02000000};
    image.input_size = 123;
    image.sha256 = image.target.sha256;
    image.entry_point = image.target.entry_point;

    image.sections.push_back({2,
                              escaped_name(".text"),
                              nwiiu::analyzer::kShtProgbits,
                              nwiiu::analyzer::kShfAlloc |
                                  nwiiu::analyzer::kShfExec,
                              0x02000000,
                              9,
                              4,
                              2,
                              3,
                              4,
                              0,
                              {0x4E, 0x80, 0x00, 0x20}});
    image.sections.push_back(
        {1,
         ".data",
         nwiiu::analyzer::kShtProgbits,
         nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfWrite,
         0x02001000,
         4,
         2,
         0,
         0,
         4,
         0,
         {0x01, 0x02}});
    image.sections.push_back(
        {3, ".adata", nwiiu::analyzer::kShtProgbits,
         nwiiu::analyzer::kShfAlloc, 0x02001000, 1, 1, 0, 0, 1, 0,
         {0x03}});
    image.sections.push_back(
        {4, ".bss", nwiiu::analyzer::kShtNobits,
         nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfWrite,
         0x02002000, 0, 32, 0, 0, 4, 0, {}});

    image.symbols.push_back({9, escaped_name("OSReport"), 0xC0001000, 4,
                             0x12, 0, 1});
    image.symbols.push_back(
        {1, "Exported", 0x02000020, 8, 0x12, 0, 1});
    image.symbols.push_back({2, "Alloc", 0xC0002000, 4, 0x12, 0, 1});
    image.symbols.push_back({0, "MemFree", 0xC0003000, 4, 0x12, 0, 1});
    image.symbols.push_back({4, "GlobalA", 0xC0004000, 4, 0x11, 0, 2});
    image.symbols.push_back({5, "GlobalB", 0xC0005000, 4, 0x11, 0, 2});
    image.symbols.push_back(image.symbols.front());
    image.symbols.push_back(image.symbols.front());

    image.imports.push_back({"coreinit", {3, 0}, {5, 4}});
    image.imports.push_back({"nn_ac", {2, 1}, {4, 5}});
    image.imports.push_back({"same", {6}, {5}});
    image.imports.push_back({"same", {7}, {4}});
    image.exports.push_back({5});
    image.exports.push_back({1});
    image.relocations.push_back(
        {0, 0x02000008, 10, 3, "MemFree", 8, 0xC0003008});
    image.relocations.push_back(
        {8, 0x02000004, 10, 0, "OSReport", -4, 0xC0000FFC});
    image.relocations.push_back(
        {9, 0x02000004, 10, 2, "Alloc", 0, 0xC0002000});
    return image;
}

nwiiu::analyzer::Analysis make_analysis() {
    nwiiu::analyzer::Analysis analysis;
    nwiiu::analyzer::Function later;
    later.start = 0x02000020;
    later.end = 0x02000028;
    later.instruction_count = 2;
    later.callers.insert(0x02000000);
    later.discovery_reasons.insert("export");
    later.dynamic_transfers.push_back({0x02000024, "return"});
    later.dynamic_transfers.push_back({0x02000020, "indirect_jump"});
    analysis.functions.emplace(later.start, later);

    nwiiu::analyzer::Function first;
    first.start = 0x02000000;
    first.end = 0x02000010;
    first.instruction_count = 4;
    first.callees.insert(0x02000020);
    first.jump_table_targets.insert(0x0200000C);
    first.discovery_reasons.insert("entry_point");
    first.dynamic_transfers.push_back({0x0200000C, "return"});
    first.dynamic_transfers.push_back({0x02000008, "indirect_call"});
    analysis.functions.emplace(first.start, first);
    analysis.unresolved.push_back(
        {0x02000020, "indirect_call", "unknown register"});
    analysis.unresolved.push_back(
        {0x02000008, "indirect_branch", escaped_name("reason")});
    return analysis;
}
void reverse_vector_backed_output_families(nwiiu::analyzer::RpxImage& image,
                                           nwiiu::analyzer::Analysis& analysis) {
    std::reverse(image.sections.begin(), image.sections.end());
    for (auto& module : image.imports) {
        std::reverse(module.function_symbols.begin(),
                     module.function_symbols.end());
        std::reverse(module.data_symbols.begin(), module.data_symbols.end());
    }
    std::reverse(image.imports.begin(), image.imports.end());
    std::reverse(image.exports.begin(), image.exports.end());
    std::reverse(image.relocations.begin(), image.relocations.end());
    for (auto& [key, function] : analysis.functions) {
        (void)key;
        std::reverse(function.dynamic_transfers.begin(),
                     function.dynamic_transfers.end());
    }
    std::reverse(analysis.unresolved.begin(), analysis.unresolved.end());

    decltype(analysis.functions) reversed_functions;
    for (auto iterator = analysis.functions.rbegin();
         iterator != analysis.functions.rend(); ++iterator) {
        reversed_functions.emplace(
            std::numeric_limits<uint32_t>::max() - iterator->first,
            iterator->second);
    }
    analysis.functions = std::move(reversed_functions);
}

std::vector<std::string_view> top_level_keys(std::string_view json) {
    std::vector<std::string_view> keys;
    size_t string_start = 0;
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0; index < json.size(); ++index) {
        const char byte = json[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (byte == '\\') {
                escaped = true;
            } else if (byte == '"') {
                in_string = false;
                size_t next = index + 1;
                while (next < json.size() &&
                       (json[next] == ' ' || json[next] == '\n')) {
                    ++next;
                }
                if (depth == 1 && next < json.size() && json[next] == ':') {
                    keys.emplace_back(json.data() + string_start,
                                      index - string_start);
                }
            }
        } else if (byte == '"') {
            in_string = true;
            string_start = index + 1;
        } else if (byte == '{' || byte == '[') {
            ++depth;
        } else if (byte == '}' || byte == ']') {
            --depth;
        }
    }
    return keys;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}
}

int main() {
    auto image = make_image();
    auto analysis = make_analysis();
    auto reversed_image = image;
    auto reversed_analysis = analysis;
    reverse_vector_backed_output_families(reversed_image, reversed_analysis);
    std::ostringstream first;
    std::ostringstream second;
    nwiiu::analyzer::write_manifest(first, image, analysis);
    nwiiu::analyzer::write_manifest(second, reversed_image, reversed_analysis);
    const std::string manifest = first.str();

    test::require(manifest == second.str(), "manifest is byte-identical");
    test::require(!manifest.empty() && manifest.back() == '\n',
                  "manifest ends with one newline");

    const std::vector<std::string_view> expected_top_level_keys{
        "target",     "sections",  "imports", "exports",
        "relocations", "functions", "unresolved", "summary"};
    test::require(top_level_keys(manifest) == expected_top_level_keys,
                  "top-level keys are exact and canonical");

    test::require(manifest.find("\"entry_point\":\"0x02000000\"") !=
                      std::string::npos,
                  "uppercase padded address");
    test::require(
        manifest.find(
            "\"executable_format\":\"RPX\",\"architecture\":\"PowerPC\","
            "\"elf_class\":\"ELF32\",\"endianness\":\"big\","
            "\"abi\":\"Cafe OS\"") != std::string::npos,
        "target emits fixed RPX format fields");
    test::require(
        manifest.find("\"stored_size\":9,\"decompressed_size\":4") !=
            std::string::npos,
        "section uses explicit decompressed size field");
    const size_t sections_position = manifest.find("\"sections\":");
    const size_t text_section_position =
        manifest.find("\"name\":\".text", sections_position);
    const size_t adata_section_position =
        manifest.find("\"name\":\".adata\"", sections_position);
    const size_t data_section_position =
        manifest.find("\"name\":\".data\"", sections_position);
    const size_t bss_section_position =
        manifest.find("\"name\":\".bss\"", sections_position);
    test::require(text_section_position < adata_section_position &&
                      adata_section_position < data_section_position &&
                      data_section_position < bss_section_position,
                  "sections sorted by address then name");
    test::require(
        manifest.find("\"stored_size\":0,\"decompressed_size\":32",
                      bss_section_position) != std::string::npos,
        "NOBITS reports zero stored and logical decompressed size");
    test::require(manifest.find(image.sha256) != std::string::npos,
                  "lowercase SHA-256");
    test::require(
        manifest.find("product\\\"\\\\\\n\\t\\u0001") !=
            std::string::npos,
        "JSON string escaping");
    test::require(manifest.find("OSReport\\\"\\\\\\n\\t\\u0001") !=
                      std::string::npos,
                  "import uses symbol data");
    const size_t imports_position = manifest.find("\"imports\":");
    test::require(manifest.find("\"imports\":[{\"name\":\"coreinit\"") !=
                      std::string::npos,
                  "import module uses name field");
    const size_t exports_position = manifest.find("\"exports\":");
    const size_t os_report_position =
        manifest.find("OSReport\\\"\\\\\\n\\t\\u0001", imports_position);
    const size_t mem_free_position =
        manifest.find("\"name\":\"MemFree\"", imports_position);
    test::require(os_report_position < mem_free_position &&
                      mem_free_position < exports_position,
                  "symbol references sorted by semantic address");
    test::require(manifest.find("\"name\":\"Exported\"") !=
                      std::string::npos,
                  "export uses symbol data");
    test::require(
        manifest.find("\"name\":\"Exported\",\"kind\":\"function\"") !=
            std::string::npos &&
            manifest.find("\"name\":\"GlobalB\",\"kind\":\"object\"") !=
                std::string::npos,
        "exports include ELF symbol kind");
    test::require(manifest.find("\"target_address\":\"0xC0000FFC\"") !=
                      std::string::npos,
                  "relocation uses target data");
    const size_t relocations_position = manifest.find("\"relocations\":");
    const size_t alloc_relocation =
        manifest.find("\"target_address\":\"0xC0002000\"",
                      relocations_position);
    const size_t os_report_relocation =
        manifest.find("\"target_address\":\"0xC0000FFC\"",
                      relocations_position);
    const size_t mem_free_relocation =
        manifest.find("\"target_address\":\"0xC0003008\"",
                      relocations_position);
    test::require(alloc_relocation < os_report_relocation &&
                      os_report_relocation < mem_free_relocation,
                  "relocations sorted by source address then symbol name");

    const size_t first_function =
        manifest.find("\"start\":\"0x02000000\"");
    const size_t later_function =
        manifest.find("\"start\":\"0x02000020\"");
    test::require(first_function != std::string::npos &&
                      later_function != std::string::npos &&
                      first_function < later_function,
                  "functions sorted by address");

    const std::string schema = read_text(NWIIU_ANALYZER_SCHEMA_PATH);
    test::require(
        schema.find("\"executable_format\": { \"const\": \"RPX\" }") !=
                std::string::npos &&
            schema.find("\"architecture\": { \"const\": \"PowerPC\" }") !=
                std::string::npos &&
            schema.find("\"elf_class\": { \"const\": \"ELF32\" }") !=
                std::string::npos &&
            schema.find("\"endianness\": { \"const\": \"big\" }") !=
                std::string::npos &&
            schema.find("\"abi\": { \"const\": \"Cafe OS\" }") !=
                std::string::npos,
        "schema fixes RPX format fields");
    test::require(schema.find("\"export\": {") != std::string::npos &&
                      schema.find("\"kind\"") != std::string::npos &&
                      schema.find("\"additionalProperties\": false") !=
                          std::string::npos,
                  "schema strictly requires export kind");

    test::TempDir temp;
    const auto output = temp.path() / "missing" / "manifest.json";
    test::require_throws(
        [&] { nwiiu::analyzer::write_manifest_file(output, image, analysis); },
        "manifest", "failed output reports error");
    test::require(!std::filesystem::exists(output),
                  "failed output leaves no final file");
    auto temporary = output;
    temporary += ".tmp";
    test::require(!std::filesystem::exists(temporary),
                  "failed output removes temporary file");

    const auto preserved_output = temp.path() / "manifest.json";
    {
        std::ofstream preserved(preserved_output, std::ios::binary);
        preserved << "pre-existing manifest";
    }
    auto invalid_image = image;
    invalid_image.exports.push_back(
        {std::numeric_limits<uint32_t>::max()});
    test::require_throws(
        [&] {
            nwiiu::analyzer::write_manifest_file(preserved_output,
                                                  invalid_image, analysis);
        },
        "", "serialization failure reports error");
    test::require(read_text(preserved_output) == "pre-existing manifest",
                  "serialization failure preserves final file");
    auto preserved_temporary = preserved_output;
    preserved_temporary += ".tmp";
    test::require(!std::filesystem::exists(preserved_temporary),
                  "serialization failure removes temporary file");
}
