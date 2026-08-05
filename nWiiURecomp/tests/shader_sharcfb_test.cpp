#include "nwiiu/recomp/shader_container.h"
#include "test_support.h"

#include <cstring>

namespace {
using namespace nwiiu::recomp;

// One VS whose program sits at a 256-byte boundary, with one attribute record
// and one uniform-var record (stride 20, the stride that matters most).
std::vector<uint8_t> build_sharcfb(bool valid_names) {
    const size_t name_end = 0x18 + 5;
    const size_t D = name_end + 0x18;
    const size_t attrib_table = D + 0x140;
    const size_t var_table = attrib_table + 16;
    const size_t name_pool = var_table + 20;
    const size_t program = 0x300;
    const uint32_t program_size = 0x20;

    std::vector<uint8_t> d(program + program_size, 0);
    std::memcpy(d.data(), "BAHS", 4);
    test::put_le32(d, 0x04, 9);
    test::put_le32(d, 0x08, static_cast<uint32_t>(d.size()));
    test::put_le32(d, 0x0C, 1);
    test::put_le32(d, 0x14, 5);
    std::memcpy(d.data() + 0x18, "test", 4);

    test::put_le32(d, D + 0x00, 0x103);         // SQ_PGM_RESOURCES_VS
    test::put_le32(d, D + 0xD0, program_size);  // shaderSize
    test::put_le32(d, D + 0xD4,
                   static_cast<uint32_t>(program - D));  // shaderPtr, rel to D
    test::put_le32(d, D + 0xD8, 0);             // shaderMode
    test::put_le32(d, D + 0xE4, 1);             // uniformVarCount
    test::put_le32(d, D + 0xE8,
                   static_cast<uint32_t>(var_table - D));  // uniformVarInfo
    test::put_le32(d, D + 0x104, 1);            // attribCount
    test::put_le32(d, D + 0x108,
                   static_cast<uint32_t>(attrib_table - D));  // attribInfo

    const uint32_t attrib_name =
        valid_names ? static_cast<uint32_t>(name_pool - D) : 0x7FFFFFFF;
    test::put_le32(d, attrib_table + 0, attrib_name);
    test::put_le32(d, attrib_table + 4, 10);  // type
    test::put_le32(d, attrib_table + 12, 0);  // location
    std::memcpy(d.data() + name_pool, "aPosition", 9);

    test::put_le32(d, var_table + 0,
                   static_cast<uint32_t>(name_pool + 10 - D));
    test::put_le32(d, var_table + 4, 11);   // type
    test::put_le32(d, var_table + 8, 1);    // array count
    test::put_le32(d, var_table + 12, 92);  // offset
    test::put_le32(d, var_table + 16, 2);   // block index
    std::memcpy(d.data() + name_pool + 10, "cAmbColor", 9);

    test::put_le32(d, program + 0, 0);
    test::put_le32(d, program + 4, 19u << 23);  // CF_INST_CALL_FS
    return d;
}
}  // namespace

int main() {
    std::vector<RawShader> out;

    test::require(
        !parse_sharcfb(std::vector<uint8_t>{'S', 'A', 'R', 'C'}, "x", out),
        "non-SHARC-FB rejected");

    const auto good = build_sharcfb(true);
    test::require(parse_sharcfb(good, "t.sharcfb", out), "SHARC-FB parses");
    test::require(out.size() == 1, "one shader recovered");
    test::require(out[0].stage == Stage::Vertex, "CALL_FS implies vertex");
    test::require(out[0].program.size() == 0x20, "program length");
    test::require(out[0].reflection.attribs.size() == 1, "attribute recovered");
    test::require(out[0].reflection.attribs[0].name == "aPosition",
                  "attribute name resolved");

    // Stride 20 for uniform vars: reading them at stride 16 misparses the
    // table and the descriptor gets rejected entirely.
    test::require(out[0].reflection.vars.size() == 1, "uniform var recovered");
    test::require(out[0].reflection.vars[0].name == "cAmbColor" &&
                      out[0].reflection.vars[0].location == 92 &&
                      out[0].reflection.vars[0].block == 2,
                  "uniform var fields decoded at stride 20");

    // regs must be canonicalised from little-endian to big-endian (1.6), or
    // the same shader hashes differently than its GFD twin.
    test::require(out[0].regs.size() == kVsRegsSize, "regs size");
    test::require(out[0].regs[0] == 0x00 && out[0].regs[3] == 0x03,
                  "regs canonicalised to big-endian");

    // A name offset that does not resolve means this is not a descriptor.
    const auto broken = build_sharcfb(false);
    test::require(parse_sharcfb(broken, "b.sharcfb", out),
                  "broken archive still parses");
    test::require(out.empty(), "unresolvable name rejects the descriptor");
    return 0;
}
