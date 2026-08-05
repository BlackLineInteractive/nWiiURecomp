#include "runtime/cafe_abi.h"
#include "runtime/machine.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <type_traits>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::ExecutionImage;
using nwii::runtime::GuestMemory;
using nwii::runtime::HleAction;
using nwii::runtime::Machine;
using nwii::runtime::StopCategory;
namespace abi = nwii::runtime::abi;

static_assert(!std::is_copy_constructible_v<Machine>);
static_assert(!std::is_move_constructible_v<Machine>);
static_assert(!std::is_copy_assignable_v<Machine>);
static_assert(!std::is_move_assignable_v<Machine>);

constexpr uint32_t kCodeA = 0x02000000;
constexpr uint32_t kCodeB = 0x02000100;
constexpr uint32_t kReturnA = 0x02000200;
constexpr uint32_t kReturnB = 0x02000300;
constexpr uint32_t kLock = 0xC0002000;
constexpr uint32_t kUnlock = 0xC0002004;
constexpr uint32_t kTryLock = 0xC0002008;
constexpr uint32_t kSleep = 0xC000200C;
constexpr uint32_t kQueueInit = 0xC0002010;
constexpr uint32_t kQueueSend = 0xC0002014;
constexpr uint32_t kQueueReceive = 0xC0002018;
constexpr uint32_t kMutex = 0x10000000;
constexpr uint32_t kAudioRegister = 0xC0002020;
constexpr uint32_t kCallbackExit = 0xC0002024;
constexpr uint32_t kCallback = 0x02000400;
constexpr uint32_t kCallbackMarker = 0x10001000;
constexpr uint32_t kWaitForVsync = 0xC0002028;

void map_words(ExecutionImage& image, uint32_t address,
               std::array<uint32_t, 8> words) {
    std::array<uint8_t, 32> bytes{};
    for (size_t index = 0; index < words.size(); ++index) {
        bytes[index * 4] = static_cast<uint8_t>(words[index] >> 24);
        bytes[index * 4 + 1] = static_cast<uint8_t>(words[index] >> 16);
        bytes[index * 4 + 2] = static_cast<uint8_t>(words[index] >> 8);
        bytes[index * 4 + 3] = static_cast<uint8_t>(words[index]);
    }
    image.memory.map(address, static_cast<uint32_t>(bytes.size()),
                     {true, false, true}, bytes);
}

ExecutionImage make_image() {
    ExecutionImage image;
    image.stack_base = 0x4FF00000;
    image.stack_top = 0x50000000;
    constexpr std::array<uint32_t, 8> increments{
        0x38630001, 0x38630001, 0x38630001, 0x38630001,
        0x38630001, 0x38630001, 0x38630001, 0x38630001,
    };
    map_words(image, kCodeA, increments);
    map_words(image, kCodeB, increments);
    constexpr std::array<uint32_t, 8> nops{
        0x60000000, 0x60000000, 0x60000000, 0x60000000,
        0x60000000, 0x60000000, 0x60000000, 0x60000000,
    };
    map_words(image, kReturnA, nops);
    map_words(image, kReturnB, nops);
    return image;
}

void test_quantum_global_budget_ticks_and_fifo() {
    auto image = make_image();
    Machine machine(image);
    machine.main_context().pc = kCodeA;
    CPUContext second;
    second.pc = kCodeB;
    const uint32_t second_thread = machine.add_thread(second, 16);

    const auto stop = machine.run(4, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      stop.reason == "global instruction budget exhausted" &&
                      stop.instruction_count == 4,
                  "global instruction budget is separate from slices");
    test::require(machine.main_context().gpr[3] == 2 &&
                      machine.context(second_thread).gpr[3] == 2,
                  "equal-priority contexts use stable FIFO quanta");
    test::require(stop.pc == machine.context(second_thread).pc,
                  "scheduler stop reports the last active thread");
    test::require(machine.coreinit().ticks() == 4,
                  "Cafe ticks advance by executed instruction deltas");
}

