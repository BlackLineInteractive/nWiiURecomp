#include "runtime/machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nwii::runtime {
namespace {
// WWHD's SZS decompressor at 0x0275F480 decodes Yaz0 streams one byte at a
// time; interpreting it costs billions of guest instructions per boot. This
// native replacement mirrors the guest semantics exactly: r3 = destination,
// r4 = Yaz0 stream with the decoded size at +0x04 and payload at +0x10.
// Returns the decoded size in r3. When a back-reference would overrun the
// remaining output budget the guest exits without copying it; so do we.
constexpr uint32_t kWwhdYaz0Decode = 0x0275F480;

void wwhd_yaz0_decode(CPUContext& cpu, GuestMemory& memory) {
    if (std::getenv("NWIIU_YAZ0_TRACE") != nullptr) {
        std::fprintf(stderr, "YAZ0-HLE dst=%08X src=%08X\n", cpu.gpr[3],
                     cpu.gpr[4]);
    }
    const uint32_t pc = cpu.pc;
    uint32_t dst = cpu.gpr[3];
    uint32_t src = cpu.gpr[4];
    const uint32_t size = memory.read32(src + 4, pc);
    src += 0x10;
    int64_t remaining = size;
    uint32_t flags = 0;
    uint32_t flag_bits = 0;
    while (remaining > 0) {
        if (flag_bits == 0) {
            flags = memory.read8(src++, pc);
            flag_bits = 8;
        }
        if ((flags & 0x80u) != 0) {
            memory.write8(dst++, memory.read8(src++, pc), pc);
            --remaining;
        } else {
            const uint32_t first = memory.read8(src++, pc);
            const uint32_t second = memory.read8(src++, pc);
            const uint32_t distance = (((first & 0x0Fu) << 8) | second) + 1;
            uint32_t length = first >> 4;
            if (length == 0) {
                length = memory.read8(src++, pc) + 0x10u;
            }
            length += 2;
            remaining -= length;
            if (remaining < 0) {
                break;
            }
            for (uint32_t index = 0; index < length; ++index, ++dst) {
                memory.write8(dst, memory.read8(dst - distance, pc), pc);
            }
        }
        flags <<= 1;
        --flag_bits;
    }
    cpu.gpr[3] = size;
    cpu.instruction_count += 32 + size / 8;
    cpu.pc = cpu.lr;
}
} // namespace

Machine::Machine(ExecutionImage& image, std::filesystem::path title_root,
                 std::optional<std::filesystem::path> save_root,
                 std::optional<std::filesystem::path> shared_font)
    : image_(image), coreinit_(image, std::move(shared_font)),
      filesystem_(image, std::move(title_root), std::move(save_root)),
      olv_(image), cafe_runtime_(image), executor_(image) {
    coreinit_.attach_machine(*this);
    cafe_runtime_.attach_machine(*this);
    coreinit_.register_handlers(cafe_runtime_);
    filesystem_.register_handlers(cafe_runtime_);
    olv_.register_handlers(cafe_runtime_);
    cafe_runtime_.register_imports(executor_);
    executor_.register_patch(kWwhdYaz0Decode, &wwhd_yaz0_decode);

    CPUContext main;
    initialize_cpu(image_, main);
    threads_.push_back(
        {main, coreinit_.main_thread_address(), 16, ThreadState::ready, 7, 0,
         enqueue_sequence_++});
    coreinit_.initialize_thread(coreinit_.main_thread_address(), 1, 16,
                                image_.stack_top, image_.stack_base);
}

CPUContext& Machine::main_context() { return threads_.front().cpu; }

uint32_t Machine::main_thread_address() const {
    return coreinit_.main_thread_address();
}

