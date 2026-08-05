#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace nwiiu::analyzer {
// One game build. Every gate is optional so that a profile can be as strict or
// as loose as the caller wants: an empty `sha256` accepts any well-formed RPX,
// and a zero `entry_point` adopts whatever the image declares. Profiles come
// from a `.toml` under `configs/` (see game_config.h); the WWHD constant below
// stays as the built-in default so an invocation without `--config` behaves
// exactly as it did when the target was pinned in this header.
struct Target {
    std::string product_code;
    std::string title_id;
    uint32_t title_version{};
    std::string sha256;
    uint32_t entry_point{};
    std::string name;

    // An empty digest means the profile does not authenticate the image. The
    // RPX header checks in load_rpx still apply — this only drops the "is it
    // *that* build" question, which no profile for an undumped game can answer.
    [[nodiscard]] bool verifies_hash() const { return !sha256.empty(); }

    // Address 0 is never a valid Cafe entry point, so it doubles as "unset".
    [[nodiscard]] bool pins_entry_point() const { return entry_point != 0; }
};

// The Wind Waker HD EU v0 (`WUP-P-BCZP`), the first authenticated target and
// the profile every regression test is written against.
inline const Target kWwhdEuV0{
    "WUP-P-BCZP",
    "0005000010143600",
    0,
    "f9f461738949a09481dc1a31c01ad27db813c4c6058fdd7d015624a4146bbf0b",
    0x028EA9E0,
    "The Legend of Zelda: The Wind Waker HD (EU v0)",
};

// Accepts any RPX that parses. Used when no profile is supplied and the caller
// opted out of authentication (`--any-title`), and as the base a `.toml`
// profile fills in. Every field is left empty so that a profile setting none of
// them still gets a name from its project_name.
inline const Target kAnyTitle{};

// The default profile for callers that do not take a `--config`. Kept pinned to
// WWHD rather than kAnyTitle so that dropping the pin is always an explicit
// choice at the command line, never a silent consequence of this refactor.
inline Target resolve_target(std::string_view product_code = "") {
    if (product_code.empty() || product_code == kWwhdEuV0.product_code) {
        return kWwhdEuV0;
    }
    Target target = kAnyTitle;
    target.product_code = std::string(product_code);
    return target;
}
} // namespace nwiiu::analyzer