void test_priority_selection() {
    {
        auto image = make_image();
        Machine machine(image);
        machine.main_context().pc = kCodeA;
        CPUContext higher_priority;
        higher_priority.pc = kCodeB;
        const uint32_t higher = machine.add_thread(higher_priority, 4);
        machine.set_thread_affinity(machine.main_thread_address(), 1);
        machine.set_thread_affinity(higher, 1);

        machine.run(3, 1);
        test::require(machine.main_context().gpr[3] == 0 &&
                          machine.context(higher).gpr[3] == 3,
                      "lowest numeric priority wins on the same CPU");
    }
    {
        auto image = make_image();
        Machine machine(image);
        machine.main_context().pc = kCodeA;
        CPUContext lower_priority;
        lower_priority.pc = kCodeB;
        const uint32_t lower = machine.add_thread(lower_priority, 17);
        machine.set_thread_affinity(lower, 4);

        machine.run(2, 1);
        test::require(machine.main_context().gpr[3] == 1 &&
                          machine.context(lower).gpr[3] == 1,
                      "lower-priority work progresses on another CPU");
    }
}

void test_cross_core_affinity_scheduling() {
    {
        auto image = make_image();
        Machine machine(image);
        machine.main_context().pc = kCodeA;
        CPUContext cpu2_only;
        cpu2_only.pc = kCodeB;
        const uint32_t second = machine.add_thread(cpu2_only, 16);
        machine.set_thread_affinity(machine.main_thread_address(), 1);
        machine.set_thread_affinity(second, 4);

        const auto stop = machine.run(2, 1);
        test::require(stop.category == StopCategory::instruction_budget &&
                          machine.main_context().gpr[3] == 1 &&
                          machine.context(second).gpr[3] == 1,
                      "CPU-specific threads run in stable FIFO order");
    }
    {
        auto image = make_image();
        Machine machine(image);
        machine.main_context().pc = kCodeA;
        CPUContext multi_affinity;
        multi_affinity.pc = kCodeB;
        const uint32_t second = machine.add_thread(multi_affinity, 16);
        machine.set_thread_affinity(machine.main_thread_address(), 2);
        machine.set_thread_affinity(second, 6);

        const auto stop = machine.run(2, 1);
        test::require(stop.category == StopCategory::instruction_budget &&
                          machine.main_context().gpr[3] == 1 &&
                          machine.context(second).gpr[3] == 1,
                      "CPU1 and multi-affinity threads run in stable FIFO order");
    }
}

void test_pinned_core_excludes_other_threads() {
    constexpr uint32_t kPin = 0xC0002FF0;
    constexpr uint32_t kUnpin = 0xC0002FF4;
    auto image = make_image();
    image.imports.emplace(
        kPin, nwii::runtime::ImportTarget{"test", "PinCurrentCore"});
    image.imports.emplace(
        kUnpin, nwii::runtime::ImportTarget{"test", "UnpinCurrentCore"});
    Machine machine(image);
    machine.executor().register_hle(
        kPin, [&machine](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = machine.current_core_id();
            machine.pin_current_core();
            return HleAction::return_to_lr;
        });
    machine.executor().register_hle(
        kUnpin, [&machine](CPUContext&, GuestMemory&) {
            machine.unpin_current_core();
            return HleAction::return_to_lr;
        });
    machine.main_context().pc = kPin;
    machine.main_context().lr = kReturnA;
    machine.run(1, 1);
    const uint32_t core = machine.main_context().gpr[3];

    CPUContext higher_priority;
    higher_priority.pc = kCodeB;
    const uint32_t second = machine.add_thread(higher_priority, 15);
    machine.set_thread_affinity(second, static_cast<uint8_t>(1u << core));

    machine.run(1, 1);
    test::require(machine.context(second).gpr[3] == 0,
                  "a pinned core runs only its owning Cafe thread");

    machine.main_context().pc = kUnpin;
    machine.main_context().lr = kReturnA;
    machine.run(1, 1);
    machine.run(1, 1);
    test::require(machine.context(second).gpr[3] == 1,
                  "unpinning releases the core to another Cafe thread");
}


