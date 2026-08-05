// Opt-in: set NWIIU_WWHD_CONTENT to the game's content directory. Without it
// the test reports "skipped" and passes, so CI never needs game data.

#include "nwiiu/recomp/shader_container.h"
#include "nwiiu/recomp/shader_identity.h"
#include "test_support.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>

namespace {
using namespace nwiiu::recomp;

// Independent oracle: count vertex-program entry points straight from the
// bytes, with no knowledge of any container format.
size_t count_call_fs(const std::filesystem::path& root) {
    size_t total = 0;
    std::error_code error;
    auto it = std::filesystem::recursive_directory_iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) continue;
        if (it->path().extension() != ".sharcfb") continue;
        std::ifstream in(it->path(), std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
        for (size_t o = 0; o + 8 <= bytes.size(); o += 4) {
            if (rd_le32(bytes, o) == 0 &&
                ((rd_le32(bytes, o + 4) >> 23) & 0x7F) == 19) {
                ++total;
            }
        }
    }
    return total;
}
}  // namespace

int main() {
    const char* content = std::getenv("NWIIU_WWHD_CONTENT");
    if (content == nullptr) {
        std::cerr << "skipped: set NWIIU_WWHD_CONTENT to run the corpus gate\n";
        return 0;
    }
    const std::filesystem::path shaders =
        std::filesystem::path(content) / "Common" / "Shaders";
    test::require(std::filesystem::exists(shaders),
                  "NWIIU_WWHD_CONTENT/Common/Shaders must exist");

    std::vector<RawShader> found;
    walk_content(shaders, [&](RawShader&& s) { found.push_back(std::move(s)); });

    size_t vertex = 0;
    size_t pixel = 0;
    for (const auto& shader : found) {
        if (shader.stage == Stage::Vertex) ++vertex;
        if (shader.stage == Stage::Pixel) ++pixel;
    }
    const size_t anchors = count_call_fs(shaders);
    std::cerr << "VS=" << vertex << " PS=" << pixel << " CALL_FS=" << anchors
              << '\n';

    // Gate 2/3: every vertex program the microcode oracle sees is recovered.
    test::require(vertex == anchors, "every CALL_FS program is recovered");
    test::require(vertex == 292 && pixel == 292,
                  "WWHD EU v0 Common/Shaders holds 292 VS and 292 PS");

    // Gate 3: recovered vertex microcode really does start with CALL_FS.
    for (const auto& shader : found) {
        if (shader.stage != Stage::Vertex) continue;
        test::require(shader.program.size() >= 8, "vertex program is non-empty");
        test::require(rd_le32(shader.program, 0) == 0 &&
                          ((rd_le32(shader.program, 4) >> 23) & 0x7F) == 19,
                      "vertex program starts with CALL_FS");
    }

    // Gate 4: identity is deterministic, and equal ids mean equal content.
    // The corpus legitimately repeats shaders across archives, so a distinct
    // count below the shader count is deduplication, not collision -- but that
    // is only true if every id group is byte-identical. Assert the invariant
    // rather than the count, because a genuine collision would silently swap
    // the wrong material at injection time.
    std::map<std::string, const RawShader*> by_id;
    size_t duplicates = 0;
    for (const auto& shader : found) {
        const std::string id =
            compute_program_id(shader.stage, shader.regs, shader.program).hex();
        const auto [entry, inserted] = by_id.emplace(id, &shader);
        if (inserted) continue;
        ++duplicates;
        const RawShader& other = *entry->second;
        test::require(shader.stage == other.stage &&
                          shader.regs == other.regs &&
                          shader.program == other.program,
                      "equal ProgramId implies byte-identical shader");
    }
    for (const auto& shader : found) {
        const std::string id =
            compute_program_id(shader.stage, shader.regs, shader.program).hex();
        test::require(by_id.at(id) != nullptr, "ProgramId is deterministic");
    }
    std::cerr << "distinct ProgramIds: " << by_id.size() << " of "
              << found.size() << " (" << duplicates
              << " exact duplicates)\n";
    return 0;
}
