#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace nwii::runtime {
class CafeRuntime;
class ExecutionImage;
class GuestMemory;

struct ProcUiCallbackRegistration {
    uint32_t callback{};
    uint32_t param{};
    uint32_t priority{};

    bool operator==(const ProcUiCallbackRegistration&) const = default;
};

// WUT ProcUICallbackType: ACQUIRE..HOME_BUTTON_DENIED (0..5).
inline constexpr uint32_t kProcUiCallbackTypeCount = 6;

struct ProcUiState {
    bool running{};
    uint32_t save_callback{};
    std::array<std::vector<ProcUiCallbackRegistration>,
               kProcUiCallbackTypeCount>
        callbacks{};

    bool operator==(const ProcUiState&) const = default;
};

class CafeProcUi {
public:
    explicit CafeProcUi(ExecutionImage& image);
    CafeProcUi(const CafeProcUi&) = delete;
    CafeProcUi& operator=(const CafeProcUi&) = delete;
    CafeProcUi(CafeProcUi&&) = delete;
    CafeProcUi& operator=(CafeProcUi&&) = delete;

    void register_handlers(CafeRuntime& runtime);
    const ProcUiState& state() const { return state_; }

private:
    GuestMemory& memory_;
    ProcUiState state_;
};
} // namespace nwii::runtime