void test_deadlock_does_not_spin() {
    auto image = make_image();
    Machine machine(image);
    machine.main_context().pc = kCodeA;
    CPUContext second;
    second.pc = kCodeB;
    const uint32_t second_thread = machine.add_thread(second, 16);
    machine.block_thread(machine.main_thread_address(), 0x1111);
    machine.block_thread(second_thread, 0x2222);

    const auto stop = machine.run(1000, 10);
    test::require(stop.category == StopCategory::deadlock &&
                      stop.instruction_count == 0 &&
                      machine.coreinit().ticks() == 0,
                  "no ready thread or future wake reports deadlock once");
    test::require(image.memory.read8(machine.main_thread_address() +
                                         abi::kOsThreadStateOffset,
                                     0) == abi::kThreadWaiting &&
                      image.memory.read8(second_thread +
                                             abi::kOsThreadStateOffset,
                                         0) == abi::kThreadWaiting,
                  "wait state mirrored before deadlock");
}

void test_mutex_block_unlock_retry_and_lr_return() {
    auto image = make_image();
    image.imports.emplace(kLock,
                          nwii::runtime::ImportTarget{"coreinit", "OSLockMutex"});
    image.imports.emplace(kUnlock, nwii::runtime::ImportTarget{
                                       "coreinit", "OSUnlockMutex"});
    image.memory.map(kMutex, abi::kOsMutexSize, {true, true, false});
    Machine machine(image);
    machine.coreinit().init_mutex(kMutex, 0);

    auto& owner = machine.main_context();
    owner.pc = kLock;
    owner.lr = kReturnB;
    owner.gpr[3] = kMutex;
    machine.run(1, 1);
    test::require(image.memory.read32(kMutex + abi::kMutexOwnerOffset, 0) ==
                          machine.main_thread_address() &&
                      image.memory.read32(
                          kMutex + abi::kMutexRecursionOffset, 0) == 1,
                  "first context owns initialized mutex");

    CPUContext waiter;
    waiter.pc = kLock;
    waiter.lr = kReturnA;
    waiter.gpr[3] = kMutex;
    const uint32_t waiter_thread = machine.add_thread(waiter, 4);
    owner.pc = kUnlock;
    owner.lr = kReturnB;
    owner.gpr[3] = kMutex;

    const auto stop = machine.run(2, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      stop.instruction_count == 2,
                  "blocked import consumes no global budget");
    const auto& resumed = machine.context(waiter_thread);
    test::require(resumed.pc == kReturnA + 4 && resumed.lr == kReturnA &&
                      resumed.history_size == 1 &&
                      resumed.pc_history[0] == kReturnA,
                  "wake retries blocked import then returns through LR");
    test::require(image.memory.read32(kMutex + abi::kMutexOwnerOffset, 0) ==
                          waiter_thread &&
                      image.memory.read32(
                          kMutex + abi::kMutexRecursionOffset, 0) == 1 &&
                      image.memory.read32(
                          kMutex + abi::kMutexQueueHeadOffset, 0) == 0 &&
                      image.memory.read32(
                          kMutex + abi::kMutexQueueTailOffset, 0) == 0,
                  "final unlock wakes and transfers ownership exactly once");
}
void test_contended_try_lock_does_not_enqueue_or_preempt() {
    auto image = make_image();
    image.imports.emplace(
        kLock, nwii::runtime::ImportTarget{"coreinit", "OSLockMutex"});
    image.imports.emplace(
        kTryLock,
        nwii::runtime::ImportTarget{"coreinit", "OSTryLockMutex"});
    image.memory.map(kMutex, abi::kOsMutexSize, {true, true, false});
    Machine machine(image);
    machine.coreinit().init_mutex(kMutex, 0);

    auto& owner = machine.main_context();
    owner.pc = kLock;
    owner.lr = kReturnB;
    owner.gpr[3] = kMutex;
    machine.run(1, 1);
    owner.pc = kCodeA;

    CPUContext contender;
    contender.pc = kTryLock;
    contender.lr = kReturnA;
    contender.gpr[3] = kMutex;
    const uint32_t contender_thread = machine.add_thread(contender, 4);

    const auto stop = machine.run(2, 2);
    const auto& after = machine.context(contender_thread);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            stop.instruction_count == 2 && after.gpr[3] == 0 &&
            after.instruction_count == 2 && after.pc == kReturnA + 8,
        "contended OSTryLockMutex returns exact FALSE and keeps running");
    test::require(
        machine.main_context().instruction_count == 1 &&
            image.memory.read8(contender_thread + abi::kOsThreadStateOffset,
                               0) == abi::kThreadReady,
        "contended OSTryLockMutex does not block or preempt");
    test::require(
        image.memory.read32(kMutex + abi::kMutexOwnerOffset, 0) ==
                machine.main_thread_address() &&
            image.memory.read32(kMutex + abi::kMutexRecursionOffset, 0) == 1 &&
            image.memory.read32(kMutex + abi::kMutexQueueHeadOffset, 0) == 0 &&
            image.memory.read32(kMutex + abi::kMutexQueueTailOffset, 0) == 0,
        "contended OSTryLockMutex does not mutate ownership or wait queues");
}

