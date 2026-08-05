#pragma once

#include "nwiiu/recomp/shader_types.h"

#include <filesystem>
#include <functional>
#include <string_view>

namespace nwiiu::recomp {

// Returns true and fills `out` when `data` is a valid Yaz0 stream. Returns
// false on a bad magic or truncated stream -- never throws, because a corpus
// walk must survive one damaged archive.
bool yaz0_decompress(std::span<const uint8_t> data, std::vector<uint8_t>& out);

// Offsets index into the span passed in, so callers subspan without copying.
struct SarcEntry {
    std::string name;
    size_t offset{};
    size_t size{};
};

// False only when the input is not a SARC. Nodes with impossible ranges are
// skipped, so one bad node does not lose the archive.
bool sarc_entries(std::span<const uint8_t> data, std::vector<SarcEntry>& out);

// Each returns false only when the blob is not that container at all.
bool parse_gfd(std::span<const uint8_t> data, std::string_view origin,
               std::vector<RawShader>& out);
bool parse_sharcfb(std::span<const uint8_t> data, std::string_view origin,
                   std::vector<RawShader>& out);

struct WalkStats {
    size_t files{};
    size_t containers{};
    size_t shaders{};
    size_t skipped{};
    size_t errors{};
};

// Walks a directory tree, recursing through Yaz0 and SARC nesting. A damaged
// archive increments `errors` and is skipped; the walk continues.
WalkStats walk_content(const std::filesystem::path& root,
                       const std::function<void(RawShader&&)>& sink);

}  // namespace nwiiu::recomp
