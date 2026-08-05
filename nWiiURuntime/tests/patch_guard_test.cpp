#include "runtime/patch_guard.h"
#include "test_support.h"

#include <array>
#include <cstdint>

namespace {
using nwii::runtime::range_has_patch;
using nwii::runtime::set_patched_addresses;

void test_no_patches_never_defers() {
    set_patched_addresses(nullptr, 0);
    test::require(!range_has_patch(0x02000000, 64),
                  "an unpatched title defers nothing");
}

void test_range_boundaries() {
    // Deliberately unsorted and duplicated: the host builds this list in patch
    // application order, not address order.
    const std::array<uint32_t, 4> patches{0x02000100, 0x02000010, 0x02000100,
                                          0x02FFFFFC};
    set_patched_addresses(patches.data(),
                          static_cast<uint32_t>(patches.size()));

    test::require(range_has_patch(0x02000010, 1),
                  "a patch on the block's first instruction defers it");
    // The bug this guard exists for: a first-instruction check cannot see this.
    test::require(range_has_patch(0x02000008, 4),
                  "a patch in the middle of a block defers it");
    test::require(range_has_patch(0x0200000C, 2),
                  "a patch on the block's last instruction defers it");

    test::require(!range_has_patch(0x02000000, 4),
                  "a block ending one word before a patch still runs");
    test::require(!range_has_patch(0x02000014, 4),
                  "a block starting one word after a patch still runs");
    test::require(!range_has_patch(0x02000020, 8),
                  "a block clear of every patch runs");

    test::require(range_has_patch(0x02FFFFF8, 2),
                  "the highest patch is found, so the search is not truncated");
    test::require(!range_has_patch(0x03000000, 16),
                  "a block past every patch runs");
}

void test_list_is_replaced_not_accumulated() {
    const std::array<uint32_t, 1> first{0x02000010};
    set_patched_addresses(first.data(), 1);
    test::require(range_has_patch(0x02000010, 1), "first list applies");

    const std::array<uint32_t, 1> second{0x02000200};
    set_patched_addresses(second.data(), 1);
    test::require(!range_has_patch(0x02000010, 1),
                  "the previous list is dropped, not merged");
    test::require(range_has_patch(0x02000200, 1), "the new list applies");

    set_patched_addresses(second.data(), 0);
    test::require(!range_has_patch(0x02000200, 1),
                  "a zero count clears the list");
}
}

int main() {
    test_no_patches_never_defers();
    test_range_boundaries();
    test_list_is_replaced_not_accumulated();
    return 0;
}
