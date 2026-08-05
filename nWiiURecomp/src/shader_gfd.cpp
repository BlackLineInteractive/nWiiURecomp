#include "nwiiu/recomp/shader_container.h"

#include <string>

namespace nwiiu::recomp {
namespace {

struct Block {
    uint32_t type{};
    size_t data_offset{};
    size_t data_size{};
};

bool descriptor_matches(std::span<const uint8_t> payload, Stage stage,
                        size_t next_block_size) {
    const size_t need =
        stage == Stage::Vertex ? kVsDescriptorSize : kPsDescriptorSize;
    if (payload.size() < need) return false;
    const size_t size_offset = stage == Stage::Vertex ? 0xD0 : 0xA4;
    const size_t mode_offset = stage == Stage::Vertex ? 0xD8 : 0xAC;
    if (rd_be32(payload, mode_offset) > 3) return false;
    const uint32_t declared = rd_be32(payload, size_offset);
    return declared != 0 && declared == next_block_size;
}

}  // namespace

bool parse_gfd(std::span<const uint8_t> data, std::string_view origin,
               std::vector<RawShader>& out) {
    out.clear();
    if (data.size() < 0x20) return false;
    if (data[0] != 'G' || data[1] != 'f' || data[2] != 'x' || data[3] != '2') {
        return false;
    }
    const size_t header_size = rd_be32(data, 4);
    if (header_size < 0x20 || header_size > data.size()) return false;

    // Collect the chain first: pairing needs one block of lookahead.
    std::vector<Block> blocks;
    size_t at = header_size;
    while (at + 0x20 <= data.size()) {
        if (data[at] != 'B' || data[at + 1] != 'L' || data[at + 2] != 'K' ||
            data[at + 3] != '{') {
            break;
        }
        const size_t block_header = rd_be32(data, at + 4);
        const uint32_t type = rd_be32(data, at + 16);
        const size_t size = rd_be32(data, at + 20);
        if (block_header < 0x20) break;
        const size_t payload = at + block_header;
        if (payload > data.size() || size > data.size() - payload) break;
        blocks.push_back(Block{type, payload, size});
        if (size == 0 && type == 0) break;
        at = payload + size;
    }

    // Never trust the block-type enum (section 1.3): accept a pair only when
    // the candidate descriptor's own shaderSize equals the next block's
    // dataSize.
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        const auto payload =
            data.subspan(blocks[i].data_offset, blocks[i].data_size);
        const size_t next = blocks[i + 1].data_size;
        if (next == 0) continue;

        for (const Stage stage : {Stage::Vertex, Stage::Pixel}) {
            if (!descriptor_matches(payload, stage, next)) continue;
            RawShader shader;
            shader.stage = stage;
            const size_t regs =
                stage == Stage::Vertex ? kVsRegsSize : kPsRegsSize;
            // GFD descriptors are already big-endian: copy verbatim (1.6).
            shader.regs.assign(
                payload.begin(),
                payload.begin() + static_cast<std::ptrdiff_t>(regs));
            const auto program = data.subspan(blocks[i + 1].data_offset, next);
            shader.program.assign(program.begin(), program.end());
            shader.origin = std::string(origin) + "#" +
                            std::to_string(blocks[i].data_offset);
            out.push_back(std::move(shader));
            break;
        }
    }
    return true;
}

}  // namespace nwiiu::recomp
