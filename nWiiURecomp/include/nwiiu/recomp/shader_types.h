#pragma once

// Core value types for GX2 shader extraction.

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nwiiu::recomp {

enum class Stage : uint8_t { Vertex = 0, Geometry = 1, Pixel = 2, Fetch = 3 };

// GX2 descriptor sizes, from the static_asserts in
// extern/Cemu/src/Cafe/OS/libs/gx2/GX2_Shader.h:97 (0x134), :139 (0xE8).
inline constexpr size_t kVsDescriptorSize = 0x134;
inline constexpr size_t kPsDescriptorSize = 0x0E8;

// The `regs` sub-blob is the pointer-free prefix of each descriptor, and the
// only part that feeds the identity hash (section 4.3 rule 2).
inline constexpr size_t kVsRegsSize = 0xD0;
inline constexpr size_t kPsRegsSize = 0xA4;

// One reflection record. Field meaning depends on the table it came from:
//   blocks   -> type = binding index, count = block size in bytes
//   vars     -> type, count = array count, location = byte offset, block index
//   samplers -> type, location
//   attribs  -> type, count = array count, location
struct ShaderVar {
    std::string name;
    uint32_t type{};
    uint32_t count{};
    uint32_t location{};
    uint32_t block{};
};

struct Reflection {
    std::vector<ShaderVar> blocks;
    std::vector<ShaderVar> vars;
    std::vector<ShaderVar> samplers;
    std::vector<ShaderVar> attribs;

    [[nodiscard]] size_t total() const {
        return blocks.size() + vars.size() + samplers.size() + attribs.size();
    }
};

// A descriptor plus its microcode. `regs` is CANONICALISED to big-endian on
// ingest (section 1.6): GFD stores it big-endian, SHARC-FB little-endian, and
// the runtime reads it big-endian from guest memory. Without normalisation the
// same shader would hash differently depending on its container.
struct RawShader {
    Stage stage{};
    std::vector<uint8_t> regs;     // canonical big-endian
    std::vector<uint8_t> program;  // little-endian microcode, verbatim
    Reflection reflection;         // populated by SHARC-FB, empty for GFD
    std::string origin;            // "path/to/file.sharcfb#6400"
};

// Bounds-checked readers. A malformed archive must produce a rejected
// candidate, never an out-of-bounds read.
[[nodiscard]] inline bool try_rd_be32(std::span<const uint8_t> d, size_t o,
                                      uint32_t& out) {
    if (o + 4 > d.size()) return false;
    out = (static_cast<uint32_t>(d[o]) << 24) |
          (static_cast<uint32_t>(d[o + 1]) << 16) |
          (static_cast<uint32_t>(d[o + 2]) << 8) |
          static_cast<uint32_t>(d[o + 3]);
    return true;
}

[[nodiscard]] inline bool try_rd_le32(std::span<const uint8_t> d, size_t o,
                                      uint32_t& out) {
    if (o + 4 > d.size()) return false;
    out = static_cast<uint32_t>(d[o]) |
          (static_cast<uint32_t>(d[o + 1]) << 8) |
          (static_cast<uint32_t>(d[o + 2]) << 16) |
          (static_cast<uint32_t>(d[o + 3]) << 24);
    return true;
}

// Saturating forms for call sites that have already validated the range.
[[nodiscard]] inline uint32_t rd_be32(std::span<const uint8_t> d, size_t o) {
    uint32_t v = 0;
    return try_rd_be32(d, o, v) ? v : 0;
}

[[nodiscard]] inline uint32_t rd_le32(std::span<const uint8_t> d, size_t o) {
    uint32_t v = 0;
    return try_rd_le32(d, o, v) ? v : 0;
}

[[nodiscard]] inline uint16_t rd_be16(std::span<const uint8_t> d, size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<uint16_t>((d[o] << 8) | d[o + 1]);
}

[[nodiscard]] inline uint16_t rd_le16(std::span<const uint8_t> d, size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
}

// Byte-swap every 32-bit word in place, to canonicalise SHARC-FB's
// little-endian descriptor registers to big-endian.
inline void swap_words_in_place(std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i + 4 <= bytes.size(); i += 4) {
        std::swap(bytes[i], bytes[i + 3]);
        std::swap(bytes[i + 1], bytes[i + 2]);
    }
}

}  // namespace nwiiu::recomp
