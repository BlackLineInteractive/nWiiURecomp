#include "nwiiu/recomp/runner_cli.h"

#include "runtime/executor.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace {
using nwii::runtime::ExecutionStop;
using nwii::runtime::StopCategory;
using nwiiu::recomp::format_stop;
using nwiiu::recomp::format_trace;
using nwiiu::recomp::parse_runner_options;

void require_rejected(std::initializer_list<std::string_view> arguments,
                      std::string_view message) {
    try {
        parse_runner_options(
            std::span<const std::string_view>(arguments.begin(),
                                              arguments.size()));
    } catch (const std::exception&) {
        return;
    }
    test::require(false, message);
}

void test_parse_runner_options() {
    constexpr std::array<std::string_view, 3> legacy{
        "/path/cking.rpx", "--max-instructions", "1000"};
    const auto legacy_options = parse_runner_options(legacy);
    test::require(legacy_options.input == "/path/cking.rpx",
                  "runner input path");
    test::require(legacy_options.max_instructions == 1000,
                  "instruction budget");
    test::require(!legacy_options.save_root, "legacy form has no save root");
    test::require(!legacy_options.trace, "legacy form has no trace");

    constexpr std::array<std::string_view, 7> with_save{
        "title/code/cking.rpx", "--save-root", "run-save", "--shared-font",
        "CafeStd.ttf", "--max-instructions", "2000"};
    const auto options = parse_runner_options(with_save);
    test::require(options.input == "title/code/cking.rpx",
                  "extended runner input path");
    test::require(options.title_root == "title", "derived title root");
    test::require(options.save_root == std::filesystem::path{"run-save"},
                  "explicit save root");
    test::require(options.shared_font == std::filesystem::path{"CafeStd.ttf"},
                  "explicit shared font");
    test::require(options.max_instructions == 2000,
                  "extended instruction budget");

    constexpr std::array<std::string_view, 6> with_trace{
        "title/code/cking.rpx", "--trace", "--save-root", "run-save",
        "--max-instructions", "2000"};
    const auto trace_options = parse_runner_options(with_trace);
    test::require(trace_options.trace, "trace flag enabled");

    constexpr std::array<std::string_view, 4> with_window{
        "title/code/cking.rpx", "--window", "--max-instructions", "2000"};
    test::require(parse_runner_options(with_window).window,
                  "window flag enabled");

    require_rejected({}, "missing arguments rejected");
    require_rejected({"/path/cking.rpx"}, "missing flag and budget rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions"},
                     "missing budget rejected");
    require_rejected({"/path/cking.rpx", "--limit", "1000"},
                     "unknown flag rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions", "0"},
                     "zero budget rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions", "+1"},
                     "positive sign rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions", "-1"},
                     "negative sign rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions", "1k"},
                     "suffix rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions",
                      "18446744073709551616"},
                     "uint64 overflow rejected");
    require_rejected({"/path/cking.rpx", "--max-instructions", "1", "extra"},
                     "extra argument rejected");
    require_rejected({"title/code/cking.rpx", "--max-instructions", "1",
                      "--save-root"},
                     "missing save root rejected");
    require_rejected({"title/code/cking.rpx", "--max-instructions", "1",
                      "--save-root", ""},
                     "empty save root rejected");
    require_rejected({"title/code/cking.rpx", "--max-instructions", "1",
                      "--max-instructions", "2"},
                     "duplicate instruction budget rejected");
    require_rejected({"title/code/cking.rpx", "--save-root", "a",
                      "--save-root", "b"},
                     "duplicate save root rejected");
    require_rejected({"title/code/cking.rpx", "--trace", "--trace",
                      "--max-instructions", "1"},
                     "duplicate trace flag rejected");
    require_rejected({"title/code/cking.rpx", "--window", "--window",
                      "--max-instructions", "1"},
                     "duplicate window flag rejected");
    require_rejected({"title/code/cking.rpx", "--trace", "value",
                      "--max-instructions", "1"},
                     "trace value rejected as unknown option");
}

