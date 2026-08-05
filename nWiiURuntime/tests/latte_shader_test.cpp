#include <array>
#include "runtime/latte_shader.h"
#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {
using nwii::runtime::LatteUniformBlockRef;
using nwii::runtime::LatteShaderInput;
using nwii::runtime::translate_latte;

void put_word(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset + 0] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

uint32_t cf_normal(uint32_t address, uint32_t opcode, uint32_t count,
                   bool end = false) {
    return (opcode << 23) | ((count - 1) << 10) |
           (static_cast<uint32_t>(end) << 21) | 0x80000000u;
}

uint32_t cf_export(uint32_t gpr, uint32_t type, uint32_t base) {
    return base | (type << 13) | (gpr << 15);
}

uint32_t cf_export_word(bool end) {
    return (0x28u << 23) | (1u << 28) |
           (static_cast<uint32_t>(end) << 21) | 0x80000000u |
           (0u << 0) | (1u << 3) | (2u << 6) | (3u << 9);
}

uint32_t alu_mov_word0(uint32_t source, uint32_t channel, bool last) {
    return source | (channel << 10) | (static_cast<uint32_t>(last) << 31);
}

uint32_t alu_mov_word1(uint32_t destination, uint32_t channel) {
    return (0x19u << 7) | (1u << 4) | (destination << 21) |
           (channel << 29);
}

uint32_t alu_dot_word0(uint32_t channel, bool last) {
    return 1u | (channel << 10) | (256u << 13) | (channel << 23) |
           (static_cast<uint32_t>(last) << 31);
}

uint32_t alu_dot_word1(uint32_t channel, bool write) {
    return (0x50u << 7) | (static_cast<uint32_t>(write) << 4) |
           (channel << 29);
}

std::vector<uint8_t> dot_vertex_program() {
    std::vector<uint8_t> bytes(112);
    put_word(bytes, 0, 3);
    put_word(bytes, 4, (8u << 26) | ((11u - 1) << 18) | 0x80000000u);
    put_word(bytes, 8, cf_export(0, 1, 60));
    put_word(bytes, 12, cf_export_word(true));

    put_word(bytes, 24, alu_mov_word0(253, 0, true));
    put_word(bytes, 28, alu_mov_word1(1, 0));
    put_word(bytes, 32, 0x3F800000u);
    for (uint32_t channel = 0; channel < 4; ++channel) {
        put_word(bytes, 40 + channel * 8,
                 alu_dot_word0(channel, channel == 3));
        put_word(bytes, 44 + channel * 8,
                 alu_dot_word1(channel, channel == 0));
    }
    for (uint32_t channel = 0; channel < 4; ++channel) {
        put_word(bytes, 72 + channel * 8,
                 alu_dot_word0(channel, false));
        put_word(bytes, 76 + channel * 8,
                 alu_dot_word1(channel, channel == 1));
    }
    put_word(bytes, 104, alu_mov_word0(1, 0, true));
    put_word(bytes, 108, alu_mov_word1(0, 0));
    return bytes;
}

std::vector<uint8_t> vertex_program() {
    std::vector<uint8_t> bytes(48);
    put_word(bytes, 0, 0);
    put_word(bytes, 4, cf_normal(0, 0x13, 1));
    put_word(bytes, 8, 4);
    put_word(bytes, 12, (8u << 26) | ((2u - 1) << 18) | 0x80000000u);
    put_word(bytes, 16, cf_export(0, 1, 60));
    put_word(bytes, 20, cf_export_word(false));
    put_word(bytes, 24, cf_export(1, 2, 0));
    put_word(bytes, 28, cf_export_word(true));
    put_word(bytes, 32, alu_mov_word0(1, 0, false));
    put_word(bytes, 36, alu_mov_word1(0, 0));
    put_word(bytes, 40, alu_mov_word0(1, 1, true));
    put_word(bytes, 44, alu_mov_word1(0, 1));
    return bytes;
}

