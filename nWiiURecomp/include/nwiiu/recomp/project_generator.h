#pragma once

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

ProjectSummary generate_native_project(
    const analyzer::RpxImage& image, const analyzer::Analysis& analysis,
    const std::filesystem::path& output_directory);
} // namespace nwiiu::recomp
