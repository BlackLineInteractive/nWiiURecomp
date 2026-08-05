#include "runtime/cafe_abi.h"
#include "runtime/cafe_coreinit.h"
#include "runtime/machine.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <string>
#include <type_traits>

namespace nwii::runtime {
struct CafeCoreinitTestAccess {
    static void seed_mutex(CafeCoreinit& coreinit, uint32_t address,
                           uint32_t owner, uint32_t recursion) {
        coreinit.mutexes_.insert_or_assign(
            address, CafeCoreinit::MutexState{owner, recursion, 0, {}});
    }

    static uint32_t mutex_owner(const CafeCoreinit& coreinit,
                                uint32_t address) {
        return coreinit.mutexes_.at(address).owner;
    }

    static uint32_t mutex_recursion(const CafeCoreinit& coreinit,
                                    uint32_t address) {
        return coreinit.mutexes_.at(address).recursion;
    }

    static void seed_message_queue(CafeCoreinit& coreinit, uint32_t address) {
        coreinit.message_queues_.insert_or_assign(
            address, CafeCoreinit::MessageQueueState{});
    }

    static uint32_t dynload_alloc(const CafeCoreinit& coreinit) {
        return coreinit.dynload_alloc_;
    }

    static uint32_t dynload_free(const CafeCoreinit& coreinit) {
        return coreinit.dynload_free_;
    }
};
} // namespace nwii::runtime

using nwii::runtime::CafeCoreinitTestAccess;

namespace {
using nwii::runtime::CafeCoreinit;
using nwii::runtime::CPUContext;
using nwii::runtime::ExecutionImage;
using nwii::runtime::ExecutionStop;
using nwii::runtime::Machine;
using nwii::runtime::StopCategory;
namespace abi = nwii::runtime::abi;

static_assert(!std::is_copy_constructible_v<CafeCoreinit>);
static_assert(!std::is_move_constructible_v<CafeCoreinit>);
static_assert(!std::is_copy_assignable_v<CafeCoreinit>);
static_assert(!std::is_move_assignable_v<CafeCoreinit>);

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kData = 0x10000000;
constexpr uint32_t kImportBase = 0xC0001000;

constexpr std::array<std::string_view, 90> kCoreinitImports{
    "IMEnableDim",
    "IMIsDimEnabled",
    "OSEnableHomeButtonMenu",
    "MEMAllocFromDefaultHeap",
    "MEMAllocFromDefaultHeapEx",
    "MEMFreeToDefaultHeap",
    "__ghs_flock_destroy",
    "__ghs_flock_file",
    "__ghs_flock_ptr",
    "__ghs_funlock_file",
    "__gh_set_errno",
    "MEMAllocFromFrmHeapEx",
    "MEMCreateExpHeapEx",
    "MEMDestroyExpHeap",
    "MEMAllocFromExpHeapEx",
    "MEMFreeToExpHeap",
    "MEMCreateFrmHeapEx",
    "MEMDestroyFrmHeap",
    "MEMGetAllocatableSizeForExpHeapEx",
    "MEMGetAllocatableSizeForFrmHeapEx",
    "MEMGetBaseHeapHandle",
    "MEMGetTotalFreeSizeForExpHeap",
    "OSBlockMove",
    "OSBlockSet",
    "OSMemoryBarrier",
    "DCZeroRange",
    "DCFlushRange",
    "DCFlushRangeNoSync",
    "DCStoreRange",
    "DCStoreRangeNoSync",
    "DCInvalidateRange",
    "OSIsAddressRangeDCValid",
    "UCOpen",
    "UCClose",
    "UCReadSysConfig",
    "OSGetSharedData",
    "OSDynLoad_SetAllocator",
    "OSDynLoad_Acquire",
    "OSDynLoad_FindExport",
    "OSReport",
    "OSYieldThread",
    "OSGetCoreId",
    "OSDisableInterrupts",
    "OSEnableInterrupts",
    "OSCreateAlarm",
    "OSSetAlarmUserData",
    "OSSetPeriodicAlarm",
    "OSGetCurrentThread",
    "OSCreateThread",
    "OSExitThread",
    "OSGetSystemInfo",
    "OSGetSystemTime",
    "OSGetTime",
    "OSGetTick",
    "OSGetThreadSpecific",
    "OSGetThreadPriority",
    "OSIsInterruptEnabled",
    "OSRestoreInterrupts",
    "OSInitEvent",
    "OSResetEvent",
    "OSSignalEvent",
    "OSSignalEventAll",
    "OSWaitEvent",
    "OSInitMutex",
    "OSInitThreadQueue",
    "OSInitMessageQueue",
    "OSInitRendezvous",
    "OSWaitRendezvous",
    "OSSendMessage",
    "OSReceiveMessage",
    "OSSleepTicks",
    "OSIsDebuggerInitialized",
    "OSLockMutex",
    "OSTryLockMutex",
    "OSResumeThread",
    "OSSetExceptionCallback",
    "OSSetThreadSpecific",
    "OSSetThreadName",
    "OSSetThreadAffinity",
    "OSUnlockMutex",
    "__ghsLock",
    "__ghsUnlock",
    "__ghs_mtx_dst",
    "__ghs_mtx_init",
    "__ghs_mtx_lock",
    "__ghs_mtx_unlock",
    "exit",
    "memmove",
    "memcpy",
    "memset",
};

ExecutionImage make_image() {
    ExecutionImage image;
    image.stack_base = 0x4FF00000;
    image.stack_top = 0x50000000;
    image.memory.map(kReturn, 0x20, {true, false, true},
                     {reinterpret_cast<const uint8_t*>(
                          "\x60\0\0\0\x60\0\0\0\x60\0\0\0\x60\0\0\0"
                          "\x60\0\0\0\x60\0\0\0\x60\0\0\0\x60\0\0\0"),
                      0x20});
    return image;
}

ExecutionStop invoke(Machine& machine, uint32_t address,
                     std::array<uint32_t, 8> args = {}) {
    auto& cpu = machine.main_context();
    cpu.running = true;
    cpu.pc = address;
    cpu.lr = kReturn;
    for (size_t index = 0; index < args.size(); ++index) {
        cpu.gpr[index + 3] = args[index];
    }
    return machine.run(1, 1);
}

void test_abi_constants_and_guest_fields() {
    static_assert(abi::kOsContextSize == 0x320);
    static_assert(abi::kOsThreadSize == 0x6A0);
    static_assert(abi::kOsThreadTagOffset == 0x320);
    static_assert(abi::kOsThreadStateOffset == 0x324);
    static_assert(abi::kOsThreadPriorityOffset == 0x32C);
    static_assert(abi::kOsThreadSpecificOffset == 0x57C);
    static_assert(abi::kMemHeapHeaderSize == 0x40);
    static_assert(abi::kOsMutexSize == 0x2C);

    auto image = make_image();
    Machine machine(image);
    const uint32_t thread = machine.main_thread_address();
    test::require(image.memory.read64(thread + abi::kOsContextTagOffset, 0) ==
                      abi::kOsContextTag,
                  "OSContext tag is big-endian at WUT offset");
    test::require(image.memory.read32(thread + abi::kOsThreadTagOffset, 0) ==
                          abi::kOsThreadTag &&
                      image.memory.read8(thread + abi::kOsThreadStateOffset, 0) ==
                          abi::kThreadReady &&
                      image.memory.read16(thread + abi::kOsThreadIdOffset, 0) == 1 &&
                      image.memory.read32(
                          thread + abi::kOsThreadPriorityOffset, 0) == 80 &&
                      image.memory.read32(
                          thread + abi::kOsThreadBasePriorityOffset, 0) == 80 &&
                      image.memory.read32(
                          thread + abi::kOsThreadTypeOffset, 0) == 2,
                  "main OSThread stores App-normalized priority fields");
    test::require(image.memory.read32(
                          thread + abi::kOsThreadStackStartOffset, 0) ==
                          image.stack_top &&
                      image.memory.read32(
                          thread + abi::kOsThreadStackEndOffset, 0) ==
                          image.stack_base,
                  "main OSThread stack bounds mirrored");

    test::require(image.memory.read32(abi::kMem1Heap, 0) ==
                          abi::kFrameHeapTag &&
                      image.memory.read32(abi::kMem2Heap, 0) ==
                          abi::kExpandedHeapTag &&
                      image.memory.read32(abi::kFgHeap, 0) ==
                          abi::kFrameHeapTag,
                  "base heap tags are big-endian");
    test::require(image.memory.read32(
                          abi::kMem1Heap + abi::kHeapDataStartOffset, 0) ==
                          abi::kMem1Start &&
                      image.memory.read32(
                          abi::kMem1Heap + abi::kHeapDataEndOffset, 0) ==
                          abi::kMem1End &&
                      image.memory.read32(
                          abi::kMem2Heap + abi::kHeapDataStartOffset, 0) ==
                          abi::kMem2Start &&
                      image.memory.read32(
                          abi::kFgHeap + abi::kHeapDataEndOffset, 0) ==
                          abi::kFgEnd,
                  "exact MEM1 MEM2 FG subdivisions mirrored");
    test::require(abi::kMem1Heap == 0xF4000000 &&
                      abi::kMem1Start == 0xF400004C &&
                      abi::kMem1End == 0xF6000000 &&
                      abi::kMem2Heap == 0x10520000 &&
                      abi::kMem2Start == 0x10520054 &&
                      abi::kMem2End == 0x4DFA0000 &&
                      abi::kFgHeap == 0xE0000000 &&
                      abi::kFgStart == 0xE000004C &&
                      abi::kFgEnd == 0xE2800000,
                  "WWHD base heaps match Cafe process memory bounds");
}

void test_sparse_cafe_memory_and_collision() {
    auto image = make_image();
    const uint64_t before = image.memory.mapped_bytes();
    CafeCoreinit coreinit(image);
    test::require(image.memory.mapped_bytes() - before ==
                      abi::kCafeMemorySize,
                  "exact Cafe memory reservation");
    test::require(image.memory.resident_bytes() <= 0x6000,
                  "Cafe memory remains sparse");

    auto collision = make_image();
    collision.memory.map(0x30000000, 0x1000, {true, true, false});
    test::require_throws([&] { CafeCoreinit rejected(collision); },
                         "Cafe memory collision",
                         "memory collision fails without relocation");
}

void test_frame_and_expanded_heaps() {
    auto image = make_image();
    CafeCoreinit coreinit(image);
    test::require(coreinit.base_heap(0) == abi::kMem1Heap &&
                      coreinit.base_heap(1) == abi::kMem2Heap &&
                      coreinit.base_heap(8) == abi::kFgHeap,
                  "sparse base heap enum keys");
    test::require_throws([&] { static_cast<void>(coreinit.base_heap(2)); },
                         "invalid base heap", "unknown base heap hard-faults");

    const uint32_t head =
        coreinit.allocate_frame(abi::kMem1Heap, 3, 16, kReturn);
    const uint32_t tail =
        coreinit.allocate_frame(abi::kMem1Heap, 3, -16, kReturn);
    test::require(head == ((abi::kMem1Start + 15) & ~uint32_t{15}) &&
                      tail == abi::kMem1End - 0x10,
                  "frame head and Green Hills tail alignment");
    test::require(coreinit.frame_allocatable(abi::kMem1Heap, 16, kReturn) ==
                      tail - ((head + 3 + 15) & ~uint32_t{15}),
                  "frame allocatable size tracks deterministic ends");
    test::require(
        coreinit.frame_allocatable(abi::kMem1Heap, -0x100, kReturn) ==
            tail - ((head + 3 + 0xFF) & ~uint32_t{0xFF}),
        "negative frame query advertises an allocatable size");
    test::require_throws(
        [&] { coreinit.allocate_frame(abi::kMem1Heap, 4, 3, kReturn); },
        "alignment", "non-power-of-two frame alignment hard-faults");
    test::require_throws(
        [&] {
            coreinit.allocate_frame(abi::kMem1Heap, 4,
                                    std::numeric_limits<int32_t>::min(),
                                    kReturn);
        },
        "alignment", "overflowing negative alignment hard-faults");

    const uint32_t initial = coreinit.expanded_total_free(abi::kMem2Heap, 0);
    const uint32_t skew = coreinit.allocate_expanded(
        abi::kMem2Heap, 1, 1, kReturn);
    test::require(
        coreinit.expanded_allocatable(abi::kMem2Heap, -0x100, kReturn) ==
            abi::kMem2End -
                ((abi::kMem2Start + 1 + 0xFF) & ~uint32_t{0xFF}),
        "negative expanded query advertises an allocatable size");
    coreinit.free_expanded(abi::kMem2Heap, skew, kReturn);
    const uint32_t first =
        coreinit.allocate_expanded(abi::kMem2Heap, 0x100, 0x20, kReturn);
    const uint32_t second =
        coreinit.allocate_expanded(abi::kMem2Heap, 0x80, 0x20, kReturn);
    coreinit.free_expanded(abi::kMem2Heap, first, kReturn);
    const uint32_t reused =
        coreinit.allocate_expanded(abi::kMem2Heap, 0x80, 0x20, kReturn);
    const uint32_t first_aligned =
        (abi::kMem2Start + 0x1F) & ~uint32_t{0x1F};
    test::require(first == first_aligned && second == first + 0x100 &&
                      reused == first,
                  "expanded free-list split and address-ordered reuse");
    coreinit.free_expanded(abi::kMem2Heap, reused, kReturn);
    coreinit.free_expanded(abi::kMem2Heap, second, kReturn);
    test::require(coreinit.expanded_total_free(abi::kMem2Heap, 0) == initial,
                  "expanded free-list coalesces");

    const uint32_t whole =
        coreinit.allocate_expanded(abi::kMem2Heap, initial, 1, kReturn);
    test::require(whole == abi::kMem2Start &&
                      coreinit.allocate_expanded(abi::kMem2Heap, 1, 1,
                                                 kReturn) == 0,
                  "expanded heap reports exhaustion without fake allocation");
    coreinit.free_expanded(abi::kMem2Heap, whole, kReturn);

    image.memory.write32(abi::kMem2Heap, abi::kFrameHeapTag, 0);
    test::require_throws(
        [&] { static_cast<void>(coreinit.expanded_total_free(abi::kMem2Heap, 0)); },
        "invalid expanded heap", "wrong guest heap tag hard-faults");
    test::require_throws(
        [&] {
            static_cast<void>(
                coreinit.frame_allocatable(abi::kMem2Heap, 8, kReturn));
        },
        "invalid frame heap", "wrong heap kind hard-faults");
}

void test_exact_import_registration() {
    for (size_t index = 0; index < kCoreinitImports.size(); ++index) {
        auto image = make_image();
        const uint32_t address =
            kImportBase + static_cast<uint32_t>(index) * 4;
        image.imports.emplace(
            address, nwii::runtime::ImportTarget{
                         "coreinit", std::string{kCoreinitImports[index]}});
        Machine machine(image);
        const auto stop = invoke(machine, address);
        test::require(stop.category != StopCategory::missing_hle,
                      kCoreinitImports[index]);
    }

    auto image = make_image();
    image.imports.emplace(kImportBase,
                          nwii::runtime::ImportTarget{"coreinit",
                                                       "OSWaitEventWithTimeout"});
    Machine machine(image);
    test::require(invoke(machine, kImportBase).category ==
                      StopCategory::missing_hle,
                  "unimplemented timed event waits stay hidden");
}

void test_default_heap_data_import_handlers() {
    auto image = make_image();
    constexpr uint32_t alloc = kImportBase;
    constexpr uint32_t alloc_ex = kImportBase + 4;
    constexpr uint32_t free = kImportBase + 8;
    image.imports.emplace(
        alloc, nwii::runtime::ImportTarget{
                   "coreinit", "MEMAllocFromDefaultHeap"});
    image.imports.emplace(
        alloc_ex, nwii::runtime::ImportTarget{
                      "coreinit", "MEMAllocFromDefaultHeapEx"});
    image.imports.emplace(
        free, nwii::runtime::ImportTarget{
                  "coreinit", "MEMFreeToDefaultHeap"});
    Machine machine(image);

    test::require(invoke(machine, alloc, {0x20}).category !=
                      StopCategory::missing_hle,
                  "default allocation data import is registered");
    const uint32_t first = machine.main_context().gpr[3];
    invoke(machine, alloc_ex, {0x20, 0x100});
    const uint32_t aligned = machine.main_context().gpr[3];
    test::require(
        first == ((abi::kMem2Start + 7) & ~uint32_t{7}) &&
            (aligned & 0xFF) == 0,
        "default heap uses MEM2 and honors explicit alignment");
    invoke(machine, free, {first});
    invoke(machine, alloc, {0x20});
    test::require(machine.main_context().gpr[3] == first,
                  "default heap free returns allocation to MEM2");
}

void test_ghs_file_lock_pointer() {
    auto image = make_image();
    constexpr uint32_t data_import = 0xC000A970;
    image.memory.map(0xC000A900, 0x90, {true, true, false});
    image.memory.write32(data_import, data_import, 0);
    image.imports.emplace(
        data_import, nwii::runtime::ImportTarget{"coreinit", "_iob"});
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "__ghs_flock_ptr"});
    image.imports.emplace(
        kImportBase + 4,
        nwii::runtime::ImportTarget{"coreinit", "__gh_set_errno"});
    Machine machine(image);

    const uint32_t iob = image.memory.read32(data_import, 0);
    test::require(iob == abi::kGhsIob, "_iob data import is runtime-backed");
    test::require(image.memory.read32(data_import + 0x24, 0) == 0,
                  "relocated _iob alias has runtime storage");
    invoke(machine, kImportBase, {iob + 2 * abi::kGhsIobEntrySize});
    test::require(machine.main_context().gpr[3] == abi::kGhsFlocks + 8,
                  "file-lock pointer preserves stream index");
    invoke(machine, kImportBase,
           {data_import + 2 * abi::kGhsIobEntrySize});
    test::require(machine.main_context().gpr[3] == abi::kGhsFlocks + 8,
                  "relocated _iob alias preserves stream index");

    invoke(machine, kImportBase + 4, {9});
    test::require(image.memory.read32(abi::kGhsErrno, 0) == 9,
                  "GHS errno setter updates runtime storage");
}

