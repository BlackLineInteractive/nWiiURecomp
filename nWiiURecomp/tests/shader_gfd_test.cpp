#include "nwiiu/recomp/shader_container.h"
#include "test_support.h"

#include <cstring>

namespace {
using namespace nwiiu::recomp;

void put_block(std::vector<uint8_t>& d, size_t at, uint32_t type,
               uint32_t data_size) {
    std::memcpy(d.data() + at, "BLK{", 4);
    test::put_be32(d, at + 4, 0x20);
    test::put_be32(d, at + 8, 1);
    test::put_be32(d, at + 16, type);
    test::put_be32(d, at + 20, data_size);
}

// Mirrors the retail primitive_renderer_cafe.gsh shape: a VS descriptor block
// followed by its program block.
std::vector<uint8_t> build_gfd(uint32_t declared_size, uint32_t actual_size) {
    const size_t vs_block = 0x20;
    const size_t vs_data = vs_block + 0x20;
    const size_t prog_block = vs_data + kVsDescriptorSize;
    const size_t prog_data = prog_block + 0x20;
    std::vector<uint8_t> d(prog_data + actual_size, 0);

    std::memcpy(d.data(), "Gfx2", 4);
    test::put_be32(d, 4, 0x20);
    test::put_be32(d, 8, 7);
    test::put_be32(d, 12, 1);
    test::put_be32(d, 16, 2);

    put_block(d, vs_block, 3, kVsDescriptorSize);
    test::put_be32(d, vs_data + 0x00, 0x104);          // SQ_PGM_RESOURCES_VS
    test::put_be32(d, vs_data + 0x0C, 1);              // vsOutIdTableSize
    test::put_be32(d, vs_data + 0x40, 2);              // semanticTableSize
    test::put_be32(d, vs_data + 0xD0, declared_size);  // shaderSize
    test::put_be32(d, vs_data + 0xD8, 0);              // shaderMode

    put_block(d, prog_block, 5, actual_size);
    test::put_le32(d, prog_data + 0, 0);
    test::put_le32(d, prog_data + 4, 19u << 23);  // CF_INST_CALL_FS
    return d;
}
}  // namespace

int main() {
    std::vector<RawShader> out;

    test::require(!parse_gfd(std::vector<uint8_t>{1, 2, 3}, "x", out),
                  "non-GFD rejected");

    const auto good = build_gfd(0x40, 0x40);
    test::require(parse_gfd(good, "good.gsh", out), "GFD parses");
    test::require(out.size() == 1, "one shader recovered");
    test::require(out[0].stage == Stage::Vertex, "stage is vertex");
    test::require(out[0].regs.size() == kVsRegsSize, "regs is the 0xD0 prefix");
    test::require(out[0].regs[3] == 0x04, "GFD regs stay big-endian");
    test::require(out[0].program.size() == 0x40, "program length");
    test::require(out[0].origin.find("good.gsh") != std::string::npos,
                  "origin recorded");

    // Self-validation: a descriptor whose shaderSize disagrees with the next
    // block's dataSize is not a descriptor. Accepting it would ship garbage.
    const auto mismatched = build_gfd(0x80, 0x40);
    test::require(parse_gfd(mismatched, "bad.gsh", out),
                  "mismatched GFD still parses");
    test::require(out.empty(), "size mismatch rejects the pair");
    return 0;
}
