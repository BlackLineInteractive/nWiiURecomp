#include "nwiiu/recomp/shader_corpus.h"

#include <fstream>
#include <istream>
#include <ostream>

namespace nwiiu::recomp {
namespace {

constexpr uint32_t kMagic = 0x4E573752;  // "NW7R"
constexpr uint32_t kVersion = 1;
// Reject absurd lengths before allocating (section 4.5 loader contract).
constexpr uint32_t kMaxBlob = 64u * 1024u * 1024u;
constexpr uint32_t kMaxRecords = 1u << 20;

void put_u32(std::ostream& out, uint32_t value) {
    const char bytes[4]{static_cast<char>(value & 0xFF),
                        static_cast<char>((value >> 8) & 0xFF),
                        static_cast<char>((value >> 16) & 0xFF),
                        static_cast<char>((value >> 24) & 0xFF)};
    out.write(bytes, 4);
}

bool get_u32(std::istream& in, uint32_t& value) {
    unsigned char bytes[4];
    if (!in.read(reinterpret_cast<char*>(bytes), 4)) return false;
    value = static_cast<uint32_t>(bytes[0]) |
            (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) |
            (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

void put_blob(std::ostream& out, const void* data, size_t size) {
    put_u32(out, static_cast<uint32_t>(size));
    if (size != 0) {
        out.write(static_cast<const char*>(data),
                  static_cast<std::streamsize>(size));
    }
}

bool get_bytes(std::istream& in, std::vector<uint8_t>& out) {
    uint32_t size = 0;
    if (!get_u32(in, size) || size > kMaxBlob) return false;
    out.resize(size);
    return size == 0 ||
           static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), size));
}

bool get_string(std::istream& in, std::string& out) {
    uint32_t size = 0;
    if (!get_u32(in, size) || size > kMaxBlob) return false;
    out.resize(size);
    return size == 0 || static_cast<bool>(in.read(out.data(), size));
}

void put_table(std::ostream& out, const std::vector<ShaderVar>& table) {
    put_u32(out, static_cast<uint32_t>(table.size()));
    for (const auto& var : table) {
        put_blob(out, var.name.data(), var.name.size());
        put_u32(out, var.type);
        put_u32(out, var.count);
        put_u32(out, var.location);
        put_u32(out, var.block);
    }
}

bool get_table(std::istream& in, std::vector<ShaderVar>& table) {
    uint32_t count = 0;
    if (!get_u32(in, count) || count > kMaxRecords) return false;
    table.resize(count);
    for (auto& var : table) {
        if (!get_string(in, var.name)) return false;
        if (!get_u32(in, var.type)) return false;
        if (!get_u32(in, var.count)) return false;
        if (!get_u32(in, var.location)) return false;
        if (!get_u32(in, var.block)) return false;
    }
    return true;
}

}  // namespace

bool store_corpus(const std::filesystem::path& path,
                  const std::vector<RawShader>& shaders) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    put_u32(out, kMagic);
    put_u32(out, kVersion);
    put_u32(out, static_cast<uint32_t>(shaders.size()));
    for (const auto& shader : shaders) {
        const auto stage = static_cast<char>(shader.stage);
        out.write(&stage, 1);
        put_blob(out, shader.regs.data(), shader.regs.size());
        put_blob(out, shader.program.data(), shader.program.size());
        put_blob(out, shader.origin.data(), shader.origin.size());
        put_table(out, shader.reflection.blocks);
        put_table(out, shader.reflection.vars);
        put_table(out, shader.reflection.samplers);
        put_table(out, shader.reflection.attribs);
    }
    return static_cast<bool>(out);
}

bool load_corpus(const std::filesystem::path& path,
                 std::vector<RawShader>& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    if (!get_u32(in, magic) || magic != kMagic) return false;
    if (!get_u32(in, version) || version != kVersion) return false;
    if (!get_u32(in, count) || count > kMaxRecords) return false;

    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RawShader shader;
        char stage = 0;
        if (!in.read(&stage, 1)) return false;
        if (stage < 0 || stage > 3) return false;
        shader.stage = static_cast<Stage>(stage);
        if (!get_bytes(in, shader.regs)) return false;
        if (!get_bytes(in, shader.program)) return false;
        if (!get_string(in, shader.origin)) return false;
        if (!get_table(in, shader.reflection.blocks)) return false;
        if (!get_table(in, shader.reflection.vars)) return false;
        if (!get_table(in, shader.reflection.samplers)) return false;
        if (!get_table(in, shader.reflection.attribs)) return false;
        out.push_back(std::move(shader));
    }
    return true;
}

}  // namespace nwiiu::recomp
