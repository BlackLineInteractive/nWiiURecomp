#include "runtime/cafe_padscore.h"

#include "runtime/cafe_runtime.h"
#include "runtime/memory.h"

#include <limits>

namespace nwii::runtime {

void CafePadscore::register_handlers(CafeRuntime& runtime) {
    // WUT: void WPADEnableURCC(BOOL enable). Decaf treats it as a stub with no
    // controller side effect; the honest observable is the reached enable flag,
    // recorded the same way coreinit records OSEnableHomeButtonMenu's argument.
    runtime.register_handler(
        "padscore", "WPADEnableURCC", [this](CPUContext& cpu, GuestMemory&) {
            state_.urcc_enabled = cpu.gpr[3] != 0;
            return HleAction::return_to_lr;
        });
    // WUT: void WPADEnableWiiRemote(BOOL enable). Decaf stub; record the flag.
    runtime.register_handler(
        "padscore", "WPADEnableWiiRemote",
        [this](CPUContext& cpu, GuestMemory&) {
            state_.wiiremote_enabled = cpu.gpr[3] != 0;
            return HleAction::return_to_lr;
        });
    // WUT: void KPADInitEx(KPADUnifiedWpadStatus *buffer, uint32_t count).
    // Decaf's implementation is a stub that only forwards to WPADInit(); the
    // buffer/count pair is the extra sampling ring buffer the library records.
    // No controller is connected, so the honest observable is the registered
    // ring buffer, not any sampled data. The whole ring is validated for write
    // (KPAD stores samples into it) before any state changes, so a short or
    // unbacked buffer faults atomically. KPADInit() forwards (NULL, 0), which
    // registers no extra buffer.
    runtime.register_handler(
        "padscore", "KPADInitEx", [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t buffer = cpu.gpr[3];
            const uint32_t count = cpu.gpr[4];
            const uint64_t bytes =
                static_cast<uint64_t>(count) * kKpadUnifiedWpadStatusSize;
            if (bytes > std::numeric_limits<uint32_t>::max()) {
                throw GuestFault("KPAD ring buffer size overflow", buffer, 4,
                                 cpu.pc, MemoryAccess::write);
            }
            if (bytes != 0) {
                memory.validate_range(buffer, static_cast<uint32_t>(bytes),
                                      cpu.pc, MemoryAccess::write);
            }
            state_.kpad_initialized = true;
            state_.kpad_ring_buffer = buffer;
            state_.kpad_ring_count = count;
            return HleAction::return_to_lr;
        });
    // WUT: uint32_t KPADGetMplsWorkSize(void). Decaf returns the fixed
    // MotionPlus work-area size 0x5FE0; it takes no arguments and touches no
    // guest memory.
    runtime.register_handler(
        "padscore", "KPADGetMplsWorkSize", [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = kKpadMplsWorkSize;
            return HleAction::return_to_lr;
        });
    // WUT: void KPADSetMplsWorkarea(void *buf). Decaf's implementation is a
    // stub; the honest observable is the registered work-area pointer. The
    // buffer must supply the KPADGetMplsWorkSize byte count that KPAD writes
    // MotionPlus results into, so the whole range is write-validated before the
    // pointer is recorded and a short or unbacked buffer faults atomically.
    runtime.register_handler(
        "padscore", "KPADSetMplsWorkarea",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t buffer = cpu.gpr[3];
            memory.validate_range(buffer, kKpadMplsWorkSize, cpu.pc,
                                  MemoryAccess::write);
            state_.kpad_mpls_workarea = buffer;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "padscore", "WPADDisconnect",
        [](CPUContext&, GuestMemory&) { return HleAction::return_to_lr; });
    runtime.register_handler(
        "padscore", "KPADReadEx",
        [](CPUContext& cpu, GuestMemory& memory) {
            if (cpu.gpr[6] != 0) {
                memory.write32(cpu.gpr[6], 0xFFFFFFFE, cpu.pc);
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
}
} // namespace nwii::runtime
