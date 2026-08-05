#pragma once

// Latte (R700) shader translation: reached WWHD title-screen instruction
// subset -> GLSL 4.50 -> SPIR-V (shaderc). Unsupported reached instructions
// throw with stage, instruction offset, and raw words.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nwii::runtime {

struct LatteVertexInput {
    uint32_t semantic{};      // fetch-shader SEMANTIC_ID (GLSL in location)
    uint32_t buffer{};        // GX2 attribute buffer slot
    uint32_t offset{};        // byte offset within the vertex
    uint32_t data_format{};   // SQ_DATA_FORMAT
    uint32_t num_format{};    // SQ_NUM_FORMAT (norm/int/scaled)
    bool is_signed{};         // FORMAT_COMP_SIGNED
    std::array<uint32_t, 4> dst_sel{};  // SQ_SEL destination swizzle
    uint32_t endian{};        // SQ_ENDIAN swap applied by the fetch
};

struct LatteShaderInput {
    // Register arrays are host-endian words decoded from the big-endian
    // GX2VertexShader/GX2PixelShader `regs` blobs.
    std::span<const uint32_t> vs_regs;
    std::span<const uint8_t> vs_program;
    std::span<const uint32_t> ps_regs;
    std::span<const uint8_t> ps_program;
    std::span<const uint8_t> fetch_program;
};

struct LatteUniformBlockRef {
    uint32_t block{};
    uint32_t vector{};

    bool operator==(const LatteUniformBlockRef&) const = default;
};

struct LatteTranslation {
    std::vector<uint32_t> vertex_spirv;
    std::vector<uint32_t> fragment_spirv;
    std::vector<LatteVertexInput> vertex_inputs;
    std::vector<uint32_t> ps_sampler_slots;  // reached PS texture resource ids
    std::vector<LatteUniformBlockRef> vertex_uniform_blocks;
    std::vector<LatteUniformBlockRef> fragment_uniform_blocks;
    std::string vertex_glsl;    // diagnostics / tests
    std::string fragment_glsl;  // diagnostics / tests
};

uint32_t decode_latte_vertex_word(uint32_t word, uint32_t endian);

// SDL_GPU SPIR-V binding conventions used by the emitted GLSL:
//   VS uniforms:  layout(std140, set=1, binding=0) uniform VsRegs {vec4 c[256];}
//   PS uniforms:  layout(std140, set=3, binding=0) uniform PsRegs {vec4 c[256];}
//   PS samplers:  layout(set=2, binding=<index into ps_sampler_slots>)
LatteTranslation translate_latte(const LatteShaderInput& input);

}  // namespace nwii::runtime
