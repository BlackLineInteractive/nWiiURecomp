#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace nwiiu::analyzer {
struct Target;
}

namespace nwiiu::recomp {
int run_recompile_cli(std::span<const std::string_view> arguments,
                      const analyzer::Target& target, std::ostream& output,
                      std::ostream& error);
}
