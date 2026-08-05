#include "nwiiu/analyzer/hash.h"

#include <array>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <stdexcept>

namespace nwiiu::analyzer {
namespace {
using DigestContext =
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

DigestContext start_sha256() {
    DigestContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context ||
        EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 failure");
    }
    return context;
}

void update_sha256(EVP_MD_CTX* context, std::span<const uint8_t> bytes) {
    if (!bytes.empty() &&
        EVP_DigestUpdate(context, bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("SHA-256 failure");
    }
}

std::string finish_sha256(EVP_MD_CTX* context) {
    std::array<unsigned char, 32> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1 ||
        digest_size != digest.size()) {
        throw std::runtime_error("SHA-256 failure");
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}
}

std::string sha256(std::span<const uint8_t> bytes) {
    auto context = start_sha256();
    update_sha256(context.get(), bytes);
    return finish_sha256(context.get());
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open input: " + path.string());
    }

    auto context = start_sha256();
    std::array<uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            update_sha256(
                context.get(),
                std::span(buffer.data(), static_cast<std::size_t>(count)));
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("cannot read input: " + path.string());
    }
    return finish_sha256(context.get());
}
}
