#include "nwiiu/recomp/shader_container.h"

namespace nwiiu::recomp {

bool yaz0_decompress(std::span<const uint8_t> data, std::vector<uint8_t>& out) {
    constexpr size_t kHeaderSize = 0x10;
    // Guard the promised size before reserving, so a corrupt header cannot
    // turn into a huge allocation.
    constexpr uint32_t kMaxOutput = 256u * 1024u * 1024u;

    if (data.size() < kHeaderSize) return false;
    if (data[0] != 'Y' || data[1] != 'a' || data[2] != 'z' || data[3] != '0') {
        return false;
    }
    uint32_t expected = 0;
    if (!try_rd_be32(data, 4, expected) || expected > kMaxOutput) return false;

    out.clear();
    out.reserve(expected);

    size_t p = kHeaderSize;
    uint8_t group = 0;
    int remaining_bits = 0;

    while (out.size() < expected) {
        if (remaining_bits == 0) {
            if (p >= data.size()) return false;
            group = data[p++];
            remaining_bits = 8;
        }
        if ((group & 0x80) != 0) {
            if (p >= data.size()) return false;
            out.push_back(data[p++]);
        } else {
            if (p + 1 >= data.size()) return false;
            const uint32_t b1 = data[p];
            const uint32_t b2 = data[p + 1];
            p += 2;
            const size_t distance = ((b1 & 0x0F) << 8) | b2;
            size_t count = b1 >> 4;
            if (count == 0) {
                if (p >= data.size()) return false;
                count = static_cast<size_t>(data[p++]) + 0x12;
            } else {
                count += 2;
            }
            if (distance + 1 > out.size()) return false;
            const size_t start = out.size() - distance - 1;
            // Byte-by-byte: runs legitimately overlap the write cursor.
            for (size_t i = 0; i < count; ++i) out.push_back(out[start + i]);
        }
        group = static_cast<uint8_t>(group << 1);
        --remaining_bits;
    }
    return out.size() == expected;
}

}  // namespace nwiiu::recomp