uint32_t Machine::add_thread(CPUContext context, int32_t priority) {
    if (priority < 0 || priority > 31) {
        throw std::invalid_argument("thread priority must be 0..31");
    }
    if (threads_.size() != 1) {
        throw std::runtime_error("Cafe runtime thread metadata exhausted");
    }
    const uint32_t address = abi::kSecondThread;
    threads_.push_back({std::move(context), address, priority,
                        ThreadState::ready, 7, 0, enqueue_sequence_++});
    coreinit_.initialize_thread(address, 2, priority,
                                threads_.back().cpu.gpr[1], 0);
    return address;
}
void Machine::add_suspended_thread(uint32_t address, CPUContext context,
                                   int32_t priority, uint8_t affinity) {
    if (priority < 0 || priority > 31) {
        throw std::invalid_argument("thread priority must be 0..31");
    }
    if (affinity == 0 || (affinity & ~uint8_t{7}) != 0) {
        throw std::invalid_argument("thread affinity must select a Cafe core");
    }
    if (has_thread(address)) {
        throw std::invalid_argument("duplicate Cafe thread");
    }
    threads_.push_back({std::move(context), address, priority,
                        ThreadState::suspended, affinity, 0, 0});
}

bool Machine::has_thread(uint32_t address) const {
    return std::any_of(
        threads_.begin(), threads_.end(),
        [address](const Thread& candidate) { return candidate.address == address; });
}
void Machine::set_thread_affinity(uint32_t thread_address, uint8_t affinity) {
    thread(thread_address).affinity = affinity & 7;
}
void Machine::resume_thread(uint32_t thread_address) {
    auto& resumed = thread(thread_address);
    if (resumed.state == ThreadState::suspended) {
        enqueue(resumed);
    }
}




void Machine::dump_threads(std::ostream& output) const {
    for (const auto& item : threads_) {
        output << "THREAD address=0x" << std::hex << item.address
               << " pc=0x" << item.cpu.pc << " lr=0x" << item.cpu.lr
               << " entry=0x"
               << image_.memory.read32(item.address + abi::kOsThreadEntryOffset,
                                       item.cpu.pc)
               << " priority=" << std::dec << item.priority
               << " affinity=" << unsigned(item.affinity)
               << " state=" << static_cast<int>(item.state)
               << " wait=0x" << std::hex << item.wait_object
               << " sleeping=" << item.sleeping << '\n';
    }
}

Machine::Thread& Machine::thread(uint32_t address) {
    const auto found = std::find_if(
        threads_.begin(), threads_.end(),
        [address](const Thread& candidate) { return candidate.address == address; });
    if (found == threads_.end()) {
        throw std::invalid_argument("unknown Cafe thread");
    }
    return *found;
}

const Machine::Thread& Machine::thread(uint32_t address) const {
    const auto found = std::find_if(
        threads_.begin(), threads_.end(),
        [address](const Thread& candidate) { return candidate.address == address; });
    if (found == threads_.end()) {
        throw std::invalid_argument("unknown Cafe thread");
    }
    return *found;
}

CPUContext& Machine::context(uint32_t thread_address) {
    return thread(thread_address).cpu;
}

const CPUContext& Machine::context(uint32_t thread_address) const {
    return thread(thread_address).cpu;
}

