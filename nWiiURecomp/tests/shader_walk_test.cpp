#include "nwiiu/recomp/shader_container.h"
#include "test_support.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
using namespace nwiiu::recomp;

std::vector<uint8_t> build_minimal_gfd() {
    const size_t vs_data = 0x40;
    const size_t prog_block = vs_data + kVsDescriptorSize;
    const size_t prog_data = prog_block + 0x20;
    const uint32_t program_size = 0x20;
    std::vector<uint8_t> d(prog_data + program_size, 0);
    std::memcpy(d.data(), "Gfx2", 4);
    test::put_be32(d, 4, 0x20);
    std::memcpy(d.data() + 0x20, "BLK{", 4);
    test::put_be32(d, 0x24, 0x20);
    test::put_be32(d, 0x30, 3);
    test::put_be32(d, 0x34, kVsDescriptorSize);
    test::put_be32(d, vs_data + 0xD0, program_size);
    std::memcpy(d.data() + prog_block, "BLK{", 4);
    test::put_be32(d, prog_block + 4, 0x20);
    test::put_be32(d, prog_block + 16, 5);
    test::put_be32(d, prog_block + 20, program_size);
    return d;
}

void write_file(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}
}  // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "nwiiu-shader-walk-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");

    write_file(root / "a.gsh", build_minimal_gfd());
    // Extension is irrelevant: dispatch is by magic.
    write_file(root / "nested" / "mislabelled.bin", build_minimal_gfd());
    write_file(root / "junk.dat", std::vector<uint8_t>{1, 2, 3, 4, 5});

    std::vector<RawShader> found;
    const WalkStats stats = walk_content(
        root, [&](RawShader&& shader) { found.push_back(std::move(shader)); });

    test::require(stats.files == 3, "every file visited");
    test::require(stats.containers == 2, "two shader containers found");
    test::require(stats.skipped == 1, "the junk file is skipped, not an error");
    test::require(found.size() == 2, "two shaders recovered");
    test::require(!found[0].origin.empty(), "origin recorded");

    std::filesystem::remove_all(root);
    return 0;
}
