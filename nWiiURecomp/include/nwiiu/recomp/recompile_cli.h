#pragma once

#include "nwiiu/analyzer/game_config.h"

#include <iosfwd>
#include <span>
#include <string_view>

namespace nwiiu::recomp {
// `fallback` supplies the profile when the arguments carry no `--config`. It is
// the built-in WWHD profile for both CLIs, which keeps a bare invocation
// behaving as it did before profiles existed.
int run_recompile_cli(std::span<const std::string_view> arguments,
                      const analyzer::GameConfig& fallback,
                      std::ostream& output, std::ostream& error);

// The profile the CLIs fall back to: WWHD's target and hooks, named "wwhd" so
// the generated targets keep the names the port scripts expect.
const analyzer::GameConfig& builtin_profile();
} // namespace nwiiu::recomp