Machine::Thread* Machine::select_ready(uint32_t excluded_thread) {
    const auto schedule = [this](Thread& thread, uint32_t core) {
        current_core_ = core;
        scheduled_threads_[scheduler_slot_++] = thread.address;
        if (scheduler_slot_ == scheduled_threads_.size()) {
            scheduler_slot_ = 0;
            scheduled_threads_.fill(0);
        }
        return &thread;
    };

    const auto core_available = [this](const Thread& thread, uint32_t core) {
        return pinned_threads_[core] == 0 ||
               pinned_threads_[core] == thread.address;
    };
    if (cross_core_wake_ != 0 && cross_core_wake_ != excluded_thread) {
        auto& woken = thread(cross_core_wake_);
        if (woken.state == ThreadState::ready && woken.affinity != 0) {
            cross_core_wake_ = 0;
            if (woken.pinned_core < 3 &&
                core_available(woken, woken.pinned_core)) {
                return schedule(woken, woken.pinned_core);
            }
            for (uint32_t core = 0; core < 3; ++core) {
                if ((woken.affinity & (1u << core)) != 0 &&
                    core != current_core_ && core_available(woken, core)) {
                    return schedule(woken, core);
                }
            }
            if (core_available(woken, current_core_)) {
                return schedule(woken, current_core_);
            }
        }
        cross_core_wake_ = 0;
    }

    constexpr std::array<uint32_t, 3> core_order{1, 0, 2};
    for (uint32_t tried = 0; tried < 6; ++tried) {
        const uint32_t core = core_order[scheduler_slot_];
        Thread* selected = nullptr;
        for (auto& candidate : threads_) {
            if (candidate.state != ThreadState::ready ||
                (candidate.affinity & (1u << core)) == 0 ||
                (candidate.pinned_core < 3 &&
                 candidate.pinned_core != core) ||
                !core_available(candidate, core) ||
                candidate.address == excluded_thread ||
                std::find(scheduled_threads_.begin(),
                          scheduled_threads_.end(),
                          candidate.address) != scheduled_threads_.end()) {
                continue;
            }
            if (selected == nullptr ||
                candidate.priority < selected->priority ||
                (candidate.priority == selected->priority &&
                 candidate.enqueue_sequence < selected->enqueue_sequence)) {
                selected = &candidate;
            }
        }
        if (selected != nullptr) {
            return schedule(*selected, core);
        }
        scheduled_threads_[scheduler_slot_++] = 0;
        if (scheduler_slot_ == scheduled_threads_.size()) {
            scheduler_slot_ = 0;
            scheduled_threads_.fill(0);
        }
    }
    return nullptr;
}

void Machine::enqueue(Thread& candidate) {
    candidate.state = ThreadState::ready;
    candidate.wait_object = 0;
    candidate.sleeping = false;
    candidate.enqueue_sequence = enqueue_sequence_++;
    coreinit_.set_thread_state(candidate.address, abi::kThreadReady);
}
void Machine::queue_callback(uint32_t function, uint32_t argument,
                             bool external) {
    if (function == 0) {
        return;
    }
    if (callback_return_address_ == 0) {
        const auto found = std::find_if(
            image_.imports.begin(), image_.imports.end(),
            [](const auto& item) {
                return item.second.module == "coreinit" &&
                       item.second.symbol == "OSExitThread";
            });
        if (found == image_.imports.end()) {
            throw std::logic_error("guest callback requires OSExitThread");
        }
        callback_return_address_ = found->first;
    }
    pending_callbacks_.push_back(
        {function, argument,
         external || current_ == nullptr ? 0 : current_->address});
}

void Machine::begin_callback(Thread& candidate) {
    const auto callback = pending_callbacks_.front();
    pending_callbacks_.pop_front();
    cafe_runtime_.prepare_audio_frame(callback.argument);
    if (std::getenv("NWIIU_CALLBACK_TRACE") != nullptr) {
        std::fprintf(stderr,
                     "CALLBACK begin function=%08X thread=%08X saved_pc=%08X "
                     "sp=%08X r3=%08X\n",
                     callback.function, candidate.address, candidate.cpu.pc,
                     candidate.cpu.gpr[1], candidate.cpu.gpr[3]);
    }
    callback_saved_context_ = candidate.cpu;
    active_callback_ = callback;
    callback_thread_address_ = candidate.address;
    candidate.cpu.pc = callback.function;
    candidate.cpu.lr = callback_return_address_;
    candidate.cpu.gpr[3] = callback.argument;
}

bool Machine::return_from_callback() {
    if (current_ == nullptr || !callback_saved_context_ ||
        current_->address != callback_thread_address_) {
        return false;
    }
    if (std::getenv("NWIIU_CALLBACK_TRACE") != nullptr) {
        std::fprintf(stderr,
                     "CALLBACK return thread=%08X callback_pc=%08X "
                     "restore_pc=%08X sp=%08X r3=%08X\n",
                     current_->address, current_->cpu.pc,
                     callback_saved_context_->pc,
                     callback_saved_context_->gpr[1],
                     callback_saved_context_->gpr[3]);
    }
    cafe_runtime_.capture_audio_frame(active_callback_->argument);
    const uint64_t instruction_count = current_->cpu.instruction_count;
    current_->cpu = std::move(*callback_saved_context_);
    current_->cpu.instruction_count = instruction_count;
    callback_saved_context_.reset();
    active_callback_.reset();
    callback_thread_address_ = 0;
    enqueue(*current_);
    return true;
}