std::vector<uint8_t> nop_vertex_program() {
    std::vector<uint8_t> bytes(32);
    put_word(bytes, 0, 3);
    put_word(bytes, 4, (8u << 26) | 0x80000000u);
    put_word(bytes, 8, cf_export(0, 1, 60));
    put_word(bytes, 12, cf_export_word(false));
    put_word(bytes, 16, 0);
    put_word(bytes, 20, cf_normal(0, 0, 1, true));
    put_word(bytes, 24, 0x80000000u);
    put_word(bytes, 28, 0x00000D00u);
    return bytes;
}

std::vector<uint8_t> arithmetic_vertex_program() {
    std::vector<uint8_t> bytes(64);
    put_word(bytes, 0, 4);
    put_word(bytes, 4,
             (8u << 26) | ((4u - 1) << 18) | 0x80000000u);
    put_word(bytes, 8, cf_export(0, 1, 60));
    put_word(bytes, 12, cf_export_word(true));

    auto op2_word0 = [](uint32_t channel, bool last) {
        return 1u | (channel << 10) | (256u << 13) |
               (channel << 23) | (static_cast<uint32_t>(last) << 31);
    };
    auto op2_word1 = [](uint32_t opcode, uint32_t channel) {
        return (opcode << 7) | (1u << 4) | (channel << 29);
    };
    put_word(bytes, 32, op2_word0(0, false));
    put_word(bytes, 36, op2_word1(0x00, 0));
    put_word(bytes, 40, op2_word0(1, false));
    put_word(bytes, 44, op2_word1(0x01, 1));
    put_word(bytes, 48, op2_word0(2, false));
    put_word(bytes, 52, op2_word1(0x09, 2));
    put_word(bytes, 56, op2_word0(3, true));
    put_word(bytes, 60,
             249u | (0x10u << 13) | (1u << 4) | (3u << 29));
    return bytes;
}
std::vector<uint8_t> integer_vertex_program() {
    std::vector<uint8_t> bytes(56);
    put_word(bytes, 0, 4);
    put_word(bytes, 4,
             (8u << 26) | ((3u - 1) << 18) | 0x80000000u);
    put_word(bytes, 8, cf_export(0, 1, 60));
    put_word(bytes, 12, cf_export_word(true));

    auto op_word0 = [](uint32_t channel, bool last) {
        return 1u | (channel << 10) | (256u << 13) |
               (channel << 23) | (static_cast<uint32_t>(last) << 31);
    };
    put_word(bytes, 32, op_word0(0, false));
    put_word(bytes, 36, (0x34u << 7) | (1u << 4));
    put_word(bytes, 40, op_word0(1, false));
    put_word(bytes, 44, (0x3Au << 7) | (1u << 4) | (1u << 29));
    put_word(bytes, 48, op_word0(2, true));
    put_word(bytes, 52,
             1u | (2u << 10) | (0x1Cu << 13) | (2u << 29));
    return bytes;
}
std::vector<uint8_t> dependent_group_vertex_program() {
    std::vector<uint8_t> bytes(32);
    put_word(bytes, 0, 2);
    put_word(bytes, 4, (8u << 26) | ((2u - 1) << 18) | 0x80000000u);
    put_word(bytes, 8, cf_export(2, 1, 60));
    put_word(bytes, 12, cf_export_word(true));
    put_word(bytes, 16, alu_mov_word0(249, 0, false));
    put_word(bytes, 20, alu_mov_word1(1, 0));
    put_word(bytes, 24, alu_mov_word0(1, 0, true));
    put_word(bytes, 28, alu_mov_word1(2, 1));
    return bytes;
}

