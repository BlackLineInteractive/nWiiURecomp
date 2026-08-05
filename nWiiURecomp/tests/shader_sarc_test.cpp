#include "nwiiu/recomp/shader_container.h"
#include "test_support.h"

#include <cstring>
#include <string>

namespace {
// A minimal big-endian SARC holding two named files.
std::vector<uint8_t> build_sarc() {
    const std::string a = "hello";
    const std::string b = "world!!";
    const std::string names =
        std::string("a.bin\0\0\0", 8) + std::string("b.bin\0\0\0", 8);
    const size_t sfat = 0x14;
    const size_t sfnt = sfat + 0x0C + 2 * 16;
    const size_t name_pool = sfnt + 8;
    const size_t data_start = name_pool + names.size();

    std::vector<uint8_t> d(data_start + a.size() + b.size(), 0);
    std::memcpy(d.data(), "SARC", 4);
    test::put_be16(d, 4, 0x14);
    test::put_be16(d, 6, 0xFEFF);
    test::put_be32(d, 8, static_cast<uint32_t>(d.size()));
    test::put_be32(d, 12, static_cast<uint32_t>(data_start));
    test::put_be32(d, 16, 0x0100);

    std::memcpy(d.data() + sfat, "SFAT", 4);
    test::put_be16(d, sfat + 4, 0x0C);
    test::put_be16(d, sfat + 6, 2);
    test::put_be32(d, sfat + 8, 0x65);

    // node 0 -> a.bin (name index 0); node 1 -> b.bin (name index 2 = 8/4)
    test::put_be32(d, sfat + 0x0C + 4, 0x01000000);
    test::put_be32(d, sfat + 0x0C + 8, 0);
    test::put_be32(d, sfat + 0x0C + 12, static_cast<uint32_t>(a.size()));
    test::put_be32(d, sfat + 0x0C + 16 + 4, 0x01000002);
    test::put_be32(d, sfat + 0x0C + 16 + 8, static_cast<uint32_t>(a.size()));
    test::put_be32(d, sfat + 0x0C + 16 + 12,
                   static_cast<uint32_t>(a.size() + b.size()));

    std::memcpy(d.data() + sfnt, "SFNT", 4);
    test::put_be16(d, sfnt + 4, 8);
    std::memcpy(d.data() + name_pool, names.data(), names.size());
    std::memcpy(d.data() + data_start, a.data(), a.size());
    std::memcpy(d.data() + data_start + a.size(), b.data(), b.size());
    return d;
}
}  // namespace

int main() {
    using namespace nwiiu::recomp;
    std::vector<SarcEntry> entries;

    const std::vector<uint8_t> bad{'Y', 'a', 'z', '0', 0, 0, 0, 0};
    test::require(!sarc_entries(bad, entries), "non-SARC rejected");

    const auto archive = build_sarc();
    test::require(sarc_entries(archive, entries), "SARC parses");
    test::require(entries.size() == 2, "two entries");
    test::require(entries[0].name == "a.bin", "first name");
    test::require(entries[1].name == "b.bin", "second name");
    test::require(entries[0].size == 5 && entries[1].size == 7, "entry sizes");
    const std::string first(
        reinterpret_cast<const char*>(archive.data() + entries[0].offset),
        entries[0].size);
    test::require(first == "hello", "first payload");

    // A node whose range runs past the end is dropped, not trusted.
    auto corrupt = archive;
    test::put_be32(corrupt, 0x14 + 0x0C + 12, 0xFFFFFF);
    test::require(sarc_entries(corrupt, entries), "corrupt archive still parses");
    test::require(entries.size() == 1, "out-of-range node dropped");
    return 0;
}