ExecutionStop Machine::scheduler_stop(StopCategory category, const char* reason,
                                      uint64_t instruction_count) const {
    ExecutionStop stop;
    stop.category = category;
    stop.reason = reason;
    stop.instruction_count = instruction_count;
    if (!threads_.empty()) {
        const auto& cpu = last_active_thread_ == 0
                              ? threads_.front().cpu
                              : thread(last_active_thread_).cpu;
        stop.pc = cpu.pc;
        stop.lr = cpu.lr;
        stop.history_size = cpu.history_size;
        const size_t oldest =
            (cpu.history_cursor + cpu.pc_history.size() - cpu.history_size) %
            cpu.pc_history.size();
        for (size_t index = 0; index < cpu.history_size; ++index) {
            stop.history[index] =
                cpu.pc_history[(oldest + index) % cpu.pc_history.size()];
        }
    }
    executor_.snapshot_trace(stop, last_active_thread_);
    return stop;
}

ExecutionStop Machine::run(uint64_t global_instruction_budget,
                           uint64_t quantum) {
    if (quantum == 0) {
        throw std::invalid_argument("scheduler quantum must be positive");
    }
    uint64_t executed = 0;
    if (global_instruction_budget == 0) {
        return scheduler_stop(StopCategory::instruction_budget,
                              "global instruction budget exhausted", 0);
    }

    while (executed < global_instruction_budget) {
        wake_elapsed_sleepers();
        current_ = select_ready();
        if (current_ == nullptr) {
            if (wake_earliest_sleeper()) {
                continue;
            }
            return scheduler_stop(StopCategory::deadlock,
                                  "Cafe scheduler deadlock", executed);
        }
        if (!pending_callbacks_.empty() && !callback_saved_context_) {
            const uint32_t owner = pending_callbacks_.front().owner_thread;
            const bool owner_blocked =
                owner == 0 || !has_thread(owner) ||
                (thread(owner).state != ThreadState::ready &&
                 thread(owner).state != ThreadState::running);
            if (owner_blocked) {
                if (auto* callback_thread = select_ready(owner)) {
                    current_ = callback_thread;
                    begin_callback(*current_);
                }
            }
        }
        current_->state = ThreadState::running;
        coreinit_.set_thread_state(current_->address, abi::kThreadRunning);
        last_active_thread_ = current_->address;

        const uint64_t remaining = global_instruction_budget - executed;
        const uint64_t allowance = std::min(quantum, remaining);
        const uint64_t before = current_->cpu.instruction_count;
        slice_start_instruction_count_ = before;
        const uint64_t endpoint =
            before > std::numeric_limits<uint64_t>::max() - allowance
                ? std::numeric_limits<uint64_t>::max()
                : before + allowance;
        auto slice = executor_.run_slice(current_->cpu, endpoint,
                                         current_->address);
        const uint64_t delta = current_->cpu.instruction_count - before;
        executed += delta;
        coreinit_.advance_ticks(delta);

        if (slice.category == SliceCategory::terminal) {
            if (slice.terminal->category == StopCategory::guest_exit) {
                current_->state = ThreadState::moribund;
                coreinit_.set_thread_state(current_->address,
                                           abi::kThreadMoribund);
            }
            current_ = nullptr;
            slice.terminal->instruction_count = executed;
            return *slice.terminal;
        }
        if (current_->state == ThreadState::running) {
            enqueue(*current_);
        }
        current_ = nullptr;
    }

    return scheduler_stop(StopCategory::instruction_budget,
                          "global instruction budget exhausted", executed);
}

uint32_t Machine::current_thread_address() const {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    return current_->address;
}

uint32_t Machine::current_core_id() const {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    return current_core_;
}

