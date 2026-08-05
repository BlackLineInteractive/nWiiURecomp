#include "runtime/gx2_runtime.h"

#include "runtime/cafe_abi.h"
#include "runtime/cafe_runtime.h"
#include "runtime/execution_image.h"
#include "runtime/latte_surface.h"
#include "runtime/memory.h"
#include "runtime/machine.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <cmath>

namespace nwii::runtime {
namespace {
constexpr uint32_t kDefaultCommandBufferSize = 0x400000;
constexpr uint32_t kMinimumCommandBufferSize = 0x2000;
constexpr uint32_t kDisplayListCommandTokenSize = 4;
constexpr uint32_t kDisplayListReplayScratch = 0x50000000;
constexpr uint32_t kDisplayListReplayScratchSize = 0x10000000;
constexpr uint32_t kDefaultAppIoStackSize = 0x1000;
constexpr uint32_t kContextStateSize = 0xA100;
constexpr uint32_t kColorBufferSize = 0x9C;
constexpr uint32_t kTextureSize = 0x9C;
constexpr uint32_t kDepthBufferSize = 0xAC;
constexpr uint32_t kContextProfilingOffset = 0x9800;
constexpr uint32_t kContextDisplayListSizeOffset = 0x9804;
constexpr uint32_t kContextDisplayListOffset = 0x9E00;
struct LatteColorFormat {
    uint32_t register_format;
    uint32_t bytes_per_element;
    uint32_t source_format;
};

std::optional<LatteColorFormat> color_format(uint32_t format) {
    switch (format & 0x3Fu) {
    case 0x01: return LatteColorFormat{1, 1, 1};
    case 0x02: return LatteColorFormat{2, 1, 1};
    case 0x05: return LatteColorFormat{5, 2, 0};
    case 0x06: return LatteColorFormat{6, 2, 1};
    case 0x07: return LatteColorFormat{7, 2, 1};
    case 0x08: return LatteColorFormat{8, 2, 1};
    case 0x0A: return LatteColorFormat{10, 2, 1};
    case 0x0B: return LatteColorFormat{11, 2, 1};
    case 0x0C: return LatteColorFormat{12, 2, 1};
    case 0x0D: return LatteColorFormat{13, 4, 0};
    case 0x0E: return LatteColorFormat{14, 4, 0};
    case 0x0F: return LatteColorFormat{15, 4, 0};
    case 0x10: return LatteColorFormat{16, 4, 1};
    case 0x11: return LatteColorFormat{17, 4, 0};
    case 0x16: return LatteColorFormat{22, 4, 1};
    case 0x19: return LatteColorFormat{27, 4, 1};
    case 0x1A: return LatteColorFormat{26, 4, 1};
    case 0x1B: return LatteColorFormat{25, 4, 1};
    case 0x1C: return LatteColorFormat{28, 8, 0};
    case 0x1D: return LatteColorFormat{29, 8, 0};
    case 0x1E: return LatteColorFormat{30, 8, 0};
    case 0x1F: return LatteColorFormat{31, 8, 0};
    case 0x20: return LatteColorFormat{32, 8, 1};
    case 0x22: return LatteColorFormat{34, 16, 0};
    case 0x23: return LatteColorFormat{35, 16, 0};
    default: return std::nullopt;
    }
}

bool supported_surface_mode(uint32_t mode) {
    return mode == 1 || mode == 2 || mode == 4 || mode == 16;
}

uint32_t surface_pixel_offset(uint32_t x, uint32_t y, uint32_t pitch,
                              uint32_t mode, uint32_t swizzle,
                              uint32_t bytes_per_element) {
    if (mode != 2 && mode != 4) {
        return (y * pitch + x) * bytes_per_element;
    }
    const uint32_t pixel_index =
        bytes_per_element == 2
            ? (x & 7u) | ((y & 7u) << 3)
            : (x & 1u) | (x & 2u) | ((y & 1u) << 2) |
                  ((x & 4u) << 1) | ((y & 2u) << 3) | ((y & 4u) << 3);
    if (mode == 2) {
        const uint32_t micro_tile =
            ((x >> 3) + (pitch >> 3) * (y >> 3)) *
            (64u * bytes_per_element);
        return micro_tile + pixel_index * bytes_per_element;
    }
    const uint32_t pipe = ((y >> 3) ^ (x >> 3)) & 1u;
    const uint32_t bank =
        (((y >> 5) ^ (x >> 3)) & 1u) |
        ((((y >> 4) ^ (x >> 4)) & 1u) << 1);
    const uint32_t bank_pipe =
        (pipe + 2u * bank) ^ ((swizzle >> 8) & 7u);
    const uint32_t macro_tile =
        2048u * ((x >> 5) + (pitch >> 5) * (y >> 4));
    const uint32_t total =
        pixel_index * bytes_per_element + (macro_tile >> 3);
    return ((total & ~0xFFu) << 3) | ((bank_pipe >> 1) << 9) |
           ((bank_pipe & 1u) << 8) | (total & 0xFFu);
}

std::string surface_error_reason(const char* service,
                                 const LatteSurfaceError& error) {
    return std::string{"unsupported GX2 surface layout: service="} + service +
           " mode=" + std::to_string(error.mode) +
           " format=" + std::to_string(error.format) +
           " buffering=" + std::to_string(error.buffering) +
           " error=" + std::to_string(static_cast<unsigned>(error.code));
}
uint32_t read_be32(const GuestMemory& memory, uint32_t address, uint32_t pc) {
    return (static_cast<uint32_t>(memory.read8(address, pc)) << 24) |
           (static_cast<uint32_t>(memory.read8(address + 1, pc)) << 16) |
           (static_cast<uint32_t>(memory.read8(address + 2, pc)) << 8) |
           memory.read8(address + 3, pc);
}

uint8_t unorm8(uint32_t bits) {
    const float value = std::bit_cast<float>(bits);
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f +
                                0.5f);
}

void clear_color_surface(GuestMemory& memory, uint32_t descriptor,
                         const std::array<uint32_t, 4>& color, uint32_t pc) {
    memory.validate_range(descriptor, kColorBufferSize, pc,
                          MemoryAccess::read);
    const uint32_t format = read_be32(memory, descriptor + 0x14, pc) & 0x3Fu;
    const uint32_t image_size = read_be32(memory, descriptor + 0x20, pc);
    const uint32_t image = read_be32(memory, descriptor + 0x24, pc);
    if ((format != 0x19 && format != 0x1A) || image == 0 ||
        image_size == 0) {
        throw GuestFault("unsupported GX2 color clear surface", descriptor,
                         kColorBufferSize, pc, MemoryAccess::read);
    }
    std::array<uint8_t, 4> rgba{
        unorm8(color[0]), unorm8(color[1]), unorm8(color[2]),
        unorm8(color[3])};
    if (format == 0x19) {
        std::swap(rgba[0], rgba[2]);
    }
    memory.fill(image, image_size, rgba, pc);
}
template <std::size_t Count>
void set_uniform_registers(GuestMemory& memory, const CPUContext& cpu,
                           std::array<uint32_t, Count>& registers,
                           bool& valid) {
    const uint32_t offset = cpu.gpr[3];
    const uint32_t count = cpu.gpr[4];
    const uint32_t data = cpu.gpr[5];
    if ((offset >> 16) != 0 || offset > registers.size() ||
        count > registers.size() - offset ||
        count > std::numeric_limits<uint32_t>::max() / 4) {
        throw GuestFault("invalid GX2 uniform register range", data,
                         count * 4, cpu.pc, MemoryAccess::read);
    }
    memory.validate_range(data, count * 4, cpu.pc, MemoryAccess::read);
    for (uint32_t index = 0; index < count; ++index) {
        registers[offset + index] =
            read_be32(memory, data + index * 4, cpu.pc);
    }
    valid = true;
}
void bind_uniform_block(
    GuestMemory& memory, const CPUContext& cpu,
    std::array<Gx2UniformBlockState, 16>& blocks) {
    const uint32_t index = cpu.gpr[3];
    const uint32_t size = cpu.gpr[4];
    const uint32_t address = cpu.gpr[5];
    if (index >= blocks.size() || size == 0) {
        throw GuestFault("invalid GX2 uniform block", address, size, cpu.pc,
                         MemoryAccess::read);
    }
    memory.validate_range(address, size, cpu.pc, MemoryAccess::read);
    blocks[index] = {true, address, size};
}

template <std::size_t Count>
void bind_texture(GuestMemory& memory, const CPUContext& cpu,
                  std::array<uint32_t, Count>& addresses) {
    const uint32_t descriptor = cpu.gpr[3];
    const uint32_t unit = cpu.gpr[4];
    if (unit >= addresses.size()) {
        throw GuestFault("invalid GX2 texture unit", unit, 4, cpu.pc,
                         MemoryAccess::read);
    }
    if (descriptor != 0) {
        memory.validate_range(descriptor, kTextureSize, cpu.pc,
                              MemoryAccess::read);
    }
    addresses[unit] = descriptor;
}
void bind_attrib_buffer(GuestMemory& memory, const CPUContext& cpu,
                        std::array<Gx2AttribBufferState, 16>& buffers) {
    const uint32_t index = cpu.gpr[3];
    const uint32_t size = cpu.gpr[4];
    const uint32_t stride = cpu.gpr[5];
    const uint32_t address = cpu.gpr[6];
    if (index >= buffers.size() || size == 0) {
        throw GuestFault("invalid GX2 attribute buffer", address, size, cpu.pc,
                         MemoryAccess::read);
    }
    memory.validate_range(address, size, cpu.pc, MemoryAccess::read);
    buffers[index] = {true, address, size, stride};
}
void write_be16(GuestMemory& memory, uint32_t address, uint16_t value,
                uint32_t pc) {
    memory.write8(address, static_cast<uint8_t>(value >> 8), pc);
    memory.write8(address + 1, static_cast<uint8_t>(value), pc);
}
void write_be32(GuestMemory& memory, uint32_t address, uint32_t value,
                uint32_t pc) {
    memory.write8(address, static_cast<uint8_t>(value >> 24), pc);
    memory.write8(address + 1, static_cast<uint8_t>(value >> 16), pc);
    memory.write8(address + 2, static_cast<uint8_t>(value >> 8), pc);
    memory.write8(address + 3, static_cast<uint8_t>(value), pc);
}
void write_le32(GuestMemory& memory, uint32_t address, uint32_t value,
                uint32_t pc) {
    memory.write8(address, static_cast<uint8_t>(value), pc);
    memory.write8(address + 1, static_cast<uint8_t>(value >> 8), pc);
    memory.write8(address + 2, static_cast<uint8_t>(value >> 16), pc);
    memory.write8(address + 3, static_cast<uint8_t>(value >> 24), pc);
}
void clear_range(GuestMemory& memory, uint32_t address, uint32_t size,
                 uint32_t pc) {
    for (uint32_t offset = 0; offset < size; ++offset) {
        memory.write8(address + offset, 0, pc);
    }
}
template <size_t Count>
std::array<uint32_t, Count> integer_arguments(const CPUContext& cpu,
                                              GuestMemory& memory) {
    std::array<uint32_t, Count> args{};
    constexpr size_t register_count = Count < 8 ? Count : 8;
    for (size_t index = 0; index < register_count; ++index) {
        args[index] = cpu.gpr[3 + index];
    }
    if constexpr (Count > 8) {
        constexpr uint32_t stack_bytes = (Count - 8) * sizeof(uint32_t);
        const auto stack_address =
            static_cast<uint64_t>(cpu.gpr[1]) + 8 + 8 * sizeof(uint32_t);
        if (stack_address + stack_bytes > 0x100000000ull) {
            throw GuestFault("invalid Cafe stack argument area", cpu.gpr[1],
                             stack_bytes, cpu.pc, MemoryAccess::read);
        }
        memory.validate_range(static_cast<uint32_t>(stack_address), stack_bytes,
                              cpu.pc, MemoryAccess::read);
        for (size_t index = 8; index < Count; ++index) {
            args[index] = read_be32(
                memory,
                static_cast<uint32_t>(stack_address + (index - 8) * 4),
                cpu.pc);
        }
    }
    return args;
}
template <size_t Count>
std::array<uint32_t, Count> float_arguments(const CPUContext& cpu) {
    std::array<uint32_t, Count> args{};
    for (size_t index = 0; index < Count; ++index) {
        const auto value = std::bit_cast<double>(cpu.fpr[1 + index][0]);
        args[index] = std::bit_cast<uint32_t>(static_cast<float>(value));
    }
    return args;
}
void apply_default_state(Gx2State& state) {
    state.depth_stencil =
        {true, {1, 1, 1, 0, 0, 7, 2, 2, 2, 7, 2, 2, 2}};
    state.stencil_mask = {true, {0xFF, 0xFF, 1, 0xFF, 0xFF, 1}};
    state.polygon_control = {true, {0, 0, 0, 0, 2, 2, 0, 0, 0}};
    state.color_control = {true, {0xCC, 0, 0, 1}};
    for (uint32_t target = 0; target < state.blend_controls.size(); ++target) {
        state.blend_controls[target] =
            {true, {target, 4, 5, 0, 1, 4, 5, 0}};
    }
    state.blend_constant = {true, {0, 0, 0, 0}};
    state.alpha_test = {true, {0, 1, 0}};
    state.target_channel_masks =
        {true, {0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF}};
    state.alpha_to_mask = {true, {0, 0}};
    state.default_state_initialized = true;
}

// Source: Decaf gx2_fetchshader.cpp GX2CalcFetchShaderSizeEx, NoTessellation
// path (the only fetch-shader type the WWHD title-screen trace reaches).
// Latte instruction sizes (latte_instructions.h): ControlFlowInst 8,
// VertexFetchInst 16.
uint32_t calc_fetch_shader_size(uint32_t attribs) {
    const uint32_t fetches = attribs;
    const uint32_t cf_insts = (fetches + 15u) / 16u + 1u;
    const uint32_t aligned = (8u * cf_insts + 15u) & ~15u;
    return 16u * fetches + aligned;
}

// Latte SQ instruction-word encoders and GX2 attribute-format tables, source-
// verified against Decaf gx2_fetchshader.cpp / gx2_format.cpp and
// latte_instructions.h / latte_enum_sq.h bit layouts.
constexpr uint32_t set_field(uint32_t word, uint32_t value, uint32_t shift,
                             uint32_t width) {
    const uint32_t mask = width >= 32u ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return word | ((value & mask) << shift);
}

// GX2AttribFormat: low five bits select the component layout (gx2_enum.h
// GX2AttribFormatType); the high bits carry INTEGER/SIGNED/SCALED flags.
constexpr uint32_t kAttribInteger = 0x100;
constexpr uint32_t kAttribSigned = 0x200;
constexpr uint32_t kAttribScaled = 0x800;

uint32_t attrib_format_bits(uint32_t format, uint32_t pc) {
    switch (format & 0x1Fu) {
    case 0x00: case 0x01:
        return 8;
    case 0x02: case 0x03: case 0x04:
        return 16;
    case 0x05: case 0x06: case 0x07: case 0x08: case 0x09:
    case 0x0A: case 0x0B:
        return 32;
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
        return 64;
    case 0x10: case 0x11:
        return 96;
    case 0x12: case 0x13:
        return 128;
    default:
        throw GuestFault("invalid GX2 attribute format", format, 4, pc,
                         MemoryAccess::read);
    }
}

// SQ_DATA_FORMAT (latte_enum_sq.h) for each GX2AttribFormatType.
uint32_t attrib_data_format(uint32_t format, uint32_t pc) {
    switch (format & 0x1Fu) {
    case 0x00: return 1;   // FMT_8
    case 0x01: return 2;   // FMT_4_4
    case 0x02: return 5;   // FMT_16
    case 0x03: return 6;   // FMT_16_FLOAT
    case 0x04: return 7;   // FMT_8_8
    case 0x05: return 13;  // FMT_32
    case 0x06: return 14;  // FMT_32_FLOAT
    case 0x07: return 15;  // FMT_16_16
    case 0x08: return 16;  // FMT_16_16_FLOAT
    case 0x09: return 22;  // FMT_10_11_11_FLOAT
    case 0x0A: return 26;  // FMT_8_8_8_8
    case 0x0B: return 27;  // FMT_10_10_10_2
    case 0x0C: return 29;  // FMT_32_32
    case 0x0D: return 30;  // FMT_32_32_FLOAT
    case 0x0E: return 31;  // FMT_16_16_16_16
    case 0x0F: return 32;  // FMT_16_16_16_16_FLOAT
    case 0x10: return 47;  // FMT_32_32_32
    case 0x11: return 48;  // FMT_32_32_32_FLOAT
    case 0x12: return 34;  // FMT_32_32_32_32
    case 0x13: return 35;  // FMT_32_32_32_32_FLOAT
    default:
        throw GuestFault("invalid GX2 attribute format", format, 4, pc,
                         MemoryAccess::read);
    }
}

// Format-implied SQ_ENDIAN (getAttribFormatSwapMode -> getSwapModeEndian,
// which is an identity map for None/8In16/8In32). Used when the attribute
// requests GX2EndianSwapMode::Default (3).
uint32_t attrib_format_endian(uint32_t format, uint32_t pc) {
    switch (format & 0x1Fu) {
    case 0x00: case 0x01: case 0x04: case 0x0A:
        return 0;  // SQ_ENDIAN::NONE
    case 0x02: case 0x03: case 0x07: case 0x08: case 0x0E: case 0x0F:
        return 1;  // SQ_ENDIAN::SWAP_8IN16
    case 0x05: case 0x06: case 0x09: case 0x0B: case 0x0C: case 0x0D:
    case 0x10: case 0x11: case 0x12: case 0x13:
        return 2;  // SQ_ENDIAN::SWAP_8IN32
    default:
        throw GuestFault("invalid GX2 attribute format", format, 4, pc,
                         MemoryAccess::read);
    }
}

// Source: Decaf gx2_fetchshader.cpp GX2InitFetchShaderEx, NoTessellation /
// Discrete path (the only fetch-shader configuration the WWHD title-screen
// trace reaches). Emits one VTX clause of vertex-fetch instructions plus a
// VTX_TC + RETURN control-flow clause, byte-identical to the deterministic
// program Decaf generates, and fills the GX2FetchShader struct per WUT.
void init_fetch_shader(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t fetch_shader = cpu.gpr[3];
    const uint32_t buffer = cpu.gpr[4];
    const uint32_t attrib_count = cpu.gpr[5];
    const uint32_t attribs = cpu.gpr[6];
    const uint32_t type = cpu.gpr[7];
    const uint32_t tess_mode = cpu.gpr[8];
    const uint32_t pc = cpu.pc;

    // GX2FetchShaderType::NoTessellation and GX2TessellationMode::Discrete are
    // both value 0; any other configuration is unimplemented and faults loudly
    // rather than emitting an unverified program (matches Decaf's decaf_abort).
    if (type != 0) {
        throw GuestFault("unsupported GX2 tessellated fetch-shader type", type,
                         4, pc, MemoryAccess::read);
    }
    if (tess_mode != 0) {
        throw GuestFault("unsupported GX2 fetch-shader tessellation mode",
                         tess_mode, 4, pc, MemoryAccess::read);
    }

    const uint32_t size = calc_fetch_shader_size(attrib_count);
    const uint32_t fetch_count = attrib_count;
    const uint32_t cf_count = (fetch_count + 15u) / 16u + 1u;
    const uint32_t cf_size = cf_count * 8u;
    const uint32_t fetch_offset = (cf_size + 15u) & ~15u;

    // Preflight every guest range before any write so a fault is atomic.
    memory.validate_range(fetch_shader, 0x20, pc, MemoryAccess::write);
    memory.validate_range(buffer, size, pc, MemoryAccess::write);
    if (attrib_count != 0) {
        memory.validate_range(attribs, attrib_count * 0x20u, pc,
                              MemoryAccess::read);
    }

    // Build the program into a staging buffer, committed only after all
    // attribute reads succeed (an invalid format faults mid-build, atomically).
    std::vector<uint32_t> words(size / 4u, 0u);
    uint32_t num_divisors = 0;
    std::array<uint32_t, 2> divisors{0, 0};

    uint32_t fetch_word = fetch_offset / 4u;
    for (uint32_t i = 0; i < attrib_count; ++i) {
        const uint32_t entry = attribs + i * 0x20u;
        const uint32_t location = read_be32(memory, entry + 0x00, pc);
        const uint32_t attr_buffer = read_be32(memory, entry + 0x04, pc);
        const uint32_t offset = read_be32(memory, entry + 0x08, pc);
        const uint32_t format = read_be32(memory, entry + 0x0C, pc);
        const uint32_t attr_type = read_be32(memory, entry + 0x10, pc);
        const uint32_t alu_divisor = read_be32(memory, entry + 0x14, pc);
        const uint32_t mask = read_be32(memory, entry + 0x18, pc);
        const uint32_t endian_swap = read_be32(memory, entry + 0x1C, pc);

        // Buffer 16 is a tessellation control channel with no emitted fetch;
        // the NoTessellation path never uses it, but skip it faithfully.
        if (attr_buffer == 16) {
            continue;
        }

        // SQ_VTX_WORD0: SEMANTIC fetch from a VS attribute resource
        // (BUFFER_ID = VS_ATTRIB_RESOURCE_0 + buffer - VS_TEX_RESOURCE_0).
        uint32_t word0 = 0;
        word0 = set_field(word0, 1u, 0, 5);                 // VTX_INST SEMANTIC
        word0 = set_field(word0, 0x140u + attr_buffer - 0xA0u, 8, 8);  // BUFFER_ID
        if (attr_type != 0) {
            // Per-instance fetch: select the divisor lane (SQ_SEL W/Y/Z) and
            // accumulate up to two custom divisors into the shader.
            uint32_t sel_x = 0;  // SQ_SEL::SEL_X
            if (alu_divisor == 1) {
                sel_x = 3;  // SEL_W
            } else if (num_divisors >= 1 && alu_divisor == divisors[0]) {
                sel_x = 1;  // SEL_Y
            } else if (num_divisors >= 2 && alu_divisor == divisors[1]) {
                sel_x = 2;  // SEL_Z
            } else if (num_divisors < divisors.size()) {
                divisors[num_divisors] = alu_divisor;
                if (num_divisors == 0) {
                    sel_x = 1;  // SEL_Y
                } else if (num_divisors == 1) {
                    sel_x = 2;  // SEL_Z
                }
                ++num_divisors;
            }
            word0 = set_field(word0, 1u, 5, 2);      // FETCH_TYPE INSTANCE_DATA
            word0 = set_field(word0, sel_x, 24, 2);  // SRC_SEL_X
        } else {
            // Per-vertex fetch: IndexMapNoTess[0] = { GPR 0, channel X }.
            word0 = set_field(word0, 0u, 16, 7);  // SRC_GPR
            word0 = set_field(word0, 0u, 24, 2);  // SRC_SEL_X = SEL_X
        }
        word0 = set_field(word0, attrib_format_bits(format, pc) / 8u - 1u, 26, 6);

        // SQ_VTX_WORD1 (shares the word with SQ_VTX_WORD1_GPR::DST_GPR).
        uint32_t word1 = 0;
        word1 = set_field(word1, location, 0, 7);          // DST_GPR
        word1 = set_field(word1, (mask >> 24) & 0x7u, 9, 3);   // DST_SEL_X
        word1 = set_field(word1, (mask >> 16) & 0x7u, 12, 3);  // DST_SEL_Y
        word1 = set_field(word1, (mask >> 8) & 0x7u, 15, 3);   // DST_SEL_Z
        word1 = set_field(word1, mask & 0x7u, 18, 3);          // DST_SEL_W
        word1 = set_field(word1, attrib_data_format(format, pc), 22, 6);
        uint32_t num_format = 0;  // SQ_NUM_FORMAT::NORM
        if (format & kAttribScaled) {
            num_format = 2;  // SCALED
        } else if (format & kAttribInteger) {
            num_format = 1;  // INT
        }
        const uint32_t format_comp = (format & kAttribSigned) ? 1u : 0u;
        word1 = set_field(word1, num_format, 28, 2);
        word1 = set_field(word1, format_comp, 30, 1);

        // SQ_VTX_WORD2: byte offset, endian swap, mega-fetch flag.
        uint32_t word2 = 0;
        word2 = set_field(word2, offset, 0, 16);
        const uint32_t endian = endian_swap == 3u
                                    ? attrib_format_endian(format, pc)
                                    : (endian_swap & 0x3u);
        word2 = set_field(word2, endian, 16, 2);  // ENDIAN_SWAP
        word2 = set_field(word2, 1u, 19, 1);       // MEGA_FETCH

        words[fetch_word + 0] = word0;
        words[fetch_word + 1] = word1;
        words[fetch_word + 2] = word2;
        words[fetch_word + 3] = 0;  // padding
        fetch_word += 4;
    }

    // VTX_TC control-flow clause: one per 16 vertex fetches (barrier is 0 on
    // the NoTessellation path).
    uint32_t cf_word = 0;
    for (uint32_t i = 0; i + 1 < cf_count && fetch_count != 0; ++i) {
        uint32_t fetches = 16u;
        if (fetch_count < (i + 1u) * 16u) {
            fetches = fetch_count % 16u;
        }
        uint32_t word0 = (fetch_offset + 16u * i * 16u) / 8u;  // ADDR (>> 3)
        uint32_t word1 = 0;
        word1 = set_field(word1, (fetches - 1u) & 0x7u, 10, 3);       // COUNT
        word1 = set_field(word1, ((fetches - 1u) >> 3) & 0x1u, 19, 1);  // COUNT_3
        word1 = set_field(word1, 3u, 23, 7);  // CF_INST VTX_TC
        words[cf_word + 0] = word0;
        words[cf_word + 1] = word1;
        cf_word += 2;
    }

    // Terminating RETURN control-flow instruction with a barrier.
    {
        uint32_t word1 = 0;
        word1 = set_field(word1, 0x14u, 23, 7);  // CF_INST RETURN
        word1 = set_field(word1, 1u, 31, 1);      // BARRIER
        words[cf_word + 0] = 0;
        words[cf_word + 1] = word1;
    }

    // Commit the program as little-endian Latte machine words.
    for (uint32_t i = 0; i < words.size(); ++i) {
        write_le32(memory, buffer + i * 4u, words[i], pc);
    }

    // Fill the GX2FetchShader struct (big-endian be2_val ABI). NoTessellation
    // uses 0 GPRs, so SQ_PGM_RESOURCES_FS::NUM_GPRS (bits 0-7) clears while any
    // other bits the caller left in the register are preserved.
    const uint32_t regs = read_be32(memory, fetch_shader + 0x04, pc);
    write_be32(memory, fetch_shader + 0x00, type, pc);          // type
    write_be32(memory, fetch_shader + 0x04, regs & ~0xFFu, pc);  // sq_pgm_resources_fs
    write_be32(memory, fetch_shader + 0x08, size, pc);          // size
    write_be32(memory, fetch_shader + 0x0C, buffer, pc);        // data
    write_be32(memory, fetch_shader + 0x10, attrib_count, pc);  // attribCount
    write_be32(memory, fetch_shader + 0x14, num_divisors, pc);  // numDivisors
    write_be32(memory, fetch_shader + 0x18, divisors[0], pc);   // divisors[0]
    write_be32(memory, fetch_shader + 0x1C, divisors[1], pc);   // divisors[1]
}

// Source: Decaf gx2_sampler.cpp GX2InitSampler + latte_registers_sq.h
// SQ_TEX_SAMPLER_WORD{0,1,2}_N bitfields. Packs the three deterministic
// Latte sampler-register words Decaf computes into the GX2Sampler struct
// (three be2_val<> words at offsets 0x00/0x04/0x08, struct size 0x0C).
void init_sampler(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t clamp_mode = cpu.gpr[4];    // GX2TexClampMode
    const uint32_t min_mag_filter = cpu.gpr[5];  // GX2TexXYFilterMode
    const uint32_t pc = cpu.pc;

    // WORD0: CLAMP_X/Y/Z (bits 0/3/6, 3 bits each) = clampMode;
    // XY_MAG_FILTER (bits 9-11), XY_MIN_FILTER (bits 12-14) = minMagFilter.
    // The GX2 enum values are stored verbatim (static_cast to the SQ enum).
    uint32_t word0 = 0;
    word0 = set_field(word0, clamp_mode, 0, 3);        // CLAMP_X
    word0 = set_field(word0, clamp_mode, 3, 3);        // CLAMP_Y
    word0 = set_field(word0, clamp_mode, 6, 3);        // CLAMP_Z
    word0 = set_field(word0, min_mag_filter, 9, 3);    // XY_MAG_FILTER
    word0 = set_field(word0, min_mag_filter, 12, 3);   // XY_MIN_FILTER

    // WORD1: MAX_LOD (bits 10-19) = fixed_from_data<ufixed_4_6_t>(1023),
    // i.e. the raw 10-bit fixed-point representation 1023. MIN_LOD/LOD_BIAS 0.
    uint32_t word1 = 0;
    word1 = set_field(word1, 1023u, 10, 10);           // MAX_LOD

    // WORD2: TYPE (bit 31) = true.
    uint32_t word2 = 0;
    word2 = set_field(word2, 1u, 31, 1);               // TYPE

    // Preflight the full struct-out range so a fault is atomic (no write).
    memory.validate_range(sampler, 0x0C, pc, MemoryAccess::write);

    // be2_val<> words: big-endian, matching the GX2Sampler struct ABI.
    write_be32(memory, sampler + 0x00, word0, pc);
    write_be32(memory, sampler + 0x04, word1, pc);
    write_be32(memory, sampler + 0x08, word2, pc);
}

// Source: Decaf gx2_contextstate.cpp GX2GetContextStateDisplayList +
// gx2_contextstate.h GX2ContextState layout. Returns the address of the
// context state's shadow display list (struct offset 0x9E00) and its recorded
// size (offset 0x9804) through the two optional out-pointers. NULL out-
// pointers are skipped, matching Decaf's `if (outX)` guards.
void get_context_state_display_list(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t state = cpu.gpr[3];
    const uint32_t out_display_list = cpu.gpr[4];
    const uint32_t out_size = cpu.gpr[5];
    const uint32_t pc = cpu.pc;

    // Preflight every guest access before writing so a fault is atomic.
    if (out_display_list != 0) {
        memory.validate_range(out_display_list, 4, pc, MemoryAccess::write);
    }
    uint32_t size = 0;
    if (out_size != 0) {
        memory.validate_range(state + kContextDisplayListSizeOffset, 4, pc,
                              MemoryAccess::read);
        memory.validate_range(out_size, 4, pc, MemoryAccess::write);
        size = read_be32(memory, state + kContextDisplayListSizeOffset, pc);
    }

    if (out_display_list != 0) {
        write_be32(memory, out_display_list,
                   state + kContextDisplayListOffset, pc);
    }
    if (out_size != 0) {
        write_be32(memory, out_size, size, pc);
    }
}

// Decaf's R600 AddrLib ComputeHtileInfo for 8x8 blocks on Latte:
// 32 bits per block, 256x256 metadata tiles, 4 KiB size alignment.
void calc_depth_buffer_hiz_info(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t depth_buffer = cpu.gpr[3];
    const uint32_t out_size = cpu.gpr[4];
    const uint32_t out_alignment = cpu.gpr[5];
    const uint32_t pc = cpu.pc;
    const uint64_t pitch =
        (uint64_t{read_be32(memory, depth_buffer + 0x3C, pc)} + 0xFF) &
        ~uint64_t{0xFF};
    const uint64_t height =
        (uint64_t{read_be32(memory, depth_buffer + 0x08, pc)} + 0xFF) &
        ~uint64_t{0xFF};
    const uint64_t slices =
        read_be32(memory, depth_buffer + 0x0C, pc);
    if ((pitch != 0 &&
         height > std::numeric_limits<uint64_t>::max() / pitch) ||
        (pitch * height != 0 &&
         slices > std::numeric_limits<uint64_t>::max() / (pitch * height))) {
        throw GuestFault("GX2 HiZ size overflow", depth_buffer, 0xAC, pc,
                         MemoryAccess::read);
    }
    const uint64_t bytes = pitch * height * slices / 16;
    const uint64_t aligned = (bytes + 0xFFF) & ~uint64_t{0xFFF};
    if (aligned > std::numeric_limits<uint32_t>::max()) {
        throw GuestFault("GX2 HiZ size overflow", depth_buffer, 0xAC, pc,
                         MemoryAccess::read);
    }

    memory.validate_range(depth_buffer + 0x84, 4, pc, MemoryAccess::write);
    if (out_size != 0) {
        memory.validate_range(out_size, 4, pc, MemoryAccess::write);
    }
    if (out_alignment != 0) {
        memory.validate_range(out_alignment, 4, pc, MemoryAccess::write);
    }
    write_be32(memory, depth_buffer + 0x84,
               static_cast<uint32_t>(aligned), pc);
    if (out_size != 0) {
        write_be32(memory, out_size, static_cast<uint32_t>(aligned), pc);
    }
    if (out_alignment != 0) {
        write_be32(memory, out_alignment, 0x200, pc);
    }
}

// Source: Decaf gx2_surface.cpp GX2CalcSurfaceSizeAndAlignment. Reads the guest
// GX2Surface (WUT gx2/surface.h layout, size 0x74), computes the AddrLib size /
// alignment / pitch / resolved tile mode via latte_surface, and writes the
// computed fields back big-endian after validating the full struct range so a
// fault is atomic. Unreached tile-mode/format/feature combos throw a structured
// unsupported stop rather than guessing.
void calc_surface_size_and_alignment(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t address = cpu.gpr[3];
    const uint32_t pc = cpu.pc;

    LatteSurfaceDescriptor descriptor{
        read_be32(memory, address + 0x00, pc),  // dim
        read_be32(memory, address + 0x04, pc),  // width
        read_be32(memory, address + 0x08, pc),  // height
        read_be32(memory, address + 0x0C, pc),  // depth
        read_be32(memory, address + 0x10, pc),  // mipLevels
        read_be32(memory, address + 0x14, pc),  // format
        read_be32(memory, address + 0x18, pc),  // aa
        read_be32(memory, address + 0x1C, pc),  // use
        read_be32(memory, address + 0x30, pc),  // tileMode
        read_be32(memory, address + 0x34, pc),  // swizzle
    };

    const auto result = calculate_surface_size_and_alignment(descriptor);
    if (const auto* error = std::get_if<LatteSurfaceComputeError>(&result)) {
        throw GuestFault(
            std::string{"unsupported GX2 surface layout: "} +
                "dim=" + std::to_string(error->dim) +
                " format=" + std::to_string(error->format) +
                " aa=" + std::to_string(error->aa) +
                " tileMode=" + std::to_string(error->tile_mode) +
                " mipLevels=" + std::to_string(error->mip_levels) +
                " width=" + std::to_string(descriptor.width) +
                " height=" + std::to_string(descriptor.height) +
                " depth=" + std::to_string(descriptor.depth) +
                " use=" + std::to_string(descriptor.use) +
                " error=" + std::to_string(static_cast<unsigned>(error->code)),
            address, 0x74, pc, MemoryAccess::read);
    }
    const auto& layout = std::get<LatteSurfaceLayout>(result);

    // Preflight the full struct range so no field is written on a fault.
    memory.validate_range(address, 0x74, pc, MemoryAccess::write);

    write_be32(memory, address + 0x10, layout.mip_levels, pc);   // mipLevels
    write_be32(memory, address + 0x20, layout.image_size, pc);   // imageSize
    write_be32(memory, address + 0x28, layout.mipmap_size, pc);  // mipmapSize
    write_be32(memory, address + 0x30, layout.tile_mode, pc);    // tileMode
    write_be32(memory, address + 0x34, layout.swizzle, pc);      // swizzle
    write_be32(memory, address + 0x38, layout.alignment, pc);    // alignment
    write_be32(memory, address + 0x3C, layout.pitch, pc);        // pitch
    for (size_t index = 0; index < layout.mip_level_offsets.size(); ++index) {
        write_be32(memory, address + 0x40 + static_cast<uint32_t>(index * 4),
                   layout.mip_level_offsets[index], pc);
    }
}
void copy_surface(GuestMemory& memory, CPUContext& cpu) {
    constexpr uint32_t surface_size = 0x74;
    constexpr uint32_t tiled_1d_thin1 = 2;
    constexpr uint32_t tiled_2d_thin1 = 4;
    const uint32_t source = cpu.gpr[3];
    const uint32_t destination = cpu.gpr[6];
    const uint32_t pc = cpu.pc;
    memory.validate_range(source, surface_size, pc, MemoryAccess::read);
    memory.validate_range(destination, surface_size, pc, MemoryAccess::read);

    const uint32_t source_width = read_be32(memory, source + 0x04, pc);
    const uint32_t source_height = read_be32(memory, source + 0x08, pc);
    const uint32_t destination_width =
        read_be32(memory, destination + 0x04, pc);
    const uint32_t destination_height =
        read_be32(memory, destination + 0x08, pc);
    const uint32_t source_format = read_be32(memory, source + 0x14, pc);
    const uint32_t destination_format =
        read_be32(memory, destination + 0x14, pc);
    const uint32_t source_aa = read_be32(memory, source + 0x18, pc);
    const uint32_t destination_aa =
        read_be32(memory, destination + 0x18, pc);
    const uint32_t source_image_size = read_be32(memory, source + 0x20, pc);
    const uint32_t destination_image_size =
        read_be32(memory, destination + 0x20, pc);
    const uint32_t source_image = read_be32(memory, source + 0x24, pc);
    const uint32_t destination_image =
        read_be32(memory, destination + 0x24, pc);
    const uint32_t source_mode = read_be32(memory, source + 0x30, pc);
    const uint32_t destination_mode =
        read_be32(memory, destination + 0x30, pc);
    const uint32_t source_swizzle = read_be32(memory, source + 0x34, pc);
    const uint32_t destination_swizzle =
        read_be32(memory, destination + 0x34, pc);
    const uint32_t source_pitch = read_be32(memory, source + 0x3C, pc);
    const uint32_t destination_pitch =
        read_be32(memory, destination + 0x3C, pc);

    uint32_t bytes_per_element = 0;
    switch (source_format & 0x3Fu) {
    case 0x07:
        bytes_per_element = 2;
        break;
    case 0x0E:
        bytes_per_element = 4;
        break;
    case 0x1A:
        bytes_per_element = 4;
        break;
    }

    const auto supported_mode = supported_surface_mode;
    if (cpu.gpr[4] != 0 || cpu.gpr[5] != 0 || cpu.gpr[7] != 0 ||
        cpu.gpr[8] != 0 || bytes_per_element == 0 ||
        destination_format != source_format || source_aa != 0 ||
        destination_aa != 0 || source_width == 0 || source_height == 0 ||
        destination_width == 0 || destination_height == 0 ||
        !supported_mode(source_mode) || !supported_mode(destination_mode)) {
        throw GuestFault("unsupported GX2CopySurface layout", source,
                         surface_size, pc, MemoryAccess::read);
    }

    const auto required_size = [bytes_per_element](
                                   uint32_t pitch, uint32_t height,
                                   uint32_t mode) -> uint64_t {
        if (pitch == 0 ||
            (mode == tiled_1d_thin1 && (pitch & 7u) != 0) ||
            (mode == tiled_2d_thin1 && (pitch & 31u) != 0)) {
            return std::numeric_limits<uint64_t>::max();
        }
        const uint64_t stored_height =
            mode == tiled_1d_thin1
                ? (static_cast<uint64_t>(height) + 7u) & ~uint64_t{7}
            : mode == tiled_2d_thin1
                ? (static_cast<uint64_t>(height) + 15u) & ~uint64_t{15}
                : height;
        return static_cast<uint64_t>(pitch) * stored_height *
               bytes_per_element;
    };
    if (source_pitch < source_width ||
        destination_pitch < destination_width ||
        required_size(source_pitch, source_height, source_mode) >
            source_image_size ||
        required_size(destination_pitch, destination_height,
                      destination_mode) > destination_image_size) {
        throw GuestFault("invalid GX2CopySurface image size", source,
                         surface_size, pc, MemoryAccess::read);
    }
    memory.validate_range(source_image, source_image_size, pc,
                          MemoryAccess::read);
    memory.validate_range(destination_image, destination_image_size, pc,
                          MemoryAccess::write);

    for (uint32_t y = 0; y < destination_height; ++y) {
        const uint32_t source_y = static_cast<uint32_t>(
            static_cast<uint64_t>(source_height) * y / destination_height);
        for (uint32_t x = 0; x < destination_width; ++x) {
            const uint32_t source_x = static_cast<uint32_t>(
                static_cast<uint64_t>(source_width) * x / destination_width);
            const uint32_t source_offset = surface_pixel_offset(
                source_x, source_y, source_pitch, source_mode, source_swizzle,
                bytes_per_element);
            const uint32_t destination_offset = surface_pixel_offset(
                x, y, destination_pitch, destination_mode,
                destination_swizzle, bytes_per_element);
            for (uint32_t byte = 0; byte < bytes_per_element; ++byte) {
                memory.write8(destination_image + destination_offset + byte,
                              memory.read8(source_image + source_offset + byte,
                                           pc),
                              pc);
            }
        }
    }
}


// Source: Decaf gx2_texture.cpp GX2InitTextureRegs + latte_registers_sq.h
// SQ_TEX_RESOURCE_WORD{0,1,4,5,6}_N bit layouts / latte_enum_sq.h enum values.
// Packs the five deterministic Latte texture-resource register words from the
// guest GX2Texture (WUT gx2/texture.h, size 0x9C) and writes them back (plus
// the clamped minimum view/surface fields) big-endian after validating the
// full struct range so a fault is atomic. Pure register packing, no PM4/draw.
void init_texture_regs(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t address = cpu.gpr[3];
    const uint32_t pc = cpu.pc;

    // Preflight the full struct range up front so a short/unbacked struct
    // faults before any field is read or written (atomic).
    memory.validate_range(address, 0x9C, pc, MemoryAccess::write);

    // Surface fields (WUT GX2Surface layout).
    const uint32_t dim = read_be32(memory, address + 0x00, pc);
    const uint32_t width = std::max<uint32_t>(
        1u, read_be32(memory, address + 0x04, pc));
    const uint32_t height = std::max<uint32_t>(
        1u, read_be32(memory, address + 0x08, pc));
    const uint32_t depth = std::max<uint32_t>(
        1u, read_be32(memory, address + 0x0C, pc));
    const uint32_t mip_levels = std::max<uint32_t>(
        1u, read_be32(memory, address + 0x10, pc));
    const uint32_t format = read_be32(memory, address + 0x14, pc);
    const uint32_t aa = read_be32(memory, address + 0x18, pc);
    const uint32_t use = read_be32(memory, address + 0x1C, pc);
    const uint32_t tile_mode = read_be32(memory, address + 0x30, pc);
    const uint32_t surface_pitch = read_be32(memory, address + 0x3C, pc);

    // GX2Texture view fields + compMap + incoming register words.
    const uint32_t view_first_mip = read_be32(memory, address + 0x74, pc);
    const uint32_t view_num_mips = std::max<uint32_t>(
        1u, read_be32(memory, address + 0x78, pc));
    const uint32_t view_first_slice = read_be32(memory, address + 0x7C, pc);
    const uint32_t view_num_slices = std::max<uint32_t>(
        1u, read_be32(memory, address + 0x80, pc));
    const uint32_t comp_map = read_be32(memory, address + 0x84, pc);
    const uint32_t word6_in = read_be32(memory, address + 0x98, pc);

    const uint32_t hw_format = format & 0x3Fu;

    // Word 0: DIM, TILE_MODE, TILE_TYPE, PITCH, TEX_WIDTH.
    uint32_t pitch = surface_pitch;
    if (hw_format >= 49u && hw_format <= 53u) {  // FMT_BC1..FMT_BC5
        pitch *= 4u;
    }
    pitch = std::max<uint32_t>(pitch, 8u);
    const uint32_t tile_type = (use & (1u << 2)) ? 1u : 0u;  // DepthBuffer
    uint32_t word0 = 0;
    word0 = set_field(word0, dim & 0x7u, 0, 3);            // DIM
    word0 = set_field(word0, tile_mode & 0xFu, 3, 4);      // TILE_MODE
    word0 = set_field(word0, tile_type, 7, 1);             // TILE_TYPE
    word0 = set_field(word0, (pitch / 8u) - 1u, 8, 11);    // PITCH
    word0 = set_field(word0, width - 1u, 19, 13);          // TEX_WIDTH

    // Word 1: TEX_HEIGHT, TEX_DEPTH, DATA_FORMAT.
    uint32_t tex_depth = 0;
    if (dim == 3u) {  // TextureCube
        tex_depth = (depth / 6u) - 1u;
    } else if (dim == 2u || dim == 7u || dim == 5u || dim == 4u) {
        tex_depth = depth - 1u;  // 3D / 2DMSAAArray / 2DArray / 1DArray
    }
    uint32_t word1 = 0;
    word1 = set_field(word1, height - 1u, 0, 13);          // TEX_HEIGHT
    word1 = set_field(word1, tex_depth, 13, 13);           // TEX_DEPTH
    word1 = set_field(word1, hw_format, 26, 6);            // DATA_FORMAT

    // Word 4: component/number format, endian, dst-select, base level.
    const uint32_t format_comp = (format & 0x200u) ? 1u : 0u;  // SIGNED
    uint32_t num_format = 0;  // NORM
    if (format & 0x800u) {
        num_format = 2;  // SCALED
    } else if (format & 0x100u) {
        num_format = 1;  // INT
    }
    const uint32_t force_degamma = (format & 0x400u) ? 1u : 0u;
    // getSurfaceFormatEndian: Decaf gx2_format.cpp sSurfaceFormatData endian
    // column is 0 for every format -> SQ_ENDIAN::NONE.
    uint32_t word4 = 0;
    word4 = set_field(word4, format_comp, 0, 2);   // FORMAT_COMP_X
    word4 = set_field(word4, format_comp, 2, 2);   // FORMAT_COMP_Y
    word4 = set_field(word4, format_comp, 4, 2);   // FORMAT_COMP_Z
    word4 = set_field(word4, format_comp, 6, 2);   // FORMAT_COMP_W
    word4 = set_field(word4, num_format, 8, 2);    // NUM_FORMAT_ALL
    word4 = set_field(word4, force_degamma, 11, 1);  // FORCE_DEGAMMA
    word4 = set_field(word4, 2u, 14, 2);           // REQUEST_SIZE
    word4 = set_field(word4, (comp_map >> 24) & 0x7u, 16, 3);  // DST_SEL_X
    word4 = set_field(word4, (comp_map >> 16) & 0x7u, 19, 3);  // DST_SEL_Y
    word4 = set_field(word4, (comp_map >> 8) & 0x7u, 22, 3);   // DST_SEL_Z
    word4 = set_field(word4, comp_map & 0x7u, 25, 3);          // DST_SEL_W
    word4 = set_field(word4, view_first_mip & 0xFu, 28, 4);    // BASE_LEVEL

    // Word 5: level/array view range (fully determined, all 32 bits).
    uint32_t last_level = view_first_mip + view_num_mips - 1u;
    if (aa == 1u) {
        last_level = 1u;
    } else if (aa == 2u) {
        last_level = 2u;
    } else if (aa == 3u) {
        last_level = 3u;
    }
    uint32_t word5 = 0;
    word5 = set_field(word5, last_level & 0xFu, 0, 4);           // LAST_LEVEL
    word5 = set_field(word5, view_first_slice & 0x1FFFu, 4, 13); // BASE_ARRAY
    word5 = set_field(word5,
                      (view_first_slice + view_num_slices - 1u) & 0x1FFFu, 17,
                      13);                                        // LAST_ARRAY
    // YUV_CONV stays 0 (only set for a cube map with non-zero TEX_DEPTH).

    // Word 6: preserve the incoming bits outside the three written fields.
    constexpr uint32_t kWord6ClearMask =
        (0x7u << 2) | (0x7u << 5) | (0x3u << 30);
    uint32_t word6 = word6_in & ~kWord6ClearMask;
    word6 = set_field(word6, 4u, 2, 3);   // MAX_ANISO_RATIO
    word6 = set_field(word6, 7u, 5, 3);   // PERF_MODULATION
    word6 = set_field(word6, 2u, 30, 2);  // TYPE = VALID_TEXTURE
    // Clamped minimum view/surface fields (Decaf writes these when zero).
    write_be32(memory, address + 0x04, width, pc);
    write_be32(memory, address + 0x08, height, pc);
    write_be32(memory, address + 0x0C, depth, pc);
    write_be32(memory, address + 0x10, mip_levels, pc);
    write_be32(memory, address + 0x78, view_num_mips, pc);
    write_be32(memory, address + 0x80, view_num_slices, pc);

    // Register words (be2_val) at GX2Texture::regs (0x88).
    write_be32(memory, address + 0x88, word0, pc);
    write_be32(memory, address + 0x8C, word1, pc);
    write_be32(memory, address + 0x90, word4, pc);
    write_be32(memory, address + 0x94, word5, pc);
    write_be32(memory, address + 0x98, word6, pc);
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerClamping + latte_registers_sq.h
// SQ_TEX_SAMPLER_WORD0_N. Replaces the CLAMP_X/Y/Z fields (bits 0-2/3-5/6-8) of
// the existing GX2Sampler word0 with the three clamp-mode arguments, preserving
// every other bit. GX2TexClampMode maps 1:1 onto SQ_TEX_CLAMP.
void init_sampler_clamping(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t clamp_x = cpu.gpr[4] & 0x7u;
    const uint32_t clamp_y = cpu.gpr[5] & 0x7u;
    const uint32_t clamp_z = cpu.gpr[6] & 0x7u;
    const uint32_t pc = cpu.pc;

    // Only word0 is touched; preflight it so the single write is atomic.
    memory.validate_range(sampler, 0x04, pc, MemoryAccess::write);
    uint32_t word0 = read_be32(memory, sampler + 0x00, pc);
    word0 &= ~0x1FFu;  // clear CLAMP_X/Y/Z
    word0 = set_field(word0, clamp_x, 0, 3);
    word0 = set_field(word0, clamp_y, 3, 3);
    word0 = set_field(word0, clamp_z, 6, 3);
    write_be32(memory, sampler + 0x00, word0, pc);
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerXYFilter + latte_registers_sq.h
// SQ_TEX_SAMPLER_WORD0_N. Replaces XY_MAG_FILTER (bits 9-11), XY_MIN_FILTER
// (bits 12-14) and MAX_ANISO_RATIO (bits 19-21) of word0, preserving all other
// bits. GX2 filter / aniso enums map 1:1 onto the SQ enums.
void init_sampler_xy_filter(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t filter_mag = cpu.gpr[4] & 0x7u;
    const uint32_t filter_min = cpu.gpr[5] & 0x7u;
    const uint32_t max_aniso = cpu.gpr[6] & 0x7u;
    const uint32_t pc = cpu.pc;

    memory.validate_range(sampler, 0x04, pc, MemoryAccess::write);
    uint32_t word0 = read_be32(memory, sampler + 0x00, pc);
    word0 &= ~((0x7u << 9) | (0x7u << 12) | (0x7u << 19));
    word0 = set_field(word0, filter_mag, 9, 3);    // XY_MAG_FILTER
    word0 = set_field(word0, filter_min, 12, 3);   // XY_MIN_FILTER
    word0 = set_field(word0, max_aniso, 19, 3);    // MAX_ANISO_RATIO
    write_be32(memory, sampler + 0x00, word0, pc);
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerZMFilter + latte_registers_sq.h
// SQ_TEX_SAMPLER_WORD0_N. Replaces Z_FILTER (bits 15-16) and MIP_FILTER (bits
// 17-18) of word0, preserving all other bits.
void init_sampler_zm_filter(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t filter_z = cpu.gpr[4] & 0x3u;
    const uint32_t filter_mip = cpu.gpr[5] & 0x3u;
    const uint32_t pc = cpu.pc;

    memory.validate_range(sampler, 0x04, pc, MemoryAccess::write);
    uint32_t word0 = read_be32(memory, sampler + 0x00, pc);
    word0 &= ~((0x3u << 15) | (0x3u << 17));
    word0 = set_field(word0, filter_z, 15, 2);    // Z_FILTER
    word0 = set_field(word0, filter_mip, 17, 2);  // MIP_FILTER
    write_be32(memory, sampler + 0x00, word0, pc);
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerLOD + latte_registers_sq.h
// SQ_TEX_SAMPLER_WORD1_N. The lodMin/lodMax/lodBias arguments arrive as floats
// (PPC FP ABI: fpr1-3), are clamped to [0,16]/[0,16]/[-32,32], and packed as
// cnl fixed-point: MIN_LOD/MAX_LOD as ufixed_4_6 (bits 0-9 / 10-19), LOD_BIAS
// as sfixed_1_5_6 (bits 20-31). cnl's native conversion truncates toward zero.
// These three fields cover all 32 bits, so word1 is fully rebuilt.
void init_sampler_lod(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t pc = cpu.pc;
    const auto fp = [&cpu](size_t index) {
        return static_cast<float>(std::bit_cast<double>(cpu.fpr[index][0]));
    };
    float lod_min = std::min(std::max(fp(1), 0.0f), 16.0f);
    float lod_max = std::min(std::max(fp(2), 0.0f), 16.0f);
    float lod_bias = std::min(std::max(fp(3), -32.0f), 32.0f);

    const uint32_t min_lod =
        static_cast<uint32_t>(lod_min * 64.0f) & 0x3FFu;
    const uint32_t max_lod =
        static_cast<uint32_t>(lod_max * 64.0f) & 0x3FFu;
    const uint32_t lod_bias_bits =
        static_cast<uint32_t>(static_cast<int32_t>(lod_bias * 64.0f)) & 0xFFFu;

    memory.validate_range(sampler + 0x04, 0x04, pc, MemoryAccess::write);
    uint32_t word1 = 0;
    word1 = set_field(word1, min_lod, 0, 10);          // MIN_LOD
    word1 = set_field(word1, max_lod, 10, 10);         // MAX_LOD
    word1 = set_field(word1, lod_bias_bits, 20, 12);   // LOD_BIAS
    write_be32(memory, sampler + 0x04, word1, pc);
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerDepthCompare +
// latte_registers_sq.h SQ_TEX_SAMPLER_WORD0_N. Replaces DEPTH_COMPARE_FUNCTION
// (bits 26-28) of word0, preserving all other bits. GX2CompareFunction maps
// 1:1 onto REF_FUNC.
void init_sampler_depth_compare(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t depth_compare = cpu.gpr[4] & 0x7u;
    const uint32_t pc = cpu.pc;

    memory.validate_range(sampler, 0x04, pc, MemoryAccess::write);
    uint32_t word0 = read_be32(memory, sampler + 0x00, pc);
    word0 &= ~(0x7u << 26);
    word0 = set_field(word0, depth_compare, 26, 3);  // DEPTH_COMPARE_FUNCTION
    write_be32(memory, sampler + 0x00, word0, pc);
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerBorderType +
// latte_registers_sq.h SQ_TEX_SAMPLER_WORD0_N. Replaces BORDER_COLOR_TYPE
// (bits 22-23) of word0, preserving all other bits. GX2TexBorderType maps 1:1
// onto SQ_TEX_BORDER_COLOR.
void init_sampler_border_type(GuestMemory& memory, CPUContext& cpu) {
    const uint32_t sampler = cpu.gpr[3];
    const uint32_t border_type = cpu.gpr[4] & 0x3u;
    const uint32_t pc = cpu.pc;

    memory.validate_range(sampler, 0x04, pc, MemoryAccess::write);
    uint32_t word0 = read_be32(memory, sampler + 0x00, pc);
    word0 &= ~(0x3u << 22);
    word0 = set_field(word0, border_type, 22, 2);  // BORDER_COLOR_TYPE
    write_be32(memory, sampler + 0x00, word0, pc);
}

Gx2State parse_init(GuestMemory& memory, uint32_t attributes, uint32_t pc) {
    Gx2State pending;
    pending.command_buffer_size = kDefaultCommandBufferSize;
    pending.app_io_stack_size = kDefaultAppIoStackSize;

    for (uint32_t cursor = attributes; cursor != 0;) {
        const auto id = memory.read32(cursor, pc);
        if (id == 0) {
            break;
        }
        const auto value = memory.read32(cursor + 4, pc);
        switch (id) {
        case 1:
            pending.command_buffer_base = value;
            break;
        case 2:
            pending.command_buffer_size = value;
            break;
        case 7:
            pending.argc = value;
            break;
        case 8:
            pending.argv = value;
            break;
        case 9:
            pending.profile_mode = value;
            break;
        case 10:
            pending.toss_stage = value;
            break;
        case 11:
            pending.app_io_stack_size = value;
            break;
        default:
            break;
        }
        cursor += 8;
    }

    if (pending.command_buffer_size < kMinimumCommandBufferSize) {
        pending.command_buffer_size = kMinimumCommandBufferSize;
    }
    const auto base = static_cast<uint64_t>(pending.command_buffer_base);
    const auto size = static_cast<uint64_t>(pending.command_buffer_size);
    const auto stack_size = static_cast<uint64_t>(pending.app_io_stack_size);
    if (pending.command_buffer_base == 0 || stack_size > size ||
        base + size > 0x100000000ull) {
        throw GuestFault("invalid GX2 command buffer pool",
                         pending.command_buffer_base,
                         pending.command_buffer_size, pc,
                         MemoryAccess::write);
    }
    memory.validate_range(pending.command_buffer_base,
                          pending.command_buffer_size, pc,
                          MemoryAccess::write);

    const auto unaligned_stack = base + size - stack_size;
    const auto stack_base = (unaligned_stack + 63u) & ~uint64_t{63u};
    const auto end = base + size;
    if (stack_base > end) {
        throw GuestFault("invalid GX2 AppIo stack",
                         pending.command_buffer_base,
                         pending.command_buffer_size, pc,
                         MemoryAccess::write);
    }
    pending.app_io_stack_base = static_cast<uint32_t>(stack_base);
    pending.app_io_stack_size = static_cast<uint32_t>(end - stack_base);
    pending.command_buffer_size =
        static_cast<uint32_t>(stack_base - base);

    pending.initialized = true;
    pending.main_core_id = 1;
    pending.events_initialized = true;
    pending.flip_callback_installed = true;
    pending.command_buffer_pool_initialized = true;
    apply_default_state(pending);
    pending.flush_count = 1;
    return pending;
}
} // namespace

Gx2Runtime::Gx2Runtime(ExecutionImage& image) : memory_(image.memory) {
    try {
        memory_.validate_range(kDisplayListReplayScratch,
                               kDisplayListReplayScratchSize, 0,
                               MemoryAccess::write);
    } catch (const GuestFault&) {
        memory_.map(kDisplayListReplayScratch, kDisplayListReplayScratchSize,
                    {true, true, false});
    }
}

bool Gx2Runtime::copy_scan_buffer(uint32_t target,
                                  std::span<uint8_t> output) const {
    const Gx2ScanBufferState* scan =
        target == 1 ? &state_.tv_scan_buffer
                    : target == 4 ? &state_.drc_scan_buffer : nullptr;
    if (scan == nullptr || !scan->valid ||
        output.size() !=
            static_cast<std::size_t>(scan->width) * scan->height * 4) {
        return false;
    }
    const std::size_t row_size = static_cast<std::size_t>(scan->width) * 4;
    for (uint32_t y = 0; y < scan->height; ++y) {
        memory_.read_bytes(
            scan->address + y * scan->pitch * 4,
            output.subspan(static_cast<std::size_t>(y) * row_size, row_size),
            0);
    }
    return true;
}

void Gx2Runtime::copy_color_buffer_to_scan(
    uint32_t descriptor, const Gx2ScanBufferState& scan, uint32_t pc) {
    if (!scan.valid) {
        throw GuestFault("GX2 scan target is not configured", descriptor,
                         kColorBufferSize, pc, MemoryAccess::read);
    }
    const uint32_t width = read_be32(memory_, descriptor + 0x04, pc);
    const uint32_t height = read_be32(memory_, descriptor + 0x08, pc);
    const uint32_t format =
        read_be32(memory_, descriptor + 0x14, pc) & 0x3Fu;
    const uint32_t aa = read_be32(memory_, descriptor + 0x18, pc);
    const uint32_t image_size = read_be32(memory_, descriptor + 0x20, pc);
    const uint32_t image = read_be32(memory_, descriptor + 0x24, pc);
    const uint32_t mode = read_be32(memory_, descriptor + 0x30, pc);
    const uint32_t swizzle = read_be32(memory_, descriptor + 0x34, pc);
    const uint32_t pitch = read_be32(memory_, descriptor + 0x3C, pc);
    if (width == 0 || height == 0 || pitch < width ||
        (format != 0x19 && format != 0x1A) || aa != 0 ||
        !supported_surface_mode(mode) || image == 0 || image_size < 4) {
        throw GuestFault("unsupported GX2 color scan-copy surface",
                         descriptor, kColorBufferSize, pc,
                         MemoryAccess::read);
    }
    memory_.validate_range(image, image_size, pc, MemoryAccess::read);
    const uint64_t scan_size =
        static_cast<uint64_t>(scan.pitch) * scan.height * 4;
    if (scan.pitch < scan.width || scan_size > scan.size) {
        throw GuestFault("invalid GX2 scan-buffer layout", scan.address,
                         scan.size, pc, MemoryAccess::write);
    }

    surface_scratch_.resize(image_size);
    memory_.read_bytes(image, surface_scratch_, pc);
    scan_scratch_.assign(static_cast<std::size_t>(scan_size), 0);
    for (uint32_t y = 0; y < scan.height; ++y) {
        const uint32_t source_y =
            static_cast<uint32_t>(static_cast<uint64_t>(height) * y /
                                  scan.height);
        for (uint32_t x = 0; x < scan.width; ++x) {
            const uint32_t source_x =
                static_cast<uint32_t>(static_cast<uint64_t>(width) * x /
                                      scan.width);
            const uint32_t source_offset = surface_pixel_offset(
                source_x, source_y, pitch, mode, swizzle, 4);
            if (source_offset > image_size - 4) {
                throw GuestFault("invalid GX2 color surface image size", image,
                                 image_size, pc, MemoryAccess::read);
            }
            const std::size_t destination =
                (static_cast<std::size_t>(y) * scan.pitch + x) * 4;
            if (format == 0x19) {
                scan_scratch_[destination] =
                    surface_scratch_[source_offset + 2];
                scan_scratch_[destination + 1] =
                    surface_scratch_[source_offset + 1];
                scan_scratch_[destination + 2] =
                    surface_scratch_[source_offset];
                scan_scratch_[destination + 3] =
                    surface_scratch_[source_offset + 3];
            } else {
                std::copy_n(surface_scratch_.data() + source_offset, 4,
                            scan_scratch_.data() + destination);
            }
        }
    }
    memory_.write_bytes(scan.address, scan_scratch_, pc);
}

Gx2DisplayListContext& Gx2Runtime::current_display_list() {
    const uint32_t core = machine_ == nullptr ? 0 : machine_->current_core_id();
    return state_.display_lists[core];
}

uint32_t Gx2Runtime::register_display_list_handler(std::string name,
                                                   HleHandler handler) {
    display_list_stack_words_.push_back(
        name == "GX2SetDepthStencilControl" ? 5
        : name == "GX2SetPolygonControl"    ? 1
                                             : 0);
    display_list_payload_bytes_.push_back(
        name == "GX2SetPixelTexture" ||
                name == "GX2SetColorBuffer" ||
                name == "GX2SetColorBufferRegs"
            ? kTextureSize
            : 0);
    display_list_handler_names_.push_back(std::move(name));
    display_list_handlers_.push_back(std::move(handler));
    return static_cast<uint32_t>(display_list_handlers_.size());
}

bool Gx2Runtime::record_display_list_command(uint32_t command,
                                             const CPUContext& cpu) {
    auto& display_list = current_display_list();
    if (!display_list.active) {
        return false;
    }
    const uint32_t stack_words =
        display_list_stack_words_[command - 1];
    const uint32_t payload = display_list_payload_bytes_[command - 1];
    const uint32_t command_size = kDisplayListCommandTokenSize;
    if (display_list.size > display_list.capacity ||
        display_list.capacity - display_list.size < command_size) {
        throw GuestFault("GX2 display list capacity exceeded",
                         display_list.address + display_list.size,
                         command_size, cpu.pc, MemoryAccess::write);
    }

    RecordedDisplayListCommand recorded;
    recorded.command = command;
    for (uint32_t index = 0; index < 8; ++index) {
        recorded.gpr[index] = cpu.gpr[3 + index];
        recorded.fpr[index] = cpu.fpr[1 + index][0];
    }
    const uint32_t stack = cpu.gpr[1] + 40;
    for (uint32_t index = 0; index < stack_words; ++index) {
        recorded.stack[index] =
            memory_.read32(stack + index * 4, cpu.pc);
    }
    uint32_t data_source = 0;
    uint32_t data_size = 0;
    const auto& name = display_list_handler_names_[command - 1];
    if (name == "GX2DrawIndexedEx") {
        const uint32_t index_size =
            cpu.gpr[5] == 0 || cpu.gpr[5] == 4 ? 2u : 4u;
        const uint64_t bytes =
            static_cast<uint64_t>(cpu.gpr[4]) * index_size;
        if (bytes > std::numeric_limits<uint32_t>::max()) {
            throw GuestFault("GX2 display list index data is too large",
                             cpu.gpr[6], cpu.gpr[4], cpu.pc,
                             MemoryAccess::read);
        }
        data_source = cpu.gpr[6];
        data_size = static_cast<uint32_t>(bytes);
    } else if (name == "GX2SetVertexUniformReg" ||
               name == "GX2SetPixelUniformReg") {
        if (cpu.gpr[4] > std::numeric_limits<uint32_t>::max() / 4) {
            throw GuestFault("GX2 display list uniform data is too large",
                             cpu.gpr[5], cpu.gpr[4], cpu.pc,
                             MemoryAccess::read);
        }
        data_source = cpu.gpr[5];
        data_size = cpu.gpr[4] * 4;
        if (name == "GX2SetVertexUniformReg" &&
            std::getenv("NWIIU_UNIFORM_TRACE") != nullptr &&
            cpu.gpr[3] < 20 && cpu.gpr[3] + cpu.gpr[4] > 12) {
            std::fprintf(stderr,
                         "VS-RECORD pc=%08X lr=%08X data=%08X "
                         "offset=%u count=%u\n",
                         cpu.pc, cpu.lr, cpu.gpr[5], cpu.gpr[3], cpu.gpr[4]);
        }
    } else if (payload != 0 && cpu.gpr[3] != 0) {
        data_source = cpu.gpr[3];
        data_size = payload;
    }
    if (data_size != 0) {
        recorded.data.resize(data_size);
        memory_.read_bytes(data_source, recorded.data, cpu.pc);
    }

    const uint32_t core =
        machine_ == nullptr ? 0 : machine_->current_core_id();
    recording_display_lists_[core].push_back(std::move(recorded));
    display_list.size += command_size;
    return true;
}

void Gx2Runtime::replay_display_list(uint32_t address, uint32_t size,
                                     uint32_t pc) {
    const auto found = recorded_display_lists_.find(address);
    if (found == recorded_display_lists_.end()) {
        return;
    }
    const auto& display_list = found->second;
    uint32_t replay_data_offset = 0;

    for (const auto& recorded : display_list.commands) {
        const uint32_t command = recorded.command;
        const uint32_t command_size = kDisplayListCommandTokenSize;
        if (size < display_list.size) {
            if (command_size > size) {
                throw GuestFault("GX2 display list ends within an HLE command",
                                 address, size, pc, MemoryAccess::read);
            }
            size -= command_size;
        }
        CPUContext replay;
        replay.pc = pc;
        for (uint32_t index = 0; index < 8; ++index) {
            replay.gpr[3 + index] = recorded.gpr[index];
            replay.fpr[1 + index][0] = recorded.fpr[index];
        }
        const uint32_t stack_words =
            display_list_stack_words_[command - 1];
        if (stack_words != 0) {
            for (uint32_t index = 0; index < stack_words; ++index) {
                memory_.write32(kDisplayListReplayScratch + index * 4,
                                recorded.stack[index], pc);
            }
            replay.gpr[1] = kDisplayListReplayScratch - 40;
        }
        if (!recorded.data.empty()) {
            replay_data_offset =
                (replay_data_offset + 15) & ~uint32_t{15};
            const uint32_t data_address =
                display_list.replay_address + replay_data_offset;
            if (recorded.data.size() >
                display_list.replay_capacity - replay_data_offset) {
                throw GuestFault("GX2 display list payload is too large",
                                 data_address, recorded.data.size(), pc,
                                 MemoryAccess::write);
            }
            memory_.write_bytes(data_address, recorded.data, pc);
            replay_data_offset += static_cast<uint32_t>(recorded.data.size());
            const auto& name = display_list_handler_names_[command - 1];
            if (name == "GX2DrawIndexedEx") {
                replay.gpr[6] = data_address;
            } else if (name == "GX2SetVertexUniformReg" ||
                       name == "GX2SetPixelUniformReg") {
                replay.gpr[5] = data_address;
            } else {
                replay.gpr[3] = data_address;
            }
        }
        try {
            const auto action =
                display_list_handlers_[command - 1](replay, memory_);
            if (action != HleAction::return_to_lr) {
                throw std::runtime_error(
                    "GX2 display list command requested control transfer");
            }
        } catch (const GuestFault& fault) {
            throw GuestFault(
                "GX2 display list " +
                    display_list_handler_names_[command - 1] + ": " +
                    fault.what(),
                fault.address, fault.width, pc, fault.access);
        }
        if (size == 0) {
            break;
        }
    }
}

void Gx2Runtime::register_handlers(CafeRuntime& runtime) {
    // AGL retires command-ring sections once the sampled GPU cycle,
    // converted through GX2GPUTimeToCPUTime (identity here), is in the
    // past. Report the current tick so sampled points always appear
    // consumed; UINT64_MAX would park every ring section forever.
    const auto sample_gpu_cycle = [this](CPUContext& cpu,
                                         GuestMemory& memory) {
        const uint64_t cycle =
            machine_ == nullptr ? 1 : machine_->current_time_ticks();
        memory.write64(cpu.gpr[3], cycle, cpu.pc);
        return HleAction::return_to_lr;
    };
    runtime.register_handler("gx2", "GX2SampleTopGPUCycle",
                             sample_gpu_cycle);
    runtime.register_handler("gx2", "GX2SampleBottomGPUCycle",
                             sample_gpu_cycle);
    runtime.register_handler(
        "gx2", "GX2GPUTimeToCPUTime",
        [](CPUContext&, GuestMemory&) { return HleAction::return_to_lr; });
    runtime.register_handler(
        "gx2", "GX2Flush", [this](CPUContext&, GuestMemory&) {
            ++state_.flush_count;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2DrawDone", [this](CPUContext& cpu, GuestMemory&) {
            ++state_.flush_count;
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2WaitForVsync", [this](CPUContext& cpu, GuestMemory&) {
            ++state_.vsync_wait_count;
            state_.last_vsync =
                machine_ == nullptr
                    ? state_.vsync_wait_count
                    : std::max<uint64_t>(1, machine_->current_time_ticks());
            if (machine_ == nullptr) {
                return HleAction::return_to_lr;
            }
            machine_->sleep_current(abi::kBusClockSpeed / 60);
            cpu.pc = cpu.lr;
            return HleAction::reschedule;
        });
    runtime.register_handler(
        "gx2", "GX2SwapScanBuffers", [this](CPUContext&, GuestMemory&) {
            ++state_.swap_count;
            ++state_.flip_count;
            state_.last_flip =
                machine_ == nullptr
                    ? state_.swap_count
                    : std::max<uint64_t>(1, machine_->current_time_ticks());
            notify(Gx2Event::swap_scan_buffers);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2GetSwapStatus",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const std::array<uint32_t, 4> outputs{
                cpu.gpr[3], cpu.gpr[4], cpu.gpr[5], cpu.gpr[6]};
            for (size_t index = 0; index < outputs.size(); ++index) {
                if (outputs[index] != 0) {
                    memory.validate_range(outputs[index], index < 2 ? 4 : 8,
                                          cpu.pc, MemoryAccess::write);
                }
            }
            if (outputs[0] != 0) {
                memory.write32(outputs[0], state_.swap_count, cpu.pc);
            }
            if (outputs[1] != 0) {
                memory.write32(outputs[1], state_.flip_count, cpu.pc);
            }
            if (outputs[2] != 0) {
                memory.write64(outputs[2], state_.last_flip, cpu.pc);
            }
            if (outputs[3] != 0) {
                memory.write64(outputs[3], state_.last_vsync, cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetContextState",
        [this](CPUContext& cpu, GuestMemory& memory) {
            if (cpu.gpr[3] != 0) {
                memory.validate_range(cpu.gpr[3], 0xA100, cpu.pc,
                                      MemoryAccess::read);
            }
            state_.context_state_address = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2Init", [this](CPUContext& cpu, GuestMemory&) {
            auto pending = parse_init(memory_, cpu.gpr[3], cpu.pc);
            state_ = pending;
            return HleAction::return_to_lr;
        });
    const auto call_display_list = [this](CPUContext& cpu, GuestMemory&) {
        memory_.validate_range(cpu.gpr[3], cpu.gpr[4], cpu.pc,
                               MemoryAccess::read);
        if (std::getenv("NWIIU_DL_TRACE") != nullptr) {
            std::fprintf(stderr, "DLCALL address=%08X size=%u\n", cpu.gpr[3],
                         cpu.gpr[4]);
        }
        replay_display_list(cpu.gpr[3], cpu.gpr[4], cpu.pc);
        return HleAction::return_to_lr;
    };
    runtime.register_handler("gx2", "GX2CallDisplayList", call_display_list);
    runtime.register_handler("gx2", "GX2DirectCallDisplayList",
                             call_display_list);
    runtime.register_handler(
        "gx2", "GX2BeginDisplayListEx",
        [this](CPUContext& cpu, GuestMemory&) {
            if (cpu.gpr[4] > 0x10000000u) {
                std::fprintf(stderr,
                             "INVALIDDL address=%08X size=%08X lr=%08X\n",
                             cpu.gpr[3], cpu.gpr[4], cpu.lr);
            }
            memory_.validate_range(cpu.gpr[3], cpu.gpr[4], cpu.pc,
                                   MemoryAccess::write);
            auto& display_list = current_display_list();
            const uint32_t core =
                machine_ == nullptr ? 0 : machine_->current_core_id();
            if (!display_list.active && machine_ != nullptr) {
                machine_->pin_current_core();
            }
            auto& commands = recording_display_lists_[core];
            if (display_list.active) {
                if (display_list.depth == display_list.stack.size()) {
                    throw GuestFault("GX2 display list nesting too deep",
                                     cpu.gpr[3], cpu.gpr[4], cpu.pc,
                                     MemoryAccess::write);
                }
                recording_display_list_stack_[core][display_list.depth] =
                    std::move(commands);
                display_list.stack[display_list.depth++] = {
                    display_list.address,
                    display_list.capacity,
                    display_list.size,
                    display_list.profiling};
            }
            commands.clear();
            display_list.active = true;
            display_list.address = cpu.gpr[3];
            display_list.capacity = cpu.gpr[4];
            display_list.size = 0;
            display_list.profiling = cpu.gpr[5] != 0;
            if (std::getenv("NWIIU_DL_TRACE") != nullptr) {
                std::fprintf(stderr, "DLBEGIN address=%08X capacity=%u\n",
                             cpu.gpr[3], cpu.gpr[4]);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2GetCurrentDisplayList",
        [this](CPUContext& cpu, GuestMemory&) {
            auto& display_list = current_display_list();
            if (!display_list.active) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            }
            const uint32_t out_address = cpu.gpr[3];
            const uint32_t out_capacity = cpu.gpr[4];
            if (out_address != 0) {
                memory_.validate_range(out_address, 4, cpu.pc,
                                       MemoryAccess::write);
            }
            if (out_capacity != 0) {
                memory_.validate_range(out_capacity, 4, cpu.pc,
                                       MemoryAccess::write);
            }
            if (out_address != 0) {
                write_be32(memory_, out_address, display_list.address, cpu.pc);
            }
            if (out_capacity != 0) {
                write_be32(memory_, out_capacity, display_list.capacity,
                           cpu.pc);
            }
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2EndDisplayList",
        [this](CPUContext& cpu, GuestMemory&) {
            auto& display_list = current_display_list();
            const uint32_t size = display_list.size;
            if (size > 0x100000u &&
                std::getenv("NWIIU_POOL_TRACE") != nullptr) {
                std::fprintf(stderr,
                             "LARGEDL address=%08X capacity=%08X size=%08X\n",
                             display_list.address, display_list.capacity,
                             size);
            }
            const uint32_t core =
                machine_ == nullptr ? 0 : machine_->current_core_id();
            auto& commands = recording_display_lists_[core];
            uint64_t payload_size = 0;
            for (const auto& command : commands) {
                if (!command.data.empty()) {
                    payload_size = (payload_size + 15) & ~uint64_t{15};
                    payload_size += command.data.size();
                }
            }
            if (payload_size > kDisplayListReplayScratchSize - 0x1000) {
                throw GuestFault("GX2 display list payload is too large",
                                 kDisplayListReplayScratch + 0x1000,
                                 payload_size, cpu.pc, MemoryAccess::write);
            }
            uint32_t replay_address = 0;
            uint32_t replay_capacity = 0;
            const auto previous =
                recorded_display_lists_.find(display_list.address);
            if (previous != recorded_display_lists_.end()) {
                replay_address = previous->second.replay_address;
                replay_capacity = previous->second.replay_capacity;
            }
            const uint32_t required = static_cast<uint32_t>(payload_size);
            if (required > replay_capacity) {
                replay_data_cursor_ =
                    (replay_data_cursor_ + 15) & ~uint32_t{15};
                if (required >
                    kDisplayListReplayScratchSize - replay_data_cursor_) {
                    throw GuestFault("GX2 display list replay arena exhausted",
                                     kDisplayListReplayScratch +
                                         replay_data_cursor_,
                                     required, cpu.pc, MemoryAccess::write);
                }
                replay_address =
                    kDisplayListReplayScratch + replay_data_cursor_;
                replay_capacity = required;
                replay_data_cursor_ += required;
            }
            recorded_display_lists_[display_list.address] = {
                size, display_list.capacity, replay_address, replay_capacity,
                std::move(commands)};
            if (std::getenv("NWIIU_DL_TRACE") != nullptr) {
                std::fprintf(stderr, "DLEND size=%u\n", size);
            }
            if (display_list.depth != 0) {
                const auto outer = display_list.stack[--display_list.depth];
                commands = std::move(
                    recording_display_list_stack_[core][display_list.depth]);
                display_list.address = outer.address;
                display_list.capacity = outer.capacity;
                display_list.size = outer.size;
                display_list.profiling = outer.profiling;
            } else {
                display_list.active = false;
                display_list.address = 0;
                display_list.capacity = 0;
                display_list.size = 0;
                display_list.profiling = false;
                commands.clear();
                if (machine_ != nullptr) {
                    machine_->unpin_current_core();
                }
            }
            cpu.gpr[3] = size;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetVertexShader",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], 0x134, cpu.pc,
                                   MemoryAccess::read);
            state_.vertex_shader_address = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPixelShader",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], 0xE8, cpu.pc,
                                   MemoryAccess::read);
            state_.pixel_shader_address = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetFetchShader",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], 0x20, cpu.pc,
                                   MemoryAccess::read);
            state_.fetch_shader_address = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPixelSampler",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t id = cpu.gpr[4];
            if (id >= state_.pixel_sampler_addresses.size()) {
                throw GuestFault("invalid GX2 pixel sampler id", id, 4,
                                 cpu.pc, MemoryAccess::read);
            }
            memory_.validate_range(cpu.gpr[3], 0x0C, cpu.pc,
                                   MemoryAccess::read);
            state_.pixel_sampler_addresses[id] = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcColorBufferAuxInfo",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], kColorBufferSize, cpu.pc,
                                   MemoryAccess::write);
            write_be32(memory_, cpu.gpr[3] + 0x84, 0x800, cpu.pc);
            if (cpu.gpr[4] != 0) {
                write_be32(memory_, cpu.gpr[4], 0x800, cpu.pc);
            }
            if (cpu.gpr[5] != 0) {
                write_be32(memory_, cpu.gpr[5], 0x800, cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcDepthBufferHiZInfo",
        [this](CPUContext& cpu, GuestMemory&) {
            calc_depth_buffer_hiz_info(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcSurfaceSizeAndAlignment",
        [this](CPUContext& cpu, GuestMemory&) {
            calc_surface_size_and_alignment(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetSurfaceSwizzle",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t address = cpu.gpr[3] + 0x34;
            memory_.validate_range(address, 4, cpu.pc, MemoryAccess::write);
            const uint32_t value =
                (read_be32(memory_, address, cpu.pc) & 0xFFFF00FF) |
                (cpu.gpr[4] << 8);
            write_be32(memory_, address, value, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CopySurface", [this](CPUContext& cpu, GuestMemory&) {
            copy_surface(memory_, cpu);
            return HleAction::return_to_lr;
        });
    for (const char* symbol :
         {"GX2ExpandAAColorBuffer", "GX2ExpandColorBuffer",
          "GX2ExpandDepthBuffer"}) {
        runtime.register_handler(
            "gx2", symbol,
            [](CPUContext&, GuestMemory&) {
                return HleAction::return_to_lr;
            });
    }
    runtime.register_handler(
        "gx2", "GX2InitDepthBufferHiZEnable",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t info = cpu.gpr[3] + 0x98;
            memory_.validate_range(info, 4, cpu.pc, MemoryAccess::write);
            uint32_t value = read_be32(memory_, info, cpu.pc);
            value = cpu.gpr[4] != 0 ? value | (1u << 25)
                                    : value & ~(1u << 25);
            write_be32(memory_, info, value, cpu.pc);
            return HleAction::return_to_lr;
        });
    const auto shader_gprs = [this](CPUContext& cpu, GuestMemory&) {
        cpu.gpr[3] = read_be32(memory_, cpu.gpr[3], cpu.pc) & 0xFFu;
        return HleAction::return_to_lr;
    };
    for (const char* symbol : std::array{
             "GX2GetPixelShaderGPRs", "GX2GetVertexShaderGPRs",
             "GX2GetGeometryShaderGPRs"}) {
        runtime.register_handler("gx2", symbol, shader_gprs);
    }
    const auto shader_stack_entries = [this](CPUContext& cpu, GuestMemory&) {
        cpu.gpr[3] =
            (read_be32(memory_, cpu.gpr[3], cpu.pc) >> 8) & 0xFFu;
        return HleAction::return_to_lr;
    };
    for (const char* symbol : std::array{
             "GX2GetPixelShaderStackEntries",
             "GX2GetVertexShaderStackEntries",
             "GX2GetGeometryShaderStackEntries"}) {
        runtime.register_handler("gx2", symbol, shader_stack_entries);
    }
    runtime.register_handler(
        "gx2", "GX2InitTextureRegs",
        [this](CPUContext& cpu, GuestMemory&) {
            init_texture_regs(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcTVSize", [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[6], 4, cpu.pc,
                                   MemoryAccess::write);
            memory_.validate_range(cpu.gpr[7], 4, cpu.pc,
                                   MemoryAccess::write);
            const auto result =
                calculate_tv_scan_buffer(cpu.gpr[3], cpu.gpr[4], cpu.gpr[5]);
            if (const auto* error = std::get_if<LatteSurfaceError>(&result)) {
                throw GuestFault(surface_error_reason("GX2CalcTVSize", *error),
                                 cpu.gpr[3], 12, cpu.pc, MemoryAccess::read);
            }
            const auto& layout = std::get<LatteScanBufferLayout>(result);
            write_be32(memory_, cpu.gpr[6], layout.size, cpu.pc);
            write_be32(memory_, cpu.gpr[7], layout.alignment, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcDRCSize", [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[6], 4, cpu.pc,
                                   MemoryAccess::write);
            memory_.validate_range(cpu.gpr[7], 4, cpu.pc,
                                   MemoryAccess::write);
            const auto result =
                calculate_drc_scan_buffer(cpu.gpr[3], cpu.gpr[4], cpu.gpr[5]);
            if (const auto* error = std::get_if<LatteSurfaceError>(&result)) {
                throw GuestFault(surface_error_reason("GX2CalcDRCSize", *error),
                                 cpu.gpr[3], 12, cpu.pc, MemoryAccess::read);
            }
            const auto& layout = std::get<LatteScanBufferLayout>(result);
            write_be32(memory_, cpu.gpr[6], layout.size, cpu.pc);
            write_be32(memory_, cpu.gpr[7], layout.alignment, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetTVBuffer", [this](CPUContext& cpu, GuestMemory&) {
            const auto result =
                calculate_tv_scan_buffer(cpu.gpr[5], cpu.gpr[6], cpu.gpr[7]);
            if (const auto* error = std::get_if<LatteSurfaceError>(&result)) {
                throw GuestFault(surface_error_reason("GX2SetTVBuffer", *error),
                                 cpu.gpr[5], 12, cpu.pc, MemoryAccess::read);
            }
            const auto& layout = std::get<LatteScanBufferLayout>(result);
            if (cpu.gpr[4] != layout.size) {
                throw GuestFault("invalid GX2 TV scan-buffer size", cpu.gpr[3],
                                 cpu.gpr[4], cpu.pc, MemoryAccess::write);
            }
            memory_.validate_range(cpu.gpr[3], cpu.gpr[4], cpu.pc,
                                   MemoryAccess::write);
            state_.tv_scan_buffer = {
                true,         cpu.gpr[3],   cpu.gpr[4],
                cpu.gpr[5],   cpu.gpr[6],   cpu.gpr[7],
                layout.width, layout.height, layout.width,
            };
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetDRCBuffer", [this](CPUContext& cpu, GuestMemory&) {
            const auto result =
                calculate_drc_scan_buffer(cpu.gpr[5], cpu.gpr[6], cpu.gpr[7]);
            if (const auto* error = std::get_if<LatteSurfaceError>(&result)) {
                throw GuestFault(surface_error_reason("GX2SetDRCBuffer", *error),
                                 cpu.gpr[5], 12, cpu.pc, MemoryAccess::read);
            }
            const auto& layout = std::get<LatteScanBufferLayout>(result);
            if (cpu.gpr[4] != layout.size) {
                throw GuestFault("invalid GX2 DRC scan-buffer size", cpu.gpr[3],
                                 cpu.gpr[4], cpu.pc, MemoryAccess::write);
            }
            memory_.validate_range(cpu.gpr[3], cpu.gpr[4], cpu.pc,
                                   MemoryAccess::write);
            state_.drc_scan_buffer = {
                true,       cpu.gpr[3], cpu.gpr[4], cpu.gpr[5], cpu.gpr[6],
                cpu.gpr[7], 854,        480,        layout.width,
            };
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetTVEnable", [this](CPUContext& cpu, GuestMemory&) {
            state_.tv_enabled = cpu.gpr[3] != 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetDRCEnable", [this](CPUContext& cpu, GuestMemory&) {
            state_.drc_enabled = cpu.gpr[3] != 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CopyColorBufferToScanBuffer",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t target = cpu.gpr[4];
            if (target != 1 && target != 4) {
                throw GuestFault("invalid GX2 scan target", target, 4, cpu.pc,
                                 MemoryAccess::read);
            }
            memory_.validate_range(cpu.gpr[3], kColorBufferSize, cpu.pc,
                                   MemoryAccess::read);
            memory_.validate_range(cpu.gpr[3], kColorBufferSize, cpu.pc,
                                   MemoryAccess::write);
            if (read_be32(memory_, cpu.gpr[3] + 0x7C, cpu.pc) == 0) {
                write_be32(memory_, cpu.gpr[3] + 0x7C, 1, cpu.pc);
            }
            copy_color_buffer_to_scan(
                cpu.gpr[3],
                target == 1 ? state_.tv_scan_buffer : state_.drc_scan_buffer,
                cpu.pc);
            state_.last_scan_copy = {true, {cpu.gpr[3], target}};
            ++state_.scan_copy_count;
            notify(Gx2Event::copy_scan_buffer);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetTVScale", [this](CPUContext& cpu, GuestMemory&) {
            state_.tv_scale = {true, {cpu.gpr[3], cpu.gpr[4]}};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetDRCScale", [this](CPUContext& cpu, GuestMemory&) {
            state_.drc_scale = {true, {cpu.gpr[3], cpu.gpr[4]}};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetSwapInterval", [this](CPUContext& cpu, GuestMemory&) {
            state_.swap_interval = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2TempGetGPUVersion",
        [](CPUContext& cpu, GuestMemory&) {
            // Decaf gx2_temp.cpp: GX2TempGetGPUVersion() returns literal 2
            // (GPU7/Latte revision). WUT gx2/temp.h: no args, no out pointer.
            cpu.gpr[3] = 2;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2Invalidate", [this](CPUContext& cpu, GuestMemory&) {
            // Decaf gx2_memory.cpp: mode 0 (NONE) returns immediately; other
            // modes align the size up to 0x100 and issue CPU cache flush /
            // GPU SurfaceSync. With a unified-memory HLE and no PM4 execution,
            // this is a coherency hint: validate the buffer range and record
            // the invalidation. No guest memory is written and no PM4 packet
            // is emitted.
            const uint32_t mode = cpu.gpr[3];
            const uint32_t buffer = cpu.gpr[4];
            const uint32_t size = cpu.gpr[5];
            if (mode == 0) {
                return HleAction::return_to_lr;
            }
            // Full-range sentinel (-1) flushes everything in Decaf; a bounded
            // request validates only the exact range the guest asked to be
            // made coherent (0x100 alignment is an internal cache detail).
            if (size != 0xFFFFFFFFu && size != 0) {
                memory_.validate_range(buffer, size, cpu.pc,
                                       MemoryAccess::read);
            }
            const uint32_t aligned = (size == 0xFFFFFFFFu)
                                         ? size
                                         : ((size + 0xFFu) & ~0xFFu);
            state_.last_invalidate = {
                true, state_.last_invalidate.count + 1, mode, buffer, size,
                aligned};
            // Let the renderer evict cached texture uploads covering this
            // range; CPU-rendered layers (fonts, 2D UI) are re-uploaded on
            // the next sample after the guest rewrites them.
            if (size != 0) {
                pending_texture_invalidates_.emplace_back(buffer, size);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcFetchShaderSizeEx",
        [](CPUContext& cpu, GuestMemory&) {
            // Pure sizing calculator: attribs=r3, type=r4, mode=r5. No guest
            // memory access. Only the NoTessellation path is reached by the
            // title; tessellated fetch shaders are unimplemented and must
            // fault loudly rather than return an unverified size.
            const uint32_t type = cpu.gpr[4];
            if (type != 0) {
                throw GuestFault("unsupported GX2 tessellated fetch-shader type",
                                 type, 4, cpu.pc, MemoryAccess::read);
            }
            cpu.gpr[3] = calc_fetch_shader_size(cpu.gpr[3]);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitFetchShaderEx",
        [this](CPUContext& cpu, GuestMemory&) {
            init_fetch_shader(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSampler",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSamplerClamping",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler_clamping(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSamplerXYFilter",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler_xy_filter(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSamplerZMFilter",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler_zm_filter(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSamplerLOD",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler_lod(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSamplerDepthCompare",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler_depth_compare(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitSamplerBorderType",
        [this](CPUContext& cpu, GuestMemory&) {
            init_sampler_border_type(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcGeometryShaderInputRingBufferSize",
        [](CPUContext& cpu, GuestMemory&) {
            // Decaf gx2_shaders.cpp: returns ringItemSize * 16384 (uint32_t).
            // Pure calculator, no guest memory access.
            cpu.gpr[3] = cpu.gpr[3] * 16384u;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2CalcGeometryShaderOutputRingBufferSize",
        [](CPUContext& cpu, GuestMemory&) {
            // Decaf gx2_shaders.cpp: returns ringItemSize * 16384 (uint32_t),
            // identical to the input ring calculator. No guest memory access.
            cpu.gpr[3] = cpu.gpr[3] * 16384u;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2GetContextStateDisplayList",
        [this](CPUContext& cpu, GuestMemory&) {
            get_context_state_display_list(memory_, cpu);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetDepthStencilControl",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<13>(cpu, memory_);
            state_.depth_stencil = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetStencilMask",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<6>(cpu, memory_);
            state_.stencil_mask = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPolygonControl",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<9>(cpu, memory_);
            state_.polygon_control = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPolygonOffsetReg",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], 5 * sizeof(uint32_t), cpu.pc,
                                   MemoryAccess::read);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetShaderModeEx",
        [this](CPUContext& cpu, GuestMemory&) {
            state_.shader_mode = {true, integer_arguments<7>(cpu, memory_)};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetVertexUniformReg",
        [this](CPUContext& cpu, GuestMemory&) {
            set_uniform_registers(memory_, cpu, state_.vertex_uniform_registers,
                                  state_.vertex_uniforms_valid);
            if (std::getenv("NWIIU_UNIFORM_TRACE") != nullptr &&
                cpu.gpr[3] < 20 && cpu.gpr[3] + cpu.gpr[4] > 12) {
                std::fprintf(
                    stderr,
                    "VS-UNIFORM draw#%llu lr=%08X data=%08X offset=%u count=%u "
                    "c3=%08X,%08X,%08X,%08X c4=%08X,%08X,%08X,%08X\n",
                    static_cast<unsigned long long>(state_.draw_count),
                    cpu.lr, cpu.gpr[5], cpu.gpr[3], cpu.gpr[4],
                    state_.vertex_uniform_registers[12],
                    state_.vertex_uniform_registers[13],
                    state_.vertex_uniform_registers[14],
                    state_.vertex_uniform_registers[15],
                    state_.vertex_uniform_registers[16],
                    state_.vertex_uniform_registers[17],
                    state_.vertex_uniform_registers[18],
                    state_.vertex_uniform_registers[19]);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPixelUniformReg",
        [this](CPUContext& cpu, GuestMemory&) {
            set_uniform_registers(memory_, cpu, state_.pixel_uniform_registers,
                                  state_.pixel_uniforms_valid);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetVertexUniformBlock",
        [this](CPUContext& cpu, GuestMemory&) {
            bind_uniform_block(memory_, cpu, state_.vertex_uniform_blocks);
            if (std::getenv("NWIIU_UNIFORM_TRACE") != nullptr) {
                std::fprintf(stderr,
                             "VS-BLOCK draw#%llu index=%u size=%u address=%08X",
                             static_cast<unsigned long long>(state_.draw_count),
                             cpu.gpr[3], cpu.gpr[4], cpu.gpr[5]);
                const uint32_t count = std::min<uint32_t>(cpu.gpr[4] / 4, 24);
                for (uint32_t index = 0; index < count; ++index) {
                    std::fprintf(stderr, " %08X",
                                 read_be32(memory_, cpu.gpr[5] + index * 4,
                                           cpu.pc));
                }
                std::fputc('\n', stderr);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPixelUniformBlock",
        [this](CPUContext& cpu, GuestMemory&) {
            bind_uniform_block(memory_, cpu, state_.pixel_uniform_blocks);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetGeometryUniformBlock",
        [](CPUContext&, GuestMemory&) {
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetPixelTexture",
        [this](CPUContext& cpu, GuestMemory&) {
            bind_texture(memory_, cpu, state_.pixel_texture_addresses);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetAttribBuffer",
        [this](CPUContext& cpu, GuestMemory&) {
            bind_attrib_buffer(memory_, cpu, state_.attribute_buffers);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2DrawEx",
        [this](CPUContext& cpu, GuestMemory&) {
            state_.last_draw = {true, false, cpu.gpr[3], cpu.gpr[4], 0, 0,
                                cpu.gpr[5], cpu.gpr[6]};
            ++state_.draw_count;
            notify(Gx2Event::draw);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2DrawIndexedEx",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t index_type = cpu.gpr[5];
            uint32_t index_size = 0;
            if (index_type == 0 || index_type == 4) {
                index_size = 2;
            } else if (index_type == 1 || index_type == 9) {
                index_size = 4;
            } else {
                throw GuestFault("invalid GX2 index type", index_type, 4,
                                 cpu.pc, MemoryAccess::read);
            }
            const uint32_t count = cpu.gpr[4];
            if (count > std::numeric_limits<uint32_t>::max() / index_size) {
                throw GuestFault("invalid GX2 index range", cpu.gpr[6],
                                 count, cpu.pc, MemoryAccess::read);
            }
            memory_.validate_range(cpu.gpr[6], count * index_size, cpu.pc,
                                   MemoryAccess::read);
            state_.last_draw = {true, true, cpu.gpr[3], count, index_type,
                                cpu.gpr[6], cpu.gpr[7], cpu.gpr[8]};
            ++state_.draw_count;
            notify(Gx2Event::draw);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetColorControl",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<4>(cpu, memory_);
            state_.color_control = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetBlendControl",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<8>(cpu, memory_);
            if (args[0] >= state_.blend_controls.size()) {
                throw GuestFault("invalid GX2 render target", args[0], 4,
                                 cpu.pc, MemoryAccess::read);
            }
            state_.blend_controls[args[0]] = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitDepthBufferRegs",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t address = cpu.gpr[3];
            memory_.validate_range(address, kDepthBufferSize, cpu.pc,
                                   MemoryAccess::write);
            const LatteSurfaceDescriptor surface{
                read_be32(memory_, address + 0x00, cpu.pc),
                read_be32(memory_, address + 0x04, cpu.pc),
                read_be32(memory_, address + 0x08, cpu.pc),
                read_be32(memory_, address + 0x0C, cpu.pc),
                read_be32(memory_, address + 0x10, cpu.pc),
                read_be32(memory_, address + 0x14, cpu.pc),
                read_be32(memory_, address + 0x18, cpu.pc),
                read_be32(memory_, address + 0x1C, cpu.pc),
                read_be32(memory_, address + 0x30, cpu.pc),
                read_be32(memory_, address + 0x34, cpu.pc)};
            const uint32_t view_mip =
                read_be32(memory_, address + 0x74, cpu.pc);
            const uint32_t first_slice =
                read_be32(memory_, address + 0x78, cpu.pc);
            const uint32_t num_slices =
                read_be32(memory_, address + 0x7C, cpu.pc);
            const uint32_t hiz =
                read_be32(memory_, address + 0x80, cpu.pc);
            uint32_t depth_format = 0;
            uint32_t bytes_per_element = 0;
            uint32_t polygon_offset = 0;
            switch (surface.format) {
            case 0x005:
                depth_format = 1;
                bytes_per_element = 2;
                polygon_offset = 0xF0;
                break;
            case 0x011:
                depth_format = 3;
                bytes_per_element = 4;
                polygon_offset = 0xE8;
                break;
            case 0x80E:
                depth_format = 6;
                bytes_per_element = 4;
                polygon_offset = 0x1E9;
                break;
            case 0x811:
                depth_format = 5;
                bytes_per_element = 4;
                polygon_offset = 0x1EC;
                break;
            case 0x81C:
                depth_format = 7;
                bytes_per_element = 8;
                polygon_offset = 0x1E9;
                break;
            }
            uint32_t slices = 1;
            switch (surface.dim) {
            case 0:
            case 1: break;
            case 2:
            case 4:
            case 5: slices = std::max(1u, surface.depth); break;
            case 3: slices = std::max(6u, surface.depth); break;
            default: slices = 0; break;
            }
            if (depth_format == 0 || surface.aa != 0 || view_mip != 0 ||
                num_slices == 0 || slices == 0) {
                throw GuestFault("unsupported GX2 depth buffer layout",
                                 address, kDepthBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const auto result = calculate_surface_size_and_alignment(surface);
            const auto* layout = std::get_if<LatteSurfaceLayout>(&result);
            if (layout == nullptr || layout->pitch < 8 ||
                (layout->pitch & 7u) != 0) {
                throw GuestFault("unsupported GX2 depth buffer layout",
                                 address, kDepthBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint64_t height_denominator =
                static_cast<uint64_t>(layout->pitch) * slices *
                bytes_per_element;
            if (height_denominator == 0 ||
                layout->image_size % height_denominator != 0) {
                throw GuestFault("unsupported GX2 depth buffer layout",
                                 address, kDepthBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint32_t pitch_tiles = layout->pitch / 8u;
            const uint64_t aligned_height =
                layout->image_size / height_denominator;
            const uint64_t slice_tiles =
                static_cast<uint64_t>(layout->pitch) * aligned_height / 64u;
            const uint64_t last_slice =
                static_cast<uint64_t>(first_slice) + num_slices - 1u;
            if (pitch_tiles > 0x400 || slice_tiles == 0 ||
                slice_tiles > 0x100000 || last_slice > 0x7FF) {
                throw GuestFault("GX2 depth buffer register overflow",
                                 address, kDepthBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint32_t size = (pitch_tiles - 1u) |
                                  (static_cast<uint32_t>(slice_tiles - 1u)
                                   << 10);
            const uint32_t view =
                first_slice | (static_cast<uint32_t>(last_slice) << 13);
            const uint32_t info =
                depth_format | (1u << 3) |
                ((layout->tile_mode & 0xFu) << 15) |
                ((hiz != 0) << 25);
            const uint32_t prefetch =
                ((surface.height / 8u) - 1u) & 0x3FFu;
            const uint32_t preload =
                ((surface.width / 32u) & 0xFFu) << 16 |
                ((surface.height / 32u) & 0xFFu) << 24;
            write_be32(memory_, address + 0x90, size, cpu.pc);
            write_be32(memory_, address + 0x94, view, cpu.pc);
            write_be32(memory_, address + 0x98, info, cpu.pc);
            write_be32(memory_, address + 0x9C, 0xB, cpu.pc);
            write_be32(memory_, address + 0xA0, prefetch, cpu.pc);
            write_be32(memory_, address + 0xA4, preload, cpu.pc);
            write_be32(memory_, address + 0xA8, polygon_offset, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetDepthBuffer",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], kDepthBufferSize, cpu.pc,
                                   MemoryAccess::read);
            state_.depth_buffer_address = cpu.gpr[3];
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2ClearColor",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], kColorBufferSize, cpu.pc,
                                   MemoryAccess::read);
            const auto color = float_arguments<4>(cpu);
            clear_color_surface(memory_, cpu.gpr[3], color, cpu.pc);
            state_.last_color_clear =
                {true, {cpu.gpr[3], color[0], color[1], color[2], color[3]}};
            ++state_.color_clear_count;
            notify(Gx2Event::clear_color);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2ClearBuffersEx",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], kColorBufferSize, cpu.pc,
                                   MemoryAccess::read);
            memory_.validate_range(cpu.gpr[4], kDepthBufferSize, cpu.pc,
                                   MemoryAccess::read);
            const auto values = float_arguments<5>(cpu);
            clear_color_surface(
                memory_, cpu.gpr[3],
                {values[0], values[1], values[2], values[3]}, cpu.pc);
            state_.last_color_clear =
                {true,
                 {cpu.gpr[3], values[0], values[1], values[2], values[3]}};
            ++state_.color_clear_count;
            notify(Gx2Event::clear_color);
            state_.last_depth_stencil_clear =
                {true, {cpu.gpr[4], values[4], cpu.gpr[5] & 0xFFu,
                        cpu.gpr[6]}};
            ++state_.depth_stencil_clear_count;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2ClearDepthStencilEx",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.validate_range(cpu.gpr[3], kDepthBufferSize, cpu.pc,
                                   MemoryAccess::read);
            state_.last_depth_stencil_clear =
                {true, {cpu.gpr[3], float_arguments<1>(cpu)[0],
                        cpu.gpr[4] & 0xFFu, cpu.gpr[5]}};
            ++state_.depth_stencil_clear_count;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetClearDepthStencil",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t address = cpu.gpr[3];
            memory_.validate_range(address, kDepthBufferSize, cpu.pc,
                                   MemoryAccess::write);
            write_be32(memory_, address + 0x88,
                       float_arguments<1>(cpu)[0], cpu.pc);
            write_be32(memory_, address + 0x8C, cpu.gpr[4] & 0xFFu, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2InitColorBufferRegs",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t address = cpu.gpr[3];
            memory_.validate_range(address, kColorBufferSize, cpu.pc,
                                   MemoryAccess::write);
            const LatteSurfaceDescriptor surface{
                read_be32(memory_, address + 0x00, cpu.pc),
                read_be32(memory_, address + 0x04, cpu.pc),
                read_be32(memory_, address + 0x08, cpu.pc),
                read_be32(memory_, address + 0x0C, cpu.pc),
                read_be32(memory_, address + 0x10, cpu.pc),
                read_be32(memory_, address + 0x14, cpu.pc),
                read_be32(memory_, address + 0x18, cpu.pc),
                read_be32(memory_, address + 0x1C, cpu.pc),
                read_be32(memory_, address + 0x30, cpu.pc),
                read_be32(memory_, address + 0x34, cpu.pc)};
            const uint32_t view_mip =
                read_be32(memory_, address + 0x74, cpu.pc);
            const uint32_t first_slice =
                read_be32(memory_, address + 0x78, cpu.pc);
            const uint32_t num_slices =
                read_be32(memory_, address + 0x7C, cpu.pc);
            auto format = color_format(surface.format);
            uint32_t number_type = 0;
            switch (surface.format >> 8) {
            case 0: number_type = 0; break;
            case 1: number_type = 4; break;
            case 2: number_type = 1; break;
            case 3: number_type = 5; break;
            case 4: number_type = 6; break;
            case 8: number_type = 7; break;
            default: format.reset(); break;
            }
            uint32_t slices = 1;
            switch (surface.dim) {
            case 0:
            case 1: break;
            case 2:
            case 4:
            case 5: slices = std::max(1u, surface.depth); break;
            case 3: slices = std::max(6u, surface.depth); break;
            default: slices = 0; break;
            }
            if (!format || surface.aa != 0 ||
                view_mip >= std::max(surface.mip_levels, 1u) ||
                num_slices == 0 || slices == 0) {
                throw GuestFault("unsupported GX2 color buffer layout",
                                 address, kColorBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            auto level_surface = surface;
            if (view_mip != 0) {
                level_surface.width =
                    std::max(1u, surface.width >> view_mip);
                if (surface.dim != 0 && surface.dim != 4) {
                    level_surface.height =
                        std::max(1u, surface.height >> view_mip);
                }
                if (surface.dim == 2) {
                    level_surface.depth =
                        std::max(1u, surface.depth >> view_mip);
                }
                level_surface.mip_levels = 1;
                if (surface.tile_mode >= 4 && surface.tile_mode != 16) {
                    level_surface.tile_mode = 0;
                }
            }
            const auto result =
                calculate_surface_size_and_alignment(level_surface);
            const auto* layout = std::get_if<LatteSurfaceLayout>(&result);
            if (layout == nullptr || layout->pitch < 8 ||
                (layout->pitch & 7u) != 0) {
                throw GuestFault("unsupported GX2 color buffer layout",
                                 address, kColorBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint64_t height_denominator =
                static_cast<uint64_t>(layout->pitch) * slices *
                format->bytes_per_element;
            if (height_denominator == 0 ||
                layout->image_size % height_denominator != 0) {
                throw GuestFault("unsupported GX2 color buffer layout",
                                 address, kColorBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint32_t pitch_tiles = layout->pitch / 8u;
            const uint64_t aligned_height =
                layout->image_size / height_denominator;
            const uint64_t slice_tiles =
                static_cast<uint64_t>(layout->pitch) * aligned_height / 64u;
            const uint64_t last_slice =
                static_cast<uint64_t>(first_slice) + num_slices - 1u;
            if (pitch_tiles > 0x400 || slice_tiles == 0 ||
                slice_tiles > 0x100000 || last_slice > 0x7FF) {
                throw GuestFault("GX2 color buffer register overflow",
                                 address, kColorBufferSize, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint32_t size = (pitch_tiles - 1u) |
                                  (static_cast<uint32_t>(slice_tiles - 1u)
                                   << 10);
            const uint32_t component_swap =
                format->register_format == 12 ||
                        format->register_format == 27
                    ? 2
                    : 0;
            const uint32_t format_type = surface.format >> 8;
            const uint32_t blend_bypass =
                format_type == 1 || surface.format == 0x11 ||
                        surface.format == 0x811 ||
                        surface.format == 0x81C
                    ? 1
                    : 0;
            const uint32_t source_format =
                format_type == 1 ? 1 : format->source_format;
            const uint32_t info =
                (format->register_format << 2) |
                ((layout->tile_mode & 0xFu) << 8) |
                (number_type << 12) | (component_swap << 16) |
                (blend_bypass << 22) | ((format_type == 8) << 25) |
                (source_format << 27);
            const uint32_t view =
                layout->tile_mode == 16
                    ? 0
                    : first_slice |
                          (static_cast<uint32_t>(last_slice) << 13);
            write_be32(memory_, address + 0x88, size, cpu.pc);
            write_be32(memory_, address + 0x8C, info, cpu.pc);
            write_be32(memory_, address + 0x90, view, cpu.pc);
            write_be32(memory_, address + 0x94, 0, cpu.pc);
            return HleAction::return_to_lr;
        });
    const auto set_color_buffer =
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t address = cpu.gpr[3];
            const uint32_t target = cpu.gpr[4];
            if (target >= state_.color_buffers.size()) {
                throw GuestFault("invalid GX2 render target", target, 4,
                                 cpu.pc, MemoryAccess::read);
            }
            memory_.validate_range(address, kColorBufferSize, cpu.pc,
                                   MemoryAccess::read);
            state_.color_buffers[target] = {true, {address, target}};
            return HleAction::return_to_lr;
        };
    runtime.register_handler(
        "gx2", "GX2SetColorBufferRegs", set_color_buffer);
    runtime.register_handler(
        "gx2", "GX2SetColorBuffer", set_color_buffer);
    runtime.register_handler(
        "gx2", "GX2SetBlendConstantColor",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = float_arguments<4>(cpu);
            state_.blend_constant = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetViewport",
        [this](CPUContext& cpu, GuestMemory&) {
            state_.viewport = {true, float_arguments<6>(cpu)};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetScissor",
        [this](CPUContext& cpu, GuestMemory&) {
            state_.scissor = {true, integer_arguments<4>(cpu, memory_)};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetAlphaTest",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto reference = float_arguments<1>(cpu);
            state_.alpha_test =
                {true, {cpu.gpr[3], cpu.gpr[4], reference[0]}};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetTargetChannelMasks",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<8>(cpu, memory_);
            state_.target_channel_masks = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetAlphaToMask",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto args = integer_arguments<2>(cpu, memory_);
            state_.alpha_to_mask = {true, args};
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "gx2", "GX2SetupContextStateEx",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto address = cpu.gpr[3];
            const auto flags = cpu.gpr[4];
            memory_.validate_range(address, kContextStateSize, cpu.pc,
                                   MemoryAccess::write);

            auto pending = state_;
            apply_default_state(pending);
            pending.context_setup = {true, {address, flags}};

            clear_range(memory_, address, kContextStateSize, cpu.pc);
            write_be32(memory_, address + kContextProfilingOffset,
                       (flags & 1u) != 0, cpu.pc);
            if ((flags & 2u) == 0) {
                write_be16(memory_, address + kContextDisplayListOffset,
                           static_cast<uint16_t>(Gx2Opcode::load_context),
                           cpu.pc);
                write_be16(memory_, address + kContextDisplayListOffset + 2, 1,
                           cpu.pc);
                write_be32(memory_, address + kContextDisplayListOffset + 4,
                           address, cpu.pc);
                write_be32(memory_, address + kContextDisplayListSizeOffset, 8,
                           cpu.pc);
            }
            state_ = pending;
            return HleAction::return_to_lr;
        });
    for (const char* symbol :
         {"GX2SetAlphaTestReg", "GX2SetBlendConstantColorReg",
          "GX2SetBlendControlReg", "GX2SetColorControlReg",
          "GX2SetDepthStencilControlReg", "GX2SetGeometrySampler",
          "GX2SetGeometrySamplerBorderColor", "GX2SetGeometryShader",
          "GX2SetGeometryShaderInputRingBuffer",
          "GX2SetGeometryShaderOutputRingBuffer", "GX2SetGeometryTexture",
          "GX2SetLineWidth", "GX2SetPixelSamplerBorderColor",
          "GX2SetPointSize", "GX2SetPolygonControlReg",
          "GX2SetRasterizerClipControl", "GX2SetVertexSampler",
          "GX2SetVertexSamplerBorderColor", "GX2SetVertexTexture"}) {
        runtime.register_handler(
            "gx2", symbol,
            [](CPUContext&, GuestMemory&) {
                return HleAction::return_to_lr;
            });
    }
}
} // namespace nwii::runtime