void test_thread_time_exception_and_debug_handlers() {
    auto image = make_image();
    constexpr uint32_t current = kImportBase;
    constexpr uint32_t specific = kImportBase + 4;
    constexpr uint32_t time = kImportBase + 8;
    constexpr uint32_t callback = kImportBase + 12;
    constexpr uint32_t debugger = kImportBase + 16;
    constexpr uint32_t set_specific = kImportBase + 20;
    constexpr uint32_t priority = kImportBase + 24;
    constexpr uint32_t wall_time = kImportBase + 28;
    constexpr uint32_t tick = kImportBase + 32;
    image.imports.emplace(current, nwii::runtime::ImportTarget{
                                       "coreinit", "OSGetCurrentThread"});
    image.imports.emplace(specific, nwii::runtime::ImportTarget{
                                        "coreinit", "OSGetThreadSpecific"});
    image.imports.emplace(time, nwii::runtime::ImportTarget{
                                    "coreinit", "OSGetSystemTime"});
    image.imports.emplace(callback, nwii::runtime::ImportTarget{
                                        "coreinit", "OSSetExceptionCallback"});
    image.imports.emplace(debugger, nwii::runtime::ImportTarget{
                                        "coreinit", "OSIsDebuggerInitialized"});
    image.imports.emplace(
        set_specific,
        nwii::runtime::ImportTarget{"coreinit", "OSSetThreadSpecific"});
    image.imports.emplace(
        priority,
        nwii::runtime::ImportTarget{"coreinit", "OSGetThreadPriority"});
    image.imports.emplace(
        wall_time, nwii::runtime::ImportTarget{"coreinit", "OSGetTime"});
    image.imports.emplace(
        tick, nwii::runtime::ImportTarget{"coreinit", "OSGetTick"});
    Machine machine(image);

    auto& memory = image.memory;
    const uint32_t thread = machine.main_thread_address();
    test::require(
        invoke(machine, set_specific, {7, 0x12345678}).category ==
                StopCategory::instruction_budget &&
            memory.read32(thread + abi::kOsThreadSpecificOffset + 7 * 4, 0) ==
                0x12345678,
        "OSSetThreadSpecific writes the current thread slot");
    test::require(invoke(machine, current).category ==
                          StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == thread,
                  "OSGetCurrentThread returns scheduler identity");
    invoke(machine, specific, {7});
    test::require(machine.main_context().gpr[3] == 0x12345678,
                  "OSGetThreadSpecific reads mirrored slot");
    invoke(machine, set_specific, {0, 0});
    invoke(machine, specific, {0});
    test::require(machine.main_context().gpr[3] == 0,
                  "OSSetThreadSpecific accepts a null value");
    memory.write32(thread + abi::kOsThreadPriorityOffset, 3, 0);
    memory.write32(thread + abi::kOsThreadBasePriorityOffset, 7, 0);
    memory.write32(thread + abi::kOsThreadTypeOffset, 0, 0);
    invoke(machine, priority, {thread});
    test::require(machine.main_context().gpr[3] == 7,
                  "Driver priority returns the raw base priority");
    memory.write32(thread + abi::kOsThreadBasePriorityOffset, 45, 0);
    memory.write32(thread + abi::kOsThreadTypeOffset, 1, 0);
    invoke(machine, priority, {thread});
    test::require(machine.main_context().gpr[3] == 13,
                  "AppIo priority normalizes the base priority by 32");
    memory.write32(thread + abi::kOsThreadBasePriorityOffset, 80, 0);
    memory.write32(thread + abi::kOsThreadTypeOffset, 2, 0);
    invoke(machine, priority, {thread});
    test::require(machine.main_context().gpr[3] == 16,
                  "App priority normalizes the base priority by 64");

    machine.coreinit().advance_ticks(0x100000002ULL);
    const uint64_t returned_ticks = machine.coreinit().ticks();
    invoke(machine, time);
    test::require(machine.main_context().gpr[3] ==
                          static_cast<uint32_t>(returned_ticks >> 32) &&
                      machine.main_context().gpr[4] ==
                          static_cast<uint32_t>(returned_ticks),
                  "OSGetSystemTime returns 64-bit ticks in r3:r4");
    const uint64_t tick_before = machine.current_time_ticks();
    invoke(machine, tick);
    const uint64_t tick_after = machine.current_time_ticks();
    test::require(
        machine.main_context().gpr[3] >= static_cast<uint32_t>(tick_before) &&
            machine.main_context().gpr[3] <=
                static_cast<uint32_t>(tick_after),
        "OSGetTick returns the low 32 bits of current system ticks");
    invoke(machine, wall_time);
    const uint64_t returned_wall_time =
        static_cast<uint64_t>(machine.main_context().gpr[3]) << 32 |
        machine.main_context().gpr[4];
    test::require(returned_wall_time >= returned_ticks,
                  "OSGetTime returns monotonic 64-bit ticks in r3:r4");
    const uint64_t previous_ticks = machine.coreinit().ticks();
    invoke(machine, debugger);
    test::require(machine.main_context().gpr[3] == 0 &&
                      machine.coreinit().ticks() > previous_ticks,
                  "debugger false and Cafe ticks monotonic");

    invoke(machine, callback, {3, 0x11112222});
    test::require(machine.main_context().gpr[3] == 0,
                  "first exception callback returns null");
    invoke(machine, callback, {3, 0x33334444});
    test::require(machine.main_context().gpr[3] == 0x11112222,
                  "exception callback returns prior state");

    auto invalid = make_image();
    invalid.imports.emplace(specific, nwii::runtime::ImportTarget{
                                          "coreinit", "OSGetThreadSpecific"});
    Machine invalid_machine(invalid);
    test::require(invoke(invalid_machine, specific, {16}).category ==
                      StopCategory::guest_fault,
                  "invalid thread-specific id hard-faults");
    auto invalid_set = make_image();
    invalid_set.imports.emplace(
        specific,
        nwii::runtime::ImportTarget{"coreinit", "OSSetThreadSpecific"});
    Machine invalid_set_machine(invalid_set);
    test::require(invoke(invalid_set_machine, specific, {16, 1}).category ==
                      StopCategory::guest_fault,
                  "OSSetThreadSpecific rejects invalid ids");
    auto invalid_priority = make_image();
    invalid_priority.imports.emplace(
        priority,
        nwii::runtime::ImportTarget{"coreinit", "OSGetThreadPriority"});
    Machine invalid_priority_machine(invalid_priority);
    test::require(
        invoke(invalid_priority_machine, priority, {0x70000000}).category ==
            StopCategory::guest_fault,
        "OSGetThreadPriority faults on an unmapped thread");
    auto invalid_type = make_image();
    invalid_type.imports.emplace(
        priority,
        nwii::runtime::ImportTarget{"coreinit", "OSGetThreadPriority"});
    Machine invalid_type_machine(invalid_type);
    const uint32_t invalid_type_thread =
        invalid_type_machine.main_thread_address();
    invalid_type.memory.write32(
        invalid_type_thread + abi::kOsThreadTypeOffset, 3, 0);
    test::require(
        invoke(invalid_type_machine, priority, {invalid_type_thread}).category ==
            StopCategory::guest_fault,
        "OSGetThreadPriority faults on an unexpected OSThread type");
}

