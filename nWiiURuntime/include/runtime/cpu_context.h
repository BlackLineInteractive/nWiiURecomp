#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace nwii::runtime {
class Executor;
struct CPUContext {
    std::array<uint32_t, 32> gpr{};
    std::array<std::array<uint64_t, 2>, 32> fpr{};
    std::array<uint8_t, 8> cr{}; // low nibble of each field
    uint32_t xer{};
    uint32_t lr{};
    uint32_t ctr{};
    uint32_t pc{};
    uint32_t fpscr{};
    uint32_t reservation_address{0xFFFFFFFF};
    // Value read by lwarx. stwcx. must fail if another thread wrote the
    // location meanwhile, which an address-only reservation cannot detect.
    uint32_t reservation_value{};
    bool reservation_valid{};
    uint64_t instruction_count{};
    uint64_t native_instruction_endpoint{};
    const Executor* native_executor{};
    std::optional<uint32_t> current_instruction;
    std::array<uint32_t, 32> pc_history{};
    size_t history_size{};
    size_t history_cursor{};
    bool running{true};
};
}
