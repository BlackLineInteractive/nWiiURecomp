#include "nwiiu/recomp/shader_identity.h"

#include <openssl/evp.h>

#include <stdexcept>

namespace nwiiu::recomp {
namespace {

// Domain separation tag plus scheme version. Both are part of the frozen
// public contract: bump the version byte rather than changing the encoding.
constexpr char kTag[] = "NWIIU-GPU7-SHADER";
constexpr uint8_t kSchemeVersion = 0x01;

void absorb(EVP_MD_CTX* ctx, const void* data, size_t size) {
    if (size != 0 && EVP_DigestUpdate(ctx, data, size) != 1) {
        throw std::runtime_error("SHA-256 update failed");
    }
}

void absorb_u32le(EVP_MD_CTX* ctx, uint32_t value) {
    const std::array<uint8_t, 4> bytes{
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    absorb(ctx, bytes.data(), bytes.size());
}

}  // namespace

std::string ProgramId::hex() const {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
    return out;
}

uint64_t ProgramId::prefix64() const noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) value = (value << 8) | bytes[i];
    return value;
}

ProgramId compute_program_id(Stage stage, std::span<const uint8_t> regs,
                             std::span<const uint8_t> program) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) throw std::runtime_error("cannot allocate digest");
    try {
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("SHA-256 init failed");
        }
        absorb(ctx, kTag, sizeof(kTag) - 1);
        absorb(ctx, &kSchemeVersion, 1);
        const auto stage_byte = static_cast<uint8_t>(stage);
        absorb(ctx, &stage_byte, 1);
        absorb_u32le(ctx, static_cast<uint32_t>(regs.size()));
        absorb(ctx, regs.data(), regs.size());
        absorb_u32le(ctx, static_cast<uint32_t>(program.size()));
        absorb(ctx, program.data(), program.size());

        std::array<uint8_t, EVP_MAX_MD_SIZE> digest{};
        unsigned int length = 0;
        if (EVP_DigestFinal_ex(ctx, digest.data(), &length) != 1) {
            throw std::runtime_error("SHA-256 final failed");
        }
        EVP_MD_CTX_free(ctx);

        ProgramId id;
        for (size_t i = 0; i < id.bytes.size(); ++i) id.bytes[i] = digest[i];
        return id;
    } catch (...) {
        EVP_MD_CTX_free(ctx);
        throw;
    }
}

}  // namespace nwiiu::recomp
