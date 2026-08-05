#include "runtime/cafe_padscore.h"
#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "test_support.h"

#include <cstdint>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwii::runtime::MemoryAccess;
using nwii::runtime::StopCategory;

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kEnableUrcc = 0xC00083D8;
constexpr uint32_t kEnableWiiRemote = 0xC00083E8;
constexpr uint32_t kInitEx = 0xC0008138;
constexpr uint32_t kRingBuffer = 0x10000000;
constexpr uint32_t kMplsWorkSize = 0xC00080E8;
constexpr uint32_t kSetMplsWorkarea = 0xC0008270;
constexpr uint32_t kMplsBuffer = 0x10008000;
constexpr uint32_t kReadEx = 0xC00081A0;

ExecutionImage make_image() {
    ExecutionImage image;
    const uint8_t blr[4] = {0x4E, 0x80, 0x00, 0x20};
    image.memory.map(kReturn, 4, {true, false, true}, blr);
    // Writable backing large enough for the reached 64-entry KPAD ring buffer.
    image.memory.map(kRingBuffer, 0x2000, {true, true, false});
    // Writable backing for the 0x5FE0-byte MotionPlus work area.
    image.memory.map(kMplsBuffer, 0x6000, {true, true, false});
    image.imports.emplace(
        kEnableUrcc, nwii::runtime::ImportTarget{"padscore", "WPADEnableURCC"});
    image.imports.emplace(
        kEnableWiiRemote,
        nwii::runtime::ImportTarget{"padscore", "WPADEnableWiiRemote"});
    image.imports.emplace(
        kInitEx, nwii::runtime::ImportTarget{"padscore", "KPADInitEx"});
    image.imports.emplace(
        kMplsWorkSize,
        nwii::runtime::ImportTarget{"padscore", "KPADGetMplsWorkSize"});
    image.imports.emplace(
        kSetMplsWorkarea,
        nwii::runtime::ImportTarget{"padscore", "KPADSetMplsWorkarea"});
    image.imports.emplace(
        kReadEx, nwii::runtime::ImportTarget{"padscore", "KPADReadEx"});
    return image;
}

// Runs a single reached import and asserts a normal LR return.
void invoke(Executor& executor, CPUContext& cpu, uint32_t import,
            const char* what) {
    cpu.pc = import;
    cpu.lr = kReturn;
    const auto stop = executor.run(cpu, cpu.instruction_count + 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn,
                  what);
}

void test_enable_urcc_records_flag() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    test::require(!cafe.padscore().state().urcc_enabled,
                  "padscore starts with URCC disabled");

    cpu.gpr[3] = 1;
    invoke(executor, cpu, kEnableUrcc, "WPADEnableURCC returns through LR");
    test::require(cafe.padscore().state().urcc_enabled,
                  "WPADEnableURCC records the reached enable flag");

    cpu.gpr[3] = 0;
    invoke(executor, cpu, kEnableUrcc, "WPADEnableURCC returns through LR");
    test::require(!cafe.padscore().state().urcc_enabled,
                  "WPADEnableURCC records a later disable flag");
}

void test_enable_wii_remote_records_flag() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    test::require(!cafe.padscore().state().wiiremote_enabled,
                  "padscore starts with Wii Remote disabled");

    cpu.gpr[3] = 1;
    invoke(executor, cpu, kEnableWiiRemote,
           "WPADEnableWiiRemote returns through LR");
    test::require(cafe.padscore().state().wiiremote_enabled,
                  "WPADEnableWiiRemote records the reached enable flag");

    cpu.gpr[3] = 0;
    invoke(executor, cpu, kEnableWiiRemote,
           "WPADEnableWiiRemote returns through LR");
    test::require(!cafe.padscore().state().wiiremote_enabled,
                  "WPADEnableWiiRemote records a later disable flag");
}

void test_init_ex_records_ring_registration() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    test::require(!cafe.padscore().state().kpad_initialized &&
                      cafe.padscore().state().kpad_ring_buffer == 0 &&
                      cafe.padscore().state().kpad_ring_count == 0,
                  "padscore starts with KPAD uninitialized");

    cpu.gpr[3] = kRingBuffer;
    cpu.gpr[4] = 64;
    invoke(executor, cpu, kInitEx, "KPADInitEx returns through LR");
    test::require(cafe.padscore().state().kpad_initialized &&
                      cafe.padscore().state().kpad_ring_buffer == kRingBuffer &&
                      cafe.padscore().state().kpad_ring_count == 64,
                  "KPADInitEx records the reached ring buffer and count");
}