void test_memcpy_memset_and_ghs_pointer_abi() {
    auto image = make_image();
    constexpr uint32_t copy = kImportBase;
    constexpr uint32_t set = kImportBase + 4;
    constexpr uint32_t init = kImportBase + 8;
    constexpr uint32_t lock = kImportBase + 12;
    constexpr uint32_t unlock = kImportBase + 16;
    constexpr uint32_t destroy = kImportBase + 20;
    constexpr uint32_t move = kImportBase + 24;
    image.imports.emplace(copy,
                          nwii::runtime::ImportTarget{"coreinit", "memcpy"});
    image.imports.emplace(set,
                          nwii::runtime::ImportTarget{"coreinit", "memset"});
    image.imports.emplace(init, nwii::runtime::ImportTarget{
                                    "coreinit", "__ghs_mtx_init"});
    image.imports.emplace(lock, nwii::runtime::ImportTarget{
                                    "coreinit", "__ghs_mtx_lock"});
    image.imports.emplace(unlock, nwii::runtime::ImportTarget{
                                      "coreinit", "__ghs_mtx_unlock"});
    image.imports.emplace(destroy, nwii::runtime::ImportTarget{
                                       "coreinit", "__ghs_mtx_dst"});
    image.imports.emplace(move,
                          nwii::runtime::ImportTarget{"coreinit", "memmove"});
    image.memory.map(kData, 0x100, {true, true, false});
    constexpr uint32_t cross_source = kData + 0x200;
    constexpr uint32_t cross_destination = kData + 0x300;
    constexpr uint32_t cross_set = kData + 0x400;
    image.memory.map(cross_source, 4, {true, true, false});
    image.memory.map(cross_source + 4, 4, {true, true, false});
    image.memory.map(cross_destination, 4, {true, true, false});
    image.memory.map(cross_destination + 4, 4, {true, true, false});
    image.memory.map(cross_set, 4, {true, true, false});
    image.memory.map(cross_set + 4, 4, {true, true, false});
    Machine machine(image);

    for (uint32_t index = 0; index < 8; ++index) {
        image.memory.write8(kData + index, static_cast<uint8_t>(0xA0 + index), 0);
    }
    invoke(machine, copy, {kData + 0x20, kData, 8});
    test::require(machine.main_context().gpr[3] == kData + 0x20 &&
                      image.memory.read64(kData + 0x20, 0) ==
                          0xA0A1A2A3A4A5A6A7ULL,
                  "memcpy copies guest bytes and returns destination");
    invoke(machine, set, {kData + 0x30, 0x1234, 4});
    test::require(machine.main_context().gpr[3] == kData + 0x30 &&
                      image.memory.read32(kData + 0x30, 0) == 0x34343434,
                  "memset uses low byte and returns destination");
    for (uint32_t index = 0; index < 8; ++index) {
        image.memory.write8(kData + index, static_cast<uint8_t>(index), 0);
    }
    invoke(machine, move, {kData + 1, kData, 7});
    test::require(image.memory.read64(kData, 0) == 0x0000010203040506ULL,
                  "memmove preserves forward overlap");
    for (uint32_t index = 0; index < 8; ++index) {
        image.memory.write8(kData + index, static_cast<uint8_t>(index), 0);
    }
    invoke(machine, move, {kData, kData + 1, 7});
    test::require(image.memory.read64(kData, 0) == 0x0102030405060707ULL,
                  "memmove preserves backward overlap");

    image.memory.write32(cross_source, 0x11223344, 0);
    image.memory.write32(cross_source + 4, 0x55667788, 0);
    invoke(machine, copy, {cross_destination, cross_source, 8});
    test::require(
        image.memory.read32(cross_destination, 0) == 0x11223344 &&
            image.memory.read32(cross_destination + 4, 0) == 0x55667788,
        "memcpy crosses adjacent guest mappings");
    invoke(machine, set, {cross_set + 2, 0x1234, 4});
    test::require(image.memory.read32(cross_set, 0) == 0x00003434 &&
                      image.memory.read32(cross_set + 4, 0) == 0x34340000,
                  "memset crosses adjacent guest mappings");

    invoke(machine, init, {kData + 0x80});
    const uint32_t mutex = image.memory.read32(kData + 0x80, 0);
    test::require((mutex & 7) == 0 &&
                      image.memory.read32(mutex + abi::kMutexTagOffset, 0) ==
                          abi::kOsMutexTag,
                  "GHS init writes aligned OSMutex through pointer-to-pointer");
    invoke(machine, lock, {kData + 0x80});
    invoke(machine, lock, {kData + 0x80});
    test::require(image.memory.read32(
                          mutex + abi::kMutexRecursionOffset, 0) == 2,
                  "GHS pointer ABI preserves recursive mutex ownership");
    invoke(machine, unlock, {kData + 0x80});
    invoke(machine, unlock, {kData + 0x80});
    invoke(machine, destroy, {kData + 0x80});
    test::require(image.memory.read32(kData + 0x80, 0) == 0,
                  "GHS destroy frees dereferenced mutex and clears pointer");

    auto invalid = make_image();
    invalid.imports.emplace(copy,
                            nwii::runtime::ImportTarget{"coreinit", "memcpy"});
    Machine invalid_machine(invalid);
    test::require(invoke(invalid_machine, copy,
                         {0x60000000, 0x70000000, 1})
                          .category == StopCategory::guest_fault,
                  "invalid memcpy guest range hard-faults");
}

void test_scalar_preflights_reject_split_objects_atomically() {
    constexpr uint32_t init_mutex = kImportBase;
    constexpr uint32_t ghs_init = kImportBase + 4;

    {
        auto image = make_image();
        image.imports.emplace(
            init_mutex,
            nwii::runtime::ImportTarget{"coreinit", "OSInitMutex"});
        image.memory.map(kData, 2, {true, true, false});
        image.memory.map(kData + 2, abi::kOsMutexSize - 2,
                         {true, true, false});
        std::array<uint8_t, abi::kOsMutexSize> before{};
        for (uint32_t index = 0; index < before.size(); ++index) {
            before[index] = static_cast<uint8_t>(0x40 + index);
            image.memory.write8(kData + index, before[index], 0);
        }
        Machine machine(image);

        const auto stop = invoke(machine, init_mutex, {kData});
        bool unchanged = true;
        for (uint32_t index = 0; index < before.size(); ++index) {
            unchanged &=
                image.memory.read8(kData + index, 0) == before[index];
        }
        test::require(
            stop.category == StopCategory::guest_fault && unchanged,
            "split OSMutex faults before zeroing or scalar tag write");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            ghs_init,
            nwii::runtime::ImportTarget{"coreinit", "__ghs_mtx_init"});
        constexpr uint32_t split_output = kData;
        image.memory.map(split_output, 2, {true, true, false});
        image.memory.map(split_output + 2, 2, {true, true, false});
        Machine machine(image);
        const uint32_t initial_free =
            machine.coreinit().expanded_total_free(abi::kMem2Heap, 0);

        const auto stop = invoke(machine, ghs_init, {split_output});
        test::require(
            stop.category == StopCategory::guest_fault &&
                machine.coreinit().expanded_total_free(abi::kMem2Heap, 0) ==
                    initial_free,
            "split GHS mutex output faults before heap or state allocation");

    }
}
void test_os_get_system_info() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSGetSystemInfo"});
    Machine machine(image);

    invoke(machine, kImportBase);
    const uint32_t system_info = machine.main_context().gpr[3];
    invoke(machine, kImportBase);
    test::require(system_info == abi::kOsSystemInfo &&
                      machine.main_context().gpr[3] == system_info,
                  "OSGetSystemInfo returns stable runtime-owned storage");
    test::require(
        image.memory.read32(system_info, 0) == 248625000 &&
            image.memory.read32(system_info + 4, 0) == 1243125000 &&
            image.memory.read64(system_info + 8, 0) == 0 &&
            image.memory.read32(system_info + 0x10, 0) == 512 * 1024 &&
            image.memory.read32(system_info + 0x14, 0) == 2 * 1024 * 1024 &&
            image.memory.read32(system_info + 0x18, 0) == 512 * 1024 &&
            image.memory.read32(system_info + 0x1C, 0) == 5,
        "OSSystemInfo exposes Cafe clocks, deterministic base, and core state");
}

void test_os_block_move() {
    constexpr uint32_t move = kImportBase;

    {
        auto image = make_image();
        image.imports.emplace(
            move, nwii::runtime::ImportTarget{"coreinit", "OSBlockMove"});
        image.memory.map(kData, 8, {true, true, false});
        Machine machine(image);
        for (uint32_t index = 0; index < 8; ++index) {
            image.memory.write8(kData + index, static_cast<uint8_t>(index), 0);
        }

        invoke(machine, move, {kData, kData + 2, 6, 0});
        test::require(machine.main_context().gpr[3] == kData &&
                          image.memory.read64(kData, 0) ==
                              0x0203040506070607ULL,
                      "OSBlockMove copies forward overlap and returns dst");

        for (uint32_t index = 0; index < 8; ++index) {
            image.memory.write8(kData + index, static_cast<uint8_t>(index), 0);
        }
        invoke(machine, move, {kData + 2, kData, 6, 1});
        test::require(machine.main_context().gpr[3] == kData + 2 &&
                          image.memory.read64(kData, 0) ==
                              0x0001000102030405ULL,
                      "OSBlockMove copies backward overlap and accepts flush");

        const auto stop =
            invoke(machine, move, {0x11110000, 0x22220000, 0, 1});
        test::require(stop.category != StopCategory::guest_fault &&
                          machine.main_context().gpr[3] == 0x11110000,
                      "zero-sized OSBlockMove accepts unmapped pointers");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            move, nwii::runtime::ImportTarget{"coreinit", "OSBlockMove"});
        image.memory.map(kData, 4, {true, true, false});
        image.memory.map(kData + 4, 4, {true, true, false});
        Machine machine(image);
        for (uint32_t index = 0; index < 8; ++index) {
            image.memory.write8(kData + index, static_cast<uint8_t>(index), 0);
        }

        invoke(machine, move, {kData, kData + 2, 6, 0});
        test::require(image.memory.read32(kData, 0) == 0x02030405 &&
                          image.memory.read32(kData + 4, 0) == 0x06070607,
                      "OSBlockMove crosses adjacent maps moving forward");

        for (uint32_t index = 0; index < 8; ++index) {
            image.memory.write8(kData + index, static_cast<uint8_t>(index), 0);
        }
        invoke(machine, move, {kData + 2, kData, 6, 0});
        test::require(image.memory.read32(kData, 0) == 0x00010001 &&
                          image.memory.read32(kData + 4, 0) == 0x02030405,
                      "OSBlockMove crosses adjacent maps moving backward");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            move, nwii::runtime::ImportTarget{"coreinit", "OSBlockMove"});
        image.memory.map(kData, 8, {true, true, false});
        image.memory.map(kData + 0x20, 4, {true, true, false});
        image.memory.write32(kData + 0x20, 0xAABBCCDD, 0);
        Machine machine(image);
        const auto stop =
            invoke(machine, move, {kData + 0x20, kData, 8, 0});
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.fault_address == kData + 0x20 &&
                          stop.fault_width == 8 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::write &&
                          image.memory.read32(kData + 0x20, 0) == 0xAABBCCDD,
                      "destination preflight prevents partial mutation");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            move, nwii::runtime::ImportTarget{"coreinit", "OSBlockMove"});
        image.memory.map(kData, 4, {true, true, false});
        image.memory.map(kData + 0x20, 8, {true, true, false});
        image.memory.write64(kData + 0x20, 0xAABBCCDDEEFF0011ULL, 0);
        Machine machine(image);
        const auto stop =
            invoke(machine, move, {kData + 0x20, kData, 8, 0});
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.fault_address == kData &&
                          stop.fault_width == 8 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::read &&
                          image.memory.read64(kData + 0x20, 0) ==
                              0xAABBCCDDEEFF0011ULL,
                      "source preflight prevents destination mutation");
    }
    {
        auto image = make_image();
        image.imports.emplace(
            move, nwii::runtime::ImportTarget{"coreinit", "OSBlockMove"});
        constexpr uint32_t destination = kData + 0x20;
        const std::array<uint8_t, 4> trailing{0xEE, 0xFF, 0x00, 0x11};
        image.memory.map(kData, 8, {true, true, false});
        image.memory.map(destination, 4, {true, true, false});
        image.memory.map(destination + 4, 4, {true, false, false}, trailing);
        image.memory.write32(destination, 0xAABBCCDD, 0);
        Machine machine(image);
        const auto stop = invoke(machine, move, {destination, kData, 8, 0});
        test::require(
            stop.category == StopCategory::guest_fault &&
                stop.fault_address == destination && stop.fault_width == 8 &&
                stop.fault_access == nwii::runtime::MemoryAccess::write &&
                image.memory.read32(destination, 0) == 0xAABBCCDD &&
                image.memory.read32(destination + 4, 0) == 0xEEFF0011,
            "second-map permission preflight prevents destination mutation");
    }

}

void test_os_block_set() {
    constexpr uint32_t set = kImportBase;

    {
        auto image = make_image();
        image.imports.emplace(
            set, nwii::runtime::ImportTarget{"coreinit", "OSBlockSet"});
        image.memory.map(kData, 8, {true, true, false});
        Machine machine(image);
        for (uint32_t index = 0; index < 8; ++index) {
            image.memory.write8(kData + index, 0xFF, 0);
        }

        const auto stop = invoke(machine, set, {kData, 0x1234, 8, 0});
        test::require(stop.category != StopCategory::guest_fault &&
                          machine.main_context().gpr[3] == kData &&
                          image.memory.read64(kData, 0) ==
                              0x3434343434343434ULL,
                      "OSBlockSet fills exact low byte and returns dst");

        for (uint32_t index = 0; index < 8; ++index) {
            image.memory.write8(kData + index, 0xFF, 0);
        }
        invoke(machine, set, {kData + 1, 0xAB, 6, 0});
        test::require(image.memory.read64(kData, 0) == 0xFFABABABABABABFFULL,
                      "OSBlockSet fills only the requested interior bytes");

        const auto zero = invoke(machine, set, {0x11110000, 0xCD, 0, 0});
        test::require(zero.category != StopCategory::guest_fault,
                      "zero-sized OSBlockSet accepts unmapped pointers");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            set, nwii::runtime::ImportTarget{"coreinit", "OSBlockSet"});
        image.memory.map(kData, 4, {true, true, false});
        image.memory.map(kData + 4, 4, {true, true, false});
        Machine machine(image);
        invoke(machine, set, {kData, 0x55, 8, 0});
        test::require(image.memory.read32(kData, 0) == 0x55555555 &&
                          image.memory.read32(kData + 4, 0) == 0x55555555,
                      "OSBlockSet fills across adjacent maps");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            set, nwii::runtime::ImportTarget{"coreinit", "OSBlockSet"});
        image.memory.map(kData, 4, {true, true, false});
        image.memory.write32(kData, 0xAABBCCDD, 0);
        Machine machine(image);
        const auto stop = invoke(machine, set, {kData, 0xAB, 8, 0});
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.fault_address == kData &&
                          stop.fault_width == 8 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::write &&
                          image.memory.read32(kData, 0) == 0xAABBCCDD,
                      "OSBlockSet gap preflight prevents partial mutation");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            set, nwii::runtime::ImportTarget{"coreinit", "OSBlockSet"});
        const std::array<uint8_t, 4> trailing{0xEE, 0xFF, 0x00, 0x11};
        image.memory.map(kData, 4, {true, true, false});
        image.memory.map(kData + 4, 4, {true, false, false}, trailing);
        image.memory.write32(kData, 0xAABBCCDD, 0);
        Machine machine(image);
        const auto stop = invoke(machine, set, {kData, 0xAB, 8, 0});
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.fault_address == kData &&
                          stop.fault_width == 8 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::write &&
                          image.memory.read32(kData, 0) == 0xAABBCCDD &&
                          image.memory.read32(kData + 4, 0) == 0xEEFF0011,
                      "OSBlockSet read-only preflight prevents any mutation");
    }
}