std::vector<uint8_t> conversion_vertex_program() {
    std::vector<uint8_t> bytes(24);
    put_word(bytes, 0, 2);
    put_word(bytes, 4, (8u << 26) | 0x80000000u);
    put_word(bytes, 8, cf_export(1, 1, 60));
    put_word(bytes, 12, cf_export_word(true));
    put_word(bytes, 16, alu_mov_word0(1, 0, true));
    put_word(bytes, 20, (0x6Cu << 7) | (1u << 4));
    return bytes;
}
std::vector<uint8_t> uniform_block_vertex_program() {
    std::vector<uint8_t> bytes(24);
    put_word(bytes, 0, 2 | (3u << 22));
    put_word(bytes, 4, (8u << 26) | (2u << 2) | 0x80000000u);
    put_word(bytes, 8, cf_export(1, 1, 60));
    put_word(bytes, 12, cf_export_word(true));
    put_word(bytes, 16, alu_mov_word0(128, 0, true));
    put_word(bytes, 20, alu_mov_word1(1, 0));
    return bytes;
}


std::vector<uint8_t> control_flow_vertex_program() {
    std::vector<uint8_t> bytes(40);
    put_word(bytes, 0, 4);
    put_word(bytes, 4, (9u << 26) | 0x80000000u);
    put_word(bytes, 8, 0);
    put_word(bytes, 12, cf_normal(0, 0x0D, 1));
    put_word(bytes, 16, 0);
    put_word(bytes, 20, cf_normal(0, 0x0E, 1) | 1u);
    put_word(bytes, 24, cf_export(0, 1, 60));
    put_word(bytes, 28, cf_export_word(true));
    put_word(bytes, 32, 248u | (1u << 13) | 0x80000000u);
    put_word(bytes, 36, (0x45u << 7) | (1u << 2) | (1u << 3));
    return bytes;
}




std::vector<uint8_t> pixel_program() {
    std::vector<uint8_t> bytes(32);
    put_word(bytes, 0, 2);
    put_word(bytes, 4, cf_normal(2, 1, 1));
    put_word(bytes, 8, cf_export(0, 0, 0));
    put_word(bytes, 12, cf_export_word(true));
    put_word(bytes, 16, 0x10u);
    put_word(bytes, 20, 0xF00D1000u);
    put_word(bytes, 24, 0x10800000u);
    return bytes;
}

std::vector<uint8_t> fetch4_pixel_program() {
    auto bytes = pixel_program();
    put_word(bytes, 16, 0x0Fu);
    return bytes;
}

std::vector<uint8_t> fetch_program() {
    std::vector<uint8_t> bytes(32);
    put_word(bytes, 0, 2);
    put_word(bytes, 4, cf_normal(2, 3, 1));
    put_word(bytes, 8, 0);
    put_word(bytes, 12, cf_normal(0, 0x14, 1));
    put_word(bytes, 16, 0x1C00A001u);
    put_word(bytes, 20, 0x27961000u);
    put_word(bytes, 24, 0x000A0000u);
    return bytes;
}

void test_reached_mov_sample_pipeline_translates_to_spirv() {
    const auto vs = vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});

    test::require(result.vertex_inputs.size() == 1 &&
                      result.vertex_inputs[0].semantic == 0 &&
                      result.vertex_inputs[0].buffer == 0 &&
                      result.vertex_inputs[0].data_format == 30,
                  "fetch semantic becomes one float2 vertex input");
    test::require(result.ps_sampler_slots == std::vector<uint32_t>{0},
                  "sample records pixel texture slot zero");
    test::require(result.vertex_glsl.find("layout(location=0) in vec2 a0") !=
                          std::string::npos &&
                      result.vertex_glsl.find(
                          "R[0] = vec4(intBitsToFloat(gl_VertexIndex)") !=
                          std::string::npos &&
                      result.vertex_glsl.find("gl_Position") !=
                          std::string::npos,
                  "vertex GLSL exposes fetched input and position export");
    test::require(result.fragment_glsl.find("sampler2D tex0") !=
                          std::string::npos &&
                      result.fragment_glsl.find("texture(tex0") !=
                          std::string::npos,
                  "pixel GLSL samples the reached texture binding");
    test::require(!result.vertex_spirv.empty() &&
                      result.vertex_spirv.front() == 0x07230203u &&
                      !result.fragment_spirv.empty() &&
                      result.fragment_spirv.front() == 0x07230203u,
                  "shaderc emits valid SPIR-V modules");
}