void test_init_ex_accepts_null_zero_count() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    // KPADInit() forwards KPADInitEx(NULL, 0); no extra ring buffer to map.
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0;
    invoke(executor, cpu, kInitEx, "KPADInitEx(NULL, 0) returns through LR");
    test::require(cafe.padscore().state().kpad_initialized &&
                      cafe.padscore().state().kpad_ring_buffer == 0 &&
                      cafe.padscore().state().kpad_ring_count == 0,
                  "KPADInitEx(NULL, 0) initializes with no ring buffer");
}

void test_init_ex_faults_on_unbacked_ring() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    const auto before = cafe.padscore().state();

    // count entries overrun the 0x2000 writable backing (64 fit, 256 do not).
    cpu.pc = kInitEx;
    cpu.lr = kReturn;
    cpu.gpr[3] = kRingBuffer;
    cpu.gpr[4] = 256;
    const auto stop = executor.run(cpu, cpu.instruction_count + 1);
    test::require(stop.category == StopCategory::guest_fault &&
                      stop.pc == kInitEx &&
                      stop.fault_access == MemoryAccess::write &&
                      cpu.pc == kInitEx,
                  "KPADInitEx faults when the ring buffer is not fully backed");
    test::require(cafe.padscore().state() == before,
                  "failed KPADInitEx leaves state unchanged");
}

void test_mpls_work_size_returns_source_value() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    cpu.gpr[3] = 0xDEADBEEF;
    invoke(executor, cpu, kMplsWorkSize, "KPADGetMplsWorkSize returns via LR");
    // Decaf padscore_kpad.cpp: KPADGetMplsWorkSize() returns 0x5FE0.
    test::require(cpu.gpr[3] == 0x5FE0,
                  "KPADGetMplsWorkSize returns the source-verified 0x5FE0");
}

void test_set_mpls_workarea_records_pointer() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    test::require(cafe.padscore().state().kpad_mpls_workarea == 0,
                  "padscore starts with no MotionPlus work area");

    cpu.gpr[3] = kMplsBuffer;
    invoke(executor, cpu, kSetMplsWorkarea,
           "KPADSetMplsWorkarea returns via LR");
    test::require(cafe.padscore().state().kpad_mpls_workarea == kMplsBuffer,
                  "KPADSetMplsWorkarea records the reached work-area pointer");
}

void test_set_mpls_workarea_faults_on_unbacked_buffer() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    const auto before = cafe.padscore().state();

    // 0x6000 backing minus a 0x100 offset leaves < 0x5FE0 writable bytes.
    cpu.pc = kSetMplsWorkarea;
    cpu.lr = kReturn;
    cpu.gpr[3] = kMplsBuffer + 0x100;
    const auto stop = executor.run(cpu, cpu.instruction_count + 1);
    test::require(stop.category == StopCategory::guest_fault &&
                      stop.pc == kSetMplsWorkarea &&
                      stop.fault_access == MemoryAccess::write &&
                      cpu.pc == kSetMplsWorkarea,
                  "KPADSetMplsWorkarea faults when the work area is too small");
    test::require(cafe.padscore().state() == before,
                  "failed KPADSetMplsWorkarea leaves state unchanged");
}
void test_read_ex_reports_no_controller_without_touching_samples() {
    auto image = make_image();
    image.memory.write32(kRingBuffer, 0xDEADBEEF, 0);
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0xDEAD0000;
    cpu.gpr[5] = 16;
    cpu.gpr[6] = kRingBuffer;

    invoke(executor, cpu, kReadEx, "KPADReadEx returns through LR");
    test::require(cpu.gpr[3] == 0 &&
                      image.memory.read32(kRingBuffer, 0) == 0xFFFFFFFE,
                  "KPADReadEx reports no connected controller and zero samples");
}

} // namespace

int main() {
    test_enable_urcc_records_flag();
    test_enable_wii_remote_records_flag();
    test_init_ex_records_ring_registration();
    test_init_ex_accepts_null_zero_count();
    test_init_ex_faults_on_unbacked_ring();
    test_mpls_work_size_returns_source_value();
    test_set_mpls_workarea_records_pointer();
    test_set_mpls_workarea_faults_on_unbacked_buffer();
    test_read_ex_reports_no_controller_without_touching_samples();
}