void test_format_stop() {
    ExecutionStop stop;
    stop.category = StopCategory::missing_hle;
    stop.reason = "unimplemented Cafe import";
    stop.pc = 0xC0009BC8;
    stop.lr = 0x028EAA04;
    stop.instruction_count = 9;
    stop.module = "coreinit";
    stop.symbol = "OSGetCurrentThread";
    stop.history_size = 9;
    for (size_t index = 0; index < stop.history_size; ++index) {
        stop.history[index] = 0x028EA9E0 + static_cast<uint32_t>(index * 4);
    }

    constexpr std::string_view expected =
        "STOP missing_hle: unimplemented Cafe import\n"
        "PC: 0xC0009BC8\n"
        "LR: 0x028EAA04\n"
        "Instructions: 9\n"
        "Import: coreinit:OSGetCurrentThread\n"
        "Arguments: r3=0x00000000 r4=0x00000000 r5=0x00000000 "
        "r6=0x00000000 r7=0x00000000 r8=0x00000000 r9=0x00000000 "
        "r10=0x00000000\n"
        "History: 0x028EA9E0 0x028EA9E4 0x028EA9E8 0x028EA9EC "
        "0x028EA9F0 0x028EA9F4 0x028EA9F8 0x028EA9FC 0x028EAA00\n";
    test::require(format_stop(stop) == expected, "exact stop formatting");
}

void test_format_global_budget_with_legacy_reason() {
    ExecutionStop stop;
    stop.category = StopCategory::instruction_budget;
    stop.reason = "global instruction budget exhausted";
    test::require(
        format_stop(stop).starts_with(
            "STOP instruction_budget: instruction budget exhausted\n"),
        "runner preserves legacy instruction-budget reason");
}

void test_format_guest_fault() {
    ExecutionStop stop;
    stop.category = StopCategory::guest_fault;
    stop.reason = "unmapped guest memory access";
    stop.pc = 0x02000000;
    stop.lr = 0x028EAA04;
    stop.instruction_count = 1;
    stop.fault_address = 0x20000000;
    stop.fault_width = 4;
    stop.fault_access = nwii::runtime::MemoryAccess::read;
    stop.raw_instruction = 0x80810000;
    stop.history_size = 1;
    stop.history[0] = 0x02000000;

    constexpr std::string_view expected_interpreter =
        "STOP guest_fault: unmapped guest memory access\n"
        "PC: 0x02000000\n"
        "LR: 0x028EAA04\n"
        "Instructions: 1\n"
        "Address: 0x20000000\n"
        "Width: 4\n"
        "Access: read\n"
        "Instruction: 0x80810000\n"
        "History: 0x02000000\n";
    test::require(format_stop(stop) == expected_interpreter,
                  "interpreter guest-fault formatting");

    stop.reason = "guest memory permission fault";
    stop.instruction_count = 0;
    stop.fault_address = 0x02000000;
    stop.fault_access = nwii::runtime::MemoryAccess::execute;
    stop.raw_instruction.reset();
    stop.history_size = 0;
    constexpr std::string_view expected_fetch =
        "STOP guest_fault: guest memory permission fault\n"
        "PC: 0x02000000\n"
        "LR: 0x028EAA04\n"
        "Instructions: 0\n"
        "Address: 0x02000000\n"
        "Width: 4\n"
        "Access: execute\n"
        "Instruction: unavailable\n"
        "History:\n";
    test::require(format_stop(stop) == expected_fetch,
                  "fetch guest-fault formatting");
}
void test_format_trace() {
    ExecutionStop stop;
    stop.active_thread = 0x10001000;
    stop.hle_call_count = 2;
    stop.hle_trace_truncated = true;
    stop.hle_calls[0] = {0xC0009BC8, 0x10001000, "coreinit",
                         "OSGetCurrentThread", true};
    stop.hle_calls[1] = {0xC000A100, 0x10002000, "gx2",
                         "GX2Init", false};

    constexpr std::string_view expected =
        "TRACE active-thread: 0x10001000\n"
        "TRACE HLE calls (oldest first, last 32, truncated):\n"
        "  ENTER thread=0x10001000 import=coreinit:OSGetCurrentThread "
        "address=0xC0009BC8\n"
        "  RETURN thread=0x10001000 import=coreinit:OSGetCurrentThread "
        "address=0xC0009BC8\n"
        "  ENTER thread=0x10002000 import=gx2:GX2Init "
        "address=0xC000A100\n";
    test::require(format_trace(stop) == expected,
                  "trace formatting is deterministic and oldest-first");
}

void test_format_deadlock() {
    ExecutionStop stop;
    stop.category = StopCategory::deadlock;
    stop.reason = "Cafe scheduler deadlock";
    const auto formatted = format_stop(stop);
    test::require(
        formatted.starts_with(
            "STOP deadlock: Cafe scheduler deadlock\n"),
        "deadlock stop category formatting");
}

} // namespace

int main() {
    test_parse_runner_options();
    test_format_stop();
    test_format_global_budget_with_legacy_reason();
    test_format_guest_fault();
    test_format_deadlock();
    test_format_trace();
    return 0;
}
