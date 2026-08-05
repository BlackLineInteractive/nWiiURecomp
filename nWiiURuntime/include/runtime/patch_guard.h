#pragma once

#include <cstdint>

// A host may rewrite guest instruction words at runtime (Cemu's GamePatch does
// this for several titles). The recompiled blocks bake the pre-patch words, so
// any block covering a patched address must be refused and left to the host.
//
// Verifying every word of every block would cost a host read per instruction.
// Instead the host hands over the patched addresses once, and each block tests
// its own range. The list is empty for an unpatched title, which is why the
// fast path is inline and reduces to a single load-and-compare.
namespace nwii::runtime {
extern const uint32_t* g_patched_addresses;
extern uint32_t g_patched_count;
// Bracket of the published patch set. The list is tiny and clustered, so a
// block outside [lo, hi] cannot overlap it and needs no search.
extern uint32_t g_patch_lo;
extern uint32_t g_patch_hi;
// Diagnostic: NWIIU_DISABLE_LO/HI defer every block overlapping [lo, hi), which
// allows bisecting a misbehaving block at runtime instead of per rebuild.
extern bool g_disable_range_active;
// Blocks holding loader-relocated words are deferred by default because
// enabling them breaks rendering; NWIIU_RELOC_BLOCKS re-enables them so the
// defect can be reproduced without regenerating the module.
extern const bool g_reloc_blocks_enabled;

void set_patched_addresses(const uint32_t* addresses, uint32_t count);

bool range_has_patch_slow(uint32_t start, uint32_t instructions);

inline bool range_has_patch(uint32_t start, uint32_t instructions) {
    if (!g_disable_range_active) {
        if (g_patched_count == 0) {
            return false;
        }
        // One compare per block instead of a call and a binary search. The
        // title publishes five patched words in one cluster, so every other
        // block, which is all of them bar a handful, rejects inline here.
        const uint64_t end =
            static_cast<uint64_t>(start) + static_cast<uint64_t>(instructions) * 4;
        if (end <= static_cast<uint64_t>(g_patch_lo) || start > g_patch_hi) {
            return false;
        }
    }
    return range_has_patch_slow(start, instructions);
}
}
