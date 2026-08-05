#pragma once

#include "runtime/executor.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace nwii::runtime {
// Native replacements for guest routines, addressed by name instead of by
// address. The routine is the portable half — Yaz0 is the same format in every
// Nintendo title — while the address it sits at is per-build, so it comes from
// the `[hle_hooks]` table of a game profile:
//
//     [hle_hooks]
//     "0275F480" = "Yaz0Decode"
//
// A name this table does not know is a load-time error. The alternative is a
// hook that silently never fires, which costs a full rebuild to notice.
NativeThunk find_native_hook(std::string_view name);

// Every name find_native_hook accepts, sorted. `nwiiu-run --list-hooks` prints
// this so a profile author does not have to read the source to learn the set.
std::span<const std::string_view> native_hook_names();

// The hooks WWHD EU v0 needs, as the profile in configs/wwhd-eu-v0.toml states
// them. Kept as the Machine default so that an invocation without a profile
// still boots the one authenticated title exactly as it did before profiles
// existed; pass an explicit map (even an empty one) to opt out.
const std::map<uint32_t, std::string>& default_hle_hooks();
} // namespace nwii::runtime