void Machine::pin_current_core() {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    if ((current_->pinned_core < 3 &&
         current_->pinned_core != current_core_) ||
        (pinned_threads_[current_core_] != 0 &&
         pinned_threads_[current_core_] != current_->address)) {
        throw std::logic_error("Cafe core already pinned");
    }
    current_->pinned_core = static_cast<uint8_t>(current_core_);
    pinned_threads_[current_core_] = current_->address;
}

void Machine::unpin_current_core() {
    if (current_ == nullptr || current_->pinned_core != current_core_ ||
        pinned_threads_[current_core_] != current_->address) {
        throw std::logic_error("Cafe thread is not pinned to this core");
    }
    pinned_threads_[current_core_] = 0;
    current_->pinned_core = 0xFF;
}

int32_t Machine::current_thread_priority() const {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    return current_->priority;
}

uint64_t Machine::current_time_ticks() const {
    if (current_ == nullptr) {
        return coreinit_.ticks();
    }
    return coreinit_.ticks() +
           (current_->cpu.instruction_count -
            slice_start_instruction_count_);
}

void Machine::block_current(uint32_t wait_object) {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    block_thread(current_->address, wait_object);
}

void Machine::exit_current(int32_t value) {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    image_.memory.write32(
        current_->address + abi::kOsThreadExitValueOffset,
        static_cast<uint32_t>(value), current_->cpu.pc);
    current_->state = ThreadState::moribund;
    coreinit_.set_thread_state(current_->address, abi::kThreadMoribund);
}

void Machine::block_thread(uint32_t thread_address, uint32_t wait_object) {
    auto& blocked = thread(thread_address);
    if (blocked.state == ThreadState::moribund) {
        throw std::invalid_argument("cannot block moribund Cafe thread");
    }
    blocked.state = ThreadState::waiting;
    blocked.wait_object = wait_object;
    coreinit_.set_thread_state(blocked.address, abi::kThreadWaiting);
}

void Machine::wake_thread(uint32_t thread_address) {
    auto& woken = thread(thread_address);
    if (woken.state != ThreadState::waiting) {
        throw std::invalid_argument("Cafe thread is not waiting");
    }
    const bool can_run_on_another_core =
        current_ != nullptr &&
        (woken.affinity &
         static_cast<uint8_t>(7u & ~(1u << current_core_id()))) != 0;
    enqueue(woken);
    if (can_run_on_another_core && cross_core_wake_ == 0) {
        cross_core_wake_ = thread_address;
    }
}

void Machine::wake_waiters(uint32_t wait_object) {
    for (auto& candidate : threads_) {
        if (candidate.state == ThreadState::waiting &&
            candidate.wait_object == wait_object) {
            enqueue(candidate);
        }
    }
}

void Machine::sleep_current(uint64_t delta_ticks) {
    if (current_ == nullptr) {
        throw std::logic_error("no current Cafe thread");
    }
    current_->state = ThreadState::waiting;
    current_->wait_object = 0;
    current_->sleeping = true;
    current_->wake_tick = current_time_ticks() + delta_ticks;
    coreinit_.set_thread_state(current_->address, abi::kThreadWaiting);
}

void Machine::wake_elapsed_sleepers() {
    const uint64_t now = coreinit_.ticks();
    for (auto& candidate : threads_) {
        if (candidate.state == ThreadState::waiting && candidate.sleeping &&
            candidate.wake_tick <= now) {
            enqueue(candidate);
        }
    }
}

bool Machine::wake_earliest_sleeper() {
    Thread* earliest = nullptr;
    for (auto& candidate : threads_) {
        if (candidate.state == ThreadState::waiting && candidate.sleeping &&
            (earliest == nullptr ||
             candidate.wake_tick < earliest->wake_tick)) {
            earliest = &candidate;
        }
    }
    if (earliest == nullptr) {
        return false;
    }
    if (earliest->wake_tick > coreinit_.ticks()) {
        coreinit_.advance_ticks(earliest->wake_tick - coreinit_.ticks());
    }
    enqueue(*earliest);
    return true;
}
} // namespace nwii::runtime
