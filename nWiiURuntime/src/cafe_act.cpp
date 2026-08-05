#include "runtime/cafe_act.h"

#include "runtime/cafe_runtime.h"

namespace nwii::runtime {

void CafeAct::register_handlers(CafeRuntime& runtime) {
    // nn::Result nn::act::Initialize(void) -- Initialize__Q2_2nn3actFv.
    // Decaf nn_act_client.cpp: takes the client mutex, on the first reference
    // opens the /dev/act IPC client and its buffer allocator, increments the
    // reference count, and returns nn::ResultSuccess. This runtime has no IPC
    // client to open, so the honest observable is the reference count; the
    // return is nn::ResultSuccess (0) in r3.
    runtime.register_handler(
        "nn_act", "Initialize__Q2_2nn3actFv",
        [this](CPUContext& cpu, GuestMemory&) {
            ++state_.init_ref_count;
            cpu.gpr[3] = kNnResultSuccess;
            return HleAction::return_to_lr;
        });
    // SlotNo nn::act::GetSlotNo(void) -- GetSlotNo__Q2_2nn3actFv. Decaf
    // nn_act_clientstandardservice.cpp routes to GetCommonInfo(InfoType::SlotNo),
    // which the IOS fpd server answers with getSlotNoForAccount(currentAccount).
    // For the single default local account that current slot is 1. SlotNo is a
    // uint8_t returned in r3.
    runtime.register_handler(
        "nn_act", "GetSlotNo__Q2_2nn3actFv",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = kActDefaultSlot;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_act",
        "GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc",
        [](CPUContext& cpu, GuestMemory& memory) {
            if (cpu.gpr[3] == 0) {
                cpu.gpr[3] = kActResultInvalidPointer;
                return HleAction::return_to_lr;
            }
            const uint8_t slot = static_cast<uint8_t>(cpu.gpr[4]);
            if (slot != kActDefaultSlot && slot != kActCurrentUserSlot) {
                cpu.gpr[3] = kActResultAccountNotFound;
                return HleAction::return_to_lr;
            }
            memory.write8(cpu.gpr[3], kActDefaultParentalControlSlot, cpu.pc);
            cpu.gpr[3] = kNnResultSuccess;
            return HleAction::return_to_lr;
        });
}
} // namespace nwii::runtime
