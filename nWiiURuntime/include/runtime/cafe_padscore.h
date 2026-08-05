#pragma once

#include <cstdint>

namespace nwii::runtime {
class CafeRuntime;

// Host-observable padscore state. Fields are added only as the authenticated
// trace reaches the call that mutates them; no controller is connected in this
// deterministic runtime, so status/probe queries report source-accurate
// no-controller values rather than fabricated devices.
struct PadscoreState {
    bool urcc_enabled{};
    bool wiiremote_enabled{};
    // KPAD ring-buffer registration recorded by KPADInitEx. No controller is
    // connected, so this is registration bookkeeping only, never sample data.
    bool kpad_initialized{};
    uint32_t kpad_ring_buffer{};
    uint32_t kpad_ring_count{};
    // MotionPlus work area registered by KPADSetMplsWorkarea (pointer only).
    uint32_t kpad_mpls_workarea{};

    bool operator==(const PadscoreState&) const = default;
};

// WUT padscore/kpad.h: sizeof(KPADUnifiedWpadStatus) == 0x44. KPADInitEx
// registers `count` such entries as the extra sampling ring buffer.
inline constexpr uint32_t kKpadUnifiedWpadStatusSize = 0x44;

// Decaf padscore_kpad.cpp: KPADGetMplsWorkSize() returns this fixed size, which
// is the byte count a KPADSetMplsWorkarea buffer must provide.
inline constexpr uint32_t kKpadMplsWorkSize = 0x5FE0;

class CafePadscore {
public:
    CafePadscore() = default;
    CafePadscore(const CafePadscore&) = delete;
    CafePadscore& operator=(const CafePadscore&) = delete;
    CafePadscore(CafePadscore&&) = delete;
    CafePadscore& operator=(CafePadscore&&) = delete;

    void register_handlers(CafeRuntime& runtime);
    const PadscoreState& state() const { return state_; }

private:
    PadscoreState state_;
};
} // namespace nwii::runtime