void test_dc_zero_range() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase, nwii::runtime::ImportTarget{"coreinit", "DCZeroRange"});
    const std::array<uint8_t, 8> bytes{
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    image.memory.map(kData, bytes.size(), {true, true, false}, bytes);
    Machine machine(image);
    const auto stop = invoke(machine, kImportBase, {kData + 2, 4, 0, 0});
    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read64(kData, 0) == 0xFFFF00000000FFFFULL,
        "DCZeroRange clears only the requested bytes");
}

void test_dc_flush_range() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase, nwii::runtime::ImportTarget{"coreinit", "DCFlushRange"});
    image.imports.emplace(
        kImportBase + 4,
        nwii::runtime::ImportTarget{"coreinit", "DCStoreRange"});
    const std::array<uint8_t, 4> bytes{0x11, 0x22, 0x33, 0x44};
    image.memory.map(kData, bytes.size(), {true, false, false}, bytes);
    Machine machine(image);
    const auto flush = invoke(machine, kImportBase, {kData, bytes.size(), 0, 0});
    const auto store =
        invoke(machine, kImportBase + 4, {kData, bytes.size(), 0, 0});
    test::require(
        flush.category == StopCategory::instruction_budget &&
            store.category == StopCategory::instruction_budget &&
            image.memory.read32(kData, 0) == 0x11223344,
        "DCFlushRange and DCStoreRange are unified-memory coherency no-ops");
}

void test_os_is_address_range_dc_valid() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSIsAddressRangeDCValid"});
    Machine machine(image);

    invoke(machine, kImportBase, {0x3E682B00, 0x40});
    test::require(machine.main_context().gpr[3] == 1,
                  "ordinary memory is data-cache valid");
    invoke(machine, kImportBase, {0xE7FFFFE0, 0x40});
    test::require(machine.main_context().gpr[3] == 0,
                  "range crossing the invalid cache window is rejected");
    invoke(machine, kImportBase, {0xEC000001, 0x20});
    test::require(machine.main_context().gpr[3] == 1,
                  "memory above the invalid cache window is valid");
}
void test_uc_open_and_close() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase, nwii::runtime::ImportTarget{"coreinit", "UCOpen"});
    image.imports.emplace(
        kImportBase + 4, nwii::runtime::ImportTarget{"coreinit", "UCClose"});
    image.imports.emplace(
        kImportBase + 8,
        nwii::runtime::ImportTarget{"coreinit", "UCReadSysConfig"});
    constexpr uint32_t second = kData + 0x54;
    constexpr uint32_t language_output = kData + 0xA8;
    constexpr uint32_t parental_output = kData + 0xAC;
    image.memory.map(kData, 0xAD, {true, true, false});
    constexpr std::string_view language_key = "cafe.language";
    constexpr std::string_view parental_key =
        "p_acct1.net_communication_on_game";
    for (size_t index = 0; index < language_key.size(); ++index) {
        image.memory.write8(kData + static_cast<uint32_t>(index),
                            static_cast<uint8_t>(language_key[index]), 0);
    }
    for (size_t index = 0; index < parental_key.size(); ++index) {
        image.memory.write8(second + static_cast<uint32_t>(index),
                            static_cast<uint8_t>(parental_key[index]), 0);
    }
    image.memory.write32(kData + 0x44, 3, 0);
    image.memory.write32(kData + 0x4C, 4, 0);
    image.memory.write32(kData + 0x50, language_output, 0);
    image.memory.write32(second + 0x44, 1, 0);
    image.memory.write32(second + 0x4C, 1, 0);
    image.memory.write32(second + 0x50, parental_output, 0);
    image.memory.write8(parental_output, 0xFF, 0);
    Machine machine(image);

    const auto opened = invoke(machine, kImportBase, {0, 0, 0, 0});
    const uint32_t handle = machine.main_context().gpr[3];
    const auto read = invoke(machine, kImportBase + 8, {1, 2, kData, 0});
    const uint32_t language = image.memory.read32(language_output, 0);
    const auto closed = invoke(machine, kImportBase + 4, {1, 0, 0, 0});
    test::require(
        opened.category == StopCategory::instruction_budget && handle == 1 &&
            read.category == StopCategory::instruction_budget &&
            language == 1 && image.memory.read8(parental_output, 0) == 0 &&
            closed.category == StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 0,
        "UC service returns English and unrestricted game networking defaults");
}

void test_os_get_shared_data_loads_standard_font() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSGetSharedData"});
    image.memory.map(kData, 8, {true, true, false});
    const auto font_path =
        std::filesystem::temp_directory_path() /
        ("nwiiu-coreinit-font-" +
         std::to_string(reinterpret_cast<uintptr_t>(&image)));
    {
        std::ofstream font(font_path, std::ios::binary);
        font.write("font", 4);
    }
    Machine machine(image, {}, std::nullopt, font_path);
    std::filesystem::remove(font_path);

    const auto stop =
        invoke(machine, kImportBase, {2, 0, kData, kData + 4});
    const uint32_t font = image.memory.read32(kData, 0);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 1 &&
            image.memory.read32(kData + 4, 0) == 4 && font != 0 &&
            image.memory.read32(font, 0) == 0x666F6E74,
        "OSGetSharedData returns configured standard font bytes");
}

void test_os_dynload_set_allocator_validates_and_stores_callbacks() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSDynLoad_SetAllocator"});
    Machine machine(image);

    const auto valid = invoke(machine, kImportBase, {0x02000100, 0x02000200});
    const auto invalid = invoke(machine, kImportBase, {0, 0x02000200});
    test::require(
        valid.category == StopCategory::instruction_budget &&
            invalid.category == StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 0xBAD10017 &&
            CafeCoreinitTestAccess::dynload_alloc(machine.coreinit()) ==
                0x02000100 &&
            CafeCoreinitTestAccess::dynload_free(machine.coreinit()) ==
                0x02000200,
        "OSDynLoad_SetAllocator accepts two callbacks and rejects null");
}


void test_os_dynload_resolves_title_stubs_and_rejects_unknown_modules() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSDynLoad_Acquire"});
    image.imports.emplace(
        kImportBase + 4,
        nwii::runtime::ImportTarget{"coreinit", "OSDynLoad_FindExport"});
    constexpr uint32_t name = kData;
    constexpr uint32_t handle = kData + 0x20;
    constexpr uint32_t export_name = kData + 0x30;
    constexpr uint32_t export_address = kData + 0x80;
    image.memory.map(kData, 0x84, {true, true, false});
    constexpr char module[] = "erreula.rpl";
    constexpr char symbol[] = "ErrEulaGetStateErrorViewer__3RplFv";
    constexpr char keyboard_module[] = "swkbd.rpl";
    for (size_t i = 0; i < sizeof(module); ++i) {
        image.memory.write8(name + static_cast<uint32_t>(i), module[i], 0);
    }
    for (size_t i = 0; i < sizeof(symbol); ++i) {
        image.memory.write8(export_name + static_cast<uint32_t>(i), symbol[i],
                            0);
    }
    Machine machine(image);

    const auto acquired = invoke(machine, kImportBase, {name, handle});
    const uint32_t module_handle = image.memory.read32(handle, 0);
    const auto found = invoke(machine, kImportBase + 4,
                              {module_handle, 0, export_name, export_address});
    const uint32_t function = image.memory.read32(export_address, 0);
    const auto called = invoke(machine, function, {});
    for (size_t i = 0; i < sizeof(keyboard_module); ++i) {
        image.memory.write8(name + static_cast<uint32_t>(i),
                            keyboard_module[i], 0);
    }
    const auto keyboard_acquired =
        invoke(machine, kImportBase, {name, handle});
    const uint32_t keyboard_handle = image.memory.read32(handle, 0);
    const auto keyboard_found =
        invoke(machine, kImportBase + 4,
               {keyboard_handle, 0, export_name, export_address});
    const uint32_t keyboard_function =
        image.memory.read32(export_address, 0);
    const auto keyboard_called = invoke(machine, keyboard_function, {});
    image.memory.write8(name, 'x', 0);
    const auto missing = invoke(machine, kImportBase, {name, handle});
    test::require(
        acquired.category == StopCategory::instruction_budget &&
            found.category == StopCategory::instruction_budget &&
            called.category == StopCategory::instruction_budget &&
            keyboard_acquired.category == StopCategory::instruction_budget &&
            keyboard_found.category == StopCategory::instruction_budget &&
            keyboard_called.category == StopCategory::instruction_budget &&
            module_handle != 0 && function != 0 &&
            keyboard_handle != 0 && keyboard_handle != module_handle &&
            keyboard_function != 0 &&
            machine.main_context().gpr[3] == 0xBAD10023 &&
            image.memory.read32(handle, 0) == 0,
        "OSDynLoad resolves title dynamic APIs and rejects unknown modules");
}

void test_dc_flush_range_no_sync() {
    constexpr uint32_t flush = kImportBase;

    {
        auto image = make_image();
        image.imports.emplace(
            flush, nwii::runtime::ImportTarget{"coreinit", "DCFlushRangeNoSync"});
        image.memory.map(kData, 0x80, {true, true, false});
        for (uint32_t index = 0; index < 0x80; ++index) {
            image.memory.write8(kData + index,
                                static_cast<uint8_t>(0xA0 + (index & 0x0F)), 0);
        }
        Machine machine(image);

        const auto stop = invoke(machine, flush, {kData, 0x80, 0, 0});
        bool unchanged = true;
        for (uint32_t index = 0; index < 0x80; ++index) {
            unchanged = unchanged &&
                        image.memory.read8(kData + index, 0) ==
                            static_cast<uint8_t>(0xA0 + (index & 0x0F));
        }
        test::require(stop.category != StopCategory::missing_hle &&
                          stop.category != StopCategory::guest_fault &&
                          machine.main_context().gpr[3] == kData && unchanged,
                      "DCFlushRangeNoSync validates range as coherency no-op "
                      "and preserves the void ABI");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            flush, nwii::runtime::ImportTarget{"coreinit", "DCFlushRangeNoSync"});
        const std::array<uint8_t, 4> bytes{0x11, 0x22, 0x33, 0x44};
        image.memory.map(kData, 4, {true, false, false}, bytes);
        Machine machine(image);
        const auto stop = invoke(machine, flush, {kData, 4, 0, 0});
        test::require(stop.category != StopCategory::guest_fault &&
                          image.memory.read32(kData, 0) == 0x11223344,
                      "DCFlushRangeNoSync accepts read-only memory without "
                      "requiring write access");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            flush, nwii::runtime::ImportTarget{"coreinit", "DCFlushRangeNoSync"});
        image.memory.map(kData, 4, {true, true, false});
        image.memory.write32(kData, 0xAABBCCDD, 0);
        Machine machine(image);
        const auto stop = invoke(machine, flush, {kData, 8, 0, 0});
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.fault_address == kData &&
                          stop.fault_width == 8 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::read &&
                          image.memory.read32(kData, 0) == 0xAABBCCDD,
                      "DCFlushRangeNoSync faults atomically on an invalid "
                      "range without mutation");
    }

    {
        auto image = make_image();
        image.imports.emplace(
            flush, nwii::runtime::ImportTarget{"coreinit", "DCFlushRangeNoSync"});
        Machine machine(image);
        const auto stop = invoke(machine, flush, {0x11110000, 0, 0, 0});
        test::require(stop.category != StopCategory::guest_fault,
                      "zero-sized DCFlushRangeNoSync accepts unmapped pointers");
    }
}

