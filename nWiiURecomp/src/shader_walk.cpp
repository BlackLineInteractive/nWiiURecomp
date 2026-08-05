#include "nwiiu/recomp/shader_container.h"

#include <fstream>
#include <iterator>
#include <string>

namespace nwiiu::recomp {
namespace {

// A malformed archive could nest indefinitely; stop rather than recurse away.
constexpr int kMaxDepth = 8;

bool magic_is(std::span<const uint8_t> d, const char (&tag)[5]) {
    if (d.size() < 4) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (d[i] != static_cast<uint8_t>(tag[i])) return false;
    }
    return true;
}

void dispatch(std::span<const uint8_t> data, const std::string& origin,
              int depth, WalkStats& stats,
              const std::function<void(RawShader&&)>& sink) {
    if (depth > kMaxDepth || data.size() < 4) return;

    if (magic_is(data, "Yaz0")) {
        std::vector<uint8_t> plain;
        if (!yaz0_decompress(data, plain)) {
            ++stats.errors;
            return;
        }
        dispatch(plain, origin + "|yaz0", depth + 1, stats, sink);
        return;
    }
    if (magic_is(data, "SARC")) {
        std::vector<SarcEntry> entries;
        if (!sarc_entries(data, entries)) {
            ++stats.errors;
            return;
        }
        for (const auto& entry : entries) {
            dispatch(data.subspan(entry.offset, entry.size),
                     origin + "/" + entry.name, depth + 1, stats, sink);
        }
        return;
    }

    std::vector<RawShader> shaders;
    bool parsed = false;
    if (magic_is(data, "Gfx2")) {
        parsed = parse_gfd(data, origin, shaders);
    } else if (magic_is(data, "BAHS")) {
        parsed = parse_sharcfb(data, origin, shaders);
    }
    if (!parsed) {
        ++stats.skipped;
        return;
    }
    ++stats.containers;
    stats.shaders += shaders.size();
    for (auto& shader : shaders) sink(std::move(shader));
}

}  // namespace

WalkStats walk_content(const std::filesystem::path& root,
                       const std::function<void(RawShader&&)>& sink) {
    WalkStats stats;
    std::error_code error;
    auto it = std::filesystem::recursive_directory_iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) continue;
        ++stats.files;

        std::ifstream input(it->path(), std::ios::binary);
        if (!input) {
            ++stats.errors;
            continue;
        }
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>()};
        std::error_code relative_error;
        dispatch(bytes,
                 std::filesystem::relative(it->path(), root, relative_error)
                     .string(),
                 0, stats, sink);
    }
    if (error) ++stats.errors;
    return stats;
}

}  // namespace nwiiu::recomp
