#pragma once

#include <cstdint>

namespace nwii::runtime {
class CafeRuntime;

// Host-observable nn::act (Nintendo account) state. This runtime models a
// single default local (non-network) account exactly as Decaf's IOS fpd act
// server materialises one when no saved account data exists
// (ios_fpd_act_accountdata.cpp createAccount / initialiseAccounts): the first
// free slot is slot 1, and the account carries source-accurate persistent id,
// principal id, and parental-control slot values. No NNID, network account,
// online credential, or server session is modeled or fabricated.
struct ActState {
    // nn::act::Initialize / Finalize reference count. Decaf's nn_act_client
    // increments on Initialize and decrements on Finalize; Finalize on a zero
    // count returns nn::act::ResultNotInitialised.
    uint32_t init_ref_count{};

    bool operator==(const ActState&) const = default;
};

// Decaf ios_fpd_act default-local-account values (no /vol save data present):
//   getSlotNoForAccount(accounts[0]) == index 0 + 1 == 1
inline constexpr uint8_t kActDefaultSlot = 1;
//   generatePersistentId() == ++persistentIdManager.persistentIdHead, and
//   loadPersistentIdManager() defaults the head to 0x80000000 with no file.
inline constexpr uint32_t kActDefaultPersistentId = 0x80000001;
//   createAccount(): account->principalId = 1
inline constexpr uint32_t kActDefaultPrincipalId = 1;
//   createAccount(): account->parentalControlSlotNo = 1
inline constexpr uint8_t kActDefaultParentalControlSlot = 1;

// nn::act slot selectors (nn_act_types.h).
inline constexpr uint8_t kActCurrentUserSlot = 254;
inline constexpr uint8_t kActSystemSlot = 255;

// nn::Result values (decaf nn/nn_result.h layout: (level << 29) |
// (module << 20) | description; NN_ACT module == 7, LEVEL_USAGE == -2 (0b110),
// LEVEL_STATUS == -3 (0b101)). Success is any value >= 0; nn::ResultSuccess is 0.
inline constexpr uint32_t kNnResultSuccess = 0;
//   nn::act::ResultInvalidPointer { NN_ACT, LEVEL_USAGE, 0x12C80 }
inline constexpr uint32_t kActResultInvalidPointer = 0xC0712C80;
//   nn::act::ResultAccountNotFound { NN_ACT, LEVEL_STATUS, 0x1F480 }
inline constexpr uint32_t kActResultAccountNotFound = 0xA071F480;
//   nn::act::ResultNotInitialised { NN_ACT, LEVEL_USAGE, 0xFA80 }
inline constexpr uint32_t kActResultNotInitialised = 0xC070FA80;

class CafeAct {
public:
    CafeAct() = default;
    CafeAct(const CafeAct&) = delete;
    CafeAct& operator=(const CafeAct&) = delete;
    CafeAct(CafeAct&&) = delete;
    CafeAct& operator=(CafeAct&&) = delete;

    void register_handlers(CafeRuntime& runtime);
    const ActState& state() const { return state_; }

private:
    ActState state_;
};
} // namespace nwii::runtime