void test_sleep_ticks_timed_wake_and_handoff() {
    {
        auto image = make_image();
        image.imports.emplace(
            kSleep,
            nwii::runtime::ImportTarget{"coreinit", "OSSleepTicks"});
        Machine machine(image);
        auto& sleeper = machine.main_context();
        sleeper.pc = kSleep;
        sleeper.lr = kReturnA;
        sleeper.gpr[3] = 0;
        sleeper.gpr[4] = 100;

        const auto stop = machine.run(4, 4);
        test::require(stop.category == StopCategory::instruction_budget &&
                          stop.instruction_count == 4 &&
                          sleeper.pc == kReturnA + 16 &&
                          machine.coreinit().ticks() == 104,
                      "lone sleeper advances time to its deadline and wakes");
    }
    {
        auto image = make_image();
        image.imports.emplace(
            kSleep,
            nwii::runtime::ImportTarget{"coreinit", "OSSleepTicks"});
        Machine machine(image);
        auto& sleeper = machine.main_context();
        sleeper.pc = kSleep;
        sleeper.lr = kReturnA;
        sleeper.gpr[3] = 0;
        sleeper.gpr[4] = 4;
        CPUContext worker;
        worker.pc = kCodeB;
        worker.lr = kReturnB;
        const uint32_t worker_thread = machine.add_thread(worker, 17);

        const auto stop = machine.run(6, 2);
        test::require(
            stop.category == StopCategory::instruction_budget &&
                machine.context(worker_thread).gpr[3] == 4 &&
                machine.main_context().instruction_count == 2 &&
                machine.main_context().pc == kReturnA + 8,
            "sleeping thread yields to lower priority and wakes on elapsed ticks");
        test::require(image.memory.read8(machine.main_thread_address() +
                                             abi::kOsThreadStateOffset,
                                         0) != abi::kThreadWaiting,
                      "elapsed sleeper leaves the guest waiting state");
    }
    {
        auto image = make_image();
        image.imports.emplace(
            kSleep,
            nwii::runtime::ImportTarget{"coreinit", "OSSleepTicks"});
        Machine machine(image);
        auto& sleeper = machine.main_context();
        sleeper.pc = kSleep;
        sleeper.lr = kReturnA;
        sleeper.gpr[3] = 0xFFFFFFFF;
        sleeper.gpr[4] = 0xFFFFFFF6;

        const auto stop = machine.run(2, 2);
        test::require(stop.category == StopCategory::instruction_budget &&
                          stop.instruction_count == 2 &&
                          sleeper.pc == kReturnA + 8 &&
                          machine.coreinit().ticks() == 2,
                      "non-positive tick counts return without sleeping");
    }
}

