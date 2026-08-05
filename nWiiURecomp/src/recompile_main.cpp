#include "nwiiu/recomp/recompile_cli.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return nwiiu::recomp::run_recompile_cli(
        arguments, nwiiu::recomp::builtin_profile(), std::cout, std::cerr);
}
