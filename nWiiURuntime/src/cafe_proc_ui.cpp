#include "runtime/cafe_proc_ui.h"

#include "runtime/cafe_runtime.h"
#include "runtime/execution_image.h"

#include <algorithm>

namespace nwii::runtime {
CafeProcUi::CafeProcUi(ExecutionImage& image) : memory_(image.memory) {}

void CafeProcUi::register_handlers(CafeRuntime& runtime) {
    runtime.register_handler(
        "proc_ui", "ProcUIInit", [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t callback = cpu.gpr[3];
            if (callback == 0 || callback % 4 != 0) {
                throw GuestFault("invalid ProcUI save callback", callback, 4,
                                 cpu.pc, MemoryAccess::execute);
            }
            memory_.validate(callback, 4, cpu.pc, MemoryAccess::execute);
            state_ = {true, callback};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "proc_ui", "ProcUIProcessMessages",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "proc_ui", "ProcUIRegisterCallback",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t type = cpu.gpr[3];
            const uint32_t callback = cpu.gpr[4];
            const uint32_t param = cpu.gpr[5];
            const uint32_t priority = cpu.gpr[6];
            if (type >= kProcUiCallbackTypeCount) {
                throw GuestFault("invalid ProcUI callback type", type, 4,
                                 cpu.pc, MemoryAccess::read);
            }
            if (callback == 0 || callback % 4 != 0) {
                throw GuestFault("invalid ProcUI callback", callback, 4,
                                 cpu.pc, MemoryAccess::execute);
            }
            memory_.validate(callback, 4, cpu.pc, MemoryAccess::execute);
            auto& slot = state_.callbacks[type];
            // Ascending priority, FIFO on ties; callbacks are never invoked
            // on the reached path, so ordering is storage-only.
            const auto position =
                std::find_if(slot.begin(), slot.end(),
                             [priority](const ProcUiCallbackRegistration& r) {
                                 return r.priority > priority;
                             });
            slot.insert(position, {callback, param, priority});
            return HleAction::return_to_lr;
        });
}
} // namespace nwii::runtime