void test_message_queue_send_wakes_blocked_receiver() {
    constexpr uint32_t queue = 0x10001000;
    constexpr uint32_t buffer = 0x10001040;
    constexpr uint32_t source = 0x10001050;
    constexpr uint32_t out = 0x10001060;
    auto image = make_image();
    image.imports.emplace(
        kQueueInit,
        nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
    image.imports.emplace(
        kQueueSend,
        nwii::runtime::ImportTarget{"coreinit", "OSSendMessage"});
    image.imports.emplace(
        kQueueReceive,
        nwii::runtime::ImportTarget{"coreinit", "OSReceiveMessage"});
    image.memory.map(queue, 0x100, {true, true, false});
    image.memory.write32(source, 0xCAFEF00D, 0);
    image.memory.write32(source + 4, 0x11111111, 0);
    image.memory.write32(source + 8, 0x22222222, 0);
    image.memory.write32(source + 12, 0x33333333, 0);
    Machine machine(image);

    auto& sender = machine.main_context();
    sender.pc = kQueueInit;
    sender.lr = kReturnB;
    sender.gpr[3] = queue;
    sender.gpr[4] = buffer;
    sender.gpr[5] = 1;
    machine.run(1, 1);

    CPUContext receiver;
    receiver.pc = kQueueReceive;
    receiver.lr = kReturnA;
    receiver.gpr[3] = queue;
    receiver.gpr[4] = out;
    receiver.gpr[5] = 1;
    const uint32_t receiver_thread = machine.add_thread(receiver, 4);
    sender.pc = kQueueSend;
    sender.lr = kReturnB;
    sender.gpr[3] = queue;
    sender.gpr[4] = source;
    sender.gpr[5] = 0;

    const auto stop = machine.run(2, 1);
    const auto& woken = machine.context(receiver_thread);
    test::require(stop.category == StopCategory::instruction_budget &&
                      stop.instruction_count == 2 &&
                      woken.gpr[3] == 1 && woken.pc == kReturnA + 8 &&
                      woken.instruction_count == 2,
                  "send wakes the blocked receiver exactly once through LR");
    test::require(image.memory.read32(out, 0) == 0xCAFEF00D &&
                      image.memory.read32(out + 12, 0) == 0x33333333 &&
                      image.memory.read32(queue + 0x38, 0) == 0 &&
                      image.memory.read32(queue + 0x1C, 0) == 0 &&
                      image.memory.read32(queue + 0x20, 0) == 0,
                  "woken receiver dequeues the sent message and clears waits");
    test::require(sender.gpr[3] == 1 && sender.pc == kReturnB &&
                      sender.instruction_count == 1,
                  "sender returns TRUE and hands off to the woken receiver");
}

void test_message_wake_runs_lower_priority_thread_on_other_core() {
    constexpr uint32_t queue = 0x10001000;
    constexpr uint32_t buffer = 0x10001040;
    constexpr uint32_t source = 0x10001050;
    constexpr uint32_t out = 0x10001060;
    auto image = make_image();
    image.imports.emplace(
        kQueueInit,
        nwii::runtime::ImportTarget{"coreinit", "OSInitMessageQueue"});
    image.imports.emplace(
        kQueueSend,
        nwii::runtime::ImportTarget{"coreinit", "OSSendMessage"});
    image.imports.emplace(
        kQueueReceive,
        nwii::runtime::ImportTarget{"coreinit", "OSReceiveMessage"});
    image.memory.map(queue, 0x100, {true, true, false});
    image.memory.write32(source, 0xCAFEF00D, 0);
    Machine machine(image);
    auto& sender = machine.main_context();
    sender.pc = kQueueInit;
    sender.lr = kReturnB;
    sender.gpr[3] = queue;
    sender.gpr[4] = buffer;
    sender.gpr[5] = 1;
    machine.run(1, 1);

    CPUContext receiver;
    receiver.pc = kQueueReceive;
    receiver.lr = kReturnA;
    receiver.gpr[3] = queue;
    receiver.gpr[4] = out;
    receiver.gpr[5] = 1;
    const uint32_t receiver_thread = machine.add_thread(receiver, 18);
    machine.set_thread_affinity(machine.main_thread_address(), 2);
    machine.set_thread_affinity(receiver_thread, 1);
    machine.block_thread(machine.main_thread_address(), 0xDEADBEEF);
    machine.run(1, 1);

    machine.wake_thread(machine.main_thread_address());
    sender.pc = kQueueSend;
    sender.lr = kReturnB;
    sender.gpr[3] = queue;
    sender.gpr[4] = source;
    sender.gpr[5] = 0;
    const auto stop = machine.run(2, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(out, 0) == 0xCAFEF00D,
        "cross-core wake runs a lower-priority blocked receiver");
}

void test_time_observes_current_slice_progress() {
    auto image = make_image();
    image.imports.emplace(
        kCodeA + 4,
        nwii::runtime::ImportTarget{"coreinit", "OSGetSystemTime"});
    Machine machine(image);
    machine.main_context().pc = kCodeA;
    machine.main_context().lr = kReturnA;

    machine.run(2, 2);
    test::require(machine.main_context().gpr[3] == 0 &&
                      machine.main_context().gpr[4] == 1 &&
                      machine.coreinit().ticks() == 2,
                  "system time includes instructions earlier in same slice");
}

void test_audio_callback_runs_without_consuming_guest_thread() {
    auto image = make_image();
    constexpr std::array<uint32_t, 8> callback{
        0x3CA01000, 0x90651000, 0x90251004, 0x4E800020,
        0x60000000, 0x60000000, 0x60000000, 0x60000000,
    };
    map_words(image, kCallback, callback);
    const std::array<uint8_t, 8> marker{};
    image.memory.map(kCallbackMarker, marker.size(), {true, true, false},
                     marker);
    image.imports.emplace(
        kAudioRegister,
        nwii::runtime::ImportTarget{"snd_core",
                                    "AXRegisterDeviceFinalMixCallback"});
    image.imports.emplace(
        kCodeA,
        nwii::runtime::ImportTarget{"coreinit", "OSSleepTicks"});
    image.imports.emplace(
        kCallbackExit,
        nwii::runtime::ImportTarget{"coreinit", "OSExitThread"});

    Machine machine(image);
    auto& main = machine.main_context();
    main.pc = kAudioRegister;
    main.lr = kCodeA;
    main.gpr[3] = 0;
    main.gpr[4] = kCallback;
    CPUContext callback_thread;
    callback_thread.pc = kCodeB;
    callback_thread.gpr[1] = 0x4FFF0000;
    callback_thread.gpr[3] = 10;
    const uint32_t callback_thread_address =
        machine.add_thread(callback_thread, 31);

    const auto stop = machine.run(5, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kCallbackMarker, main.pc) == 0x0101D000 &&
            image.memory.read32(kCallbackMarker + 4, main.pc) ==
                callback_thread.gpr[1] &&
            image.memory.read32(0x0101D000, main.pc) == 0x0101D010 &&
            image.memory.read32(0x0101D010, main.pc) == 0x0101D100 &&
            image.memory.read16(0x0101D004, main.pc) == 6 &&
            image.memory.read16(0x0101D006, main.pc) == 144 &&
            image.memory.read16(0x0101D008, main.pc) == 1 &&
            image.memory.read16(0x0101D00A, main.pc) == 6 &&
            main.gpr[3] == 0 &&
            machine.context(callback_thread_address).gpr[3] == 11,
        "AX callback runs asynchronously and restores its borrowed thread");
    image.memory.write32(kCallbackMarker, 0, main.pc);
    machine.cafe_runtime().service_audio_frame();
    const auto repeated = machine.run(4, 1);
    test::require(
        repeated.category == StopCategory::instruction_budget &&
            image.memory.read32(kCallbackMarker, main.pc) == 0x0101D000,
        "AX callback repeats on a serviced audio frame");
}

