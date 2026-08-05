#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace nwiiu::recomp {
std::string generate_native_block(
    std::string_view name, uint32_t start_address,
    std::span<const uint32_t> instructions,
    const std::map<uint32_t, uint32_t>& branch_overrides);
std::string generate_native_function(
    std::string_view name, uint32_t start_address,
    std::span<const uint32_t> instructions,
    const std::map<uint32_t, uint32_t>& branch_overrides);
} // namespace nwiiu::recomp