void test_os_create_alarm_initializes_guest_object() {
    auto image = make_image();
    image.memory.map(kData, abi::kOsAlarmSize, {true, true, false});
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSCreateAlarm"});
    image.imports.emplace(
        kImportBase + 4,
        nwii::runtime::ImportTarget{"coreinit", "OSSetAlarmUserData"});
    image.imports.emplace(
        kImportBase + 8,
        nwii::runtime::ImportTarget{"coreinit", "OSSetPeriodicAlarm"});
    for (uint32_t offset = 0; offset < abi::kOsAlarmSize; offset += 4) {
        image.memory.write32(kData + offset, 0xA5A5A5A5, 0);
    }
    Machine machine(image);

    const auto stop = invoke(machine, kImportBase, {kData});
    test::require(stop.category == StopCategory::instruction_budget,
                  "OSCreateAlarm returns to the guest");
    for (uint32_t offset = 0; offset < abi::kOsAlarmSize; offset += 4) {
        const uint32_t expected =
            offset == abi::kAlarmTagOffset
                ? abi::kOsAlarmTag
                : offset == abi::kAlarmThreadQueueOffset +
                                  abi::kThreadQueueParentOffset
                      ? kData
                      : 0;
        test::require(image.memory.read32(kData + offset, 0) == expected,
                      "OSCreateAlarm initializes the complete alarm ABI");
    }

    const auto set_stop =
        invoke(machine, kImportBase + 4, {kData, 0x12345678});
    test::require(
        set_stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kData + abi::kAlarmUserDataOffset, 0) ==
                0x12345678,
        "OSSetAlarmUserData stores the guest pointer");

    const auto periodic_stop = invoke(
        machine, kImportBase + 8,
        {kData, 0xDEADBEEF, 0x11223344, 0x55667788, 0x99AABBCC,
         0xDDEEFF00, kReturn});
    test::require(
        periodic_stop.category == StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 1 &&
            image.memory.read32(kData + abi::kAlarmCallbackOffset, 0) ==
                kReturn &&
            image.memory.read64(kData + abi::kAlarmNextFireOffset, 0) ==
                0x1122334455667788ULL &&
            image.memory.read64(kData + abi::kAlarmPeriodOffset, 0) ==
                0x99AABBCCDDEEFF00ULL &&
            image.memory.read32(kData + abi::kAlarmStateOffset, 0) == 1 &&
            image.memory.read32(kData + abi::kAlarmContextOffset, 0) == 0,
        "OSSetPeriodicAlarm decodes aligned 64-bit arguments and arms alarm");
}

void test_os_get_core_id_returns_emulated_main_core() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSGetCoreId"});
    Machine machine(image);

    const auto stop = invoke(machine, kImportBase);
    test::require(stop.category == StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == 1,
                  "OSGetCoreId returns the emulated Cafe main core");
}

void test_interrupt_enable_state_round_trip() {
    constexpr uint32_t disable = kImportBase;
    constexpr uint32_t enable = kImportBase + 4;
    constexpr uint32_t restore = kImportBase + 8;
    constexpr uint32_t is_enabled = kImportBase + 12;
    auto image = make_image();
    image.imports.emplace(
        disable,
        nwii::runtime::ImportTarget{"coreinit", "OSDisableInterrupts"});
    image.imports.emplace(
        enable, nwii::runtime::ImportTarget{"coreinit", "OSEnableInterrupts"});
    image.imports.emplace(
        restore,
        nwii::runtime::ImportTarget{"coreinit", "OSRestoreInterrupts"});
    image.imports.emplace(
        is_enabled,
        nwii::runtime::ImportTarget{"coreinit", "OSIsInterruptEnabled"});
    Machine machine(image);

    test::require(invoke(machine, is_enabled).category ==
                          StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == 1,
                  "interrupts begin enabled");
    invoke(machine, disable);
    test::require(machine.main_context().gpr[3] == 1,
                  "OSDisableInterrupts returns previous enabled state");
    invoke(machine, disable);
    test::require(machine.main_context().gpr[3] == 0,
                  "second OSDisableInterrupts returns disabled state");
    invoke(machine, restore, {1});
    test::require(machine.main_context().gpr[3] == 0,
                  "OSRestoreInterrupts returns previous state");
    invoke(machine, enable);
    test::require(machine.main_context().gpr[3] == 1,
                  "OSEnableInterrupts returns previous enabled state");
}

void test_os_init_rendezvous_clears_core_flags() {
    auto image = make_image();
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSInitRendezvous"});
    image.imports.emplace(
        kImportBase + 4,
        nwii::runtime::ImportTarget{"coreinit", "OSWaitRendezvous"});
    image.memory.map(kData, 0x10, {true, true, false});
    for (uint32_t offset = 0; offset < 0x10; offset += 4) {
        image.memory.write32(kData + offset, 0xA5A5A5A5, 0);
    }
    Machine machine(image);

    const auto stop = invoke(machine, kImportBase, {kData});
    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kData, 0) == 0 &&
            image.memory.read32(kData + 4, 0) == 0 &&
            image.memory.read32(kData + 8, 0) == 0 &&
            image.memory.read32(kData + 0x0C, 0) == 0xA5A5A5A5,
        "OSInitRendezvous clears three core flags and preserves padding");

    CPUContext peer;
    peer.pc = kImportBase + 4;
    peer.lr = kReturn;
    peer.gpr[3] = kData;
    peer.gpr[4] = 2;
    const uint32_t peer_thread = machine.add_thread(peer, 17);
    machine.set_thread_affinity(peer_thread, 1);
    machine.block_thread(peer_thread, kData + 0x20);
    auto& main = machine.main_context();
    main.running = true;
    main.pc = kImportBase + 4;
    main.lr = kReturn;
    main.gpr[3] = kData;
    main.gpr[4] = 1;

    machine.run(1, 1);
    test::require(
        image.memory.read32(kData, 0) == 0 &&
            image.memory.read32(kData + 4, 0) == 1 &&
            image.memory.read8(machine.main_thread_address() +
                                   abi::kOsThreadStateOffset,
                               0) == abi::kThreadWaiting,
        "OSWaitRendezvous blocks core 1 until requested core 0 arrives");
    machine.wake_thread(peer_thread);
    machine.run(1, 1);
    test::require(machine.context(peer_thread).gpr[3] == 1,
                  "core 0 rendezvous returns TRUE");
    test::require(image.memory.read32(kData, 0) == 1,
                  "core 0 records its rendezvous arrival");
    test::require(image.memory.read32(kData + 4, 0) == 1,
                  "core 1 retains its rendezvous arrival");
    test::require(
        image.memory.read8(machine.main_thread_address() +
                               abi::kOsThreadStateOffset,
                           0) == abi::kThreadReady,
        "released core 1 thread returns to the ready queue");
    machine.run(1, 1);
    test::require(machine.main_context().gpr[3] == 1,
                  "released core 1 rendezvous returns TRUE");
}

void test_os_init_thread_queue_preserves_padding() {
    auto image = make_image();
    image.memory.map(kData, abi::kOsThreadQueueSize, {true, true, false});
    image.imports.emplace(
        kImportBase,
        nwii::runtime::ImportTarget{"coreinit", "OSInitThreadQueue"});
    for (uint32_t offset = 0; offset < abi::kOsThreadQueueSize; offset += 4) {
        image.memory.write32(kData + offset, 0xA5A5A5A5, 0);
    }
    Machine machine(image);

    const auto stop = invoke(machine, kImportBase, {kData});
    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kData, 0) == 0 &&
            image.memory.read32(kData + 4, 0) == 0 &&
            image.memory.read32(kData + 8, 0) == 0 &&
            image.memory.read32(kData + 12, 0) == 0xA5A5A5A5,
        "OSInitThreadQueue clears links and parent but preserves padding");
}

void test_os_init_event_initializes_guest_object() {
    auto image = make_image();
    image.memory.map(kData, abi::kOsEventSize, {true, true, false});
    image.imports.emplace(
        kImportBase, nwii::runtime::ImportTarget{"coreinit", "OSInitEvent"});
    for (uint32_t offset = 0; offset < abi::kOsEventSize; offset += 4) {
        image.memory.write32(kData + offset, 0xA5A5A5A5, 0);
    }
    Machine machine(image);

    const auto stop = invoke(machine, kImportBase, {kData, 1, 1});
    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kData, 0) == abi::kOsEventTag &&
            image.memory.read32(kData + abi::kEventNameOffset, 0) == 0 &&
            image.memory.read32(kData + 0x08, 0) == 0xA5A5A5A5 &&
            image.memory.read32(kData + abi::kEventValueOffset, 0) == 1 &&
            image.memory.read32(kData + abi::kEventQueueOffset, 0) == 0 &&
            image.memory.read32(kData + abi::kEventQueueOffset + 4, 0) == 0 &&
            image.memory.read32(kData + abi::kEventQueueOffset + 8, 0) ==
                kData &&
            image.memory.read32(kData + abi::kEventQueueOffset + 12, 0) ==
                0xA5A5A5A5 &&
            image.memory.read32(kData + abi::kEventModeOffset, 0) == 1,
        "OSInitEvent writes the Cafe event fields and preserves padding");
}

void test_auto_reset_event_wakes_one_waiter() {
    constexpr uint32_t init = kImportBase;
    constexpr uint32_t wait = kImportBase + 4;
    constexpr uint32_t signal = kImportBase + 8;
    auto image = make_image();
    image.memory.map(kData, abi::kOsEventSize, {true, true, false});
    image.imports.emplace(
        init, nwii::runtime::ImportTarget{"coreinit", "OSInitEvent"});
    image.imports.emplace(
        wait, nwii::runtime::ImportTarget{"coreinit", "OSWaitEvent"});
    image.imports.emplace(
        signal, nwii::runtime::ImportTarget{"coreinit", "OSSignalEvent"});
    Machine machine(image);
    invoke(machine, init, {kData, 0, 1});

    CPUContext peer;
    peer.pc = signal;
    peer.lr = kReturn;
    peer.gpr[3] = kData;
    const uint32_t peer_thread = machine.add_thread(peer, 17);
    machine.set_thread_affinity(peer_thread, 1);
    machine.block_thread(peer_thread, kData + 0x40);
    auto& main = machine.main_context();
    main.running = true;
    main.pc = wait;
    main.lr = kReturn;
    main.gpr[3] = kData;

    machine.run(1, 1);
    test::require(
        image.memory.read8(machine.main_thread_address() +
                               abi::kOsThreadStateOffset,
                           0) == abi::kThreadWaiting,
        "OSWaitEvent blocks on a cleared event");
    machine.wake_thread(peer_thread);
    machine.run(1, 1);
    test::require(
        image.memory.read32(kData + abi::kEventValueOffset, 0) == 0 &&
            image.memory.read8(machine.main_thread_address() +
                                   abi::kOsThreadStateOffset,
                               0) == abi::kThreadReady,
        "OSSignalEvent transfers an auto-reset signal to one waiter");
    machine.run(1, 1);
    test::require(
        machine.main_context().pc != wait &&
            image.memory.read8(machine.main_thread_address() +
                                   abi::kOsThreadStateOffset,
                               0) == abi::kThreadReady,
        "woken event waiter returns without blocking again");
}

