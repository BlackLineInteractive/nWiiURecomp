#include "nwiiu/recomp/shader_container.h"

#include <map>
#include <string>

namespace nwiiu::recomp {
namespace {

// Reflection record strides differ per kind (section 1.5). Using one stride
// for all of them silently rejects every shader that declares uniforms.
constexpr size_t kBlockStride = 12;
constexpr size_t kVarStride = 20;
constexpr size_t kSamplerStride = 12;
constexpr size_t kAttribStride = 16;

constexpr size_t kProgramAlignment = 0x100;
constexpr uint32_t kCfInstCallFs = 19;
constexpr size_t kMaxNameLength = 128;
// Retail shaders reach 171 uniform vars; keep the ceiling well clear of that.
constexpr uint32_t kMaxRecords = 8192;

struct TableSpec {
    size_t count_offset;
    size_t table_offset;
    size_t stride;
};

struct StageSpec {
    Stage stage;
    size_t descriptor_size;
    size_t regs_size;
    size_t size_offset;
    size_t ptr_offset;
    size_t mode_offset;
    TableSpec blocks;
    TableSpec vars;
    TableSpec samplers;
    TableSpec attribs;
    bool has_attribs;
};

constexpr StageSpec kVertexSpec{
    Stage::Vertex,
    kVsDescriptorSize,
    kVsRegsSize,
    0xD0,
    0xD4,
    0xD8,
    {0xDC, 0xE0, kBlockStride},
    {0xE4, 0xE8, kVarStride},
    {0xFC, 0x100, kSamplerStride},
    {0x104, 0x108, kAttribStride},
    true};

constexpr StageSpec kPixelSpec{
    Stage::Pixel,
    kPsDescriptorSize,
    kPsRegsSize,
    0xA4,
    0xA8,
    0xAC,
    {0xB0, 0xB4, kBlockStride},
    {0xB8, 0xBC, kVarStride},
    {0xD0, 0xD4, kSamplerStride},
    {0, 0, 0},
    false};

// Names are offsets relative to the descriptor base. A real descriptor's
// names always resolve; this predicate is what eliminates false positives.
bool read_name(std::span<const uint8_t> d, size_t at, std::string& out) {
    if (at >= d.size()) return false;
    const size_t limit = std::min(d.size(), at + kMaxNameLength);
    size_t stop = at;
    while (stop < limit && d[stop] != 0) {
        if (d[stop] < 0x20 || d[stop] >= 0x7F) return false;
        ++stop;
    }
    if (stop == at || stop == limit) return false;
    out.assign(reinterpret_cast<const char*>(d.data() + at), stop - at);
    return true;
}

bool read_table(std::span<const uint8_t> d, size_t base, const TableSpec& spec,
                std::vector<ShaderVar>& out) {
    uint32_t count = 0;
    uint32_t table = 0;
    if (!try_rd_le32(d, base + spec.count_offset, count)) return false;
    if (!try_rd_le32(d, base + spec.table_offset, table)) return false;
    if (count > kMaxRecords) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t record = base + table + i * spec.stride;
        uint32_t name_offset = 0;
        if (!try_rd_le32(d, record, name_offset)) return false;
        ShaderVar var;
        if (!read_name(d, base + name_offset, var.name)) return false;
        var.type = rd_le32(d, record + 4);
        if (spec.stride >= 12) var.count = rd_le32(d, record + 8);
        if (spec.stride >= 16) var.location = rd_le32(d, record + 12);
        if (spec.stride >= 20) var.block = rd_le32(d, record + 16);
        out.push_back(std::move(var));
    }
    return true;
}

bool try_descriptor(std::span<const uint8_t> d, size_t base,
                    const StageSpec& spec, RawShader& out, size_t& program) {
    if (base + spec.descriptor_size > d.size()) return false;
    uint32_t size = 0;
    uint32_t ptr = 0;
    uint32_t mode = 0;
    if (!try_rd_le32(d, base + spec.size_offset, size)) return false;
    if (!try_rd_le32(d, base + spec.ptr_offset, ptr)) return false;
    if (!try_rd_le32(d, base + spec.mode_offset, mode)) return false;
    if (mode > 3 || size == 0 || (size % 8) != 0 || ptr == 0) return false;

    program = base + ptr;
    if ((program % kProgramAlignment) != 0) return false;
    if (program >= d.size() || size > d.size() - program) return false;

    Reflection reflection;
    if (!read_table(d, base, spec.blocks, reflection.blocks)) return false;
    if (!read_table(d, base, spec.vars, reflection.vars)) return false;
    if (!read_table(d, base, spec.samplers, reflection.samplers)) return false;
    if (spec.has_attribs &&
        !read_table(d, base, spec.attribs, reflection.attribs)) {
        return false;
    }

    // Cross-check against the microcode, which knows nothing about the
    // container: CALL_FS at word 0 means vertex.
    const uint32_t word0 = rd_le32(d, program);
    const uint32_t word1 = rd_le32(d, program + 4);
    const bool is_vertex =
        word0 == 0 && ((word1 >> 23) & 0x7F) == kCfInstCallFs;
    if (is_vertex != (spec.stage == Stage::Vertex)) return false;

    out.stage = spec.stage;
    out.regs.assign(
        d.begin() + static_cast<std::ptrdiff_t>(base),
        d.begin() + static_cast<std::ptrdiff_t>(base + spec.regs_size));
    // SHARC-FB stores registers little-endian; canonicalise to big-endian so
    // the identity hash matches the GFD and runtime representations (1.6).
    swap_words_in_place(out.regs);
    const auto code = d.subspan(program, size);
    out.program.assign(code.begin(), code.end());
    out.reflection = std::move(reflection);
    return true;
}

}  // namespace

bool parse_sharcfb(std::span<const uint8_t> data, std::string_view origin,
                   std::vector<RawShader>& out) {
    out.clear();
    if (data.size() < 0x20) return false;
    if (data[0] != 'B' || data[1] != 'A' || data[2] != 'H' || data[3] != 'S') {
        return false;
    }

    // The per-record chain is not fully mapped, so scan candidate bases and
    // let the acceptance predicate decide. Keyed by program offset so each
    // program is claimed once; the richer reflection wins ties.
    std::map<size_t, RawShader> claimed;
    const size_t limit = data.size() > 0x40 ? data.size() - 0x40 : 0;
    for (size_t base = 0x18; base < limit; ++base) {
        for (const StageSpec* spec : {&kVertexSpec, &kPixelSpec}) {
            RawShader candidate;
            size_t program = 0;
            if (!try_descriptor(data, base, *spec, candidate, program)) {
                continue;
            }
            candidate.origin =
                std::string(origin) + "#" + std::to_string(program);
            const auto existing = claimed.find(program);
            if (existing == claimed.end() ||
                candidate.reflection.total() >
                    existing->second.reflection.total()) {
                claimed[program] = std::move(candidate);
            }
            break;
        }
    }
    out.reserve(claimed.size());
    for (auto& entry : claimed) out.push_back(std::move(entry.second));
    return true;
}

}  // namespace nwiiu::recomp