void test_vsync_wait_advances_one_sixtieth_second() {
    auto image = make_image();
    image.imports.emplace(
        kWaitForVsync,
        nwii::runtime::ImportTarget{"gx2", "GX2WaitForVsync"});
    Machine machine(image);
    machine.main_context().pc = kWaitForVsync;
    machine.main_context().lr = kReturnA;

    const auto stop = machine.run(2, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            machine.coreinit().ticks() == abi::kBusClockSpeed / 60 + 2,
        "GX2WaitForVsync advances one 60 Hz host frame");
}

void test_machine_binds_owned_filesystem_handlers() {
    auto image = make_image();
    image.imports.emplace(
        kCodeA,
        nwii::runtime::ImportTarget{"coreinit", "FSInit"});
    Machine machine(image, std::filesystem::current_path(), std::nullopt);
    CPUContext cpu;
    cpu.pc = kCodeA;
    cpu.lr = kReturnA;
    const auto stop = machine.executor().run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturnA + 4,
                  "Machine binds owned filesystem state into its router");
}

void test_machine_binds_owned_proc_ui_handlers() {
    auto image = make_image();
    image.imports.emplace(
        kCodeA, nwii::runtime::ImportTarget{"proc_ui", "ProcUIInit"});
    Machine machine(image);
    CPUContext cpu;
    cpu.pc = kCodeA;
    cpu.lr = kReturnA;
    cpu.gpr[3] = kCodeB;
    const auto stop = machine.executor().run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturnA + 4,
                  "Machine binds owned ProcUI state into its router");
}

