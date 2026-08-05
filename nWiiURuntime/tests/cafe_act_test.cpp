#include "runtime/cafe_act.h"
#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "test_support.h"

#include <cstdint>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwii::runtime::StopCategory;

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kInitialize = 0xC00046E8;
constexpr uint32_t kGetSlotNo = 0xC0004660;
constexpr uint32_t kGetParentalSlot = 0xC0004618;
constexpr uint32_t kData = 0x10000000;

ExecutionImage make_image() {
    ExecutionImage image;
    const uint8_t blr[4] = {0x4E, 0x80, 0x00, 0x20};
    image.memory.map(kReturn, 4, {true, false, true}, blr);
    image.imports.emplace(
        kInitialize,
        nwii::runtime::ImportTarget{"nn_act", "Initialize__Q2_2nn3actFv"});
    image.imports.emplace(
        kGetSlotNo,
        nwii::runtime::ImportTarget{"nn_act", "GetSlotNo__Q2_2nn3actFv"});
    image.imports.emplace(
        kGetParentalSlot,
        nwii::runtime::ImportTarget{
            "nn_act", "GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc"});
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

void test_initialize_returns_success_and_counts_references() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    test::require(cafe.act().state().init_ref_count == 0,
                  "nn::act starts uninitialized");

    invoke(executor, cpu, kInitialize, "nn::act::Initialize returns through LR");
    test::require(cpu.gpr[3] == nwii::runtime::kNnResultSuccess,
                  "nn::act::Initialize returns nn::ResultSuccess (0)");
    test::require(cafe.act().state().init_ref_count == 1,
                  "nn::act::Initialize takes the first reference");

    invoke(executor, cpu, kInitialize, "nn::act::Initialize returns through LR");
    test::require(cpu.gpr[3] == nwii::runtime::kNnResultSuccess,
                  "nn::act::Initialize stays successful when nested");
    test::require(cafe.act().state().init_ref_count == 2,
                  "nn::act::Initialize counts a nested reference");
}

void test_get_slot_no_returns_default_local_slot() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;

    invoke(executor, cpu, kGetSlotNo, "nn::act::GetSlotNo returns through LR");
    test::require(cpu.gpr[3] == nwii::runtime::kActDefaultSlot,
                  "nn::act::GetSlotNo returns the default local account slot 1");
}
void test_get_parental_control_slot_writes_default_account_value() {
    auto image = make_image();
    image.memory.map(kData, 1, {true, true, false});
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.gpr[3] = kData;
    cpu.gpr[4] = nwii::runtime::kActDefaultSlot;

    invoke(executor, cpu, kGetParentalSlot,
           "nn::act::GetParentalControlSlotNoEx returns through LR");
    test::require(
        cpu.gpr[3] == nwii::runtime::kNnResultSuccess &&
            image.memory.read8(kData, 0) ==
                nwii::runtime::kActDefaultParentalControlSlot,
        "parental-control query returns the default account slot value");
}

} // namespace

int main() {
    test_initialize_returns_success_and_counts_references();
    test_get_slot_no_returns_default_local_slot();
    test_get_parental_control_slot_writes_default_account_value();
    return 0;
}
