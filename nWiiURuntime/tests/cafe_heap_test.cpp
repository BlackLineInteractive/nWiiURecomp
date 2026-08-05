#include "runtime/cafe_abi.h"
#include "runtime/cafe_coreinit.h"
#include "runtime/execution_image.h"
#include "runtime/machine.h"
#include "test_support.h"

#include <array>
#include <cstdint>

namespace {
using nwii::runtime::CafeCoreinit;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Machine;
using nwii::runtime::StopCategory;
namespace abi = nwii::runtime::abi;

constexpr uint32_t kHeapBuffer = 0x10010003;
constexpr uint32_t kHeapMapping = 0x10010000;
constexpr uint32_t kHeapMappingSize = 0x1000;
constexpr uint32_t kPc = 0x02000000;
constexpr uint32_t kImportBase = 0xC0002000;
constexpr uint32_t kExpandedBlockSize = 0x14;
constexpr uint16_t kExpandedFreeTag = 0x4654;
constexpr uint16_t kExpandedUsedTag = 0x5544;

ExecutionImage make_image() {
    ExecutionImage image;
    image.stack_base = 0x4FF00000;
    image.stack_top = 0x50000000;
    constexpr std::array<uint8_t, 4> nop{0x60, 0, 0, 0};
    image.memory.map(kPc, nop.size(), {true, false, true}, nop);
    image.memory.map(kHeapMapping, kHeapMappingSize, {true, true, false});
    return image;
}

nwii::runtime::ExecutionStop
invoke(Machine& machine, uint32_t address,
       std::array<uint32_t, 8> arguments = {}) {
    auto& cpu = machine.main_context();
    cpu.running = true;
    cpu.pc = address;
    cpu.lr = kPc;
    for (size_t index = 0; index < arguments.size(); ++index) {
        cpu.gpr[index + 3] = arguments[index];
    }
    return machine.run(1, 1);
}

void test_dynamic_frame_heap_lifecycle() {
    auto image = make_image();
    CafeCoreinit coreinit(image);
    const uint32_t handle =
        coreinit.create_frame_heap(kHeapBuffer, 0x400, 0, kPc);

    test::require(handle == 0x10010004,
                  "frame heap aligns its handle to four bytes");
    test::require(image.memory.read32(handle + abi::kHeapTagOffset, kPc) ==
                          abi::kFrameHeapTag &&
                      image.memory.read32(handle + abi::kHeapDataStartOffset,
                                          kPc) == handle + abi::kFrameHeapSize &&
                      image.memory.read32(handle + abi::kHeapDataEndOffset,
                                          kPc) == 0x10010400,
                  "frame heap mirrors exact Cafe header bounds");

    const uint32_t head = coreinit.allocate_frame(handle, 3, 16, kPc);
    const uint32_t tail = coreinit.allocate_frame(handle, 3, -16, kPc);
    test::require(head == 0x10010050 && tail == 0x100103F0,
                  "dynamic frame heap allocates from both ends");
    test::require(coreinit.destroy_frame_heap(handle, kPc) == handle,
                  "destroy returns the original handle");
    test::require_throws(
        [&] { static_cast<void>(coreinit.allocate_frame(handle, 4, 4, kPc)); },
        "invalid frame heap", "destroyed frame heap cannot allocate");
}

void test_dynamic_expanded_heap_lifecycle() {
    auto image = make_image();
    CafeCoreinit coreinit(image);
    const uint32_t handle =
        coreinit.create_expanded_heap(kHeapBuffer, 0x800, 0, kPc);
    const uint32_t first_free = handle + abi::kExpandedHeapSize;

    test::require(handle == 0x10010004,
                  "expanded heap aligns its handle to four bytes");
    test::require(
        image.memory.read32(handle + abi::kHeapTagOffset, kPc) ==
                abi::kExpandedHeapTag &&
            image.memory.read32(handle + abi::kExpandedHeapFreeListOffset,
                                kPc) == first_free &&
            image.memory.read16(first_free + 0x10, kPc) ==
                kExpandedFreeTag,
        "expanded heap mirrors its first free block");

    const uint32_t first =
        coreinit.allocate_expanded(handle, 0x80, 0x20, kPc);
    const uint32_t second =
        coreinit.allocate_expanded(handle, 0x40, -0x20, kPc);
    test::require((first & 0x1F) == 0 && (second & 0x1F) == 0 &&
                      first < second,
                  "expanded heap honors signed allocation direction");
    test::require(image.memory.read16(first - kExpandedBlockSize + 0x10,
                                      kPc) == kExpandedUsedTag,
                  "expanded allocation mirrors a used block header");

    coreinit.free_expanded(handle, first, kPc);
    coreinit.free_expanded(handle, second, kPc);
    test::require(
        coreinit.expanded_total_free(handle, kPc) ==
            0x10010800 - first_free - kExpandedBlockSize,
        "expanded free coalesces back to one source-sized block");
    test::require(coreinit.destroy_expanded_heap(handle, kPc) == handle,
                  "expanded destroy returns its handle");
}

void test_heap_lifecycle_hle_bindings() {
    auto image = make_image();
    constexpr std::array<const char*, 6> symbols{
        "MEMCreateExpHeapEx",   "MEMDestroyExpHeap",
        "MEMAllocFromExpHeapEx", "MEMFreeToExpHeap",
        "MEMCreateFrmHeapEx",   "MEMDestroyFrmHeap"};
    for (size_t index = 0; index < symbols.size(); ++index) {
        image.imports.emplace(
            kImportBase + static_cast<uint32_t>(index) * 4,
            nwii::runtime::ImportTarget{"coreinit", symbols[index]});
    }
    Machine machine(image);

    auto stop = invoke(machine, kImportBase, {kHeapBuffer, 0x800, 0});
    test::require(stop.category == StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == 0x10010004,
                  "MEMCreateExpHeapEx returns a dynamic heap handle");
    const uint32_t expanded = machine.main_context().gpr[3];

    stop = invoke(machine, kImportBase + 8, {expanded, 0x80, 0x20});
    const uint32_t allocation = machine.main_context().gpr[3];
    test::require(stop.category == StopCategory::instruction_budget &&
                      allocation != 0,
                  "MEMAllocFromExpHeapEx allocates through HLE");
    stop = invoke(machine, kImportBase + 12, {expanded, allocation});
    test::require(stop.category == StopCategory::instruction_budget,
                  "MEMFreeToExpHeap frees through HLE");
    stop = invoke(machine, kImportBase + 4, {expanded});
    test::require(stop.category == StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == expanded,
                  "MEMDestroyExpHeap returns its handle through HLE");

    stop = invoke(machine, kImportBase + 16, {kHeapBuffer, 0x400, 0});
    const uint32_t frame = machine.main_context().gpr[3];
    test::require(stop.category == StopCategory::instruction_budget &&
                      frame == 0x10010004,
                  "MEMCreateFrmHeapEx returns a dynamic heap handle");
    stop = invoke(machine, kImportBase + 20, {frame});
    test::require(stop.category == StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == frame,
                  "MEMDestroyFrmHeap returns its handle through HLE");
}
} // namespace

int main() {
    test_dynamic_frame_heap_lifecycle();
    test_dynamic_expanded_heap_lifecycle();
    test_heap_lifecycle_hle_bindings();
}