void test_os_init_message_queue() {
    constexpr uint32_t init = kImportBase;
    constexpr uint32_t queue = kData;
    constexpr uint32_t messages = kData + 0x80;
    auto image = make_image();
    image.imports.emplace(
        init, nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
    image.memory.map(kData, 0x100, {true, true, false});
    Machine machine(image);
    for (uint32_t offset = 0; offset < abi::kOsMessageQueueSize; ++offset) {
        image.memory.write8(queue + offset, 0xA5, 0);
    }

    test::require(invoke(machine, init, {queue, messages, 0x20}).category ==
                          StopCategory::instruction_budget &&
                      image.memory.read32(queue, 0) ==
                          abi::kOsMessageQueueTag &&
                      image.memory.read32(queue + 4, 0) == 0 &&
                      image.memory.read32(queue + 8, 0) == 0xA5A5A5A5 &&
                      image.memory.read32(queue + 0x0C, 0) == 0 &&
                      image.memory.read32(queue + 0x10, 0) == 0 &&
                      image.memory.read32(queue + 0x14, 0) == queue &&
                      image.memory.read32(queue + 0x18, 0) == 0xA5A5A5A5 &&
                      image.memory.read32(queue + 0x1C, 0) == 0 &&
                      image.memory.read32(queue + 0x20, 0) == 0 &&
                      image.memory.read32(queue + 0x24, 0) == queue &&
                      image.memory.read32(queue + 0x28, 0) == 0xA5A5A5A5 &&
                      image.memory.read32(queue + 0x2C, 0) == messages &&
                      image.memory.read32(queue + 0x30, 0) == 0x20 &&
                      image.memory.read32(queue + 0x34, 0) == 0 &&
                      image.memory.read32(queue + 0x38, 0) == 0,
                  "OSInitMessageQueue writes the WUT queue fields only");

    auto split = make_image();
    split.imports.emplace(
        init, nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
    split.memory.map(kData, 0x20, {true, true, false});
    split.memory.map(kData + 0x20, abi::kOsMessageQueueSize - 0x20,
                     {true, true, false});
    split.memory.write32(kData, 0xAABBCCDD, 0);
    Machine split_machine(split);
    test::require(
        invoke(split_machine, init, {kData, messages, 0x20}).category ==
                StopCategory::guest_fault &&
            split.memory.read32(kData, 0) == 0xAABBCCDD,
        "split OSMessageQueue faults before mutating any field");
}

void test_os_send_message_ring_flags_and_atomicity() {
    constexpr uint32_t init = kImportBase;
    constexpr uint32_t send = kImportBase + 4;
    constexpr uint32_t queue = kData;
    constexpr uint32_t message_a = kData + 0x40;
    constexpr uint32_t message_b = kData + 0x50;
    constexpr uint32_t message_c = kData + 0x60;
    constexpr uint32_t buffer = kData + 0x100;
    constexpr std::array<uint32_t, 12> payload{
        0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00,
        0x21436587, 0xA9CBED0F, 0x02461357, 0x8ACE9BDF,
        0x0F1E2D3C, 0x4B5A6978, 0x8796A5B4, 0xC3D2E1F0};

    const auto make_queue_image = [&] {
        auto image = make_image();
        image.imports.emplace(
            init,
            nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
        image.imports.emplace(
            send, nwii::runtime::ImportTarget{"coreinit", "OSSendMessage"});
        image.memory.map(kData, 0x200, {true, true, false});
        for (uint32_t offset = 0; offset < 0x40; ++offset) {
            image.memory.write8(buffer + offset, 0xA5, 0);
        }
        for (uint32_t word = 0; word < 4; ++word) {
            image.memory.write32(message_a + word * 4, payload[word], 0);
            image.memory.write32(message_b + word * 4, payload[4 + word], 0);
            image.memory.write32(message_c + word * 4, payload[8 + word], 0);
        }
        return image;
    };
    const auto slot_holds = [&](ExecutionImage& image, uint32_t slot,
                                size_t payload_base) {
        for (uint32_t word = 0; word < 4; ++word) {
            if (image.memory.read32(buffer + slot * abi::kOsMessageSize +
                                        word * 4,
                                    0) != payload[payload_base + word]) {
                return false;
            }
        }
        return true;
    };
    const auto slot_untouched = [&](ExecutionImage& image, uint32_t slot) {
        for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
            if (image.memory.read8(
                    buffer + slot * abi::kOsMessageSize + offset, 0) != 0xA5) {
                return false;
            }
        }
        return true;
    };
    const auto ring = [&](ExecutionImage& image, uint32_t address) {
        return std::array<uint32_t, 2>{
            image.memory.read32(address + abi::kMessageQueueFirstOffset, 0),
            image.memory.read32(address + abi::kMessageQueueUsedOffset, 0)};
    };

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        const auto appended = invoke(machine, send, {queue, message_a, 0});
        test::require(
            appended.category == StopCategory::instruction_budget &&
                machine.main_context().gpr[3] == 1 &&
                ring(image, queue) == std::array<uint32_t, 2>{0, 1} &&
                slot_holds(image, 0, 0) && slot_untouched(image, 1),
            "OSSendMessage appends one big-endian copy at the ring tail");
        invoke(machine, send, {queue, message_b, 0});
        test::require(machine.main_context().gpr[3] == 1 &&
                          ring(image, queue) ==
                              std::array<uint32_t, 2>{0, 2} &&
                          slot_holds(image, 0, 0) && slot_holds(image, 1, 4),
                      "second send fills the next FIFO slot");
        const auto full = invoke(machine, send, {queue, message_c, 0});
        test::require(
            full.category == StopCategory::instruction_budget &&
                machine.main_context().gpr[3] == 0 &&
                ring(image, queue) == std::array<uint32_t, 2>{0, 2} &&
                slot_holds(image, 0, 0) && slot_holds(image, 1, 4),
            "non-blocking send to a full queue returns FALSE without mutation");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        image.memory.write32(queue + abi::kMessageQueueFirstOffset, 1, 0);
        image.memory.write32(queue + abi::kMessageQueueUsedOffset, 1, 0);
        invoke(machine, send, {queue, message_a, 0});
        test::require(machine.main_context().gpr[3] == 1 &&
                          ring(image, queue) ==
                              std::array<uint32_t, 2>{1, 2} &&
                          slot_holds(image, 0, 0) && slot_untouched(image, 1),
                      "ring tail wraps modulo queue size");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 4});
        image.memory.write32(queue + abi::kMessageQueueFirstOffset, 2, 0);
        image.memory.write32(queue + abi::kMessageQueueUsedOffset, 1, 0);
        invoke(machine, send, {queue, message_a, 2});
        test::require(machine.main_context().gpr[3] == 1 &&
                          ring(image, queue) ==
                              std::array<uint32_t, 2>{1, 2} &&
                          slot_holds(image, 1, 0) && slot_untouched(image, 0) &&
                          slot_untouched(image, 2) && slot_untouched(image, 3),
                      "high-priority send prepends in front of the ring head");
        auto wrap = make_queue_image();
        Machine wrap_machine(wrap);
        invoke(wrap_machine, init, {queue, buffer, 4});
        invoke(wrap_machine, send, {queue, message_b, 2});
        test::require(wrap_machine.main_context().gpr[3] == 1 &&
                          ring(wrap, queue) ==
                              std::array<uint32_t, 2>{3, 1} &&
                          slot_holds(wrap, 3, 4) && slot_untouched(wrap, 0),
                      "high-priority send wraps the head down from zero");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 1});
        invoke(machine, send, {queue, message_a, 0});
        const auto blocked = invoke(machine, send, {queue, message_b, 1});
        test::require(
            blocked.category == StopCategory::deadlock &&
                image.memory.read8(machine.main_thread_address() +
                                       abi::kOsThreadStateOffset,
                                   0) == abi::kThreadWaiting &&
                image.memory.read32(queue +
                                        abi::kMessageQueueSendQueueOffset,
                                    0) == machine.main_thread_address() &&
                image.memory.read32(queue +
                                        abi::kMessageQueueSendQueueOffset + 4,
                                    0) == machine.main_thread_address() &&
                ring(image, queue) == std::array<uint32_t, 2>{0, 1} &&
                slot_holds(image, 0, 0),
            "blocking send to a full queue parks on the guest send queue");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        image.memory.write32(queue, 0xDEADBEEF, 0);
        test::require(
            invoke(machine, send, {queue, message_a, 0}).category ==
                    StopCategory::guest_fault &&
                ring(image, queue) == std::array<uint32_t, 2>{0, 0} &&
                slot_untouched(image, 0),
            "corrupted queue tag faults before any mutation");
        auto forged_image = make_queue_image();
        Machine forged_machine(forged_image);
        constexpr uint32_t forged = kData + 0x1A0;
        forged_image.memory.write32(forged + abi::kMessageQueueTagOffset,
                                    abi::kOsMessageQueueTag, 0);
        forged_image.memory.write32(
            forged + abi::kMessageQueueMessagesOffset, buffer, 0);
        forged_image.memory.write32(forged + abi::kMessageQueueSizeOffset, 2,
                                    0);
        test::require(
            invoke(forged_machine, send, {forged, message_a, 0}).category ==
                    StopCategory::guest_fault &&
                slot_untouched(forged_image, 0),
            "forged tag without OSInitMessageQueue registration faults");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        image.memory.write32(queue + abi::kMessageQueueUsedOffset, 3, 0);
        test::require(
            invoke(machine, send, {queue, message_a, 0}).category ==
                    StopCategory::guest_fault &&
                image.memory.read32(queue + abi::kMessageQueueUsedOffset,
                                    0) == 3 &&
                slot_untouched(image, 0) && slot_untouched(image, 1),
            "corrupt ring counters fault before any mutation");
        auto huge = make_queue_image();
        Machine huge_machine(huge);
        invoke(huge_machine, init, {queue, buffer, 0x10000001});
        test::require(
            invoke(huge_machine, send, {queue, message_a, 0}).category ==
                    StopCategory::guest_fault &&
                huge.memory.read32(queue + abi::kMessageQueueUsedOffset,
                                   0) == 0 &&
                slot_untouched(huge, 0),
            "checked buffer byte count rejects size overflow");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        test::require(
            invoke(machine, send, {queue, 0x60000000, 0}).category ==
                    StopCategory::guest_fault &&
                ring(image, queue) == std::array<uint32_t, 2>{0, 0} &&
                slot_untouched(image, 0),
            "unmapped source message faults before any mutation");

        auto partial = make_image();
        partial.imports.emplace(
            init,
            nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
        partial.imports.emplace(
            send, nwii::runtime::ImportTarget{"coreinit", "OSSendMessage"});
        partial.memory.map(kData, 0x100, {true, true, false});
        partial.memory.map(buffer, abi::kOsMessageSize, {true, true, false});
        for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
            partial.memory.write8(buffer + offset, 0xA5, 0);
        }
        for (uint32_t word = 0; word < 4; ++word) {
            partial.memory.write32(message_a + word * 4, payload[word], 0);
        }
        Machine partial_machine(partial);
        invoke(partial_machine, init, {queue, buffer, 2});
        test::require(
            invoke(partial_machine, send, {queue, message_a, 0}).category ==
                    StopCategory::guest_fault &&
                partial.memory.read32(queue + abi::kMessageQueueUsedOffset,
                                      0) == 0 &&
                slot_untouched(partial, 0),
            "whole-buffer preflight faults even when the target slot is mapped");

        auto readonly = make_image();
        readonly.imports.emplace(
            send, nwii::runtime::ImportTarget{"coreinit", "OSSendMessage"});
        std::array<uint8_t, abi::kOsMessageQueueSize> queue_bytes{};
        const auto put_word = [&queue_bytes](uint32_t offset, uint32_t value) {
            queue_bytes[offset] = static_cast<uint8_t>(value >> 24);
            queue_bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
            queue_bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
            queue_bytes[offset + 3] = static_cast<uint8_t>(value);
        };
        put_word(abi::kMessageQueueTagOffset, abi::kOsMessageQueueTag);
        put_word(abi::kMessageQueueMessagesOffset, buffer);
        put_word(abi::kMessageQueueSizeOffset, 2);
        readonly.memory.map(queue, abi::kOsMessageQueueSize,
                            {true, false, false}, queue_bytes);
        readonly.memory.map(message_a, abi::kOsMessageSize,
                            {true, true, false});
        readonly.memory.map(buffer, 2 * abi::kOsMessageSize,
                            {true, true, false});
        for (uint32_t offset = 0; offset < 2 * abi::kOsMessageSize;
             ++offset) {
            readonly.memory.write8(buffer + offset, 0xA5, 0);
        }
        Machine readonly_machine(readonly);
        CafeCoreinitTestAccess::seed_message_queue(readonly_machine.coreinit(),
                                                   queue);
        test::require(
            invoke(readonly_machine, send, {queue, message_a, 0}).category ==
                    StopCategory::guest_fault &&
                slot_untouched(readonly, 0) && slot_untouched(readonly, 1),
            "whole-queue write preflight faults before buffer mutation");
    }
}