void test_reached_literal_and_dot4_forms_translate() {
    const auto vs = dot_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});

    test::require(
        result.vertex_glsl.find("uintBitsToFloat(1065353216u)") !=
                std::string::npos &&
            result.vertex_glsl.find("float dotResult1") != std::string::npos &&
            result.vertex_glsl.find("float dotResult2") != std::string::npos &&
            result.vertex_glsl.find("R[0].y = dotResult2") !=
                std::string::npos &&
            result.vertex_glsl.find("PS = alu2_4") != std::string::npos &&
            result.vertex_glsl.find("gl_Position") != std::string::npos,
        "multiple reached DOT4 groups keep unique values and reach exports");
    test::require(result.vertex_spirv.front() == 0x07230203u,
                  "literal and DOT4 vertex shader compiles to SPIR-V");
}

void test_reached_alu_nop_continues_to_program_end() {
    const auto vs = nop_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});

    test::require(result.vertex_glsl.find("gl_Position") != std::string::npos &&
                      result.vertex_spirv.front() == 0x07230203u,
                  "reached ALU NOP preserves later exports and compiles");
}

void test_reached_title_arithmetic_and_fetch4_translate() {
    const auto vs = arithmetic_vertex_program();
    const auto ps = fetch4_pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});

    test::require(result.vertex_glsl.find("R[1].x + c[0].x") !=
                      std::string::npos,
                  "title ADD translates");
    test::require(result.vertex_glsl.find("R[1].y * c[0].y") !=
                      std::string::npos,
                  "title MUL translates");
    test::require(result.vertex_glsl.find("R[1].z > c[0].z") !=
                      std::string::npos,
                  "title SETGT translates");
    test::require(result.vertex_glsl.find("R[1].w * c[0].w + 1.0") !=
                      std::string::npos,
                  "title MULADD translates");
    test::require(result.fragment_glsl.find("texture(tex0") !=
                          std::string::npos &&
                      result.vertex_spirv.front() == 0x07230203u &&
                      result.fragment_spirv.front() == 0x07230203u,
                  "FETCH4 fallback and title arithmetic compile to SPIR-V");
}

void test_reached_integer_alu_forms_translate() {
    const auto vs = integer_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});

    test::require(
        result.vertex_glsl.find("floatBitsToUint(R[1].x) +") !=
                std::string::npos &&
            result.vertex_glsl.find("? 0xFFFFFFFFu : 0u") !=
                std::string::npos &&
            result.vertex_glsl.find("floatBitsToInt(R[1].z) == 0") !=
                std::string::npos &&
            result.vertex_glsl.find("R[0].z = alu") != std::string::npos &&
            result.vertex_spirv.front() == 0x07230203u,
        "title integer ADD, SET, and CND forms compile to SPIR-V");
}

void test_alu_group_commits_after_all_lanes_execute() {
    const auto vs = dependent_group_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});
    const auto second_lane = result.vertex_glsl.find("float alu0_1");
    const auto first_write = result.vertex_glsl.find("R[1].x = alu0_0");
    test::require(second_lane != std::string::npos &&
                      first_write != std::string::npos &&
                      second_lane < first_write,
                  "ALU lanes read the register state from before their group");
}

void test_transcendental_alu_output_uses_previous_scalar() {
    const auto vs = conversion_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});
    test::require(result.vertex_glsl.find("PS = alu0_0") !=
                          std::string::npos &&
                      result.vertex_glsl.find("PV.x = alu0_0") ==
                          std::string::npos,
                  "transcendental ALU output is routed through PS");
}

