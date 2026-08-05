#include "runtime/cafe_proc_ui.h"
#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwii::runtime::MemoryAccess;
using nwii::runtime::StopCategory;

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kCallbackA = 0x02000100;
constexpr uint32_t kCallbackB = 0x02000200;
constexpr uint32_t kNonExecutable = 0x02000300;
constexpr uint32_t kUnalignedMapping = 0x02000400;
constexpr uint32_t kInit = 0xC0007868;
constexpr uint32_t kRegister = 0xC0007890;
constexpr uint32_t kProcess = 0xC0007880;
constexpr uint32_t kCallbackTypeCount = 6;

ExecutionImage make_image() {
    ExecutionImage image;
    const uint8_t instruction[4] = {0x4E, 0x80, 0x00, 0x20};
    image.memory.map(kReturn, 4, {true, false, true}, instruction);
    image.memory.map(kCallbackA, 4, {true, false, true}, instruction);
    image.memory.map(kCallbackB, 4, {true, false, true}, instruction);
    image.memory.map(kNonExecutable, 4, {true, false, false}, instruction);
    image.memory.map(0, 4, {true, false, true}, instruction);
    image.memory.map(kUnalignedMapping, 8, {true, false, true}, instruction);
    image.imports.emplace(
        kInit, nwii::runtime::ImportTarget{"proc_ui", "ProcUIInit"});
    image.imports.emplace(kRegister, nwii::runtime::ImportTarget{
                                         "proc_ui", "ProcUIRegisterCallback"});
    image.imports.emplace(
        kProcess,
        nwii::runtime::ImportTarget{"proc_ui", "ProcUIProcessMessages"});
    return image;
}

void invoke_init(Executor& executor, CPUContext& cpu, uint32_t callback) {
    cpu.pc = kInit;
    cpu.lr = kReturn;
    cpu.gpr[3] = callback;
    const auto stop = executor.run(cpu, cpu.instruction_count + 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn,
                  "ProcUIInit returns through LR");
}

void invoke_register(Executor& executor, CPUContext& cpu, uint32_t type,
                     uint32_t callback, uint32_t param, uint32_t priority) {
    cpu.pc = kRegister;
    cpu.lr = kReturn;
    cpu.gpr[3] = type;
    cpu.gpr[4] = callback;
    cpu.gpr[5] = param;
    cpu.gpr[6] = priority;
    const auto stop = executor.run(cpu, cpu.instruction_count + 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn,
                  "ProcUIRegisterCallback returns through LR");
}

void test_init_and_repeat_replace_callback() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    test::require(!cafe.proc_ui().state().running &&
                      cafe.proc_ui().state().save_callback == 0,
                  "ProcUI starts stopped without a callback");

    invoke_init(executor, cpu, kCallbackA);
    test::require(cafe.proc_ui().state().running &&
                      cafe.proc_ui().state().save_callback == kCallbackA,
                  "ProcUIInit transitions to running with guest save callback");

    invoke_init(executor, cpu, kCallbackB);
    test::require(cafe.proc_ui().state().running &&
                      cafe.proc_ui().state().save_callback == kCallbackB,
                  "repeat ProcUIInit replaces the save callback");
}

void test_invalid_callback_faults_without_mutating_state() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    invoke_init(executor, cpu, kCallbackA);
    const auto before = cafe.proc_ui().state();

    for (const uint32_t callback :
         {0u, 0x03000000u, kNonExecutable, kUnalignedMapping + 1}) {
        cpu.pc = kInit;
        cpu.lr = kReturn;
        cpu.gpr[3] = callback;
        const auto stop = executor.run(cpu, cpu.instruction_count + 1);
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.pc == kInit &&
                          stop.fault_address == callback &&
                          stop.fault_access == MemoryAccess::execute &&
                          cpu.pc == kInit,
                      "invalid ProcUI save callback faults at the import");
        test::require(cafe.proc_ui().state() == before,
                      "failed ProcUIInit leaves state unchanged");
    }
}

