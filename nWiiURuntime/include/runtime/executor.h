#pragma once

#include "runtime/execution_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace nwii::runtime {
enum class StopCategory {
    input_error,
    guest_fault,
    missing_hle,
    instruction_budget,
    guest_exit,
    deadlock,
};

inline constexpr size_t kHleTraceCapacity = 32;

struct HleCallTrace {
    uint32_t address{};
    uint32_t thread_id{};
    std::string_view module;
    std::string_view symbol;
    bool returned{};
};

struct ExecutionStop {
    StopCategory category{};
    std::string reason;
    uint32_t pc{};
    uint32_t lr{};
    uint64_t instruction_count{};
    std::array<uint32_t, 32> history{};
    size_t history_size{};
    uint32_t fault_address{};
    uint32_t fault_width{};
    MemoryAccess fault_access{};
    std::optional<uint32_t> raw_instruction;
    std::string module;
    std::string symbol;
    std::array<uint32_t, 8> argument_gprs{};
    uint32_t active_thread{};
    std::array<HleCallTrace, kHleTraceCapacity> hle_calls{};
    size_t hle_call_count{};
    bool hle_trace_truncated{};
};

enum class HleAction { return_to_lr, reschedule, exit };
enum class SliceCategory { quantum, reschedule, terminal };

struct ExecutionSlice {
    SliceCategory category{};
    std::optional<ExecutionStop> terminal;
};

using NativeThunk = void (*)(CPUContext&, GuestMemory&);
using HleHandler = std::function<HleAction(CPUContext&, GuestMemory&)>;

class Executor {
public:
    explicit Executor(ExecutionImage& image);
    void register_native(uint32_t address, uint64_t instruction_count,
                         NativeThunk thunk);
    void register_patch(uint32_t address, NativeThunk thunk);
    bool is_patched(uint32_t address) const {
        return patched_addresses_.contains(address);
    }
    uint64_t native_dispatch_count() const { return native_dispatch_count_; }
    uint64_t native_fallback_count() const { return native_fallback_count_; }
    void register_hle(uint32_t address, HleHandler handler);
    void set_trace_enabled(bool enabled) { trace_enabled_ = enabled; }
    void snapshot_trace(ExecutionStop& stop, uint32_t active_thread) const;
    void step(CPUContext& cpu);
    ExecutionStop run(CPUContext& cpu, uint64_t max_instructions);
    ExecutionSlice run_slice(CPUContext& cpu,
                             uint64_t absolute_instruction_endpoint);
    ExecutionSlice run_slice(CPUContext& cpu,
                             uint64_t absolute_instruction_endpoint,
                             uint32_t active_thread);

private:
    ExecutionStop with_faulting_word(ExecutionStop stop) const;
    [[noreturn]] void unsupported(uint32_t pc, const char* reason);
    struct RegisteredNative {
        uint64_t instruction_count{};
        NativeThunk thunk{};
    };
    using NativePage = std::array<RegisteredNative, 0x4000>;
    size_t trace_enter(uint32_t address, uint32_t thread_id,
                       const ImportTarget& import);

    ExecutionImage& image_;
    std::vector<std::unique_ptr<NativePage>> native_pages_;
    std::map<uint32_t, RegisteredNative> unaligned_native_thunks_;
    std::set<uint32_t> patched_addresses_;
    uint64_t native_dispatch_count_{};
    uint64_t native_fallback_count_{};
    std::map<uint32_t, HleHandler> hle_handlers_;
    std::array<HleCallTrace, kHleTraceCapacity> hle_trace_{};
    size_t hle_trace_size_{};
    size_t hle_trace_cursor_{};
    bool hle_trace_truncated_{};
    bool trace_enabled_{};
};
} // namespace nwii::runtime