void test_reciprocal_square_root_translates() {
    auto vs = conversion_vertex_program();
    put_word(vs, 20, (0x69u << 7) | (1u << 4));
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    bool supported = false;
    try {
        const auto result = translate_latte(LatteShaderInput{
            no_registers, vs, no_registers, ps, fs});
        supported = result.vertex_glsl.find("inversesqrt(R[1].x)") !=
                        std::string::npos &&
                    result.vertex_spirv.front() == 0x07230203u;
    } catch (const std::exception&) {
    }
    test::require(supported, "RECIPSQRT_IEEE compiles to SPIR-V");
}

void test_reached_title_alu_frontier_translates() {
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};
    for (const auto& [opcode, expected] :
         std::array<std::pair<uint32_t, std::string_view>, 6>{{
             {0x0D, "intBitsToFloat((R[1].x > R[0].x ? -1 : 0))"},
             {0x13, "roundEven(R[1].x)"},
             {0x61, "exp2(R[1].x)"},
             {0x62, "log2(max(0.0, R[1].x))"},
             {0x6A, "sqrt(R[1].x)"},
             {0x73, "floatBitsToUint(R[1].x) * floatBitsToUint(R[0].x)"},
         }}) {
        auto vs = conversion_vertex_program();
        put_word(vs, 20, (opcode << 7) | (1u << 4));
        bool supported = false;
        try {
            const auto result = translate_latte(LatteShaderInput{
                no_registers, vs, no_registers, ps, fs});
            supported = result.vertex_glsl.find(expected) != std::string::npos &&
                        result.vertex_spirv.front() == 0x07230203u;
        } catch (const std::exception&) {
        }
        test::require(supported, "reached title ALU opcode compiles to SPIR-V");
    }
}

void test_sample_lz_translates() {
    const auto vs = vertex_program();
    auto ps = pixel_program();
    put_word(ps, 16, 0x13u);
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    bool supported = false;
    try {
        const auto result = translate_latte(LatteShaderInput{
            no_registers, vs, no_registers, ps, fs});
        supported =
            result.fragment_glsl.find("textureLod(tex0") != std::string::npos &&
            result.fragment_spirv.front() == 0x07230203u;
    } catch (const std::exception&) {
    }
    test::require(supported, "SAMPLE_LZ compiles to SPIR-V");
}

void test_sample_l_translates() {
    const auto vs = vertex_program();
    auto ps = pixel_program();
    put_word(ps, 16, 0x11u);
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    bool supported = false;
    try {
        const auto result = translate_latte(LatteShaderInput{
            no_registers, vs, no_registers, ps, fs});
        supported =
            result.fragment_glsl.find("textureLod(tex0") != std::string::npos &&
            result.fragment_glsl.find("R[0].w") != std::string::npos &&
            result.fragment_spirv.front() == 0x07230203u;
    } catch (const std::exception&) {
    }
    test::require(supported, "SAMPLE_L compiles to SPIR-V");
}

void test_integer_kill_discards_fragment() {
    const auto vs = vertex_program();
    auto ps = conversion_vertex_program();
    put_word(ps, 8, cf_export(1, 0, 0));
    put_word(ps, 20, 0x46u << 7);
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    bool supported = false;
    try {
        const auto result = translate_latte(LatteShaderInput{
            no_registers, vs, no_registers, ps, fs});
        supported = result.fragment_glsl.find("discard") != std::string::npos &&
                    result.fragment_spirv.front() == 0x07230203u;
    } catch (const std::exception&) {
    }
    test::require(supported, "KILLE_INT compiles to fragment discard");
}

void test_uniform_block_source_records_compact_binding() {
    const auto vs = uniform_block_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});
    test::require(
        result.vertex_uniform_blocks ==
                std::vector<LatteUniformBlockRef>{{3, 32}} &&
            result.vertex_glsl.find("b[0].x") != std::string::npos,
        "kcache sources map to compact uniform-block entries");
}

