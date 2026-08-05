#include "nwiiu/recomp/recompile_cli.h"

#include "nwiiu/analyzer/hash.h"
#include "nwiiu/analyzer/target.h"
#include "test_support.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view kUsage =
    "usage: nwiiu-recompile [--config <profile.toml>] "
    "<game.rpx> <output-directory>\n"
    "       nwiiu-recompile shader-extract --content <dir> --out <file.raw>\n"
    "       nwiiu-recompile shader-inspect --in <file.raw> [--id <hex>]\n";

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string quote(const std::filesystem::path& path) {
    return '"' + path.string() + '"';
}

int configure(const std::filesystem::path& build,
              std::string_view generated_program, std::string_view suffix,
              std::filesystem::path& log) {
    log = build.parent_path() / ("configure-" + std::string(suffix) + ".log");
    std::string command = quote(NWIIU_CMAKE_COMMAND) + " -S " +
                          quote(NWIIU_SOURCE_DIR) + " -B " + quote(build);
    if (!generated_program.empty()) {
        command += " -DNWIIU_GENERATED_PROGRAM=" +
                   quote(std::filesystem::path(generated_program));
    }
    command += " >" + quote(log) + " 2>&1";
    return std::system(command.c_str());
}

void require_unchanged(const std::filesystem::path& output) {
    test::require(std::filesystem::is_directory(output),
                  "invalid input preserves output directory");
    test::require(read_text(output / "sentinel.txt") == "keep",
                  "invalid input preserves output contents");
    test::require(std::distance(std::filesystem::directory_iterator(output),
                               std::filesystem::directory_iterator{}) == 1,
                  "invalid input creates no output files");
}
} // namespace

int main() {
    test::TempDir temp;
    const auto fixture = temp.path() / "fixture.rpx";
    const auto bytes = test::build_test_rpx();
    test::write_bytes(fixture, bytes);
    const std::string fixture_argument = fixture.string();

    const auto protected_output = temp.path() / "protected";
    std::filesystem::create_directory(protected_output);
    std::ofstream(protected_output / "sentinel.txt") << "keep";
    const std::string protected_output_argument = protected_output.string();

    {
        std::ostringstream out;
        std::ostringstream error;
        const std::array<std::string_view, 0> arguments{};
        test::require(nwiiu::recomp::run_recompile_cli(
                          arguments, nwiiu::recomp::builtin_profile(), out, error) ==
                          2,
                      "no arguments returns usage error");
        test::require(out.str().empty(), "no arguments has no stdout");
        test::require(error.str() == kUsage, "no arguments exact usage");
        require_unchanged(protected_output);
    }

    {
        std::ostringstream out;
        std::ostringstream error;
        const std::array<std::string_view, 3> arguments{
            fixture_argument, protected_output_argument, "extra"};
        test::require(nwiiu::recomp::run_recompile_cli(
                          arguments, nwiiu::recomp::builtin_profile(), out, error) ==
                          2,
                      "extra arguments returns usage error");
        test::require(out.str().empty(), "extra arguments has no stdout");
        test::require(error.str() == kUsage, "extra arguments exact usage");
        require_unchanged(protected_output);
    }

    {
        const auto bad = temp.path() / "bad.rpx";
        const std::array<std::byte, 4> bad_bytes{};
        test::write_bytes(bad, bad_bytes);
        const std::string bad_argument = bad.string();
        std::ostringstream out;
        std::ostringstream error;
        const std::array<std::string_view, 2> arguments{
            bad_argument, protected_output_argument};
        test::require(nwiiu::recomp::run_recompile_cli(
                          arguments, nwiiu::recomp::builtin_profile(), out, error) ==
                          2,
                      "malformed RPX returns input error");
        test::require(out.str().empty(), "malformed RPX has no stdout");
        test::require(error.str() ==
                          "nwiiu-recompile: RPX validation error\n",
                      "malformed RPX exact diagnostic: " + error.str());
        require_unchanged(protected_output);
    }

    {
        std::ostringstream out;
        std::ostringstream error;
        const std::array<std::string_view, 2> arguments{
            fixture_argument, protected_output_argument};
        test::require(nwiiu::recomp::run_recompile_cli(
                          arguments, nwiiu::recomp::builtin_profile(), out, error) ==
                          2,
                      "production target rejects hash-mismatched RPX");
        test::require(out.str().empty(), "hash mismatch has no stdout");
        test::require(error.str() ==
                          "nwiiu-recompile: RPX validation error\n",
                      "hash mismatch exact diagnostic");
        require_unchanged(protected_output);
    }

    const std::string hash = nwiiu::analyzer::sha256_file(fixture);
    // A profile for the synthesized fixture: the same shape a real game gets,
    // pinned to this build so the loader still authenticates something.
    nwiiu::analyzer::GameConfig fixture_profile;
    fixture_profile.project_name = "Fixture";
    fixture_profile.target_prefix_override = "wwhd";
    fixture_profile.target = {"fixture", "fixture", 0, hash, 0x02000000,
                              "fixture title"};
    const auto generated = temp.path() / "generated";
    const std::string generated_argument = generated.string();
    {
        std::ostringstream out;
        std::ostringstream error;
        const std::array<std::string_view, 2> arguments{
            fixture_argument, generated_argument};
        test::require(nwiiu::recomp::run_recompile_cli(
                          arguments, fixture_profile, out, error) == 0,
                      "valid fixture recompiles");
        test::require(error.str().empty(), "valid fixture has no stderr");
        test::require(out.str() ==
                          "Title: fixture title\nRPX: " + hash +
                              "\nTargets: wwhd-native, wwhd-module"
                              "\nBlocks: 6\nInstructions: 6\nShards: 1\n",
                      "valid fixture deterministic summary: " + out.str());
        test::require(std::filesystem::is_regular_file(generated /
                                                       "program.cmake"),
                      "valid fixture emits project");
    }

    std::filesystem::path log;
    test::require(configure(temp.path() / "build-unset", {}, "unset", log) == 0,
                  "root configures without generated program");

    test::require(configure(temp.path() / "build-relative",
                            "relative-generated", "relative", log) != 0,
                  "root rejects relative generated program");
    test::require(read_text(log).find(
                      "NWIIU_GENERATED_PROGRAM must be an absolute path") !=
                      std::string::npos,
                  "relative generated program diagnostic");

    const auto missing = temp.path() / "missing";
    test::require(configure(temp.path() / "build-missing", missing.string(),
                            "missing", log) != 0,
                  "root rejects missing generated program");
    test::require(read_text(log).find(
                      "NWIIU_GENERATED_PROGRAM does not contain program.cmake") !=
                      std::string::npos,
                  "missing generated program diagnostic");

    test::require(configure(temp.path() / "build-generated", generated.string(),
                            "generated", log) == 0,
                  "root includes generated program");
}
