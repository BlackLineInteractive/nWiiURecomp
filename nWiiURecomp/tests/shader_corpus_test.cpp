#include "nwiiu/recomp/shader_corpus.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <iterator>

int main() {
    using namespace nwiiu::recomp;
    const auto dir =
        std::filesystem::temp_directory_path() / "nwiiu-shader-corpus-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "corpus.raw";

    RawShader shader;
    shader.stage = Stage::Pixel;
    shader.regs = std::vector<uint8_t>(kPsRegsSize, 0xAB);
    shader.program = std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8};
    shader.origin = "a.sharcfb#256";
    shader.reflection.samplers.push_back(ShaderVar{"tex_map0", 2, 0, 5, 0});

    test::require(store_corpus(path, {shader}), "corpus writes");

    std::vector<RawShader> loaded;
    test::require(load_corpus(path, loaded), "corpus reads back");
    test::require(loaded.size() == 1, "one entry");
    test::require(loaded[0].stage == Stage::Pixel, "stage round-trips");
    test::require(loaded[0].regs == shader.regs, "regs round-trip");
    test::require(loaded[0].program == shader.program, "program round-trips");
    test::require(loaded[0].origin == shader.origin, "origin round-trips");
    test::require(loaded[0].reflection.samplers.size() == 1 &&
                      loaded[0].reflection.samplers[0].name == "tex_map0" &&
                      loaded[0].reflection.samplers[0].location == 5,
                  "reflection round-trips");

    // A truncated corpus degrades to false; it must never throw or hang.
    const auto truncated = dir / "truncated.raw";
    {
        std::ifstream in(path, std::ios::binary);
        const std::vector<char> all{std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>()};
        std::ofstream out(truncated, std::ios::binary);
        out.write(all.data(), static_cast<std::streamsize>(all.size() / 2));
    }
    std::vector<RawShader> partial;
    test::require(!load_corpus(truncated, partial), "truncated corpus rejected");

    std::vector<RawShader> missing;
    test::require(!load_corpus(dir / "nope.raw", missing),
                  "missing corpus rejected");

    std::filesystem::remove_all(dir);
    return 0;
}
