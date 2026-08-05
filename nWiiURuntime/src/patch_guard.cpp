#include "runtime/patch_guard.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace nwii::runtime {
namespace {
std::vector<uint32_t> g_sorted;
}

const uint32_t* g_patched_addresses = nullptr;
uint32_t g_patch_lo = 0xFFFFFFFFu;
uint32_t g_patch_hi = 0u;
uint32_t g_patched_count = 0;
bool g_disable_range_active = false;
extern const bool g_reloc_blocks_enabled =
    std::getenv("NWIIU_RELOC_BLOCKS") != nullptr;

namespace {
uint32_t env_u32(const char* name, uint32_t fallback) {
    const char* raw = std::getenv(name);
    return raw == nullptr ? fallback
                          : static_cast<uint32_t>(std::strtoul(raw, nullptr, 0));
}

struct DisableRange {
    uint32_t lo = env_u32("NWIIU_DISABLE_LO", 0);
    uint32_t hi = env_u32("NWIIU_DISABLE_HI", 0);
    // Inverted mode: defer everything EXCEPT [lo, hi), which isolates a
    // candidate range instead of merely perturbing the workload.
    uint32_t only_lo = env_u32("NWIIU_ONLY_LO", 0);
    uint32_t only_hi = env_u32("NWIIU_ONLY_HI", 0);
    DisableRange() {
        g_disable_range_active = hi > lo || only_hi > only_lo;
    }
};
const DisableRange g_disable_range;
}

void set_patched_addresses(const uint32_t* addresses, uint32_t count) {
    if (addresses == nullptr || count == 0) {
        g_sorted.clear();
        g_patched_addresses = nullptr;
        g_patched_count = 0;
        g_patch_lo = 0xFFFFFFFFu;
        g_patch_hi = 0u;
        return;
    }
    g_sorted.assign(addresses, addresses + count);
    std::sort(g_sorted.begin(), g_sorted.end());
    g_sorted.erase(std::unique(g_sorted.begin(), g_sorted.end()),
                   g_sorted.end());
    g_patched_addresses = g_sorted.data();
    g_patched_count = static_cast<uint32_t>(g_sorted.size());
    // Bracket the set so range_has_patch can reject the vast majority of
    // blocks with one compare instead of a call and a binary search.
    g_patch_lo = g_sorted.front();
    g_patch_hi = g_sorted.back();
}

bool range_has_patch_slow(uint32_t start, uint32_t instructions) {
    // Blocks are contiguous instruction runs, so the range is [start, end).
    const uint64_t end =
        static_cast<uint64_t>(start) + static_cast<uint64_t>(instructions) * 4;
    const auto* first = g_patched_addresses;
    const auto* last = first + g_patched_count;
    if (g_disable_range.only_hi > g_disable_range.only_lo) {
        // Keep only blocks fully inside the window under test.
        if (start < g_disable_range.only_lo ||
            end > static_cast<uint64_t>(g_disable_range.only_hi)) {
            return true;
        }
    }
    if (g_disable_range.hi > g_disable_range.lo && start < g_disable_range.hi &&
        g_disable_range.lo < end) {
        return true;
    }
    if (g_patched_count == 0) {
        return false;
    }
    const auto* it = std::lower_bound(first, last, start);
    return it != last && static_cast<uint64_t>(*it) < end;
}
}