void test_os_receive_message_fifo_wrap_and_atomicity() {
    constexpr uint32_t init = kImportBase;
    constexpr uint32_t send = kImportBase + 4;
    constexpr uint32_t receive = kImportBase + 8;
    constexpr uint32_t queue = kData;
    constexpr uint32_t message_a = kData + 0x40;
    constexpr uint32_t message_b = kData + 0x50;
    constexpr uint32_t message_c = kData + 0x60;
    constexpr uint32_t out = kData + 0x70;
    constexpr uint32_t buffer = kData + 0x100;
    constexpr std::array<uint32_t, 12> payload{
        0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00,
        0x21436587, 0xA9CBED0F, 0x02461357, 0x8ACE9BDF,
        0x0F1E2D3C, 0x4B5A6978, 0x8796A5B4, 0xC3D2E1F0};

    const auto make_queue_image = [&] {
        auto image = make_image();
        image.imports.emplace(
            init,
            nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
        image.imports.emplace(
            send, nwii::runtime::ImportTarget{"coreinit", "OSSendMessage"});
        image.imports.emplace(
            receive,
            nwii::runtime::ImportTarget{"coreinit", "OSReceiveMessage"});
        image.memory.map(kData, 0x200, {true, true, false});
        for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
            image.memory.write8(out + offset, 0x5A, 0);
        }
        for (uint32_t word = 0; word < 4; ++word) {
            image.memory.write32(message_a + word * 4, payload[word], 0);
            image.memory.write32(message_b + word * 4, payload[4 + word], 0);
            image.memory.write32(message_c + word * 4, payload[8 + word], 0);
        }
        return image;
    };
    const auto out_holds = [&](ExecutionImage& image, size_t payload_base) {
        for (uint32_t word = 0; word < 4; ++word) {
            if (image.memory.read32(out + word * 4, 0) !=
                payload[payload_base + word]) {
                return false;
            }
        }
        return true;
    };
    const auto out_untouched = [&](ExecutionImage& image) {
        for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
            if (image.memory.read8(out + offset, 0) != 0x5A) {
                return false;
            }
        }
        return true;
    };
    const auto ring = [&](ExecutionImage& image) {
        return std::array<uint32_t, 2>{
            image.memory.read32(queue + abi::kMessageQueueFirstOffset, 0),
            image.memory.read32(queue + abi::kMessageQueueUsedOffset, 0)};
    };

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        invoke(machine, send, {queue, message_a, 0});
        invoke(machine, send, {queue, message_b, 0});
        const auto received = invoke(machine, receive, {queue, out, 0});
        test::require(
            received.category == StopCategory::instruction_budget &&
                machine.main_context().gpr[3] == 1 && out_holds(image, 0) &&
                ring(image) == std::array<uint32_t, 2>{1, 1},
            "OSReceiveMessage dequeues the FIFO head big-endian copy");
        invoke(machine, send, {queue, message_c, 0});
        invoke(machine, receive, {queue, out, 0});
        test::require(machine.main_context().gpr[3] == 1 &&
                          out_holds(image, 4) &&
                          ring(image) == std::array<uint32_t, 2>{0, 1},
                      "ring head wraps modulo queue size on dequeue");
        invoke(machine, receive, {queue, out, 0});
        test::require(machine.main_context().gpr[3] == 1 &&
                          out_holds(image, 8) &&
                          ring(image) == std::array<uint32_t, 2>{1, 0},
                      "wrapped slot is dequeued in FIFO order");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        const auto empty = invoke(machine, receive, {queue, out, 0});
        test::require(
            empty.category == StopCategory::instruction_budget &&
                machine.main_context().gpr[3] == 0 && out_untouched(image) &&
                ring(image) == std::array<uint32_t, 2>{0, 0},
            "non-blocking receive from an empty queue returns FALSE");
        const auto blocked = invoke(machine, receive, {queue, out, 1});
        test::require(
            blocked.category == StopCategory::deadlock &&
                image.memory.read8(machine.main_thread_address() +
                                       abi::kOsThreadStateOffset,
                                   0) == abi::kThreadWaiting &&
                image.memory.read32(queue +
                                        abi::kMessageQueueRecvQueueOffset,
                                    0) == machine.main_thread_address() &&
                image.memory.read32(
                    queue + abi::kMessageQueueRecvQueueOffset + 4, 0) ==
                    machine.main_thread_address() &&
                out_untouched(image),
            "blocking receive from an empty queue parks on the recv queue");
    }

    {
        auto image = make_queue_image();
        Machine machine(image);
        invoke(machine, init, {queue, buffer, 2});
        invoke(machine, send, {queue, message_a, 0});
        test::require(
            invoke(machine, receive, {queue, 0x60000000, 0}).category ==
                    StopCategory::guest_fault &&
                ring(image) == std::array<uint32_t, 2>{0, 1},
            "unmapped destination faults before dequeuing");

        auto readonly = make_image();
        readonly.imports.emplace(
            receive,
            nwii::runtime::ImportTarget{"coreinit", "OSReceiveMessage"});
        std::array<uint8_t, abi::kOsMessageQueueSize> queue_bytes{};
        const auto put_word = [&queue_bytes](uint32_t offset, uint32_t value) {
            queue_bytes[offset] = static_cast<uint8_t>(value >> 24);
            queue_bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
            queue_bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
            queue_bytes[offset + 3] = static_cast<uint8_t>(value);
        };
        put_word(abi::kMessageQueueTagOffset, abi::kOsMessageQueueTag);
        put_word(abi::kMessageQueueMessagesOffset, buffer);
        put_word(abi::kMessageQueueSizeOffset, 2);
        put_word(abi::kMessageQueueUsedOffset, 1);
        readonly.memory.map(queue, abi::kOsMessageQueueSize,
                            {true, false, false}, queue_bytes);
        readonly.memory.map(out, abi::kOsMessageSize, {true, true, false});
        for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
            readonly.memory.write8(out + offset, 0x5A, 0);
        }
        readonly.memory.map(buffer, 2 * abi::kOsMessageSize,
                            {true, true, false});
        Machine readonly_machine(readonly);
        CafeCoreinitTestAccess::seed_message_queue(readonly_machine.coreinit(),
                                                   queue);
        test::require(
            invoke(readonly_machine, receive, {queue, out, 0}).category ==
                    StopCategory::guest_fault &&
                out_untouched(readonly),
            "whole-queue write preflight faults before copying out");
    }
}

void test_os_create_thread_abi_and_atomic_validation() {
    constexpr uint32_t create = kImportBase;
    constexpr uint32_t exit_thread = kImportBase + 4;
    constexpr uint32_t thread = 0x3BC35DF0;
    constexpr uint32_t entry = kReturn;
    constexpr uint32_t argv = 0x3BC2DB08;
    constexpr uint32_t stack = 0x3BC35DE0;
    constexpr uint32_t stack_size = 0x8000;

    auto image = make_image();
    image.imports.emplace(
        create, nwii::runtime::ImportTarget{"coreinit", "OSCreateThread"});
    image.imports.emplace(
        exit_thread, nwii::runtime::ImportTarget{"coreinit", "OSExitThread"});
    Machine machine(image);
    for (uint32_t offset = 0; offset < abi::kOsThreadSize; ++offset) {
        image.memory.write8(thread + offset, 0xA5, 0);
    }
    image.memory.write32(stack - stack_size, 0xA5A5A5A5, 0);

    const auto stop = invoke(machine, create,
                             {thread, entry, 3, argv, stack, stack_size, 17, 0});
    test::require(
        stop.category == StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 1,
        "OSCreateThread consumes the exact eight-register ABI and returns TRUE");
    const auto& created = machine.context(thread);
    test::require(
        created.pc == entry && created.lr == exit_thread &&
            created.gpr[1] == stack - 8 && created.gpr[3] == 3 &&
            created.gpr[4] == argv && created.instruction_count == 0,
        "created host CPU context preserves entry, stack, argc, argv, and exit LR");
    test::require(
        image.memory.read64(thread, 0) == abi::kOsContextTag &&
            image.memory.read32(thread + 0x0C, 0) == stack - 8 &&
            image.memory.read32(thread + 0x14, 0) == 3 &&
            image.memory.read32(thread + 0x18, 0) == argv &&
            image.memory.read32(thread + 0x8C, 0) == exit_thread &&
            image.memory.read32(thread + 0x98, 0) == entry,
        "created guest OSContext mirrors the reached CPU context");
    test::require(
        image.memory.read32(thread + abi::kOsThreadTagOffset, 0) ==
                abi::kOsThreadTag &&
            image.memory.read8(thread + abi::kOsThreadStateOffset, 0) ==
                abi::kThreadReady &&
            image.memory.read8(thread + abi::kOsThreadAttrOffset, 0) == 7 &&
            image.memory.read16(thread + abi::kOsThreadIdOffset, 0) == 2 &&
            image.memory.read32(thread + 0x328, 0) == 1 &&
            image.memory.read32(thread + abi::kOsThreadPriorityOffset, 0) ==
                81 &&
            image.memory.read32(
                thread + abi::kOsThreadBasePriorityOffset, 0) == 81 &&
            image.memory.read32(thread + abi::kOsThreadStackStartOffset, 0) ==
                stack &&
            image.memory.read32(thread + abi::kOsThreadStackEndOffset, 0) ==
                stack - stack_size &&
            image.memory.read32(thread + 0x39C, 0) == entry &&
            image.memory.read32(thread + abi::kOsThreadTypeOffset, 0) == 2,
        "created OSThread has source layout, App priorities, inherited affinity, and suspend count");
    test::require(
        image.memory.read32(stack - 4, 0) == 0 &&
            image.memory.read32(stack - 8, 0) == 0 &&
            image.memory.read32(stack - stack_size, 0) == 0xDEADBABE,
        "OSCreateThread initializes aligned stack words and bottom sentinel");

    machine.main_context().pc = entry;
    const auto before = machine.context(thread).instruction_count;
    machine.run(1, 1);
    test::require(machine.context(thread).instruction_count == before,
                  "created suspended thread is not scheduler-visible");

    auto invalid = make_image();
    invalid.imports.emplace(
        create, nwii::runtime::ImportTarget{"coreinit", "OSCreateThread"});
    invalid.imports.emplace(
        exit_thread, nwii::runtime::ImportTarget{"coreinit", "OSExitThread"});
    invalid.memory.map(kData, abi::kOsThreadSize, {true, true, false});
    invalid.memory.write32(kData, 0xA5A5A5A5, 0);
    Machine invalid_machine(invalid);
    invalid.memory.write32(stack - stack_size, 0xA5A5A5A5, 0);
    test::require(
        invoke(invalid_machine, create,
               {kData, kData, 0, argv, stack, stack_size, 17, 0})
                .category == StopCategory::guest_fault &&
            invalid.memory.read32(kData, 0) == 0xA5A5A5A5 &&
            invalid.memory.read32(stack - stack_size, 0) == 0xA5A5A5A5,
        "non-executable entry faults before guest or host thread mutation");
    test::require_throws(
        [&] { static_cast<void>(invalid_machine.context(kData)); },
        "unknown Cafe thread", "invalid create leaks no Machine thread record");

    auto four_aligned = make_image();
    four_aligned.imports.emplace(
        create, nwii::runtime::ImportTarget{"coreinit", "OSCreateThread"});
    four_aligned.imports.emplace(
        exit_thread, nwii::runtime::ImportTarget{"coreinit", "OSExitThread"});
    Machine four_aligned_machine(four_aligned);
    test::require(
        invoke(four_aligned_machine, create,
               {thread, entry, 0, argv, stack + 4, stack_size, 17, 0})
                    .category == StopCategory::instruction_budget &&
            four_aligned_machine.context(thread).gpr[1] == stack - 8 &&
            four_aligned.memory.read32(
                thread + abi::kOsThreadStackStartOffset, 0) == stack + 4,
        "OSCreateThread aligns a four-byte-aligned stack down for the context");

    auto bad_priority = make_image();
    bad_priority.imports.emplace(
        create, nwii::runtime::ImportTarget{"coreinit", "OSCreateThread"});
    bad_priority.imports.emplace(
        exit_thread, nwii::runtime::ImportTarget{"coreinit", "OSExitThread"});
    Machine bad_priority_machine(bad_priority);
    test::require(
        invoke(bad_priority_machine, create,
               {thread, entry, 0, argv, stack, stack_size, 32, 0})
                .category == StopCategory::guest_fault &&
            bad_priority.memory.read32(thread, 0) == 0,
        "out-of-range App priority faults without creating the thread");
}

void test_os_set_thread_name_preserves_guest_pointer() {
    constexpr uint32_t set_name = kImportBase;
    constexpr uint32_t name = 0x10144D94;
    auto image = make_image();
    image.imports.emplace(
        set_name, nwii::runtime::ImportTarget{"coreinit", "OSSetThreadName"});
    Machine machine(image);
    const uint32_t thread = machine.main_thread_address();

    test::require(
        invoke(machine, set_name, {thread, name}).category ==
                StopCategory::instruction_budget &&
            image.memory.read32(thread + abi::kOsThreadNameOffset, 0) == name &&
            machine.main_context().gpr[3] == thread,
        "OSSetThreadName stores the pointer without dereferencing it");

    auto invalid = make_image();
    invalid.imports.emplace(
        set_name, nwii::runtime::ImportTarget{"coreinit", "OSSetThreadName"});
    Machine invalid_machine(invalid);
    test::require(
        invoke(invalid_machine, set_name, {0x70000000, name}).category ==
            StopCategory::guest_fault,
        "OSSetThreadName faults on an unwritable thread object");
}

void test_os_set_thread_affinity_updates_scheduler_metadata() {
    constexpr uint32_t set_affinity = kImportBase;
    auto image = make_image();
    image.imports.emplace(
        set_affinity,
        nwii::runtime::ImportTarget{"coreinit", "OSSetThreadAffinity"});
    Machine machine(image);
    const uint32_t thread = machine.main_thread_address();
    image.memory.write8(thread + abi::kOsThreadAttrOffset, 0xA7, 0);

    test::require(
        invoke(machine, set_affinity, {thread, 2}).category ==
                StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 1 &&
            image.memory.read8(thread + abi::kOsThreadAttrOffset, 0) == 0xA2,
        "OSSetThreadAffinity preserves non-affinity attributes and returns TRUE");

    constexpr uint32_t pending_thread = 0x10000000;
    auto pending = make_image();
    pending.memory.map(pending_thread, abi::kOsThreadSize,
                       {true, true, false});
    pending.imports.emplace(
        set_affinity,
        nwii::runtime::ImportTarget{"coreinit", "OSSetThreadAffinity"});
    pending.memory.write8(pending_thread + abi::kOsThreadAttrOffset, 0xA7, 0);
    Machine pending_machine(pending);
    test::require(
        invoke(pending_machine, set_affinity, {pending_thread, 4}).category ==
                StopCategory::instruction_budget &&
            pending_machine.main_context().gpr[3] == 1 &&
            pending.memory.read8(
                pending_thread + abi::kOsThreadAttrOffset, 0) == 0xA4,
        "OSSetThreadAffinity supports mapped thread objects before creation");
}

