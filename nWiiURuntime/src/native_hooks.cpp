#include "runtime/native_hooks.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace nwii::runtime {
namespace {
// Yaz0/SZS, decoded one byte at a time by the guest: interpreting it costs
// billions of guest instructions per boot. This mirrors the guest semantics
// exactly — r3 = destination, r4 = stream with the decoded size at +0x04 and
// the payload at +0x10, returns the decoded size in r3. When a back-reference
// would overrun the remaining output budget the guest exits without copying it;
// so do we.
void yaz0_decode(CPUContext& cpu, GuestMemory& memory) {
    if (std::getenv("NWIIU_YAZ0_TRACE") != nullptr) {
        std::fprintf(stderr, "YAZ0-HLE dst=%08X src=%08X\n", cpu.gpr[3],
                     cpu.gpr[4]);
    }
    const uint32_t pc = cpu.pc;
    uint32_t dst = cpu.gpr[3];
    uint32_t src = cpu.gpr[4];
    const uint32_t size = memory.read32(src + 4, pc);
    src += 0x10;
    int64_t remaining = size;
    uint32_t flags = 0;
    uint32_t flag_bits = 0;
    while (remaining > 0) {
        if (flag_bits == 0) {
            flags = memory.read8(src++, pc);
            flag_bits = 8;
        }
        if ((flags & 0x80u) != 0) {
            memory.write8(dst++, memory.read8(src++, pc), pc);
            --remaining;
        } else {
            const uint32_t first = memory.read8(src++, pc);
            const uint32_t second = memory.read8(src++, pc);
            const uint32_t distance = (((first & 0x0Fu) << 8) | second) + 1;
            uint32_t length = first >> 4;
            if (length == 0) {
                length = memory.read8(src++, pc) + 0x10u;
            }
            length += 2;
            remaining -= length;
            if (remaining < 0) {
                break;
            }
            for (uint32_t index = 0; index < length; ++index, ++dst) {
                memory.write8(dst, memory.read8(dst - distance, pc), pc);
            }
        }
        flags <<= 1;
        --flag_bits;
    }
    cpu.gpr[3] = size;
    cpu.instruction_count += 32 + size / 8;
    cpu.pc = cpu.lr;
}

struct NamedHook {
    std::string_view name;
    NativeThunk thunk;
};

// Sorted by name so native_hook_names() can hand out a view of it directly.
constexpr std::array<NamedHook, 1> kNativeHooks{{
    {"Yaz0Decode", &yaz0_decode},
}};
} // namespace

NativeThunk find_native_hook(std::string_view name) {
    const auto found = std::find_if(
        kNativeHooks.begin(), kNativeHooks.end(),
        [name](const NamedHook& hook) { return hook.name == name; });
    return found == kNativeHooks.end() ? nullptr : found->thunk;
}

std::span<const std::string_view> native_hook_names() {
    static const std::array<std::string_view, kNativeHooks.size()> names = [] {
        std::array<std::string_view, kNativeHooks.size()> result{};
        for (size_t index = 0; index < kNativeHooks.size(); ++index) {
            result[index] = kNativeHooks[index].name;
        }
        return result;
    }();
    return names;
}

const std::map<uint32_t, std::string>& default_hle_hooks() {
    // Mirrors the [hle_hooks] table of configs/wwhd-eu-v0.toml. If you change
    // one, change the other: game_config_test pins the profile's contents and
    // machine_test pins these.
    static const std::map<uint32_t, std::string> hooks{
        {0x0275F480u, "Yaz0Decode"},
    };
    return hooks;
}
} // namespace nwii::runtime
