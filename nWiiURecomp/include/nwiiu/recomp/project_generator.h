#pragma once

#include "nwiiu/analyzer/game_config.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace nwiiu::analyzer {
struct Analysis;
struct RpxImage;
} // namespace nwiiu::analyzer

namespace nwiiu::recomp {
struct ProjectSummary {
    uint64_t block_count{};
    uint64_t instruction_count{};
    uint64_t shard_count{};
    std::vector<std::filesystem::path> emitted_files;
};

// The profile decides what the emitted project is called (`<prefix>-native`,
// `<prefix>-module`), which title id the module reports to the host, and which
// image the generated runner will accept. Everything it contributes is baked
// into the generated sources, so the built program never reads the .toml.
ProjectSummary generate_native_project(
    const analyzer::RpxImage& image, const analyzer::Analysis& analysis,
    const std::filesystem::path& output_directory,
    const analyzer::GameConfig& config);
} // namespace nwiiu::recomp
