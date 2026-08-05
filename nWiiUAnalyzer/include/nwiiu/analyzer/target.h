#pragma once
#include <cstdint>
#include <string_view>

namespace nwiiu::analyzer {
struct Target {
    std::string_view product_code;
    std::string_view title_id;
    uint32_t title_version;
    std::string_view sha256;
    uint32_t entry_point;
};

inline constexpr Target kWwhdEuV0{
    "WUP-P-BCZP",
    "0005000010143600",
    0,
    "f9f461738949a09481dc1a31c01ad27db813c4c6058fdd7d015624a4146bbf0b",
    0x028EA9E0,
};

// TODO: Expand this to parse from a config file or arguments dynamically
inline Target resolve_target(std::string_view product_code = "") {
    // Currently defaults to Wind Waker HD EU v0 for universality stub
    (void)product_code;
    return kWwhdEuV0;
}
}