void test_register_stores_per_type_registrations() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    for (uint32_t type = 0; type < kCallbackTypeCount; ++type) {
        test::require(cafe.proc_ui().state().callbacks[type].empty(),
                      "ProcUI starts with no registered callbacks");
    }

    // Exact reached trace arguments: type=5, callback, param=0, priority=1.
    invoke_register(executor, cpu, 5, kCallbackA, 0, 1);
    const nwii::runtime::ProcUiCallbackRegistration reached{kCallbackA, 0, 1};
    test::require(cafe.proc_ui().state().callbacks[5].size() == 1 &&
                      cafe.proc_ui().state().callbacks[5][0] == reached,
                  "registration stores callback, param, and priority by type");

    for (uint32_t type = 0; type < kCallbackTypeCount; ++type) {
        if (type != 5) {
            test::require(cafe.proc_ui().state().callbacks[type].empty(),
                          "registration touches only its own type slot");
        }
    }

    for (uint32_t type = 0; type < kCallbackTypeCount; ++type) {
        invoke_register(executor, cpu, type, kCallbackB, 0x1000 + type, 7);
        const nwii::runtime::ProcUiCallbackRegistration entry{
            kCallbackB, 0x1000 + type, 7};
        test::require(!cafe.proc_ui().state().callbacks[type].empty() &&
                          cafe.proc_ui().state().callbacks[type].back() ==
                              entry,
                      "every WUT callback type accepts a registration");
    }
}

void test_register_orders_by_priority_with_stable_ties() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    invoke_register(executor, cpu, 0, kCallbackA, 0x11, 5);
    invoke_register(executor, cpu, 0, kCallbackB, 0x22, 1);
    invoke_register(executor, cpu, 0, kCallbackA, 0x33, 3);
    invoke_register(executor, cpu, 0, kCallbackB, 0x44, 3);
    invoke_register(executor, cpu, 0, kCallbackA, 0x55, 5);

    const std::vector<nwii::runtime::ProcUiCallbackRegistration> expected{
        {kCallbackB, 0x22, 1},
        {kCallbackA, 0x33, 3},
        {kCallbackB, 0x44, 3},
        {kCallbackA, 0x11, 5},
        {kCallbackA, 0x55, 5},
    };
    test::require(cafe.proc_ui().state().callbacks[0] == expected,
                  "registrations order by ascending priority, FIFO on ties, "
                  "and duplicates append");
}

void test_register_invalid_type_faults_atomically() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    invoke_register(executor, cpu, 5, kCallbackA, 0, 1);
    const auto before = cafe.proc_ui().state();

    for (const uint32_t type : {kCallbackTypeCount, 0xFFFFFFFFu}) {
        cpu.pc = kRegister;
        cpu.lr = kReturn;
        cpu.gpr[3] = type;
        cpu.gpr[4] = kCallbackA;
        cpu.gpr[5] = 0;
        cpu.gpr[6] = 1;
        const auto stop = executor.run(cpu, cpu.instruction_count + 1);
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.pc == kRegister && stop.fault_address == type &&
                          cpu.pc == kRegister,
                      "out-of-range ProcUI callback type faults at the import");
        test::require(cafe.proc_ui().state() == before,
                      "failed registration leaves state unchanged");
    }
}

void test_register_invalid_callback_faults_atomically() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    invoke_register(executor, cpu, 2, kCallbackA, 0, 1);
    const auto before = cafe.proc_ui().state();

    for (const uint32_t callback :
         {0u, 0x03000000u, kNonExecutable, kUnalignedMapping + 1}) {
        cpu.pc = kRegister;
        cpu.lr = kReturn;
        cpu.gpr[3] = 2;
        cpu.gpr[4] = callback;
        cpu.gpr[5] = 0;
        cpu.gpr[6] = 1;
        const auto stop = executor.run(cpu, cpu.instruction_count + 1);
        test::require(stop.category == StopCategory::guest_fault &&
                          stop.pc == kRegister &&
                          stop.fault_address == callback &&
                          stop.fault_access == MemoryAccess::execute &&
                          cpu.pc == kRegister,
                      "invalid ProcUI callback faults at the import");
        test::require(cafe.proc_ui().state() == before,
                      "failed registration leaves state unchanged");
    }
}
void test_process_messages_stays_in_foreground() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    invoke_init(executor, cpu, kCallbackA);
    cpu.pc = kProcess;
    cpu.lr = kReturn;
    cpu.gpr[3] = 1;

    const auto stop = executor.run(cpu, cpu.instruction_count + 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 0 && cafe.proc_ui().state().running,
                  "ProcUIProcessMessages keeps the host app in foreground");
}

} // namespace

int main() {
    test_init_and_repeat_replace_callback();
    test_invalid_callback_faults_without_mutating_state();
    test_register_stores_per_type_registrations();
    test_register_orders_by_priority_with_stable_ties();
    test_register_invalid_type_faults_atomically();
    test_register_invalid_callback_faults_atomically();
    test_process_messages_stays_in_foreground();
}
