#include "nwiiu/recomp/recompile_cli.h"

#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/analyzer/rpx.h"
#include "nwiiu/recomp/project_generator.h"
#include "nwiiu/recomp/shader_container.h"
#include "nwiiu/recomp/shader_corpus.h"
#include "nwiiu/recomp/shader_identity.h"

#include <exception>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace nwiiu::recomp {
namespace {

const char* shader_stage_name(Stage stage) {
    switch (stage) {
        case Stage::Vertex: return "VS";
        case Stage::Geometry: return "GS";
        case Stage::Pixel: return "PS";
        case Stage::Fetch: return "FS";
    }
    return "??";
}

std::string shader_option(std::span<const std::string_view> arguments,
                          std::string_view name) {
    for (size_t i = 1; i + 1 < arguments.size(); ++i) {
        if (arguments[i] == name) return std::string(arguments[i + 1]);
    }
    return {};
}

int run_shader_extract(std::span<const std::string_view> arguments,
                       std::ostream& output, std::ostream& error) {
    const std::string content = shader_option(arguments, "--content");
    const std::string out_path = shader_option(arguments, "--out");
    if (content.empty() || out_path.empty()) {
        error << "usage: nwiiu-recompile shader-extract --content <dir> "
                 "--out <file.raw>\n";
        return 2;
    }
    std::vector<RawShader> shaders;
    const WalkStats stats = walk_content(
        content, [&](RawShader&& s) { shaders.push_back(std::move(s)); });

    size_t vertex = 0;
    size_t pixel = 0;
    for (const auto& shader : shaders) {
        if (shader.stage == Stage::Vertex) ++vertex;
        if (shader.stage == Stage::Pixel) ++pixel;
    }
    output << "files=" << stats.files << " containers=" << stats.containers
           << " shaders=" << shaders.size() << " VS=" << vertex
           << " PS=" << pixel << " skipped=" << stats.skipped
           << " errors=" << stats.errors << '\n';
    if (!store_corpus(out_path, shaders)) {
        error << "error: cannot write " << out_path << '\n';
        return 1;
    }
    return shaders.empty() ? 1 : 0;
}

int run_shader_inspect(std::span<const std::string_view> arguments,
                       std::ostream& output, std::ostream& error) {
    const std::string in_path = shader_option(arguments, "--in");
    const std::string wanted = shader_option(arguments, "--id");
    if (in_path.empty()) {
        error << "usage: nwiiu-recompile shader-inspect --in <file.raw> "
                 "[--id <hex>]\n";
        return 2;
    }
    std::vector<RawShader> shaders;
    if (!load_corpus(in_path, shaders)) {
        error << "error: cannot read " << in_path << '\n';
        return 1;
    }
    for (const auto& shader : shaders) {
        const std::string hex =
            compute_program_id(shader.stage, shader.regs, shader.program).hex();
        if (!wanted.empty() && hex != wanted) continue;
        output << hex << "  " << shader_stage_name(shader.stage) << "  "
               << shader.program.size()
               << "B  vars=" << shader.reflection.vars.size();
        if (!shader.reflection.samplers.empty()) {
            output << "  samplers=[";
            for (size_t i = 0; i < shader.reflection.samplers.size(); ++i) {
                if (i != 0) output << ',';
                output << shader.reflection.samplers[i].name;
            }
            output << ']';
        }
        output << "  " << shader.origin << '\n';
    }
    return 0;
}

}  // namespace

const analyzer::GameConfig& builtin_profile() {
    static const analyzer::GameConfig profile = [] {
        analyzer::GameConfig config;
        config.project_name = "Wind Waker HD";
        config.target_prefix_override = "wwhd";
        config.target = analyzer::kWwhdEuV0;
        // Mirrors configs/wwhd-eu-v0.toml; nwii::runtime::default_hle_hooks()
        // is the runtime-side copy of the same table.
        config.hle_hooks = {{0x0275F480u, "Yaz0Decode"}};
        return config;
    }();
    return profile;
}

int run_recompile_cli(std::span<const std::string_view> arguments,
                      const analyzer::GameConfig& fallback,
                      std::ostream& output, std::ostream& error) {
    // Shader AOT subcommands run before the recompile argument check, which
    // requires exactly two positional arguments.
    if (!arguments.empty() && arguments.front() == "shader-extract") {
        return run_shader_extract(arguments, output, error);
    }
    if (!arguments.empty() && arguments.front() == "shader-inspect") {
        return run_shader_inspect(arguments, output, error);
    }

    std::vector<std::string_view> positional;
    std::string_view config_path;
    for (size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == "--config") {
            if (++index == arguments.size()) {
                positional.clear();
                break;
            }
            config_path = arguments[index];
        } else {
            positional.push_back(arguments[index]);
        }
    }

    if (positional.size() != 2) {
        error << "usage: nwiiu-recompile [--config <profile.toml>] "
                 "<game.rpx> <output-directory>\n"
              << "       nwiiu-recompile shader-extract --content <dir> "
                 "--out <file.raw>\n"
              << "       nwiiu-recompile shader-inspect --in <file.raw> "
                 "[--id <hex>]\n";
        return 2;
    }

    try {
        const analyzer::GameConfig config =
            config_path.empty() ? fallback
                                : analyzer::load_game_config(config_path);
        const auto image = analyzer::load_rpx(positional[0], config.target);
        const auto analysis = analyzer::analyze(image);
        const auto summary =
            generate_native_project(image, analysis, positional[1], config);

        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << "Title: "
             << (image.target.name.empty() ? "unidentified" : image.target.name)
             << '\n'
             << "RPX: " << image.sha256 << '\n'
             << "Targets: " << config.target_prefix() << "-native, "
             << config.target_prefix() << "-module\n"
             << "Blocks: " << summary.block_count << '\n'
             << "Instructions: " << summary.instruction_count << '\n'
             << "Shards: " << summary.shard_count << '\n';
        output << text.str();
        return 0;
    } catch (const std::exception& exception) {
        error << "nwiiu-recompile: " << exception.what() << '\n';
        return 2;
    }
}
} // namespace nwiiu::recomp