void corrupting_native_thunk(CPUContext& cpu, GuestMemory& memory) {
    (void)memory;
    cpu.gpr[3] = 0xDEADBEEF;
    cpu.instruction_count += 1;
    cpu.pc = cpu.lr;
}

void test_machine_patches_wwhd_yaz0_decompressor() {
    auto image = make_image();
    constexpr uint32_t kDecoder = 0x0275F480;
    constexpr uint32_t kStream = 0x10100000;
    constexpr uint32_t kOutput = 0x10200000;
    // Yaz0 header (decoded size 35 at +0x04, payload at +0x10) followed by
    // literals 'A' 'B' 'C', a distance-3 length-9 back-reference, and a
    // distance-1 long-form run of 23 bytes.
    constexpr std::array<uint8_t, 26> stream{
        'Y',  'a', 'z', '0', 0,    0,    0,    35,   0,    0,
        0,    0,   0,   0,   0,    0,    0xE0, 'A',  'B',  'C',
        0x70, 0x02, 0x00, 0x00, 0x05, 0x00};
    image.memory.map(kStream, 32, {true, false, false}, stream);
    image.memory.map(kOutput, 64, {true, true, false});
    Machine machine(image);
    // A recompiled block at the decoder address must lose to the patch.
    machine.executor().register_native(kDecoder, 4,
                                       &corrupting_native_thunk);
    CPUContext cpu;
    cpu.pc = kDecoder;
    cpu.lr = kReturnA;
    cpu.gpr[3] = kOutput;
    cpu.gpr[4] = kStream;

    const auto stop = machine.executor().run(cpu, 40);

    bool bytes_match = true;
    const char* expected = "ABCABCABCABC";
    for (uint32_t index = 0; index < 12; ++index) {
        bytes_match = bytes_match &&
                      image.memory.read8(kOutput + index, cpu.pc) ==
                          static_cast<uint8_t>(expected[index]);
    }
    for (uint32_t index = 12; index < 35; ++index) {
        bytes_match =
            bytes_match && image.memory.read8(kOutput + index, cpu.pc) == 'C';
    }
    test::require(
        stop.category == StopCategory::instruction_budget &&
            bytes_match && cpu.gpr[3] == 35 &&
            image.memory.read8(kOutput + 35, cpu.pc) == 0 &&
            cpu.pc >= kReturnA,
        "Machine patches the WWHD Yaz0 decompressor with a native decoder");
}

} // namespace

int main() {
    test_quantum_global_budget_ticks_and_fifo();
    test_priority_selection();
    test_cross_core_affinity_scheduling();
    test_pinned_core_excludes_other_threads();
    test_deadlock_does_not_spin();
    test_mutex_block_unlock_retry_and_lr_return();
    test_contended_try_lock_does_not_enqueue_or_preempt();
    test_sleep_ticks_timed_wake_and_handoff();
    test_message_queue_send_wakes_blocked_receiver();
    test_message_wake_runs_lower_priority_thread_on_other_core();
    test_time_observes_current_slice_progress();
    test_audio_callback_runs_without_consuming_guest_thread();
    test_vsync_wait_advances_one_sixtieth_second();
    test_machine_binds_owned_filesystem_handlers();
    test_machine_binds_owned_proc_ui_handlers();
    test_machine_patches_wwhd_yaz0_decompressor();
}
