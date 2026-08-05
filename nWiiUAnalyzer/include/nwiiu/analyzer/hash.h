#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace nwiiu::analyzer {
std::string sha256(std::span<const uint8_t> bytes);
std::string sha256_file(const std::filesystem::path& path);
}
