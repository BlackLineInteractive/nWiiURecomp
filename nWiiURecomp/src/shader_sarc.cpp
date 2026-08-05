#include "nwiiu/recomp/shader_container.h"

namespace nwiiu::recomp {
namespace {

constexpr uint32_t kNameFlag = 0x01000000;

bool magic_is(std::span<const uint8_t> d, size_t o, const char (&tag)[5]) {
    if (o + 4 > d.size()) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (d[o + i] != static_cast<uint8_t>(tag[i])) return false;
    }
    return true;
}

}  // namespace

bool sarc_entries(std::span<const uint8_t> data, std::vector<SarcEntry>& out) {
    out.clear();
    if (!magic_is(data, 0, "SARC")) return false;
    // WWHD is big-endian; require that BOM rather than guessing.
    if (rd_be16(data, 6) != 0xFEFF) return false;

    const size_t header_len = rd_be16(data, 4);
    uint32_t data_offset = 0;
    if (!try_rd_be32(data, 12, data_offset)) return false;
    if (data_offset > data.size()) return false;

    const size_t sfat = header_len;
    if (!magic_is(data, sfat, "SFAT")) return false;
    const size_t sfat_header_len = rd_be16(data, sfat + 4);
    const size_t node_count = rd_be16(data, sfat + 6);
    const size_t nodes = sfat + sfat_header_len;
    if (nodes + node_count * 16 > data.size()) return false;

    const size_t sfnt = nodes + node_count * 16;
    if (!magic_is(data, sfnt, "SFNT")) return false;
    const size_t name_pool = sfnt + rd_be16(data, sfnt + 4);
    if (name_pool > data.size()) return false;

    out.reserve(node_count);
    for (size_t i = 0; i < node_count; ++i) {
        const size_t node = nodes + i * 16;
        const uint32_t attributes = rd_be32(data, node + 4);
        const uint32_t start = rd_be32(data, node + 8);
        const uint32_t end = rd_be32(data, node + 12);
        if (end < start) continue;
        const size_t begin = data_offset + start;
        const size_t length = end - start;
        if (begin > data.size() || length > data.size() - begin) continue;

        SarcEntry entry;
        entry.offset = begin;
        entry.size = length;
        if ((attributes & kNameFlag) != 0) {
            const size_t at = name_pool + (attributes & 0xFFFFFFu) * 4;
            if (at < data.size()) {
                size_t stop = at;
                while (stop < data.size() && data[stop] != 0) ++stop;
                entry.name.assign(
                    reinterpret_cast<const char*>(data.data() + at), stop - at);
            }
        }
        out.push_back(std::move(entry));
    }
    return true;
}

}  // namespace nwiiu::recomp
