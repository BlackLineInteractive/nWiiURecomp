#include "runtime/executor.h"
#include "test_support.h"

#include <cstdint>

namespace {
constexpr uint32_t kFirstImport = 0xC000A000;
constexpr uint32_t kThread = 0x10001000;

void test_bounded_hle_call_trace() {
    nwii::runtime::ExecutionImage image;
    nwii::runtime::Executor executor(image);
    executor.set_trace_enabled(true);

    constexpr size_t call_count = nwii::runtime::kHleTraceCapacity + 1;
    for (size_t index = 0; index < call_count; ++index) {
        const auto address =
            kFirstImport + static_cast<uint32_t>(index * sizeof(uint32_t));
        image.imports.emplace(
            address, nwii::runtime::ImportTarget{"coreinit", "TraceCall"});
        executor.register_hle(
            address, [index, call_count](nwii::runtime::CPUContext& cpu,
                             nwii::runtime::GuestMemory&) {
                if (index + 1 == call_count) {
                    return nwii::runtime::HleAction::exit;
                }
                cpu.lr = kFirstImport +
                         static_cast<uint32_t>((index + 1) * sizeof(uint32_t));
                return nwii::runtime::HleAction::return_to_lr;
            });
    }

    nwii::runtime::CPUContext cpu;
    cpu.pc = kFirstImport;
    const auto slice = executor.run_slice(cpu, 1, kThread);
    test::require(slice.terminal.has_value(), "trace fixture exits");
    const auto& stop = *slice.terminal;
    test::require(stop.active_thread == kThread, "active thread captured");
    test::require(stop.hle_call_count == nwii::runtime::kHleTraceCapacity,
                  "trace has fixed capacity");
    test::require(stop.hle_trace_truncated, "trace reports truncation");
    test::require(stop.hle_calls[0].address == kFirstImport + 4,
                  "trace is oldest-first after truncation");
    test::require(stop.hle_calls[0].thread_id == kThread,
                  "trace call has thread ID");
    test::require(stop.hle_calls[0].module == "coreinit" &&
                      stop.hle_calls[0].symbol == "TraceCall",
                  "trace call identifies import");
    test::require(stop.hle_calls[0].returned,
                  "trace records HLE return");
}

void test_trace_records_only_guest_returns() {
    constexpr uint32_t kExitImport = kFirstImport + 4;
    nwii::runtime::ExecutionImage image;
    image.imports.emplace(
        kFirstImport, nwii::runtime::ImportTarget{"coreinit", "Return"});
    image.imports.emplace(
        kExitImport, nwii::runtime::ImportTarget{"coreinit", "Exit"});
    nwii::runtime::Executor executor(image);
    executor.set_trace_enabled(true);
    executor.register_hle(
        kFirstImport,
        [](nwii::runtime::CPUContext&, nwii::runtime::GuestMemory&) {
            return nwii::runtime::HleAction::return_to_lr;
        });
    executor.register_hle(
        kExitImport,
        [](nwii::runtime::CPUContext&, nwii::runtime::GuestMemory&) {
            return nwii::runtime::HleAction::exit;
        });

    nwii::runtime::CPUContext cpu;
    cpu.pc = kFirstImport;
    cpu.lr = kExitImport;
    const auto exit_slice = executor.run_slice(cpu, 1, kThread);
    test::require(exit_slice.terminal.has_value(),
                  "return then exit fixture is terminal");
    test::require(exit_slice.terminal->hle_call_count == 2 &&
                      exit_slice.terminal->hle_calls[0].returned &&
                      !exit_slice.terminal->hle_calls[1].returned,
                  "RETURN is formatted only for return_to_lr");

    nwii::runtime::ExecutionImage reschedule_image;
    reschedule_image.imports.emplace(
        kFirstImport, nwii::runtime::ImportTarget{"coreinit", "Wait"});
    nwii::runtime::Executor reschedule_executor(reschedule_image);
    reschedule_executor.set_trace_enabled(true);
    reschedule_executor.register_hle(
        kFirstImport,
        [](nwii::runtime::CPUContext&, nwii::runtime::GuestMemory&) {
            return nwii::runtime::HleAction::reschedule;
        });
    nwii::runtime::CPUContext reschedule_cpu;
    reschedule_cpu.pc = kFirstImport;
    const auto reschedule_slice =
        reschedule_executor.run_slice(reschedule_cpu, 1, kThread);
    test::require(
        reschedule_slice.category == nwii::runtime::SliceCategory::reschedule,
        "reschedule fixture blocks at import");
    reschedule_cpu.running = false;
    const auto stop =
        reschedule_executor.run_slice(reschedule_cpu, 1, kThread).terminal;
    test::require(stop.has_value() && stop->hle_call_count == 1 &&
                      !stop->hle_calls[0].returned,
                  "reschedule does not format RETURN");
}

void test_trace_is_disabled_by_default() {
    nwii::runtime::ExecutionImage image;
    image.imports.emplace(
        kFirstImport,
        nwii::runtime::ImportTarget{"coreinit", "OSGetCurrentThread"});
    nwii::runtime::Executor executor(image);
    executor.register_hle(
        kFirstImport,
        [](nwii::runtime::CPUContext&, nwii::runtime::GuestMemory&) {
            return nwii::runtime::HleAction::exit;
        });

    nwii::runtime::CPUContext cpu;
    cpu.pc = kFirstImport;
    const auto slice = executor.run_slice(cpu, 1, kThread);
    test::require(slice.terminal.has_value(), "default trace fixture exits");
    test::require(slice.terminal->hle_call_count == 0,
                  "default execution emits no HLE trace");
    test::require(!slice.terminal->hle_trace_truncated,
                  "default trace is not marked truncated");
}
} // namespace

int main() {
    test_bounded_hle_call_trace();
    test_trace_is_disabled_by_default();
    test_trace_records_only_guest_returns();
    return 0;
}
