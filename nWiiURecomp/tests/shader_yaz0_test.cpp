#include "nwiiu/recomp/shader_container.h"
#include "test_support.h"

#include <cstring>
#include <string>

int main() {
    using namespace nwiiu::recomp;

    // Bounds-checked readers: container input is untrusted and a straddling
    // read must be reported, never performed.
    const std::vector<uint8_t> four{0x01, 0x02, 0x03, 0x04};
    test::require(rd_be32(four, 0) == 0x01020304u, "big-endian read");
    test::require(rd_le32(four, 0) == 0x04030201u, "little-endian read");
    uint32_t sink = 0;
    test::require(!try_rd_be32(four, 1, sink), "straddling read rejected");

    std::vector<uint8_t> swapped{0x00, 0x00, 0x01, 0x03};
    swap_words_in_place(swapped);
    test::require(swapped[0] == 0x03 && swapped[3] == 0x00,
                  "word swap canonicalises endianness");

    std::vector<uint8_t> out;
    const std::vector<uint8_t> not_yaz0{'S', 'A', 'R', 'C', 0, 0, 0, 0};
    test::require(!yaz0_decompress(not_yaz0, out), "bad magic rejected");

    // Eight literals: one 0xFF control byte then the payload.
    std::vector<uint8_t> literals(0x10 + 1 + 8, 0);
    std::memcpy(literals.data(), "Yaz0", 4);
    test::put_be32(literals, 4, 8);
    literals[0x10] = 0xFF;
    for (int i = 0; i < 8; ++i) {
        literals[0x11 + static_cast<size_t>(i)] =
            static_cast<uint8_t>('a' + i);
    }
    test::require(yaz0_decompress(literals, out), "literal run decodes");
    test::require(std::string(out.begin(), out.end()) == "abcdefgh",
                  "literal run content");

    // Emit 'x', then copy 3 bytes from distance 0. The copy overlaps the write
    // cursor, so a memcpy-based implementation would produce garbage here.
    std::vector<uint8_t> backref(0x10 + 1 + 1 + 2, 0);
    std::memcpy(backref.data(), "Yaz0", 4);
    test::put_be32(backref, 4, 4);
    backref[0x10] = 0x80;
    backref[0x11] = 'x';
    backref[0x12] = 0x10;  // n = 1 -> count 3
    backref[0x13] = 0x00;  // dist = 0 -> source = size - 1
    test::require(yaz0_decompress(backref, out), "back-reference decodes");
    test::require(std::string(out.begin(), out.end()) == "xxxx",
                  "overlapping copy expands correctly");

    // A stream that promises more than it delivers is rejected, not truncated.
    std::vector<uint8_t> truncated(0x10 + 1, 0);
    std::memcpy(truncated.data(), "Yaz0", 4);
    test::put_be32(truncated, 4, 64);
    truncated[0x10] = 0xFF;
    test::require(!yaz0_decompress(truncated, out), "truncated stream rejected");
    return 0;
}
