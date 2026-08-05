#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/analyzer/game_config.h"
#include "nwiiu/analyzer/manifest.h"
#include "nwiiu/analyzer/rpx.h"
#include "nwiiu/analyzer/target.h"

#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr std::string_view kUsage =
    "usage: nwiiu-analyze [--config <profile.toml>] [--any-title] "
    "<game.rpx> <manifest.json>\n";
constexpr std::string_view kConfigPrefix = "--config=";
} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> positional;
    std::string_view config_path;
    bool any_title = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--config") {
            if (++index == argc) {
                std::cerr << kUsage;
                return 2;
            }
            config_path = argv[index];
        } else if (argument.starts_with(kConfigPrefix)) {
            config_path = argument.substr(kConfigPrefix.size());
        } else if (argument == "--any-title") {
            any_title = true;
        } else if (argument.starts_with("--")) {
            std::cerr << kUsage;
            return 2;
        } else {
            positional.push_back(argument);
        }
    }
    // --any-title is the no-profile escape hatch; with a profile in hand the
    // profile decides, so accepting both would leave the stricter one silently
    // ignored.
    if (positional.size() != 2 || (any_title && !config_path.empty())) {
        std::cerr << kUsage;
        return 2;
    }

    try {
        auto target = nwiiu::analyzer::resolve_target();
        if (!config_path.empty()) {
            target = nwiiu::analyzer::load_game_config(config_path).target;
        } else if (any_title) {
            target = nwiiu::analyzer::kAnyTitle;
        }

        const auto image =
            nwiiu::analyzer::load_rpx(positional[0], std::move(target));
        const auto analysis = nwiiu::analyzer::analyze(image);
        nwiiu::analyzer::write_manifest_file(positional[1], image, analysis);
        std::cout << "Title: "
                  << (image.target.name.empty() ? "unidentified"
                                                : image.target.name)
                  << '\n'
                  << "RPX: " << image.sha256 << '\n'
                  << "Entry: 0x" << std::uppercase << std::hex
                  << std::setfill('0') << std::setw(8) << image.entry_point
                  << std::dec << '\n'
                  << "Sections: " << image.sections.size() << '\n'
                  << "Imports: " << image.imports.size() << '\n'
                  << "Relocations: " << image.relocations.size() << '\n'
                  << "Functions: " << analysis.functions.size() << '\n'
                  << "Unresolved: " << analysis.unresolved.size() << '\n';
        return analysis.unresolved.empty() ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "nwiiu-analyze: " << error.what() << '\n';
        return 2;
    }
}
