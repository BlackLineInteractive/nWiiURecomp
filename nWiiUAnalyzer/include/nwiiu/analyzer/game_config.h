#pragma once
#include "nwiiu/analyzer/target.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace nwiiu::analyzer {
// Everything that used to be a constant in the tree, per game. The file format
// is the one NWiiRecomp uses (`project_name`, `output_dir`, `[hle_hooks]`), so
// a profile written for either project reads the same way:
//
//     project_name = "wwhd"
//     output_dir   = "export/wwhd"
//
//     [target]
//     product_code = "WUP-P-BCZP"
//     sha256       = "f9f4..."     # omit to accept any build
//     entry_point  = 0x028EA9E0    # omit to adopt the image's own
//
//     [hle_hooks]
//     "0275F480" = "Yaz0Decode"    # guest address -> native replacement
//
// Only `project_name` is required. Anything absent keeps the default below,
// which is the loosest profile that still parses an RPX.
struct GameConfig {
    std::string project_name;
    std::string platform{"WiiU"};
    Target target{kAnyTitle};
    std::filesystem::path input;
    std::filesystem::path output_dir{"export"};
    std::filesystem::path title_root;
    std::filesystem::path save_root;
    // Ghidra's Name,Start,End,Size export (nWiiUAnalyzer/Ghidra/
    // ExportWiiUProfile.java). Recorded so a profile round-trips; nothing in
    // the recompiler reads it yet — the analyzer still recovers functions from
    // the RPX itself.
    std::filesystem::path symbols_csv;
    // Guest address -> the name of a native routine registered by the runtime.
    // Unknown names are a load-time error, not a silent no-op: a typo here
    // costs a 25-minute rebuild to discover otherwise.
    std::map<uint32_t, std::string> hle_hooks;
    // 0 keeps the generator's own default rather than pinning it here.
    uint32_t blocks_per_shard{};
    // Set `target_prefix` in the profile to name the generated CMake targets
    // yourself. WWHD pins "wwhd" because tools/build-wwhd-port.sh and the Cemu
    // patch look for `libwwhd-module.so` by that exact name.
    std::string target_prefix_override;
    std::filesystem::path source_path;

    // `target_prefix_override` if set, else `project_name` reduced to the
    // characters CMake targets and C++ prefixes tolerate: lowercase
    // alphanumerics and dashes. "Wind Waker HD" -> "wind-waker-hd".
    [[nodiscard]] std::string target_prefix() const;
};

// Both throw std::runtime_error with a line number on malformed input; the CLIs
// catch at main and map that to their usage exit code.
GameConfig parse_game_config(std::string_view text,
                             const std::filesystem::path& origin = {});
GameConfig load_game_config(const std::filesystem::path& path);
} // namespace nwiiu::analyzer