void test_execute_mask_control_flow_translates() {
    const auto vs = control_flow_vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    const std::array<uint32_t, 1> no_registers{};

    const auto result = translate_latte(LatteShaderInput{
        no_registers, vs, no_registers, ps, fs});
    test::require(
        result.vertex_glsl.find(
            "execStack[execStackIndex++] = execActive") !=
                std::string::npos &&
            result.vertex_glsl.find("execActive = pred0_0") !=
                std::string::npos &&
            result.vertex_glsl.find("PV.x = alu0_0") ==
                std::string::npos &&
            result.vertex_glsl.find("execActive = !execActive") !=
                std::string::npos &&
            result.vertex_spirv.front() == 0x07230203u,
        "execute-mask PUSH, ELSE, and POP compile to SPIR-V");
}

void test_vertex_semantic_table_maps_semantic_to_gpr_plus_one() {
    const auto vs = vertex_program();
    const auto ps = pixel_program();
    const auto fs = fetch_program();
    std::array<uint32_t, 19> vs_registers{};
    vs_registers[16] = 2;
    vs_registers[17] = 5;
    vs_registers[18] = 0;
    const std::array<uint32_t, 1> no_pixel_registers{};

    const auto result = translate_latte(LatteShaderInput{
        vs_registers, vs, no_pixel_registers, ps, fs});

    test::require(result.vertex_glsl.find("R[2] = vec4(a0.x") !=
                      std::string::npos,
                  "semantic table index plus one selects the destination GPR");
}
void test_unmapped_fetch_semantics_are_ignored() {
    auto fs = fetch_program();
    fs.resize(48);
    put_word(fs, 4, cf_normal(2, 3, 2));
    put_word(fs, 32, 0x1C00A001u);
    put_word(fs, 36, 0x27961001u);
    put_word(fs, 40, 0x000A0000u);
    const auto vs = vertex_program();
    const auto ps = pixel_program();
    std::array<uint32_t, 18> vs_registers{};
    vs_registers[16] = 1;
    vs_registers[17] = 0;
    const std::array<uint32_t, 1> no_pixel_registers{};

    bool filtered = false;
    try {
        const auto result = translate_latte(LatteShaderInput{
            vs_registers, vs, no_pixel_registers, ps, fs});
        filtered = result.vertex_inputs.size() == 1 &&
                   result.vertex_inputs[0].semantic == 0 &&
                   result.vertex_glsl.find("a1") == std::string::npos;
    } catch (const std::exception&) {
    }
    test::require(filtered,
                  "fetch attributes absent from the VS semantic table are ignored");
}

void test_vertex_fetch_endian_modes_match_cemu() {
    using nwii::runtime::decode_latte_vertex_word;
    constexpr uint32_t guest_word = 0x3F800000;
    test::require(
        decode_latte_vertex_word(guest_word, 0) == 0x0000803F &&
            decode_latte_vertex_word(guest_word, 1) == 0x00003F80 &&
            decode_latte_vertex_word(guest_word, 2) == 0x3F800000,
        "vertex words honor Latte SWAP_NONE, SWAP_U16, and SWAP_U32");
}

} // namespace

int main() {
    test_reached_mov_sample_pipeline_translates_to_spirv();
    test_reached_literal_and_dot4_forms_translate();
    test_reached_alu_nop_continues_to_program_end();
    test_reached_title_arithmetic_and_fetch4_translate();
    test_reached_integer_alu_forms_translate();
    test_alu_group_commits_after_all_lanes_execute();
    test_transcendental_alu_output_uses_previous_scalar();
    test_reciprocal_square_root_translates();
    test_uniform_block_source_records_compact_binding();
    test_reached_title_alu_frontier_translates();
    test_sample_lz_translates();
    test_sample_l_translates();
    test_integer_kill_discards_fragment();
    test_execute_mask_control_flow_translates();
    test_vertex_semantic_table_maps_semantic_to_gpr_plus_one();
    test_unmapped_fetch_semantics_are_ignored();
    test_vertex_fetch_endian_modes_match_cemu();
}
