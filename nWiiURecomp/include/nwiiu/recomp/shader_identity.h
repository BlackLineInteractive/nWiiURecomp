#pragma once

#include "nwiiu/recomp/shader_types.h"

#include <array>
#include <compare>

namespace nwiiu::recomp {

// WHAT the shader is. Stable forever, offline-computable, machine- and
// run-independent. This is the public key that resource swaps and ray-tracing
// material overrides bind to. It deliberately excludes all render state, so a
// binding survives every draw that uses the shader (sections 4.1, 4.6).
struct ProgramId {
    std::array<uint8_t, 16> bytes{};

    [[nodiscard]] std::string hex() const;
    [[nodiscard]] uint64_t prefix64() const noexcept;

    bool operator==(const ProgramId&) const = default;
    auto operator<=>(const ProgramId&) const = default;
};

// `regs` MUST already be canonicalised to big-endian (section 1.6) and MUST be
// only the pointer-free regs prefix -- 0xD0 bytes for VS, 0xA4 for PS.
// Including any later field makes the key allocation-dependent: on disc those
// fields hold file-relative offsets, at runtime absolute addresses (4.3 rule 2).
ProgramId compute_program_id(Stage stage, std::span<const uint8_t> regs,
                             std::span<const uint8_t> program);

}  // namespace nwiiu::recomp
