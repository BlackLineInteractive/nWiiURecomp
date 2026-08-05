#include "nwiiu/recomp/shader_identity.h"
#include "test_support.h"

int main() {
    using namespace nwiiu::recomp;
    const std::vector<uint8_t> regs{0x00, 0x00, 0x01, 0x04};
    const std::vector<uint8_t> program{0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x80, 0x09};

    const ProgramId a = compute_program_id(Stage::Vertex, regs, program);
    test::require(a == compute_program_id(Stage::Vertex, regs, program),
                  "hash is deterministic");
    test::require(a.hex().size() == 32, "hex is 128 bits");

    // Stage participates: identical bytes in different stages are different
    // shaders (section 4.3 rule 4).
    test::require(!(compute_program_id(Stage::Pixel, regs, program) == a),
                  "stage changes the identity");

    // Length prefixes remove the concatenation ambiguity (4.3 rule 3): moving
    // one byte across the regs/program boundary must change the hash.
    const std::vector<uint8_t> shifted_regs{0x00, 0x00, 0x01};
    const std::vector<uint8_t> shifted_program{
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x09};
    test::require(
        !(compute_program_id(Stage::Vertex, shifted_regs, shifted_program) == a),
        "field boundaries are unambiguous");

    auto altered = program;
    altered[0] = 0x01;
    test::require(!(compute_program_id(Stage::Vertex, regs, altered) == a),
                  "program content changes the identity");

    // prefix64 must be the leading digest bytes, since the runtime looks up by
    // prefix and then verifies the full id.
    test::require((a.prefix64() >> 56) == a.bytes[0],
                  "prefix64 starts at the first digest byte");
    test::require((a.prefix64() & 0xFF) == a.bytes[7],
                  "prefix64 ends at the eighth digest byte");

    static constexpr char kDigits[] = "0123456789abcdef";
    const std::string expected_first{kDigits[a.bytes[0] >> 4],
                                     kDigits[a.bytes[0] & 0x0F]};
    test::require(a.hex().substr(0, 2) == expected_first,
                  "hex matches the digest bytes");
    return 0;
}
