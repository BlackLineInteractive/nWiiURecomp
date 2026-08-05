#pragma once

#include "runtime/executor.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nwiiu::recomp {
struct RunnerOptions {
    std::filesystem::path input;
    std::filesystem::path title_root;
    std::optional<std::filesystem::path> save_root;
    std::optional<std::filesystem::path> shared_font;
    // Absent means the caller's built-in profile decides. The generated runner
    // ignores this: its profile is compiled in.
    std::optional<std::filesystem::path> config;
    uint64_t max_instructions{};
    bool trace{};
    bool window{};
};

RunnerOptions parse_runner_options(std::span<const std::string_view> args);
std::string format_stop(const nwii::runtime::ExecutionStop& stop);
std::string format_trace(const nwii::runtime::ExecutionStop& stop);
} // namespace nwiiu::recomp