void test_os_resume_thread_returns_counter_and_hands_off() {
    constexpr uint32_t create = kImportBase;
    constexpr uint32_t resume = kImportBase + 4;
    constexpr uint32_t exit_thread = kImportBase + 8;
    constexpr uint32_t thread = 0x3BC35DF0;
    constexpr uint32_t stack = 0x3BC35DE0;
    constexpr uint32_t entry = 0x02000100;
    constexpr std::array<uint8_t, 4> increment{0x38, 0x63, 0x00, 0x01};
    auto image = make_image();
    image.memory.map(entry, 4, {true, false, true}, increment);
    image.imports.emplace(
        create, nwii::runtime::ImportTarget{"coreinit", "OSCreateThread"});
    image.imports.emplace(
        resume, nwii::runtime::ImportTarget{"coreinit", "OSResumeThread"});
    image.imports.emplace(
        exit_thread, nwii::runtime::ImportTarget{"coreinit", "OSExitThread"});
    Machine machine(image);

    test::require(
        invoke(machine, create,
               {thread, entry, 0x1234, kData, stack, 0x8000, 0, 2})
                .category == StopCategory::instruction_budget &&
            machine.context(thread).instruction_count == 0,
        "new high-priority thread stays suspended");

    auto& main = machine.main_context();
    main.pc = resume;
    main.lr = kReturn;
    main.gpr[3] = thread;
    const auto stop = machine.run(1, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            main.gpr[3] == 1 && main.pc == kReturn &&
            image.memory.read32(thread + abi::kOsThreadSuspendOffset, 0) == 0 &&
            machine.context(thread).instruction_count == 1 &&
            machine.context(thread).gpr[3] == 0x1235,
        "OSResumeThread returns old count, queues once, and hands off by priority");

    auto& worker = machine.context(thread);
    machine.clear_active_callback();
    worker.pc = exit_thread;
    worker.gpr[3] = 0x5678;
    const auto exit_stop = machine.run(1, 1);
    test::require(exit_stop.category == StopCategory::instruction_budget,
                  "OSExitThread reschedules another runnable thread");
    const uint32_t val = image.memory.read32(thread + abi::kOsThreadExitValueOffset, 0);
    std::cout << "EXIT VAL: 0x" << std::hex << val << std::dec << std::endl;
    test::require(val == 0x5678, "OSExitThread records the result");
    test::require(
        image.memory.read8(thread + abi::kOsThreadStateOffset, 0) ==
            abi::kThreadMoribund,
        "OSExitThread removes the worker from dispatch");
}

void test_os_try_lock_mutex_lifecycle_and_faults() {
    constexpr uint32_t try_lock = kImportBase;
    constexpr uint32_t unlock = kImportBase + 4;

    auto image = make_image();
    image.imports.emplace(
        try_lock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    image.imports.emplace(
        unlock, nwii::runtime::ImportTarget{"coreinit", "OSUnlockMutex"});
    image.memory.map(kData, abi::kOsMutexSize, {true, true, false});
    Machine machine(image);
    machine.coreinit().init_mutex(kData, 0);
    const uint32_t current = machine.main_thread_address();

    test::require(
        invoke(machine, try_lock, {kData}).category ==
                StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 1 &&
            image.memory.read32(kData + abi::kMutexOwnerOffset, 0) == current &&
            image.memory.read32(kData + abi::kMutexRecursionOffset, 0) == 1 &&
            image.memory.read32(kData + abi::kMutexQueueHeadOffset, 0) == 0 &&
            image.memory.read32(kData + abi::kMutexQueueTailOffset, 0) == 0,
        "free OSTryLockMutex returns exact TRUE and mirrors ownership");
    invoke(machine, try_lock, {kData});
    test::require(
        machine.main_context().gpr[3] == 1 &&
            image.memory.read32(kData + abi::kMutexOwnerOffset, 0) == current &&
            image.memory.read32(kData + abi::kMutexRecursionOffset, 0) == 2,
        "recursive OSTryLockMutex returns exact TRUE and increments count");

    invoke(machine, unlock, {kData});
    test::require(
        image.memory.read32(kData + abi::kMutexOwnerOffset, 0) == current &&
            image.memory.read32(kData + abi::kMutexRecursionOffset, 0) == 1,
        "recursive unlock decrements without releasing ownership");
    invoke(machine, unlock, {kData});
    test::require(
        image.memory.read32(kData + abi::kMutexOwnerOffset, 0) == 0 &&
            image.memory.read32(kData + abi::kMutexRecursionOffset, 0) == 0,
        "final unlock reaches recursion zero");
    test::require(invoke(machine, unlock, {kData}).category ==
                      StopCategory::guest_fault,
                  "unowned unlock rejects recursion underflow");

    auto overflow = make_image();
    overflow.imports.emplace(
        try_lock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    overflow.memory.map(kData, abi::kOsMutexSize, {true, true, false});
    Machine overflow_machine(overflow);
    overflow_machine.coreinit().init_mutex(kData, 0);
    const uint32_t overflow_owner = overflow_machine.main_thread_address();
    CafeCoreinitTestAccess::seed_mutex(
        overflow_machine.coreinit(), kData, overflow_owner,
        std::numeric_limits<uint32_t>::max());
    overflow.memory.write32(kData + abi::kMutexOwnerOffset, overflow_owner, 0);
    overflow.memory.write32(kData + abi::kMutexRecursionOffset,
                            std::numeric_limits<uint32_t>::max(), 0);
    test::require(
        invoke(overflow_machine, try_lock, {kData}).category ==
                StopCategory::guest_fault &&
            CafeCoreinitTestAccess::mutex_owner(overflow_machine.coreinit(),
                                                kData) == overflow_owner &&
            CafeCoreinitTestAccess::mutex_recursion(
                overflow_machine.coreinit(), kData) ==
                std::numeric_limits<uint32_t>::max() &&
            overflow.memory.read32(kData + abi::kMutexRecursionOffset, 0) ==
                std::numeric_limits<uint32_t>::max(),
        "recursive OSTryLockMutex overflow faults without mutation");

    auto bad_tag = make_image();
    bad_tag.imports.emplace(
        try_lock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    bad_tag.memory.map(kData, abi::kOsMutexSize, {true, true, false});
    Machine bad_tag_machine(bad_tag);
    bad_tag_machine.coreinit().init_mutex(kData, 0);
    bad_tag.memory.write32(kData + abi::kMutexTagOffset, 0, 0);
    test::require(
        invoke(bad_tag_machine, try_lock, {kData}).category ==
                StopCategory::guest_fault &&
            CafeCoreinitTestAccess::mutex_owner(bad_tag_machine.coreinit(),
                                                kData) == 0 &&
            CafeCoreinitTestAccess::mutex_recursion(bad_tag_machine.coreinit(),
                                                    kData) == 0,
        "OSTryLockMutex validates the guest tag before mutation");

    auto invalid_address = make_image();
    invalid_address.imports.emplace(
        try_lock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    Machine invalid_address_machine(invalid_address);
    test::require(
        invoke(invalid_address_machine, try_lock, {kData}).category ==
            StopCategory::guest_fault,
        "OSTryLockMutex rejects unmapped guest objects");
    auto misaligned = make_image();
    misaligned.imports.emplace(
        try_lock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    Machine misaligned_machine(misaligned);
    test::require(
        invoke(misaligned_machine, try_lock, {kData + 2}).category ==
            StopCategory::guest_fault,
        "OSTryLockMutex rejects misaligned guest objects");

    auto readonly = make_image();
    readonly.imports.emplace(
        try_lock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    std::array<uint8_t, abi::kOsMutexSize> mutex_bytes{};
    mutex_bytes[0] = static_cast<uint8_t>(abi::kOsMutexTag >> 24);
    mutex_bytes[1] = static_cast<uint8_t>(abi::kOsMutexTag >> 16);
    mutex_bytes[2] = static_cast<uint8_t>(abi::kOsMutexTag >> 8);
    mutex_bytes[3] = static_cast<uint8_t>(abi::kOsMutexTag);
    readonly.memory.map(kData, abi::kOsMutexSize, {true, false, false},
                        mutex_bytes);
    Machine readonly_machine(readonly);
    CafeCoreinitTestAccess::seed_mutex(readonly_machine.coreinit(), kData, 0,
                                       0);
    test::require(
        invoke(readonly_machine, try_lock, {kData}).category ==
                StopCategory::guest_fault &&
            CafeCoreinitTestAccess::mutex_owner(readonly_machine.coreinit(),
                                                kData) == 0 &&
            CafeCoreinitTestAccess::mutex_recursion(readonly_machine.coreinit(),
                                                    kData) == 0 &&
            readonly.memory.read32(kData + abi::kMutexOwnerOffset, 0) == 0 &&
            readonly.memory.read32(kData + abi::kMutexRecursionOffset, 0) == 0,
        "OSTryLockMutex preflights the complete writable mirror atomically");
}

void test_im_is_dim_enabled_writes_runtime_parameter() {
    auto image = make_image();
    image.memory.map(kData, 0x10, {true, true, false});
    image.imports.emplace(
        kImportBase, nwii::runtime::ImportTarget{"coreinit", "IMIsDimEnabled"});
    image.imports.emplace(kImportBase + 4, nwii::runtime::ImportTarget{
                                               "coreinit", "IMEnableDim"});
    Machine machine(image);

    image.memory.write32(kData + 4, 0xDEADBEEF, 0);
    const auto stop = invoke(machine, kImportBase, {kData + 4});
    test::require(stop.category != StopCategory::guest_fault &&
                      machine.main_context().gpr[3] == 0,
                  "IMIsDimEnabled returns IMError OK");
    test::require(image.memory.read32(kData + 4, 0) == 1,
                  "IMIsDimEnabled reports the dim runtime parameter enabled");

    // Decaf IMEnableDim: NV dim_enable already TRUE -> OK without changes.
    const auto enable_stop = invoke(machine, kImportBase + 4, {});
    test::require(enable_stop.category == StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == 0 &&
                      image.memory.read32(kData + 4, 0) == 1,
                  "IMEnableDim returns IMError OK without state changes");

    for (const uint32_t output : {0u, 0x03000000u, kReturn}) {
        auto faulty = make_image();
        faulty.imports.emplace(kImportBase, nwii::runtime::ImportTarget{
                                                "coreinit", "IMIsDimEnabled"});
        Machine faulty_machine(faulty);
        const auto fault = invoke(faulty_machine, kImportBase, {output});
        test::require(fault.category == StopCategory::guest_fault &&
                          fault.fault_address == output &&
                          faulty_machine.main_context().pc == kImportBase,
                      "IMIsDimEnabled faults on unwritable output pointers");
    }
}

void test_os_enable_home_button_menu_stores_flag() {
    auto image = make_image();
    image.imports.emplace(kImportBase,
                          nwii::runtime::ImportTarget{
                              "coreinit", "OSEnableHomeButtonMenu"});
    Machine machine(image);
    test::require(machine.coreinit().home_button_menu_enabled(),
                  "home button menu starts enabled");

    const auto stop = invoke(machine, kImportBase, {0});
    test::require(stop.category == StopCategory::instruction_budget &&
                      machine.main_context().gpr[3] == 1 &&
                      !machine.coreinit().home_button_menu_enabled(),
                  "OSEnableHomeButtonMenu disables the flag and returns TRUE");

    invoke(machine, kImportBase, {1});
    test::require(machine.main_context().gpr[3] == 1 &&
                      machine.coreinit().home_button_menu_enabled(),
                  "OSEnableHomeButtonMenu re-enables the flag");
}

void test_wwhd_yaz0_decompressor() {
    auto image = make_image();
    constexpr uint32_t source = 0x60000000;
    constexpr uint32_t destination = source + 0x40;
    image.memory.map(source, 0x80, {true, true, false});
    constexpr std::array<uint8_t, 26> stream{
        'Y',  'a', 'z', '0', 0,    0,    0,    35,   0,    0,
        0,    0,   0,   0,   0,    0,    0xE0, 'A',  'B',  'C',
        0x70, 0x02, 0x00, 0x00, 0x05, 0x00};
    image.memory.write_bytes(source, stream, 0);
    Machine machine(image);

    const auto stop =
        invoke(machine, 0x0275F480, {destination, source});
    bool bytes_match = true;
    const char* expected = "ABCABCABCABC";
    for (uint32_t index = 0; index < 12; ++index) {
        bytes_match = bytes_match &&
                      image.memory.read8(destination + index, 0) ==
                          static_cast<uint8_t>(expected[index]);
    }
    test::require(
        stop.category == StopCategory::instruction_budget &&
            machine.main_context().gpr[3] == 35 && bytes_match,
        "WWHD Yaz0 decoder expands literals and overlapping back-references");
}

} // namespace

int main() {
    test_abi_constants_and_guest_fields();
    test_sparse_cafe_memory_and_collision();
    test_frame_and_expanded_heaps();
    test_exact_import_registration();
    test_default_heap_data_import_handlers();
    test_thread_time_exception_and_debug_handlers();
    test_ghs_file_lock_pointer();
    test_memcpy_memset_and_ghs_pointer_abi();
    test_scalar_preflights_reject_split_objects_atomically();
    test_os_get_system_info();
    test_os_block_move();
    test_os_block_set();
    test_dc_zero_range();
    test_uc_open_and_close();
    test_os_get_shared_data_loads_standard_font();
    test_os_dynload_set_allocator_validates_and_stores_callbacks();
    test_os_dynload_resolves_title_stubs_and_rejects_unknown_modules();
    test_dc_flush_range();
    test_os_is_address_range_dc_valid();
    test_dc_flush_range_no_sync();
    test_os_create_alarm_initializes_guest_object();
    test_os_get_core_id_returns_emulated_main_core();
    test_os_init_rendezvous_clears_core_flags();
    test_os_init_thread_queue_preserves_padding();
    test_os_init_event_initializes_guest_object();
    test_os_init_message_queue();
    test_os_send_message_ring_flags_and_atomicity();
    test_interrupt_enable_state_round_trip();
    test_os_receive_message_fifo_wrap_and_atomicity();
    test_auto_reset_event_wakes_one_waiter();
    test_os_create_thread_abi_and_atomic_validation();
    test_os_set_thread_name_preserves_guest_pointer();
    test_os_set_thread_affinity_updates_scheduler_metadata();
    test_os_resume_thread_returns_counter_and_hands_off();
    test_os_try_lock_mutex_lifecycle_and_faults();
    test_os_enable_home_button_menu_stores_flag();
    test_im_is_dim_enabled_writes_runtime_parameter();
    test_wwhd_yaz0_decompressor();
}
