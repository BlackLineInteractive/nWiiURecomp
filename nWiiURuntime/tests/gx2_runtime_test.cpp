#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "runtime/gx2_runtime.h"
#include "test_support.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <type_traits>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwii::runtime::Gx2Runtime;
using nwii::runtime::Gx2UniformBlockState;
using nwii::runtime::MemoryAccess;
using nwii::runtime::StopCategory;

static_assert(!std::is_copy_constructible_v<Gx2Runtime>);
static_assert(!std::is_move_constructible_v<Gx2Runtime>);
static_assert(!std::is_copy_assignable_v<Gx2Runtime>);
static_assert(!std::is_move_assignable_v<Gx2Runtime>);

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kImport = 0xC00063A8;
constexpr uint32_t kAttributes = 0x10000000;
constexpr uint32_t kCommandBuffer = 0x30000000;
constexpr uint32_t kDepthStencilImport = 0xC0006848;
constexpr uint32_t kStencilMaskImport = 0xC0006990;
constexpr uint32_t kPolygonControlImport = 0xC0006930;
constexpr uint32_t kColorControlImport = 0xC00067D0;
constexpr uint32_t kBlendControlImport = 0xC00067B0;
constexpr uint32_t kBlendConstantImport = 0xC00067A0;
constexpr uint32_t kAlphaTestImport = 0xC0006778;
constexpr uint32_t kTargetMasksImport = 0xC00069F0;
constexpr uint32_t kAlphaToMaskImport = 0xC0006788;
constexpr uint32_t kContextSetupImport = 0xC0006A60;
constexpr uint32_t kViewportImport = 0xC0006A50;
constexpr uint32_t kScissorImport = 0xC0006970;
constexpr uint32_t kContext = 0x21000000;
constexpr uint32_t kCalcTvSizeImport = 0xC00060D8;
constexpr uint32_t kCalcDrcSizeImport = 0xC00060A8;
constexpr uint32_t kSetTvBufferImport = 0xC00069C8;
constexpr uint32_t kSetDrcBufferImport = 0xC00067F8;
constexpr uint32_t kSetTvEnableImport = 0xC00069D0;
constexpr uint32_t kSetDrcEnableImport = 0xC00067E8;
constexpr uint32_t kCopyColorToScanImport = 0xC0006110;
constexpr uint32_t kSetTvScaleImport = 0xC00069E0;
constexpr uint32_t kSetDrcScaleImport = 0xC0006818;
constexpr uint32_t kSetSwapIntervalImport = 0xC00069C0;
constexpr uint32_t kTempGpuVersionImport = 0xC0006AA8;
constexpr uint32_t kInvalidateImport = 0xC00064A8;
constexpr uint32_t kInvalBuffer = 0x3BC4A600;
constexpr uint32_t kCalcFetchShaderSizeImport = 0xC00060B8;
constexpr uint32_t kTvBuffer = 0x3C000000;
constexpr uint32_t kDrcBuffer = 0x3CFD2000;
constexpr uint32_t kOutputs = 0x22000000;
constexpr uint32_t kStack = 0x20000000;
constexpr uint32_t kInitFetchShaderImport = 0xC0006400;
constexpr uint32_t kFetchShader = 0x23000000;
constexpr uint32_t kFetchBuffer = 0x23001000;
constexpr uint32_t kAttribStreams = 0x23002000;
constexpr uint32_t kInitSamplerImport = 0xC0006438;
constexpr uint32_t kSampler = 0x24000000;
constexpr uint32_t kCalcGsInputRingImport = 0xC00060C0;
constexpr uint32_t kCalcGsOutputRingImport = 0xC00060C8;
constexpr uint32_t kGetContextDisplayListImport = 0xC00061F0;
constexpr uint32_t kContextState = 0x25000000;
constexpr uint32_t kCalcSurfaceImport = 0xC00060D0;
constexpr uint32_t kCalcDepthHiZImport = 0xC00060B0;
constexpr uint32_t kSetSurfaceSwizzleImport = 0xC00069B8;
constexpr uint32_t kSurface = 0x26000000;
constexpr uint32_t kCopySurfaceImport = 0xC0006120;
constexpr uint32_t kCopySource = 0x29000000;
constexpr uint32_t kCopyDestination = 0x29001000;
constexpr uint32_t kInitDepthHiZImport = 0xC00063E8;
constexpr uint32_t kDepthBuffer = 0x2A000000;
constexpr uint32_t kBeginDisplayListImport = 0xC0006090;
constexpr uint32_t kCallDisplayListImport = 0xC00060E0;
constexpr uint32_t kDirectCallDisplayListImport = 0xC0006160;
constexpr uint32_t kDisplayList = 0x2B100000;
constexpr uint32_t kSetVertexShaderImport = 0xC0006A28;
constexpr uint32_t kSetPixelShaderImport = 0xC00068F0;
constexpr uint32_t kSetFetchShaderImport = 0xC0006860;
constexpr uint32_t kSetPixelSamplerImport = 0xC00068E0;
constexpr uint32_t kGetCurrentDisplayListImport = 0xC0006218;
constexpr uint32_t kEndDisplayListImport = 0xC0006190;
constexpr uint32_t kShaderQueryBase = 0xC0007000;
constexpr uint32_t kGpuTimeImport = 0xC00061B8;
constexpr uint32_t kSampleBottomImport = 0xC0006748;
constexpr uint32_t kSampleTopImport = 0xC0006760;
constexpr uint32_t kSetContextStateImport = 0xC00067E0;
constexpr uint32_t kFlushImport = 0xC00061A8;
constexpr uint32_t kDrawDoneImport = 0xC0006168;
constexpr uint32_t kWaitForVsyncImport = 0xC0006AE8;
constexpr uint32_t kSwapScanBuffersImport = 0xC0006A90;
constexpr uint32_t kGetSwapStatusImport = 0xC0006348;
constexpr uint32_t kVertexShader = 0x2B000001;
constexpr uint32_t kInitTextureImport = 0xC0006498;
constexpr uint32_t kTexture = 0x27000000;
constexpr uint32_t kInitSamplerClampingImport = 0xC0006448;
constexpr uint32_t kClampSampler = 0x28000000;
constexpr uint32_t kInitSamplerXYFilterImport = 0xC0006470;
constexpr uint32_t kInitSamplerZMFilterImport = 0xC0006478;
constexpr uint32_t kInitSamplerLODImport = 0xC0006460;
constexpr uint32_t kInitSamplerDepthCompareImport = 0xC0006450;
constexpr uint32_t kInitSamplerBorderTypeImport = 0xC0006440;
constexpr uint32_t kSetColorBufferImport = 0xC0006D00;
constexpr uint32_t kColorBuffer = 0x2C000000;
constexpr uint32_t kInitColorBufferImport = 0xC00063D0;
constexpr uint32_t kColorImage = 0x2C001000;
constexpr uint32_t kSetColorBufferPublicImport = 0xC00067C8;
constexpr uint32_t kInitDepthBufferRegsImport = 0xC00063F0;
constexpr uint32_t kSetDepthBufferImport = 0xC0006838;
constexpr uint32_t kSetClearDepthStencilImport = 0xC00067C0;
constexpr uint32_t kClearColorImport = 0xC00060F8;
constexpr uint32_t kClearBuffersImport = 0xC00060F0;
constexpr uint32_t kClearDepthImport = 0xC0006100;
constexpr uint32_t kSetShaderModeImport = 0xC0006988;
constexpr uint32_t kSetVertexUniformRegImport = 0xC0006A40;
constexpr uint32_t kSetVertexUniformBlockImport = 0xC0006A44;
constexpr uint32_t kUniformData = 0x2D000000;
constexpr uint32_t kSetPixelTextureImport = 0xC00068F8;
constexpr uint32_t kBoundTexture = 0x2E000000;
constexpr uint32_t kSetAttribBufferImport = 0xC0006798;
constexpr uint32_t kAttribData = 0x2F000000;
constexpr uint32_t kDrawIndexedImport = 0xC0006178;
constexpr uint32_t kIndexData = 0x30000000;

ExecutionImage make_image(uint32_t attribute_bytes = 0x100) {
    ExecutionImage image;
    constexpr std::array<uint8_t, 4> branch{0x48, 0, 0, 0};
    image.memory.map(kReturn, branch.size(), {true, false, true}, branch);
    image.memory.map(kAttributes, attribute_bytes, {true, true, false});
    image.imports.emplace(
        kImport, nwii::runtime::ImportTarget{"gx2", "GX2Init"});
    return image;
}

void write_attribute(ExecutionImage& image, uint32_t& cursor, uint32_t id,
                     uint32_t value) {
    image.memory.write32(cursor, id, 0);
    image.memory.write32(cursor + 4, value, 0);
    cursor += 8;
}

CPUContext init_cpu() {
    CPUContext cpu;
    cpu.pc = kImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kAttributes;
    return cpu;
}

void map_rgba8_color_buffer(ExecutionImage& image, uint32_t width = 2,
                            uint32_t height = 2) {
    const uint32_t image_size = width * height * 4;
    image.memory.map(kColorBuffer, 0x9C, {true, true, false});
    image.memory.map(kColorImage, image_size, {true, true, false});
    image.memory.write32(kColorBuffer + 0x04, width, 0);
    image.memory.write32(kColorBuffer + 0x08, height, 0);
    image.memory.write32(kColorBuffer + 0x0C, 1, 0);
    image.memory.write32(kColorBuffer + 0x10, 1, 0);
    image.memory.write32(kColorBuffer + 0x14, 0x1A, 0);
    image.memory.write32(kColorBuffer + 0x20, image_size, 0);
    image.memory.write32(kColorBuffer + 0x24, kColorImage, 0);
    image.memory.write32(kColorBuffer + 0x30, 16, 0);
    image.memory.write32(kColorBuffer + 0x3C, width, 0);
    image.memory.write32(kColorBuffer + 0x7C, 1, 0);
}

void test_gx2_init_parses_attributes_and_initializes_subsystems() {
    auto image = make_image();
    image.memory.map(kCommandBuffer, 0x6000, {true, true, false});
    uint32_t cursor = kAttributes;
    write_attribute(image, cursor, 1, kCommandBuffer);
    write_attribute(image, cursor, 2, 0x6000);
    write_attribute(image, cursor, 7, 3);
    write_attribute(image, cursor, 8, 0x12345678);
    write_attribute(image, cursor, 9, 2);
    write_attribute(image, cursor, 10, 7);
    write_attribute(image, cursor, 11, 0x1000);
    image.memory.write32(cursor, 0, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_cpu();
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2Init exact import is bound and returns");
    const auto& state = runtime.gx2().state();
    test::require(state.initialized && state.main_core_id == 1,
                  "GX2Init records initialized state and application core");
    test::require(state.command_buffer_base == kCommandBuffer &&
                      state.command_buffer_size == 0x5000 &&
                      state.app_io_stack_base == kCommandBuffer + 0x5000 &&
                      state.app_io_stack_size == 0x1000,
                  "GX2Init reserves the AppIo stack from the pool tail");
    test::require(state.argc == 3 && state.argv == 0x12345678 &&
                      state.profile_mode == 2 && state.toss_stage == 7,
                  "GX2Init preserves source-defined attribute outputs");
    test::require(state.events_initialized && state.flip_callback_installed &&
                      state.command_buffer_pool_initialized &&
                      state.default_state_initialized && state.flush_count == 1,
                  "GX2Init initializes callbacks, state, and initial flush");
}

void test_gx2_init_applies_modeled_default_state() {
    auto image = make_image();
    image.memory.map(kCommandBuffer, 0x2000, {true, true, false});
    uint32_t cursor = kAttributes;
    write_attribute(image, cursor, 1, kCommandBuffer);
    write_attribute(image, cursor, 2, 0x2000);
    image.memory.write32(cursor, 0, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_cpu();
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2Init returns before default-state inspection");
    const auto& state = runtime.gx2().state();
    test::require(
        state.depth_stencil.valid &&
            state.depth_stencil.args ==
                std::array<uint32_t, 13>{1, 1, 1, 0, 0, 7, 2, 2, 2, 7, 2,
                                         2, 2} &&
            state.stencil_mask.valid &&
            state.stencil_mask.args ==
                std::array<uint32_t, 6>{0xFF, 0xFF, 1, 0xFF, 0xFF, 1} &&
            state.polygon_control.valid &&
            state.polygon_control.args ==
                std::array<uint32_t, 9>{0, 0, 0, 0, 2, 2, 0, 0, 0} &&
            state.color_control.valid &&
            state.color_control.args ==
                std::array<uint32_t, 4>{0xCC, 0, 0, 1} &&
            state.blend_constant.valid &&
            state.blend_constant.args ==
                std::array<uint32_t, 4>{0, 0, 0, 0} &&
            state.alpha_test.valid &&
            state.alpha_test.args == std::array<uint32_t, 3>{0, 1, 0} &&
            state.target_channel_masks.valid &&
            state.target_channel_masks.args ==
                std::array<uint32_t, 8>{0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF,
                                         0xF} &&
            state.alpha_to_mask.valid &&
            state.alpha_to_mask.args == std::array<uint32_t, 2>{0, 0},
        "GX2Init applies every modeled non-indexed default state");
    for (uint32_t target = 0; target < state.blend_controls.size(); ++target) {
        test::require(
            state.blend_controls[target].valid &&
                state.blend_controls[target].args ==
                    std::array<uint32_t, 8>{target, 4, 5, 0, 1, 4, 5, 0},
            "GX2Init applies every indexed blend default");
    }
}

void test_gx2_init_attribute_fault_is_atomic() {
    auto image = make_image(4);
    image.memory.write32(kAttributes, 1, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    auto cpu = init_cpu();
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::read,
                  "GX2Init reports a truncated attribute pair as a read fault");
    test::require(runtime.gx2().state() == before,
                  "GX2Init validates attributes before state mutation");
}

void test_gx2_init_pool_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kCommandBuffer, 0x6000, {true, false, false});
    uint32_t cursor = kAttributes;
    write_attribute(image, cursor, 1, kCommandBuffer);
    write_attribute(image, cursor, 2, 0x6000);
    write_attribute(image, cursor, 11, 0x1000);
    image.memory.write32(cursor, 0, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    auto cpu = init_cpu();
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kCommandBuffer &&
                      stop.fault_width == 0x6000,
                  "GX2Init preflights the complete command buffer pool");
    test::require(runtime.gx2().state() == before,
                  "GX2Init pool validation faults before state mutation");
}
void test_depth_stencil_control_reads_full_cafe_abi() {
    auto image = make_image();
    image.memory.map(kStack, 0x100, {true, true, false});
    image.imports.emplace(
        kDepthStencilImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDepthStencilControl"});
    constexpr std::array<uint32_t, 13> expected{
        1, 1, 3, 1, 1, 2, 3, 4, 5, 6, 7, 0, 1};
    for (uint32_t index = 8; index < expected.size(); ++index) {
        image.memory.write32(kStack + 8 + index * 4, expected[index], 0);
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kDepthStencilImport;
    cpu.lr = kReturn;
    cpu.gpr[1] = kStack;
    for (uint32_t index = 0; index < 8; ++index) {
        cpu.gpr[3 + index] = expected[index];
    }
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetDepthStencilControl exact import returns");
    test::require(runtime.gx2().state().depth_stencil.valid &&
                      runtime.gx2().state().depth_stencil.args == expected,
                  "GX2SetDepthStencilControl reads registers and stack slots");
}

void test_depth_stencil_stack_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kStack, 0x2C, {true, true, false});
    image.imports.emplace(
        kDepthStencilImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDepthStencilControl"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    CPUContext cpu;
    cpu.pc = kDepthStencilImport;
    cpu.lr = kReturn;
    cpu.gpr[1] = kStack;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::read &&
                      stop.fault_address == kStack + 40 &&
                      stop.fault_width == 20,
                  "GX2SetDepthStencilControl preflights stack arguments");
    test::require(runtime.gx2().state() == before,
                  "stack fault occurs before depth-stencil state mutation");
}

void test_depth_stencil_stack_range_preflight() {
    constexpr std::array<uint32_t, 13> expected{
        1, 1, 3, 1, 1, 2, 3, 4, 9, 10, 11, 12, 13};
    constexpr std::array<uint8_t, 2> first{0, 0};
    constexpr std::array<uint8_t, 8> second{
        0, 9, 0, 0, 0, 10, 0, 0};
    constexpr std::array<uint8_t, 10> third{
        0, 11, 0, 0, 0, 12, 0, 0, 0, 13};
    {
        auto image = make_image();
        image.memory.map(kStack, first.size(), {true, false, false}, first);
        image.memory.map(kStack + first.size(), second.size(),
                         {true, false, false}, second);
        image.memory.map(kStack + first.size() + second.size(),
                         third.size(), {true, false, false}, third);
        image.imports.emplace(
            kDepthStencilImport,
            nwii::runtime::ImportTarget{"gx2",
                                        "GX2SetDepthStencilControl"});

        CafeRuntime runtime(image);
        Executor executor(image);
        runtime.register_imports(executor);
        CPUContext cpu;
        cpu.pc = kDepthStencilImport;
        cpu.lr = kReturn;
        cpu.gpr[1] = kStack - 40;
        for (uint32_t index = 0; index < 8; ++index) {
            cpu.gpr[3 + index] = expected[index];
        }
        const auto stop = executor.run(cpu, 1);

        test::require(
            stop.category == StopCategory::instruction_budget &&
                runtime.gx2().state().depth_stencil.valid &&
                runtime.gx2().state().depth_stencil.args == expected,
            "stack arguments span contiguous read-only mappings");
    }
    {
        auto image = make_image();
        image.memory.map(kStack, first.size(), {true, false, false}, first);
        image.memory.map(kStack + first.size(), second.size(),
                         {false, false, false}, second);
        image.memory.map(kStack + first.size() + second.size(),
                         third.size(), {false, false, false}, third);
        image.imports.emplace(
            kDepthStencilImport,
            nwii::runtime::ImportTarget{"gx2",
                                        "GX2SetDepthStencilControl"});

        CafeRuntime runtime(image);
        Executor executor(image);
        runtime.register_imports(executor);
        const auto before = runtime.gx2().state();
        CPUContext cpu;
        cpu.pc = kDepthStencilImport;
        cpu.lr = kReturn;
        cpu.gpr[1] = kStack - 40;
        const auto stop = executor.run(cpu, 1);

        test::require(stop.category == StopCategory::guest_fault &&
                          stop.fault_access == MemoryAccess::read &&
                          runtime.gx2().state() == before,
                      "unreadable stack segment faults before state mutation");
    }
}

void test_stencil_mask_updates_indexed_values() {
    auto image = make_image();
    image.imports.emplace(
        kStencilMaskImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetStencilMask"});
    constexpr std::array<uint32_t, 6> expected{
        0xFF, 0xFF, 0, 0xA5, 0x5A, 0x7F};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kStencilMaskImport;
    cpu.lr = kReturn;
    for (uint32_t index = 0; index < expected.size(); ++index) {
        cpu.gpr[3 + index] = expected[index];
    }
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetStencilMask exact import returns");
    test::require(runtime.gx2().state().stencil_mask.valid &&
                      runtime.gx2().state().stencil_mask.args == expected,
                  "GX2SetStencilMask preserves all six WUT arguments");
}

void test_polygon_control_reads_overflow_argument() {
    auto image = make_image();
    image.memory.map(kStack, 0x100, {true, true, false});
    image.imports.emplace(
        kPolygonControlImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetPolygonControl"});
    constexpr std::array<uint32_t, 9> expected{
        1, 1, 0, 1, 2, 0, 1, 0, 1};
    image.memory.write32(kStack + 40, expected[8], 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kPolygonControlImport;
    cpu.lr = kReturn;
    cpu.gpr[1] = kStack;
    for (uint32_t index = 0; index < 8; ++index) {
        cpu.gpr[3 + index] = expected[index];
    }
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetPolygonControl exact import returns");
    test::require(runtime.gx2().state().polygon_control.valid &&
                      runtime.gx2().state().polygon_control.args == expected,
                  "GX2SetPolygonControl preserves all nine WUT arguments");
}

void test_color_control_updates_state() {
    auto image = make_image();
    image.imports.emplace(
        kColorControlImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetColorControl"});
    constexpr std::array<uint32_t, 4> expected{0xCC, 0xFF, 0, 1};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kColorControlImport;
    cpu.lr = kReturn;
    for (uint32_t index = 0; index < expected.size(); ++index) {
        cpu.gpr[3 + index] = expected[index];
    }
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetColorControl exact import returns");
    test::require(runtime.gx2().state().color_control.valid &&
                      runtime.gx2().state().color_control.args == expected,
                  "GX2SetColorControl preserves its WUT state arguments");
}

void test_blend_control_updates_target_state() {
    auto image = make_image();
    image.imports.emplace(
        kBlendControlImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetBlendControl"});
    constexpr std::array<uint32_t, 8> first{3, 4, 5, 0, 1, 4, 5, 0};
    constexpr std::array<uint32_t, 8> second{5, 1, 0, 4, 0, 1, 0, 4};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.lr = kReturn;
    for (uint32_t index = 0; index < first.size(); ++index) {
        cpu.gpr[3 + index] = first[index];
    }
    cpu.pc = kBlendControlImport;
    auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetBlendControl exact import returns");

    for (uint32_t index = 0; index < second.size(); ++index) {
        cpu.gpr[3 + index] = second[index];
    }
    cpu.pc = kBlendControlImport;
    cpu.instruction_count = 0;
    stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetBlendControl supports another render target");
    test::require(runtime.gx2().state().blend_controls[3].valid &&
                      runtime.gx2().state().blend_controls[3].args == first &&
                      runtime.gx2().state().blend_controls[5].valid &&
                      runtime.gx2().state().blend_controls[5].args == second,
                  "GX2 blend targets retain independent state");
}

void test_blend_constant_uses_floating_argument_channel() {
    auto image = make_image();
    image.imports.emplace(
        kBlendConstantImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetBlendConstantColor"});
    constexpr std::array<float, 4> values{0.25f, 0.5f, 0.75f, 1.0f};
    constexpr std::array<uint32_t, 4> expected{
        std::bit_cast<uint32_t>(values[0]),
        std::bit_cast<uint32_t>(values[1]),
        std::bit_cast<uint32_t>(values[2]),
        std::bit_cast<uint32_t>(values[3])};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kBlendConstantImport;
    cpu.lr = kReturn;
    for (uint32_t index = 0; index < values.size(); ++index) {
        cpu.fpr[1 + index][0] =
            std::bit_cast<uint64_t>(static_cast<double>(values[index]));
    }
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetBlendConstantColor exact import returns");
    test::require(runtime.gx2().state().blend_constant.valid &&
                      runtime.gx2().state().blend_constant.args == expected,
                  "GX2 floating arguments come from f1 through f4");
}

void test_viewport_uses_six_floating_arguments() {
    auto image = make_image();
    image.imports.emplace(
        kViewportImport, nwii::runtime::ImportTarget{"gx2", "GX2SetViewport"});
    image.imports.emplace(
        kScissorImport, nwii::runtime::ImportTarget{"gx2", "GX2SetScissor"});
    constexpr std::array<float, 6> values{0.0f, 0.0f, 1280.0f,
                                          720.0f, 0.0f, 1.0f};
    std::array<uint32_t, 6> expected{};
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kViewportImport;
    cpu.lr = kReturn;
    for (uint32_t index = 0; index < values.size(); ++index) {
        cpu.fpr[1 + index][0] =
            std::bit_cast<uint64_t>(static_cast<double>(values[index]));
        expected[index] = std::bit_cast<uint32_t>(values[index]);
    }

    auto stop = executor.run(cpu, 1);
    cpu.pc = kScissorImport;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0;
    cpu.gpr[5] = 1920;
    cpu.gpr[6] = 1080;
    stop = executor.run(cpu, 2);
    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().viewport.valid &&
                      runtime.gx2().state().viewport.args == expected &&
                      runtime.gx2().state().scissor.valid &&
                      runtime.gx2().state().scissor.args ==
                          std::array<uint32_t, 4>{0, 0, 1920, 1080},
                  "GX2 viewport and scissor record raster bounds");
}

void test_alpha_test_uses_mixed_argument_channels() {
    auto image = make_image();
    image.imports.emplace(
        kAlphaTestImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetAlphaTest"});
    constexpr float reference = 0.625f;
    constexpr std::array<uint32_t, 3> expected{
        1, 6, std::bit_cast<uint32_t>(reference)};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kAlphaTestImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = expected[0];
    cpu.gpr[4] = expected[1];
    cpu.gpr[5] = 0xDEADBEEF;
    cpu.fpr[1][0] =
        std::bit_cast<uint64_t>(static_cast<double>(reference));
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetAlphaTest exact import returns");
    test::require(runtime.gx2().state().alpha_test.valid &&
                      runtime.gx2().state().alpha_test.args == expected,
                  "GX2SetAlphaTest separates integer and floating lanes");
}

void test_target_channel_masks_preserve_all_targets() {
    auto image = make_image();
    image.imports.emplace(
        kTargetMasksImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetTargetChannelMasks"});
    constexpr std::array<uint32_t, 8> expected{
        0xF, 1, 2, 3, 4, 5, 6, 7};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kTargetMasksImport;
    cpu.lr = kReturn;
    for (uint32_t index = 0; index < expected.size(); ++index) {
        cpu.gpr[3 + index] = expected[index];
    }
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetTargetChannelMasks exact import returns");
    test::require(runtime.gx2().state().target_channel_masks.valid &&
                      runtime.gx2().state().target_channel_masks.args ==
                          expected,
                  "GX2SetTargetChannelMasks preserves eight target masks");
}

void test_alpha_to_mask_updates_state() {
    auto image = make_image();
    image.imports.emplace(
        kAlphaToMaskImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetAlphaToMask"});
    constexpr std::array<uint32_t, 2> expected{1, 4};

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kAlphaToMaskImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = expected[0];
    cpu.gpr[4] = expected[1];
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetAlphaToMask exact import returns");
    test::require(runtime.gx2().state().alpha_to_mask.valid &&
                      runtime.gx2().state().alpha_to_mask.args == expected,
                  "GX2SetAlphaToMask preserves enable and dither mode");
}

void test_setup_context_state_writes_valid_shadow_list() {
    auto image = make_image();
    image.memory.map(kContext, 0x5002, {true, true, false});
    image.memory.map(kContext + 0x5002, 0x50FE, {true, true, false});
    image.imports.emplace(
        kContextSetupImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetupContextStateEx"});
    for (uint32_t offset = 0; offset < 0xA100; ++offset) {
        image.memory.write8(kContext + offset, 0xA5, 0);
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kContextSetupImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kContext;
    cpu.gpr[4] = 0;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2SetupContextStateEx exact import returns");
    for (uint32_t offset = 0; offset < 0x9800; ++offset) {
        test::require(image.memory.read8(kContext + offset, 0) == 0,
                      "context shadow state is cleared");
    }
    test::require(image.memory.read32(kContext + 0x9800, 0) == 0 &&
                      image.memory.read32(kContext + 0x9804, 0) == 8,
                  "context profiling and shadow-list size are initialized");
    for (uint32_t offset = 0x9808; offset < 0x9E00; ++offset) {
        test::require(image.memory.read8(kContext + offset, 0) == 0,
                      "context reserved bytes are cleared");
    }
    test::require(
        image.memory.read16(kContext + 0x9E00, 0) ==
                static_cast<uint16_t>(
                    nwii::runtime::Gx2Opcode::load_context) &&
            image.memory.read16(kContext + 0x9E02, 0) == 1 &&
            image.memory.read32(kContext + 0x9E04, 0) == kContext,
        "context shadow list uses compact big-endian GX2 record format");
    for (uint32_t offset = 0x9E08; offset < 0xA100; ++offset) {
        test::require(image.memory.read8(kContext + offset, 0) == 0,
                      "unused shadow-list storage is cleared");
    }
    test::require(runtime.gx2().state().context_setup.valid &&
                      runtime.gx2().state().context_setup.args ==
                          std::array<uint32_t, 2>{kContext, 0},
                  "GX2 setup state records context address and flags");
}

void test_setup_context_state_range_preflight() {
    {
        auto image = make_image();
        image.memory.map(kContext, 0x5000, {true, true, false});
        image.memory.map(kContext + 0x5000, 0x5100,
                         {true, true, false});
        image.imports.emplace(
            kContextSetupImport,
            nwii::runtime::ImportTarget{"gx2", "GX2SetupContextStateEx"});
        image.memory.write8(kContext, 0xA5, 0);
        image.memory.write8(kContext + 0xA0FF, 0xA5, 0);

        CafeRuntime runtime(image);
        Executor executor(image);
        runtime.register_imports(executor);
        CPUContext cpu;
        cpu.pc = kContextSetupImport;
        cpu.lr = kReturn;
        cpu.gpr[3] = kContext;
        const auto stop = executor.run(cpu, 1);

        test::require(
            stop.category == StopCategory::instruction_budget &&
                image.memory.read8(kContext, 0) == 0 &&
                image.memory.read8(kContext + 0xA0FF, 0) == 0 &&
                runtime.gx2().state().context_setup.valid,
            "context setup spans contiguous writable mappings");
    }
    {
        auto image = make_image();
        image.memory.map(kContext, 0x5000, {true, true, false});
        image.memory.map(kContext + 0x5004, 0x50FC,
                         {true, true, false});
        image.imports.emplace(
            kContextSetupImport,
            nwii::runtime::ImportTarget{"gx2", "GX2SetupContextStateEx"});
        image.memory.write8(kContext, 0xA5, 0);
        image.memory.write8(kContext + 0x5004, 0x5A, 0);

        CafeRuntime runtime(image);
        Executor executor(image);
        runtime.register_imports(executor);
        const auto before = runtime.gx2().state();
        CPUContext cpu;
        cpu.pc = kContextSetupImport;
        cpu.lr = kReturn;
        cpu.gpr[3] = kContext;
        const auto stop = executor.run(cpu, 1);

        test::require(
            stop.category == StopCategory::guest_fault &&
                stop.fault_access == MemoryAccess::write &&
                image.memory.read8(kContext, 0) == 0xA5 &&
                image.memory.read8(kContext + 0x5004, 0) == 0x5A &&
                runtime.gx2().state() == before,
            "context gap faults before guest or runtime mutation");
    }
}

void test_setup_context_state_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kContext, 0x5000, {true, true, false});
    image.memory.map(kContext + 0x5000, 0x5100, {true, false, false});
    image.imports.emplace(
        kContextSetupImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetupContextStateEx"});
    image.memory.write8(kContext, 0xA5, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    CPUContext cpu;
    cpu.pc = kContextSetupImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kContext;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kContext &&
                      stop.fault_width == 0xA100,
                  "GX2SetupContextStateEx preflights the complete context");
    test::require(image.memory.read8(kContext, 0) == 0xA5 &&
                      runtime.gx2().state() == before,
                  "context fault occurs before guest or runtime mutation");
}

void test_calc_tv_size_writes_observed_scan_buffer_size() {
    auto image = make_image();
    image.memory.map(kOutputs, 8, {true, true, false});
    image.imports.emplace(
        kCalcTvSizeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcTVSize"});
    image.memory.write32(kOutputs, 0xAAAAAAAA, 0);
    image.memory.write32(kOutputs + 4, 0xBBBBBBBB, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcTvSizeImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 5;
    cpu.gpr[4] = 0x1A;
    cpu.gpr[5] = 2;
    cpu.gpr[6] = kOutputs;
    cpu.gpr[7] = kOutputs + 4;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      image.memory.read32(kOutputs, 0) == 0x00FD2000 &&
                      image.memory.read32(kOutputs + 4, 0) == 0,
                  "GX2CalcTVSize writes exact observed size and auxiliary");
}

void test_calc_tv_size_preflights_both_outputs() {
    auto image = make_image();
    image.memory.map(kOutputs, 4, {true, true, false});
    constexpr std::array<uint8_t, 4> alignment_bytes{0xBB, 0xBB, 0xBB, 0xBB};
    image.memory.map(kOutputs + 4, 4, {true, false, false}, alignment_bytes);
    image.memory.write32(kOutputs, 0xAAAAAAAA, 0);
    image.imports.emplace(
        kCalcTvSizeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcTVSize"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    CPUContext cpu;
    cpu.pc = kCalcTvSizeImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 5;
    cpu.gpr[4] = 0x1A;
    cpu.gpr[5] = 2;
    cpu.gpr[6] = kOutputs;
    cpu.gpr[7] = kOutputs + 4;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_address == kOutputs + 4 &&
                      stop.fault_width == 4 &&
                      image.memory.read32(kOutputs, 0) == 0xAAAAAAAA &&
                      runtime.gx2().state() == before,
                  "GX2CalcTVSize faults before either output or state changes");
}

void test_calc_tv_size_rejects_unsupported_layout() {
    auto image = make_image();
    image.memory.map(kOutputs, 8, {true, true, false});
    image.memory.write32(kOutputs, 0xAAAAAAAA, 0);
    image.memory.write32(kOutputs + 4, 0xBBBBBBBB, 0);
    image.imports.emplace(
        kCalcTvSizeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcTVSize"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcTvSizeImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0x1A;
    cpu.gpr[5] = 2;
    cpu.gpr[6] = kOutputs;
    cpu.gpr[7] = kOutputs + 4;
    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::guest_fault &&
            stop.reason.find("service=GX2CalcTVSize mode=0 format=26 "
                             "buffering=2 error=0") != std::string::npos &&
            image.memory.read32(kOutputs, 0) == 0xAAAAAAAA &&
            image.memory.read32(kOutputs + 4, 0) == 0xBBBBBBBB,
        "GX2CalcTVSize reports structured unsupported-layout metadata");
}

void test_calc_drc_size_writes_observed_scan_buffer_size() {
    auto image = make_image();
    image.memory.map(kOutputs, 8, {true, true, false});
    image.imports.emplace(
        kCalcDrcSizeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcDRCSize"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcDrcSizeImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 1;
    cpu.gpr[4] = 0x1A;
    cpu.gpr[5] = 2;
    cpu.gpr[6] = kOutputs;
    cpu.gpr[7] = kOutputs + 4;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      image.memory.read32(kOutputs, 0) == 0x0032A000 &&
                      image.memory.read32(kOutputs + 4, 0) == 0,
                  "GX2CalcDRCSize writes exact observed size and auxiliary");
}

void test_set_tv_buffer_records_observed_scan_buffer() {
    auto image = make_image();
    image.memory.map(kTvBuffer, 0x00FD2000, {true, true, false});
    image.imports.emplace(
        kSetTvBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetTVBuffer"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetTvBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kTvBuffer;
    cpu.gpr[4] = 0x00FD2000;
    cpu.gpr[5] = 5;
    cpu.gpr[6] = 0x1A;
    cpu.gpr[7] = 2;
    const auto stop = executor.run(cpu, 1);

    const auto& scan = runtime.gx2().state().tv_scan_buffer;
    test::require(stop.category == StopCategory::instruction_budget &&
                      scan.valid && scan.address == kTvBuffer &&
                      scan.size == 0x00FD2000 && scan.mode == 5 &&
                      scan.format == 0x1A && scan.buffering == 2 &&
                      scan.width == 1920 && scan.height == 1080,
                  "GX2SetTVBuffer records exact observed scan metadata");
}

void test_set_tv_buffer_preflights_complete_range() {
    auto image = make_image();
    image.memory.map(kTvBuffer, 0x1000, {true, true, false});
    image.memory.map(kTvBuffer + 0x2000, 0x00FD0000,
                     {true, true, false});
    image.imports.emplace(
        kSetTvBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetTVBuffer"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    CPUContext cpu;
    cpu.pc = kSetTvBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kTvBuffer;
    cpu.gpr[4] = 0x00FD2000;
    cpu.gpr[5] = 5;
    cpu.gpr[6] = 0x1A;
    cpu.gpr[7] = 2;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_address == kTvBuffer &&
                      stop.fault_width == 0x00FD2000 &&
                      runtime.gx2().state() == before,
                  "GX2SetTVBuffer faults before scan state mutation");
}

void test_set_drc_buffer_records_observed_scan_buffer() {
    auto image = make_image();
    image.memory.map(kDrcBuffer, 0x0032A000, {true, true, false});
    image.imports.emplace(
        kSetDrcBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDRCBuffer"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetDrcBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDrcBuffer;
    cpu.gpr[4] = 0x0032A000;
    cpu.gpr[5] = 1;
    cpu.gpr[6] = 0x1A;
    cpu.gpr[7] = 2;
    const auto stop = executor.run(cpu, 1);

    const auto& scan = runtime.gx2().state().drc_scan_buffer;
    test::require(stop.category == StopCategory::instruction_budget &&
                      scan.valid && scan.address == kDrcBuffer &&
                      scan.size == 0x0032A000 && scan.mode == 1 &&
                      scan.format == 0x1A && scan.buffering == 2 &&
                      scan.width == 854 && scan.height == 480 &&
                      scan.pitch == 864,
                  "GX2SetDRCBuffer records exact observed scan metadata");
}

void test_set_scan_buffer_enable_state() {
    auto image = make_image();
    image.imports.emplace(
        kSetTvEnableImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetTVEnable"});
    image.imports.emplace(
        kSetDrcEnableImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDRCEnable"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext tv;
    tv.pc = kSetTvEnableImport;
    tv.lr = kReturn;
    tv.gpr[3] = 1;
    executor.run(tv, 1);
    CPUContext drc;
    drc.pc = kSetDrcEnableImport;
    drc.lr = kReturn;
    drc.gpr[3] = 1;
    executor.run(drc, 1);

    test::require(runtime.gx2().state().tv_enabled &&
                      runtime.gx2().state().drc_enabled,
                  "GX2 scan-output enables are retained");
}

void test_copy_color_buffer_records_scan_target() {
    auto image = make_image();
    map_rgba8_color_buffer(image);
    image.memory.write32(kColorBuffer + 0x14, 0x19, 0);
    image.memory.write_bytes(
        kColorImage,
        std::array<uint8_t, 16>{0, 0, 255, 255, 0, 255, 0, 255,
                                255, 0, 0, 255, 255, 255, 255, 255},
        0);
    image.memory.map(kTvBuffer, 0x0012C000, {true, true, false});
    image.memory.map(kDrcBuffer, 0x00195000, {true, true, false});
    image.imports.emplace(
        kSetTvBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetTVBuffer"});
    image.imports.emplace(
        kSetDrcBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDRCBuffer"});
    image.imports.emplace(
        kCopyColorToScanImport,
        nwii::runtime::ImportTarget{
            "gx2", "GX2CopyColorBufferToScanBuffer"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext set_tv;
    set_tv.pc = kSetTvBufferImport;
    set_tv.lr = kReturn;
    set_tv.gpr[3] = kTvBuffer;
    set_tv.gpr[4] = 0x0012C000;
    set_tv.gpr[5] = 1;
    set_tv.gpr[6] = 0x1A;
    set_tv.gpr[7] = 1;
    executor.run(set_tv, 1);
    CPUContext set_drc;
    set_drc.pc = kSetDrcBufferImport;
    set_drc.lr = kReturn;
    set_drc.gpr[3] = kDrcBuffer;
    set_drc.gpr[4] = 0x00195000;
    set_drc.gpr[5] = 1;
    set_drc.gpr[6] = 0x1A;
    set_drc.gpr[7] = 1;
    executor.run(set_drc, 1);

    CPUContext cpu;
    cpu.pc = kCopyColorToScanImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    cpu.gpr[4] = 1;
    auto stop = executor.run(cpu, 1);

    std::vector<uint8_t> frame(640 * 480 * 4);
    const bool copied = runtime.gx2().copy_scan_buffer(1, frame);
    const auto pixel = [&frame](uint32_t x, uint32_t y) {
        const auto offset = (y * 640 + x) * 4;
        return std::array<uint8_t, 4>{
            frame[offset], frame[offset + 1], frame[offset + 2],
            frame[offset + 3]};
    };
    test::require(
        stop.category == StopCategory::instruction_budget && copied &&
            pixel(0, 0) == std::array<uint8_t, 4>{255, 0, 0, 255} &&
            pixel(639, 0) == std::array<uint8_t, 4>{0, 255, 0, 255} &&
            pixel(0, 479) == std::array<uint8_t, 4>{0, 0, 255, 255} &&
            pixel(639, 479) ==
                std::array<uint8_t, 4>{255, 255, 255, 255} &&
            runtime.gx2().state().scan_copy_count == 1,
        "GX2CopyColorBufferToScanBuffer produces linear TV RGBA pixels");

    cpu.pc = kCopyColorToScanImport;
    cpu.gpr[4] = 4;
    stop = executor.run(cpu, 2);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().last_scan_copy.args ==
                std::array<uint32_t, 2>{kColorBuffer, 4} &&
            runtime.gx2().state().scan_copy_count == 2,
        "GX2CopyColorBufferToScanBuffer accepts the DRC target");
}

void test_set_tv_scale_records_observed_dimensions() {
    auto image = make_image();
    image.imports.emplace(
        kSetTvScaleImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetTVScale"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetTvScaleImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 1920;
    cpu.gpr[4] = 1080;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().tv_scale.valid &&
                      runtime.gx2().state().tv_scale.args ==
                          std::array<uint32_t, 2>{1920, 1080},
                  "GX2SetTVScale records observed scan dimensions");
}

void test_set_drc_scale_records_observed_dimensions() {
    auto image = make_image();
    image.imports.emplace(
        kSetDrcScaleImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDRCScale"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetDrcScaleImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 854;
    cpu.gpr[4] = 480;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().drc_scale.valid &&
                      runtime.gx2().state().drc_scale.args ==
                          std::array<uint32_t, 2>{854, 480},
                  "GX2SetDRCScale records observed scan dimensions");
}

void test_set_swap_interval_records_observed_interval() {
    auto image = make_image();
    image.imports.emplace(
        kSetSwapIntervalImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetSwapInterval"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetSwapIntervalImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 2;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().swap_interval == 2,
                  "GX2SetSwapInterval records observed interval");
}

void test_temp_get_gpu_version_returns_latte_revision() {
    auto image = make_image();
    image.imports.emplace(
        kTempGpuVersionImport,
        nwii::runtime::ImportTarget{"gx2", "GX2TempGetGPUVersion"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kTempGpuVersionImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x00000001;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 2,
                  "GX2TempGetGPUVersion returns GPU7/Latte revision 2");
}

void test_invalidate_records_aligned_range() {
    auto image = make_image();
    image.memory.map(kInvalBuffer, 0x300, {true, true, false});
    image.imports.emplace(
        kInvalidateImport,
        nwii::runtime::ImportTarget{"gx2", "GX2Invalidate"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInvalidateImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x48;  // CPU | SHADER
    cpu.gpr[4] = kInvalBuffer;
    cpu.gpr[5] = 0x270;
    const auto stop = executor.run(cpu, 1);

    const nwii::runtime::Gx2InvalidateState expected{
        true, 1, 0x48, kInvalBuffer, 0x270, 0x300};
    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().last_invalidate == expected,
                  "GX2Invalidate records mode/buffer/size and 0x100 aligned size");
}

void test_invalidate_none_mode_skips_validation() {
    auto image = make_image();
    image.imports.emplace(
        kInvalidateImport,
        nwii::runtime::ImportTarget{"gx2", "GX2Invalidate"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    CPUContext cpu;
    cpu.pc = kInvalidateImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;             // GX2_INVALIDATE_MODE_NONE
    cpu.gpr[4] = kInvalBuffer;  // unmapped
    cpu.gpr[5] = 0x100;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state() == before,
                  "GX2Invalidate with mode NONE returns without touching state");
}

void test_invalidate_unmapped_buffer_faults_atomically() {
    auto image = make_image();
    image.imports.emplace(
        kInvalidateImport,
        nwii::runtime::ImportTarget{"gx2", "GX2Invalidate"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto before = runtime.gx2().state();
    CPUContext cpu;
    cpu.pc = kInvalidateImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x48;
    cpu.gpr[4] = kInvalBuffer;  // unmapped
    cpu.gpr[5] = 0x270;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::read &&
                      stop.fault_address == kInvalBuffer &&
                      stop.fault_width == 0x270 &&
                      runtime.gx2().state() == before,
                  "GX2Invalidate faults on unmapped buffer before state change");
}

void test_calc_fetch_shader_size_matches_latte_formula() {
    auto image = make_image();
    image.imports.emplace(
        kCalcFetchShaderSizeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcFetchShaderSizeEx"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    struct Case {
        uint32_t attribs;
        uint32_t type;
        uint32_t mode;
        uint32_t size;
    };
    // NoTessellation (type 0): fetch=attribs, alu=0,
    // cf=ceil(fetch/16)+1; size=16*fetch+align_up(8*cf,16).
    // reached: (3,0,0)=48+align_up(16,16)=0x40.
    // (16,0,0): cf=2 -> 256+16=0x110. (17,0,0): cf=3 -> 272+align_up(24,16)=304.
    const Case cases[] = {
        {3, 0, 0, 0x40}, {16, 0, 0, 0x110}, {17, 0, 0, 304}};
    for (const auto& c : cases) {
        CPUContext cpu;
        cpu.pc = kCalcFetchShaderSizeImport;
        cpu.lr = kReturn;
        cpu.gpr[3] = c.attribs;
        cpu.gpr[4] = c.type;
        cpu.gpr[5] = c.mode;
        const auto stop = executor.run(cpu, 1);
        test::require(stop.category == StopCategory::instruction_budget &&
                          cpu.gpr[3] == c.size,
                      "GX2CalcFetchShaderSizeEx matches Latte size formula");
    }

    // Tessellated fetch-shader types are unimplemented and must fault
    // deterministically rather than return an unverified size.
    CPUContext tess;
    tess.pc = kCalcFetchShaderSizeImport;
    tess.lr = kReturn;
    tess.gpr[3] = 3;
    tess.gpr[4] = 1;
    tess.gpr[5] = 0;
    const auto tess_stop = executor.run(tess, 1);
    test::require(tess_stop.category == StopCategory::guest_fault,
                  "GX2CalcFetchShaderSizeEx faults on a tessellated type");
}

void write_attrib_stream(ExecutionImage& image, uint32_t index,
                         uint32_t location, uint32_t buffer, uint32_t offset,
                         uint32_t format, uint32_t type, uint32_t alu_divisor,
                         uint32_t mask, uint32_t endian_swap) {
    const uint32_t base = kAttribStreams + index * 0x20;
    image.memory.write32(base + 0x00, location, 0);
    image.memory.write32(base + 0x04, buffer, 0);
    image.memory.write32(base + 0x08, offset, 0);
    image.memory.write32(base + 0x0C, format, 0);
    image.memory.write32(base + 0x10, type, 0);
    image.memory.write32(base + 0x14, alu_divisor, 0);
    image.memory.write32(base + 0x18, mask, 0);
    image.memory.write32(base + 0x1C, endian_swap, 0);
}

CPUContext init_fetch_cpu(uint32_t attrib_count) {
    CPUContext cpu;
    cpu.pc = kInitFetchShaderImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kFetchShader;
    cpu.gpr[4] = kFetchBuffer;
    cpu.gpr[5] = attrib_count;
    cpu.gpr[6] = kAttribStreams;
    cpu.gpr[7] = 0;
    cpu.gpr[8] = 0;
    return cpu;
}

void write_three_pervertex_attribs(ExecutionImage& image) {
    // Controlled per-vertex attribute set; expectations below are derived from
    // Decaf gx2_fetchshader.cpp + latte_instructions.h, not from the impl.
    write_attrib_stream(image, 0, 0, 0, 0x00, 0x811, 0, 0, 0x00010203, 3);
    write_attrib_stream(image, 1, 1, 0, 0x0C, 0x80D, 0, 0, 0x00010405, 3);
    write_attrib_stream(image, 2, 2, 1, 0x14, 0x00A, 0, 0, 0x03020100, 0);
}

void test_init_fetch_shader_emits_exact_latte_program() {
    auto image = make_image();
    image.memory.map(kFetchShader, 0x20, {true, true, false});
    image.memory.map(kFetchBuffer, 0x40, {true, true, false});
    image.memory.map(kAttribStreams, 3 * 0x20, {true, true, false});
    image.imports.emplace(
        kInitFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitFetchShaderEx"});
    // Sentinel-fill the struct so the read-modify-write of
    // regs.sq_pgm_resources_fs (NUM_GPRS cleared, other bits preserved) shows.
    for (uint32_t offset = 0; offset < 0x20; ++offset) {
        image.memory.write8(kFetchShader + offset, 0xA5, 0);
    }
    write_three_pervertex_attribs(image);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_fetch_cpu(3);
    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitFetchShaderEx returns to caller");

    // Latte fetch-shader program (little-endian machine words):
    //   CF[0] VTX_TC: ADDR=fetchOffset/8=2, COUNT=(3-1)=2
    //   CF[1] RETURN: BARRIER=1 (0x8A000000)
    //   VFETCH x3 SEMANTIC at fetchOffset 0x10.
    const uint32_t expected[16] = {
        0x00000002, 0x01800800, 0x00000000, 0x8A000000,
        0x2C00A001, 0x2C0D1000, 0x000A0000, 0x00000000,
        0x1C00A001, 0x27961001, 0x000A000C, 0x00000000,
        0x0C00A101, 0x0680A602, 0x00080014, 0x00000000,
    };
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t word = expected[i];
        const uint32_t at = kFetchBuffer + i * 4;
        test::require(
            image.memory.read8(at + 0, 0) == (word & 0xFF) &&
                image.memory.read8(at + 1, 0) == ((word >> 8) & 0xFF) &&
                image.memory.read8(at + 2, 0) == ((word >> 16) & 0xFF) &&
                image.memory.read8(at + 3, 0) == ((word >> 24) & 0xFF),
            "fetch-shader program word matches Latte encoding");
    }
    test::require(image.memory.read32(kFetchShader + 0x00, 0) == 0,
                  "fetch shader type = NoTessellation");
    test::require(image.memory.read32(kFetchShader + 0x04, 0) == 0xA5A5A500,
                  "sq_pgm_resources_fs clears NUM_GPRS, preserves other bits");
    test::require(image.memory.read32(kFetchShader + 0x08, 0) == 0x40,
                  "fetch shader size matches GX2CalcFetchShaderSizeEx");
    test::require(image.memory.read32(kFetchShader + 0x0C, 0) == kFetchBuffer,
                  "fetch shader data points at program buffer");
    test::require(image.memory.read32(kFetchShader + 0x10, 0) == 3,
                  "fetch shader records attribute count");
    test::require(image.memory.read32(kFetchShader + 0x14, 0) == 0 &&
                      image.memory.read32(kFetchShader + 0x18, 0) == 0 &&
                      image.memory.read32(kFetchShader + 0x1C, 0) == 0,
                  "no divisors for a fully per-vertex fetch shader");
}

void test_init_fetch_shader_empty_attrib_set() {
    auto image = make_image();
    image.memory.map(kFetchShader, 0x20, {true, true, false});
    image.memory.map(kFetchBuffer, 0x10, {true, true, false});
    image.imports.emplace(
        kInitFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitFetchShaderEx"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_fetch_cpu(0);
    const auto stop = executor.run(cpu, 1);

    // Zero attributes: no VTX_TC clause, just the RETURN control-flow inst.
    test::require(stop.category == StopCategory::instruction_budget &&
                      image.memory.read8(kFetchBuffer + 0, 0) == 0 &&
                      image.memory.read8(kFetchBuffer + 4, 0) == 0 &&
                      image.memory.read8(kFetchBuffer + 5, 0) == 0 &&
                      image.memory.read8(kFetchBuffer + 6, 0) == 0 &&
                      image.memory.read8(kFetchBuffer + 7, 0) == 0x8A &&
                      image.memory.read32(kFetchShader + 0x08, 0) == 0x10 &&
                      image.memory.read32(kFetchShader + 0x10, 0) == 0,
                  "empty fetch shader emits only the RETURN clause");
}

void test_init_fetch_shader_struct_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kFetchBuffer, 0x40, {true, true, false});
    image.memory.map(kAttribStreams, 3 * 0x20, {true, true, false});
    image.imports.emplace(
        kInitFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitFetchShaderEx"});
    for (uint32_t offset = 0; offset < 0x40; ++offset) {
        image.memory.write8(kFetchBuffer + offset, 0x5A, 0);
    }
    write_three_pervertex_attribs(image);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_fetch_cpu(3);
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kFetchShader &&
                      stop.fault_width == 0x20 &&
                      image.memory.read8(kFetchBuffer + 0, 0) == 0x5A &&
                      image.memory.read8(kFetchBuffer + 0x3F, 0) == 0x5A,
                  "unmapped struct-out faults before program buffer is written");
}

void test_init_fetch_shader_buffer_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kFetchShader, 0x20, {true, true, false});
    image.memory.map(kAttribStreams, 3 * 0x20, {true, true, false});
    image.imports.emplace(
        kInitFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitFetchShaderEx"});
    for (uint32_t offset = 0; offset < 0x20; ++offset) {
        image.memory.write8(kFetchShader + offset, 0xA5, 0);
    }
    write_three_pervertex_attribs(image);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_fetch_cpu(3);
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kFetchBuffer &&
                      stop.fault_width == 0x40 &&
                      image.memory.read32(kFetchShader + 0, 0) == 0xA5A5A5A5,
                  "unmapped program buffer faults before struct is written");
}

void test_init_fetch_shader_attribs_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kFetchShader, 0x20, {true, true, false});
    image.memory.map(kFetchBuffer, 0x40, {true, true, false});
    image.imports.emplace(
        kInitFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitFetchShaderEx"});
    for (uint32_t offset = 0; offset < 0x20; ++offset) {
        image.memory.write8(kFetchShader + offset, 0xA5, 0);
    }
    for (uint32_t offset = 0; offset < 0x40; ++offset) {
        image.memory.write8(kFetchBuffer + offset, 0x5A, 0);
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    auto cpu = init_fetch_cpu(3);
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::read &&
                      stop.fault_address == kAttribStreams &&
                      stop.fault_width == 0x60 &&
                      image.memory.read32(kFetchShader + 0, 0) == 0xA5A5A5A5 &&
                      image.memory.read8(kFetchBuffer, 0) == 0x5A,
                  "unmapped attribute streams fault before any write");
}

void test_init_fetch_shader_rejects_tessellation() {
    auto image = make_image();
    image.memory.map(kFetchShader, 0x20, {true, true, false});
    image.memory.map(kFetchBuffer, 0x40, {true, true, false});
    image.memory.map(kAttribStreams, 3 * 0x20, {true, true, false});
    image.imports.emplace(
        kInitFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitFetchShaderEx"});
    for (uint32_t offset = 0; offset < 0x20; ++offset) {
        image.memory.write8(kFetchShader + offset, 0xA5, 0);
    }
    write_three_pervertex_attribs(image);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    auto tess_type = init_fetch_cpu(3);
    tess_type.gpr[7] = 1;  // LineTessellation
    const auto type_stop = executor.run(tess_type, 1);
    test::require(type_stop.category == StopCategory::guest_fault &&
                      image.memory.read32(kFetchShader + 0, 0) == 0xA5A5A5A5,
                  "tessellated fetch-shader type faults before any write");

    auto tess_mode = init_fetch_cpu(3);
    tess_mode.gpr[8] = 1;  // non-Discrete tessellation mode
    const auto mode_stop = executor.run(tess_mode, 1);
    test::require(mode_stop.category == StopCategory::guest_fault &&
                      image.memory.read32(kFetchShader + 0, 0) == 0xA5A5A5A5,
                  "non-Discrete tessellation mode faults before any write");
}

void test_init_sampler_emits_exact_sampler_registers() {
    auto image = make_image();
    image.memory.map(kSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSampler"});
    // Sentinel-fill so a full three-word overwrite is observable.
    for (uint32_t offset = 0; offset < 0x0C; ++offset) {
        image.memory.write8(kSampler + offset, 0x5A, 0);
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSampler;   // GX2Sampler* out
    cpu.gpr[4] = 2;          // GX2TexClampMode (reached: 2)
    cpu.gpr[5] = 1;          // GX2TexXYFilterMode (reached: 1)
    const auto stop = executor.run(cpu, 1);

    // Source: Decaf gx2_sampler.cpp GX2InitSampler + latte_registers_sq.h
    // SQ_TEX_SAMPLER_WORD{0,1,2}_N bit layouts. clamp=2 into CLAMP_X/Y/Z
    // (bits 0/3/6), filter=1 into XY_MAG/MIN_FILTER (bits 9/12);
    // MAX_LOD = fixed_from_data<ufixed_4_6_t>(1023) raw at bits 10-19;
    // WORD2 TYPE (bit 31) set. Stored big-endian (be2_val) at 0x00/0x04/0x08.
    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSampler returns to caller");
    test::require(image.memory.read32(kSampler + 0x00, 0) == 0x00001292,
                  "sampler word0 packs clamp X/Y/Z and mag/min filter");
    test::require(image.memory.read32(kSampler + 0x04, 0) == 0x000FFC00,
                  "sampler word1 sets MAX_LOD raw 1023");
    test::require(image.memory.read32(kSampler + 0x08, 0) == 0x80000000,
                  "sampler word2 sets TYPE");
}

void test_init_sampler_struct_fault_is_atomic() {
    auto image = make_image();
    image.imports.emplace(
        kInitSamplerImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSampler"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSampler;
    cpu.gpr[4] = 2;
    cpu.gpr[5] = 1;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kSampler &&
                      stop.fault_width == 0x0C,
                  "unmapped sampler struct-out faults before any write");
}

void test_calc_gs_input_ring_size_matches_decaf_formula() {
    auto image = make_image();
    image.imports.emplace(
        kCalcGsInputRingImport,
        nwii::runtime::ImportTarget{
            "gx2", "GX2CalcGeometryShaderInputRingBufferSize"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    // Source: Decaf gx2_shaders.cpp — returns ringItemSize * 16384 (uint32_t).
    CPUContext cpu;
    cpu.pc = kCalcGsInputRingImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x20;
    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 0x20u * 16384u,
                  "GS input ring size = ringItemSize * 16384");

    CPUContext zero;
    zero.pc = kCalcGsInputRingImport;
    zero.lr = kReturn;
    zero.gpr[3] = 0;
    const auto zero_stop = executor.run(zero, 1);
    test::require(zero_stop.category == StopCategory::instruction_budget &&
                      zero.gpr[3] == 0,
                  "zero ring item size yields zero buffer");
}

void test_calc_gs_output_ring_size_matches_decaf_formula() {
    auto image = make_image();
    image.imports.emplace(
        kCalcGsOutputRingImport,
        nwii::runtime::ImportTarget{
            "gx2", "GX2CalcGeometryShaderOutputRingBufferSize"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    // Source: Decaf gx2_shaders.cpp — returns ringItemSize * 16384 (uint32_t).
    CPUContext cpu;
    cpu.pc = kCalcGsOutputRingImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x40;
    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 0x40u * 16384u,
                  "GS output ring size = ringItemSize * 16384");
}

void test_get_context_state_display_list_returns_shadow_list() {
    auto image = make_image();
    // GX2ContextState is 0xA100 bytes; only the size field (0x9804) is read.
    image.memory.map(kContextState, 0xA100, {true, true, false});
    image.memory.map(kOutputs, 0x10, {true, true, false});
    image.imports.emplace(
        kGetContextDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GetContextStateDisplayList"});
    // Source: Decaf gx2_contextstate.h — shadowDisplayListSize at 0x9804,
    // shadowDisplayList at 0x9E00. Seed a recorded size.
    image.memory.write32(kContextState + 0x9804, 8, 0);
    // Sentinel-fill both outputs so writes are observable.
    image.memory.write32(kOutputs + 0, 0xDEADBEEF, 0);
    image.memory.write32(kOutputs + 4, 0xDEADBEEF, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kGetContextDisplayListImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kContextState;
    cpu.gpr[4] = kOutputs;      // out display-list pointer
    cpu.gpr[5] = kOutputs + 4;  // out size pointer
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2GetContextStateDisplayList returns to caller");
    test::require(
        image.memory.read32(kOutputs + 0, 0) == kContextState + 0x9E00,
        "returns address of the shadow display list");
    test::require(image.memory.read32(kOutputs + 4, 0) == 8,
                  "returns recorded shadow display-list size");
}

void test_get_context_state_display_list_skips_null_outputs() {
    auto image = make_image();
    image.memory.map(kContextState, 0xA100, {true, true, false});
    image.imports.emplace(
        kGetContextDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GetContextStateDisplayList"});
    image.memory.write32(kContextState + 0x9804, 8, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kGetContextDisplayListImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kContextState;
    cpu.gpr[4] = 0;  // NULL out display-list: skipped, no fault
    cpu.gpr[5] = 0;  // NULL out size: skipped, no fault
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "NULL out-pointers are skipped without faulting");
}

void test_get_context_state_display_list_fault_is_atomic() {
    auto image = make_image();
    image.memory.map(kContextState, 0xA100, {true, true, false});
    image.imports.emplace(
        kGetContextDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GetContextStateDisplayList"});
    image.memory.write32(kContextState + 0x9804, 8, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kGetContextDisplayListImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kContextState;
    cpu.gpr[4] = kOutputs;  // unmapped output faults
    cpu.gpr[5] = kOutputs + 4;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kOutputs,
                  "unmapped out-pointer faults before any write");
}

// Writes the reached 4x4 UNORM_R8_G8_B8_A8 (0x1A) 2D Default-tiled surface and
// checks the exact fields GX2CalcSurfaceSizeAndAlignment writes back (verified
// against the real decaf-emu/addrlib: degrade to Tiled1DThin1, size/align 256,
// pitch 8, swizzle cleared).
void test_calc_surface_writes_addrlib_size_and_alignment() {
    auto image = make_image();
    image.memory.map(kSurface, 0x74, {true, true, false});
    image.imports.emplace(
        kCalcSurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcSurfaceSizeAndAlignment"});
    image.memory.write32(kSurface + 0x00, 1, 0);      // dim = Texture2D
    image.memory.write32(kSurface + 0x04, 4, 0);      // width
    image.memory.write32(kSurface + 0x08, 4, 0);      // height
    image.memory.write32(kSurface + 0x0C, 1, 0);      // depth
    image.memory.write32(kSurface + 0x10, 1, 0);      // mipLevels
    image.memory.write32(kSurface + 0x14, 0x1A, 0);   // format RGBA8
    image.memory.write32(kSurface + 0x18, 0, 0);      // aa
    image.memory.write32(kSurface + 0x1C, 1, 0);      // use = Texture
    image.memory.write32(kSurface + 0x30, 0, 0);      // tileMode = Default
    image.memory.write32(kSurface + 0x34, 0, 0);      // swizzle

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcSurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2CalcSurfaceSizeAndAlignment returns to caller");
    test::require(image.memory.read32(kSurface + 0x10, 0) == 1,
                  "mipLevels clamped to 1");
    test::require(image.memory.read32(kSurface + 0x20, 0) == 256,
                  "imageSize matches AddrLib micro-tiled size");
    test::require(image.memory.read32(kSurface + 0x28, 0) == 0,
                  "mipmapSize is 0 for a single-level surface");
    test::require(image.memory.read32(kSurface + 0x30, 0) == 2,
                  "tileMode degraded to Tiled1DThin1");
    test::require(image.memory.read32(kSurface + 0x34, 0) == 0,
                  "swizzle cleared after degrade to 1D");
    test::require(image.memory.read32(kSurface + 0x38, 0) == 256,
                  "alignment matches AddrLib base alignment");
    test::require(image.memory.read32(kSurface + 0x3C, 0) == 8,
                  "pitch matches AddrLib micro-tiled pitch");
    test::require(image.memory.read32(kSurface + 0x40, 0) == 0,
                  "mipLevelOffset[0] is 0");
}

// Reached WWHD case. Decaf's R600 AddrLib aligns 8x8 HTILE metadata to
// 256x256 pixels, 4096 bytes total, and a 512-byte base address.
void test_calc_depth_hiz_info_writes_addrlib_size_and_alignment() {
    auto image = make_image();
    constexpr uint32_t outputs = kDepthBuffer + 0x100;
    image.memory.map(kDepthBuffer, 0xAC, {true, true, false});
    image.memory.map(outputs, 8, {true, true, false});
    image.imports.emplace(
        kCalcDepthHiZImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcDepthBufferHiZInfo"});
    image.memory.write32(kDepthBuffer + 0x08, 1024, 0);
    image.memory.write32(kDepthBuffer + 0x0C, 3, 0);
    image.memory.write32(kDepthBuffer + 0x3C, 1024, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcDepthHiZImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDepthBuffer;
    cpu.gpr[4] = outputs;
    cpu.gpr[5] = outputs + 4;
    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kDepthBuffer + 0x84, 0) == 0x30000 &&
            image.memory.read32(outputs, 0) == 0x30000 &&
            image.memory.read32(outputs + 4, 0) == 0x200,
        "GX2CalcDepthBufferHiZInfo returns the R600 AddrLib layout");
}

void test_set_surface_swizzle_replaces_only_swizzle_byte() {
    auto image = make_image();
    image.memory.map(kSurface, 0x74, {true, true, false});
    image.imports.emplace(
        kSetSurfaceSwizzleImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetSurfaceSwizzle"});
    image.memory.write32(kSurface + 0x34, 0xAABBCCDD, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetSurfaceSwizzleImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    cpu.gpr[4] = 0x5A;
    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kSurface + 0x34, 0) == 0xAABB5ADD,
        "GX2SetSurfaceSwizzle replaces bits 8 through 15");
}

void test_init_depth_buffer_hiz_enable_replaces_only_enable_bit() {
    auto image = make_image();
    image.memory.map(kDepthBuffer, 0xAC, {true, true, false});
    image.imports.emplace(
        kInitDepthHiZImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitDepthBufferHiZEnable"});
    constexpr uint32_t initial = 0xA5A5A5A5;
    image.memory.write32(kDepthBuffer + 0x98, initial, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    const auto invoke = [&](uint32_t enabled) {
        CPUContext cpu;
        cpu.pc = kInitDepthHiZImport;
        cpu.lr = kReturn;
        cpu.gpr[3] = kDepthBuffer;
        cpu.gpr[4] = enabled;
        return executor.run(cpu, 1);
    };
    test::require(
        invoke(0xFFFFFFFF).category == StopCategory::instruction_budget &&
            image.memory.read32(kDepthBuffer + 0x98, 0) ==
                (initial | (1u << 25)),
        "GX2InitDepthBufferHiZEnable sets only TILE_SURFACE_ENABLE");
    test::require(
        invoke(0).category == StopCategory::instruction_budget &&
            image.memory.read32(kDepthBuffer + 0x98, 0) == initial,
        "GX2InitDepthBufferHiZEnable clears only TILE_SURFACE_ENABLE");
}

void test_shader_resource_queries_read_packed_resource_word() {
    struct Query {
        const char* symbol;
        uint32_t expected;
    };
    constexpr std::array queries{
        Query{"GX2GetPixelShaderGPRs", 0x2D},
        Query{"GX2GetVertexShaderGPRs", 0x2D},
        Query{"GX2GetGeometryShaderGPRs", 0x2D},
        Query{"GX2GetPixelShaderStackEntries", 0x5C},
        Query{"GX2GetVertexShaderStackEntries", 0x5C},
        Query{"GX2GetGeometryShaderStackEntries", 0x5C},
    };
    auto image = make_image();
    image.memory.map(kVertexShader, 4, {true, true, false});
    image.memory.write32(kVertexShader, 0xA5B65C2D, 0);
    for (size_t index = 0; index < queries.size(); ++index) {
        image.imports.emplace(
            kShaderQueryBase + static_cast<uint32_t>(index * 8),
            nwii::runtime::ImportTarget{"gx2", queries[index].symbol});
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    for (size_t index = 0; index < queries.size(); ++index) {
        CPUContext cpu;
        cpu.pc = kShaderQueryBase + static_cast<uint32_t>(index * 8);
        cpu.lr = kReturn;
        cpu.gpr[3] = kVertexShader;
        const auto stop = executor.run(cpu, 1);
        test::require(stop.category == StopCategory::instruction_budget &&
                          cpu.gpr[3] == queries[index].expected,
                      queries[index].symbol);
    }
}

void test_begin_display_list_tracks_user_buffer() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kBeginDisplayListImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDisplayList;
    cpu.gpr[4] = 0x200;
    cpu.gpr[5] = 1;
    const auto stop = executor.run(cpu, 1);
    const auto& state = runtime.gx2().state().display_lists[0];
    test::require(
        stop.category == StopCategory::instruction_budget && state.active &&
            state.address == kDisplayList && state.capacity == 0x200 &&
            state.size == 0 && state.profiling,
        "GX2BeginDisplayListEx tracks the validated user command buffer");
}

void test_set_vertex_shader_tracks_packed_shader() {
    constexpr uint32_t shader = kDisplayList + 3;
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.imports.emplace(
        kSetVertexShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetVertexShader"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetVertexShaderImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = shader;
    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().vertex_shader_address == shader,
        "GX2SetVertexShader accepts and tracks a packed shader struct");
}

void test_set_pixel_shader_tracks_shader() {
    constexpr uint32_t shader = kDisplayList + 0x100;
    auto image = make_image();
    image.memory.map(shader, 0xE8, {true, true, false});
    image.imports.emplace(
        kSetPixelShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetPixelShader"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetPixelShaderImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = shader;
    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().pixel_shader_address == shader,
        "GX2SetPixelShader tracks the validated shader struct");
}

void test_set_fetch_shader_tracks_shader() {
    auto image = make_image();
    image.memory.map(kFetchShader, 0x20, {true, true, false});
    image.imports.emplace(
        kSetFetchShaderImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetFetchShader"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetFetchShaderImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kFetchShader;
    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().fetch_shader_address == kFetchShader,
        "GX2SetFetchShader tracks the validated shader struct");
}

void test_set_pixel_sampler_tracks_slot_and_rejects_invalid_id() {
    auto image = make_image();
    image.memory.map(kSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kSetPixelSamplerImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetPixelSampler"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetPixelSamplerImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSampler;
    cpu.gpr[4] = 17;
    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().pixel_sampler_addresses[17] == kSampler,
        "GX2SetPixelSampler tracks a validated sampler slot");

    CPUContext invalid;
    invalid.pc = kSetPixelSamplerImport;
    invalid.lr = kReturn;
    invalid.gpr[3] = kSampler;
    invalid.gpr[4] = 18;
    test::require(executor.run(invalid, 1).category ==
                      StopCategory::guest_fault &&
                      runtime.gx2().state().pixel_sampler_addresses[17] ==
                          kSampler,
                  "GX2SetPixelSampler rejects an invalid slot atomically");
}

void test_get_current_display_list_returns_active_buffer() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.memory.map(kOutputs, 8, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kGetCurrentDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GetCurrentDisplayList"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext begin;
    begin.pc = kBeginDisplayListImport;
    begin.lr = kReturn;
    begin.gpr[3] = kDisplayList;
    begin.gpr[4] = 0x200;
    test::require(
        executor.run(begin, 1).category == StopCategory::instruction_budget,
        "GX2BeginDisplayListEx activates a display list");

    CPUContext current;
    current.pc = kGetCurrentDisplayListImport;
    current.lr = kReturn;
    current.gpr[3] = kOutputs;
    current.gpr[4] = kOutputs + 4;
    const auto stop = executor.run(current, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            current.gpr[3] == 1 &&
            image.memory.read32(kOutputs, 0) == kDisplayList &&
            image.memory.read32(kOutputs + 4, 0) == 0x200,
        "GX2GetCurrentDisplayList returns active address and capacity");
}

void test_nested_display_list_restores_outer_buffer() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x300, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext outer;
    outer.pc = kBeginDisplayListImport;
    outer.lr = kReturn;
    outer.gpr[3] = kDisplayList;
    outer.gpr[4] = 0x200;
    test::require(executor.run(outer, 1).category ==
                      StopCategory::instruction_budget,
                  "outer display list begins");

    CPUContext inner;
    inner.pc = kBeginDisplayListImport;
    inner.lr = kReturn;
    inner.gpr[3] = kDisplayList + 0x200;
    inner.gpr[4] = 0x100;
    test::require(executor.run(inner, 1).category ==
                      StopCategory::instruction_budget,
                  "nested display list begins");

    CPUContext end;
    end.pc = kEndDisplayListImport;
    end.lr = kReturn;
    const auto stop = executor.run(end, 1);
    const auto& state = runtime.gx2().state().display_lists[0];
    test::require(
        stop.category == StopCategory::instruction_budget && state.active &&
            state.address == kDisplayList && state.capacity == 0x200,
        "ending nested display list restores the outer buffer");
}

void test_end_display_list_returns_size_and_clears_active_buffer() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kBeginDisplayListImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDisplayList;
    cpu.gpr[4] = 0x200;
    test::require(
        executor.run(cpu, 1).category == StopCategory::instruction_budget,
        "GX2BeginDisplayListEx activates a display list");

    CPUContext end;
    end.pc = kEndDisplayListImport;
    end.lr = kReturn;
    end.gpr[3] = kDisplayList;
    const auto stop = executor.run(end, 1);
    const auto& state = runtime.gx2().state().display_lists[0];
    test::require(
        stop.category == StopCategory::instruction_budget && end.gpr[3] == 0 &&
            !state.active && state.address == 0 && state.capacity == 0 &&
            state.size == 0,
        "GX2EndDisplayList returns used bytes and clears the user buffer");
}

void test_display_list_counts_hle_commands() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kFlushImport, nwii::runtime::ImportTarget{"gx2", "GX2Flush"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});
    image.imports.emplace(
        kCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CallDisplayList"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext begin;
    begin.pc = kBeginDisplayListImport;
    begin.lr = kReturn;
    begin.gpr[3] = kDisplayList;
    begin.gpr[4] = 0x200;
    executor.run(begin, 1);
    CPUContext flush;
    flush.pc = kFlushImport;
    flush.lr = kReturn;
    executor.run(flush, 1);
    flush.pc = kFlushImport;
    flush.instruction_count = 0;
    executor.run(flush, 1);
    CPUContext end;
    end.pc = kEndDisplayListImport;
    end.lr = kReturn;
    executor.run(end, 1);
    CPUContext call;
    call.pc = kCallDisplayListImport;
    call.lr = kReturn;
    call.gpr[3] = kDisplayList;
    call.gpr[4] = 4;
    executor.run(call, 1);

    test::require(
        end.gpr[3] == 8 && runtime.gx2().state().flush_count == 1,
        "GX2 display lists replay a command-aligned prefix");
}

void test_display_list_rerecord_reuses_transient_storage() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.memory.map(kIndexData, 0x100000, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kDrawIndexedImport,
        nwii::runtime::ImportTarget{"gx2", "GX2DrawIndexedEx"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});
    image.imports.emplace(
        kCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CallDisplayList"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    uint32_t size = 0;
    for (uint32_t iteration = 0; iteration < 257; ++iteration) {
        CPUContext begin;
        begin.pc = kBeginDisplayListImport;
        begin.lr = kReturn;
        begin.gpr[3] = kDisplayList;
        begin.gpr[4] = 0x200;
        executor.run(begin, 1);

        CPUContext draw;
        draw.pc = kDrawIndexedImport;
        draw.lr = kReturn;
        draw.gpr[3] = 0x13;
        draw.gpr[4] = 0x80000;
        draw.gpr[5] = 4;
        draw.gpr[6] = kIndexData;
        executor.run(draw, 1);

        CPUContext end;
        end.pc = kEndDisplayListImport;
        end.lr = kReturn;
        executor.run(end, 1);
        size = end.gpr[3];
    }

    CPUContext call;
    call.pc = kCallDisplayListImport;
    call.lr = kReturn;
    call.gpr[3] = kDisplayList;
    call.gpr[4] = size;
    executor.run(call, 1);
    test::require(runtime.gx2().state().draw_count == 1,
                  "re-recording a display list retires its old payload");
}

void test_display_list_captures_transient_index_data() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.memory.map(kIndexData, 8, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kDrawIndexedImport,
        nwii::runtime::ImportTarget{"gx2", "GX2DrawIndexedEx"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});
    image.imports.emplace(
        kCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CallDisplayList"});
    for (uint32_t index = 0; index < 4; ++index) {
        image.memory.write16(kIndexData + index * 2,
                             static_cast<uint16_t>(index), 0);
    }
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext begin;
    begin.pc = kBeginDisplayListImport;
    begin.lr = kReturn;
    begin.gpr[3] = kDisplayList;
    begin.gpr[4] = 0x200;
    executor.run(begin, 1);
    CPUContext draw;
    draw.pc = kDrawIndexedImport;
    draw.lr = kReturn;
    draw.gpr[3] = 0x13;
    draw.gpr[4] = 4;
    draw.gpr[5] = 4;
    draw.gpr[6] = kIndexData;
    draw.gpr[8] = 1;
    executor.run(draw, 1);
    CPUContext end;
    end.pc = kEndDisplayListImport;
    end.lr = kReturn;
    executor.run(end, 1);
    image.memory.write16(kIndexData, 0xFFFF, 0);
    CPUContext call;
    call.pc = kCallDisplayListImport;
    call.lr = kReturn;
    call.gpr[3] = kDisplayList;
    call.gpr[4] = end.gpr[3];
    executor.run(call, 1);

    const auto& replayed = runtime.gx2().state().last_draw;
    test::require(
        replayed.indices != kIndexData &&
            image.memory.read16(replayed.indices, 0) == 0 &&
            image.memory.read16(replayed.indices + 2, 0) == 1 &&
            runtime.gx2().state().draw_count == 1,
        "GX2 display lists capture transient indexed-draw data");
}

void test_display_list_captures_transient_uniform_data() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.memory.map(kUniformData, 16, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kSetVertexUniformRegImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetVertexUniformReg"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});
    image.imports.emplace(
        kCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CallDisplayList"});
    constexpr std::array<uint32_t, 4> expected{
        0x3F800000, 0x40000000, 0x40400000, 0x40800000};
    for (uint32_t index = 0; index < expected.size(); ++index) {
        image.memory.write32(kUniformData + index * 4, expected[index], 0);
    }
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext begin;
    begin.pc = kBeginDisplayListImport;
    begin.lr = kReturn;
    begin.gpr[3] = kDisplayList;
    begin.gpr[4] = 0x200;
    executor.run(begin, 1);
    CPUContext uniform;
    uniform.pc = kSetVertexUniformRegImport;
    uniform.lr = kReturn;
    uniform.gpr[3] = 28;
    uniform.gpr[4] = expected.size();
    uniform.gpr[5] = kUniformData;
    executor.run(uniform, 1);
    CPUContext end;
    end.pc = kEndDisplayListImport;
    end.lr = kReturn;
    executor.run(end, 1);
    for (uint32_t index = 0; index < expected.size(); ++index) {
        image.memory.write32(kUniformData + index * 4, 0, 0);
    }
    CPUContext call;
    call.pc = kCallDisplayListImport;
    call.lr = kReturn;
    call.gpr[3] = kDisplayList;
    call.gpr[4] = end.gpr[3];
    executor.run(call, 1);

    test::require(
        std::equal(expected.begin(), expected.end(),
                   runtime.gx2().state().vertex_uniform_registers.begin() +
                       28),
        "GX2 display lists capture transient uniform-register data");
}

void test_display_list_payloads_survive_later_replays() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x200, {true, true, false});
    image.memory.map(kBoundTexture, 0x9C, {true, true, false});
    image.imports.emplace(
        kBeginDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2BeginDisplayListEx"});
    image.imports.emplace(
        kSetPixelTextureImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetPixelTexture"});
    image.imports.emplace(
        kEndDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2EndDisplayList"});
    image.imports.emplace(
        kCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CallDisplayList"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    const auto record_and_call = [&](uint32_t list, uint32_t unit) {
        CPUContext begin;
        begin.pc = kBeginDisplayListImport;
        begin.lr = kReturn;
        begin.gpr[3] = list;
        begin.gpr[4] = 0x100;
        executor.run(begin, 1);
        CPUContext bind;
        bind.pc = kSetPixelTextureImport;
        bind.lr = kReturn;
        bind.gpr[3] = kBoundTexture;
        bind.gpr[4] = unit;
        executor.run(bind, 1);
        CPUContext end;
        end.pc = kEndDisplayListImport;
        end.lr = kReturn;
        executor.run(end, 1);
        CPUContext call;
        call.pc = kCallDisplayListImport;
        call.lr = kReturn;
        call.gpr[3] = list;
        call.gpr[4] = end.gpr[3];
        executor.run(call, 1);
    };

    image.memory.write32(kBoundTexture + 0x34, 0x12345678, 0);
    record_and_call(kDisplayList, 3);
    const uint32_t first =
        runtime.gx2().state().pixel_texture_addresses[3];
    image.memory.write32(kBoundTexture + 0x34, 0x87654321, 0);
    record_and_call(kDisplayList + 0x100, 4);

    test::require(
        first != kBoundTexture &&
            image.memory.read32(first + 0x34, 0) == 0x12345678 &&
            runtime.gx2().state().pixel_texture_addresses[3] == first,
        "GX2 display-list payloads remain immutable across later replays");
}

void test_call_display_list_validates_guest_commands() {
    auto image = make_image();
    image.memory.map(kDisplayList, 0x20, {true, true, false});
    image.imports.emplace(
        kCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CallDisplayList"});
    image.imports.emplace(
        kDirectCallDisplayListImport,
        nwii::runtime::ImportTarget{"gx2", "GX2DirectCallDisplayList"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext valid;
    valid.pc = kCallDisplayListImport;
    valid.lr = kReturn;
    valid.gpr[3] = kDisplayList;
    valid.gpr[4] = 0x18;
    test::require(executor.run(valid, 1).category ==
                      StopCategory::instruction_budget,
                  "GX2CallDisplayList accepts mapped command bytes");

    CPUContext invalid;
    invalid.pc = kDirectCallDisplayListImport;
    invalid.lr = kReturn;
    invalid.gpr[3] = kInvalBuffer;
    invalid.gpr[4] = 0x10;
    test::require(executor.run(invalid, 1).category ==
                      StopCategory::guest_fault,
                  "GX2CallDisplayList rejects unmapped command bytes");
}


void test_copy_surface_linear_special_to_micro_tiled() {
    auto image = make_image();
    constexpr uint32_t destination_surface = kSurface + 0x100;
    image.memory.map(kSurface, 0x174, {true, true, false});
    image.memory.map(kCopySource, 0x200, {true, true, false});
    image.memory.map(kCopyDestination, 0x200, {true, true, false});
    image.imports.emplace(
        kCopySurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CopySurface"});

    for (const auto [surface, pixels, tile_mode, alignment] :
         std::array{
             std::array<uint32_t, 4>{kSurface, kCopySource, 16, 1},
             std::array<uint32_t, 4>{destination_surface, kCopyDestination, 2,
                                     0x100}}) {
        image.memory.write32(surface + 0x00, 1, 0);
        image.memory.write32(surface + 0x04, 16, 0);
        image.memory.write32(surface + 0x08, 8, 0);
        image.memory.write32(surface + 0x0C, 1, 0);
        image.memory.write32(surface + 0x10, 1, 0);
        image.memory.write32(surface + 0x14, 0x1A, 0);
        image.memory.write32(surface + 0x18, 0, 0);
        image.memory.write32(surface + 0x1C, 1, 0);
        image.memory.write32(surface + 0x20, 0x200, 0);
        image.memory.write32(surface + 0x24, pixels, 0);
        image.memory.write32(surface + 0x30, tile_mode, 0);
        image.memory.write32(surface + 0x38, alignment, 0);
        image.memory.write32(surface + 0x3C, 16, 0);
    }
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            image.memory.write32(kCopySource + (y * 16 + x) * 4,
                                 (y << 16) | x, 0);
        }
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCopySurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    cpu.gpr[6] = destination_surface;
    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2CopySurface returns to caller");

    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            const uint32_t pixel_index =
                (x & 1) | (x & 2) | ((y & 1) << 2) |
                ((x & 4) << 1) | ((y & 2) << 3) | ((y & 4) << 3);
            const uint32_t offset = (x >> 3) * 0x100 + pixel_index * 4;
            test::require(
                image.memory.read32(kCopyDestination + offset, 0) ==
                    ((y << 16) | x),
                "GX2CopySurface preserves pixels through 1D micro-tiling");
        }
    }
}
void test_copy_surface_reached_rg8_linear_special_to_micro_tiled() {
    auto image = make_image();
    constexpr uint32_t destination_surface = kSurface + 0x100;
    image.memory.map(kSurface, 0x174, {true, true, false});
    image.memory.map(kCopySource, 0x20, {true, true, false});
    image.memory.map(kCopyDestination, 0x100, {true, true, false});
    image.imports.emplace(
        kCopySurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CopySurface"});

    for (const auto [surface, pixels, tile_mode, image_size, pitch] :
         std::array{
             std::array<uint32_t, 5>{kSurface, kCopySource, 16, 0x20, 4},
             std::array<uint32_t, 5>{destination_surface, kCopyDestination, 2,
                                     0x100, 16}}) {
        image.memory.write32(surface + 0x00, 1, 0);
        image.memory.write32(surface + 0x04, 4, 0);
        image.memory.write32(surface + 0x08, 4, 0);
        image.memory.write32(surface + 0x10, 3, 0);
        image.memory.write32(surface + 0x14, 0x07, 0);
        image.memory.write32(surface + 0x18, 0, 0);
        image.memory.write32(surface + 0x20, image_size, 0);
        image.memory.write32(surface + 0x24, pixels, 0);
        image.memory.write32(surface + 0x30, tile_mode, 0);
        image.memory.write32(surface + 0x3C, pitch, 0);
    }
    for (uint32_t pixel = 0; pixel < 16; ++pixel) {
        image.memory.write16(kCopySource + pixel * 2,
                             static_cast<uint16_t>(pixel + 1), 0);
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCopySurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    cpu.gpr[6] = destination_surface;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "reached RG8 GX2CopySurface returns to caller");
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t pixel_index = (x & 7u) | ((y & 7u) << 3);
            test::require(
                image.memory.read16(kCopyDestination + pixel_index * 2, 0) ==
                    y * 4 + x + 1,
                "GX2CopySurface preserves RG8 pixels through micro-tiling");
        }
    }
}
void test_copy_surface_reached_rgba8_linear_special_to_macro_tiled() {
    auto image = make_image();
    constexpr uint32_t destination_surface = kSurface + 0x100;
    image.memory.map(kSurface, 0x174, {true, true, false});
    image.memory.map(kCopySource, 0x800, {true, true, false});
    image.memory.map(kCopyDestination, 0x800, {true, true, false});
    image.imports.emplace(
        kCopySurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CopySurface"});

    for (const auto [surface, pixels, tile_mode] :
         std::array{
             std::array<uint32_t, 3>{kSurface, kCopySource, 16},
             std::array<uint32_t, 3>{destination_surface, kCopyDestination, 4}}) {
        image.memory.write32(surface + 0x00, 1, 0);
        image.memory.write32(surface + 0x04, 32, 0);
        image.memory.write32(surface + 0x08, 16, 0);
        image.memory.write32(surface + 0x14, 0x080E, 0);
        image.memory.write32(surface + 0x20, 0x800, 0);
        image.memory.write32(surface + 0x24, pixels, 0);
        image.memory.write32(surface + 0x30, tile_mode, 0);
        image.memory.write32(surface + 0x3C, 32, 0);
    }
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            image.memory.write32(kCopySource + (y * 32 + x) * 4,
                                 (y << 16) | x, 0);
        }
    }

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCopySurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    cpu.gpr[6] = destination_surface;
    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget,
                  "reached macro-tiled GX2CopySurface returns to caller");

    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const uint32_t pixel =
                (x & 1u) | (x & 2u) | ((y & 1u) << 2) |
                ((x & 4u) << 1) | ((y & 2u) << 3) | ((y & 4u) << 3);
            const uint32_t pipe = ((y >> 3) ^ (x >> 3)) & 1u;
            const uint32_t bank =
                (((y >> 5) ^ (x >> 3)) & 1u) |
                ((((y >> 4) ^ (x >> 4)) & 1u) << 1);
            const uint32_t total = pixel * 4;
            const uint32_t offset = ((total & ~0xFFu) << 3) |
                                    (bank << 9) | (pipe << 8) |
                                    (total & 0xFFu);
            test::require(
                image.memory.read32(kCopyDestination + offset, 0) ==
                    ((y << 16) | x),
                "GX2CopySurface preserves pixels through macro-tiling");
        }
    }
}



// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on the 4x4x6
// UNORM_R8_G8_B8_A8 (0x1A) TextureCube (dim=3) at 0x3BC358E8 in the WWHD trace.
// Six faces are sized as array slices (numSlices=max(6,depth)); the surface
// degrades to Tiled1DThin1 -> imageSize 1536 (8*8*6*4). Ground truth from the
// real decaf-emu/addrlib with the cube flag set.
void test_calc_surface_cube_writes_six_slice_size() {
    auto image = make_image();
    image.memory.map(kSurface, 0x74, {true, true, false});
    image.imports.emplace(
        kCalcSurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcSurfaceSizeAndAlignment"});
    image.memory.write32(kSurface + 0x00, 3, 0);      // dim = TextureCube
    image.memory.write32(kSurface + 0x04, 4, 0);      // width
    image.memory.write32(kSurface + 0x08, 4, 0);      // height
    image.memory.write32(kSurface + 0x0C, 6, 0);      // depth = 6 faces
    image.memory.write32(kSurface + 0x10, 1, 0);      // mipLevels
    image.memory.write32(kSurface + 0x14, 0x1A, 0);   // format RGBA8
    image.memory.write32(kSurface + 0x18, 0, 0);      // aa
    image.memory.write32(kSurface + 0x1C, 1, 0);      // use = Texture
    image.memory.write32(kSurface + 0x30, 0, 0);      // tileMode = Default
    image.memory.write32(kSurface + 0x34, 0, 0);      // swizzle

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcSurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2CalcSurfaceSizeAndAlignment cube returns to caller");
    test::require(image.memory.read32(kSurface + 0x20, 0) == 1536,
                  "cube imageSize is 6 faces of the micro-tiled slice");
    test::require(image.memory.read32(kSurface + 0x30, 0) == 2,
                  "cube tileMode degraded to Tiled1DThin1");
    test::require(image.memory.read32(kSurface + 0x38, 0) == 256,
                  "cube alignment matches AddrLib base alignment");
    test::require(image.memory.read32(kSurface + 0x3C, 0) == 8,
                  "cube pitch matches AddrLib micro-tiled pitch");
}

void test_calc_surface_struct_fault_is_atomic() {
    auto image = make_image();
    // Map only the struct header so the reads and setup succeed but the
    // full-struct write-range preflight faults.
    image.memory.map(kSurface, 0x38, {true, true, false});
    image.imports.emplace(
        kCalcSurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcSurfaceSizeAndAlignment"});
    image.memory.write32(kSurface + 0x00, 1, 0);
    image.memory.write32(kSurface + 0x04, 4, 0);
    image.memory.write32(kSurface + 0x08, 4, 0);
    image.memory.write32(kSurface + 0x0C, 1, 0);
    image.memory.write32(kSurface + 0x10, 1, 0);
    image.memory.write32(kSurface + 0x14, 0x1A, 0);
    image.memory.write32(kSurface + 0x1C, 1, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcSurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kSurface &&
                      stop.fault_width == 0x74 &&
                      image.memory.read32(kSurface + 0x20, 0) == 0,
                  "read-only surface struct faults before any write");
}

void test_calc_surface_rejects_unreached_layout() {
    auto image = make_image();
    image.memory.map(kSurface, 0x74, {true, true, false});
    image.imports.emplace(
        kCalcSurfaceImport,
        nwii::runtime::ImportTarget{"gx2", "GX2CalcSurfaceSizeAndAlignment"});
    image.memory.write32(kSurface + 0x00, 1, 0);
    image.memory.write32(kSurface + 0x04, 4, 0);
    image.memory.write32(kSurface + 0x08, 4, 0);
    image.memory.write32(kSurface + 0x0C, 1, 0);
    image.memory.write32(kSurface + 0x10, 1, 0);
    image.memory.write32(kSurface + 0x14, 0x24, 0);   // unsupported format
    image.memory.write32(kSurface + 0x1C, 1, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kCalcSurfaceImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSurface;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      image.memory.read32(kSurface + 0x20, 0) == 0,
                  "unreached surface format stops without writing a size");
}

// Reached authenticated case: GX2InitTextureRegs on the 4x4 UNORM_R8_G8_B8_A8
// (0x1A) 2D texture produced by GX2CalcSurfaceSizeAndAlignment (Tiled1DThin1,
// pitch 8), compMap 0x00010203. Source: Decaf gx2_texture.cpp GX2InitTextureRegs
// + latte_registers_sq.h word layouts. Verified word packing.
void test_init_texture_regs_emits_exact_resource_words() {
    auto image = make_image();
    image.memory.map(kTexture, 0x9C, {true, true, false});
    image.imports.emplace(
        kInitTextureImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitTextureRegs"});
    image.memory.write32(kTexture + 0x00, 1, 0);       // dim = Texture2D
    image.memory.write32(kTexture + 0x04, 4, 0);       // width
    image.memory.write32(kTexture + 0x08, 4, 0);       // height
    image.memory.write32(kTexture + 0x0C, 1, 0);       // depth
    image.memory.write32(kTexture + 0x10, 1, 0);       // mipLevels
    image.memory.write32(kTexture + 0x14, 0x1A, 0);    // format RGBA8
    image.memory.write32(kTexture + 0x18, 0, 0);       // aa
    image.memory.write32(kTexture + 0x1C, 1, 0);       // use = Texture
    image.memory.write32(kTexture + 0x30, 2, 0);       // tileMode = 1DThin1
    image.memory.write32(kTexture + 0x3C, 8, 0);       // pitch
    image.memory.write32(kTexture + 0x74, 0, 0);       // viewFirstMip
    image.memory.write32(kTexture + 0x78, 1, 0);       // viewNumMips
    image.memory.write32(kTexture + 0x7C, 0, 0);       // viewFirstSlice
    image.memory.write32(kTexture + 0x80, 1, 0);       // viewNumSlices
    image.memory.write32(kTexture + 0x84, 0x00010203, 0);  // compMap

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitTextureImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kTexture;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitTextureRegs returns to caller");
    test::require(image.memory.read32(kTexture + 0x88, 0) == 0x00180011,
                  "word0 packs DIM/TILE_MODE/PITCH/TEX_WIDTH");
    test::require(image.memory.read32(kTexture + 0x8C, 0) == 0x68000003,
                  "word1 packs TEX_HEIGHT/TEX_DEPTH/DATA_FORMAT");
    test::require(image.memory.read32(kTexture + 0x90, 0) == 0x06888000,
                  "word4 packs REQUEST_SIZE and RGBA dst-select");
    test::require(image.memory.read32(kTexture + 0x94, 0) == 0x00000000,
                  "word5 packs single-level single-array view");
    test::require(image.memory.read32(kTexture + 0x98, 0) == 0x800000F0,
                  "word6 packs aniso/perf/VALID_TEXTURE type");
}

void test_init_texture_regs_struct_fault_is_atomic() {
    auto image = make_image();
    // Map only the header so reads/setup succeed but the write preflight faults.
    image.memory.map(kTexture, 0x88, {true, true, false});
    image.imports.emplace(
        kInitTextureImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitTextureRegs"});
    image.memory.write32(kTexture + 0x04, 4, 0);
    image.memory.write32(kTexture + 0x08, 4, 0);
    image.memory.write32(kTexture + 0x0C, 1, 0);
    image.memory.write32(kTexture + 0x10, 1, 0);
    image.memory.write32(kTexture + 0x14, 0x1A, 0);
    image.memory.write32(kTexture + 0x30, 2, 0);
    image.memory.write32(kTexture + 0x3C, 8, 0);
    image.memory.write32(kTexture + 0x78, 1, 0);
    image.memory.write32(kTexture + 0x80, 1, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitTextureImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kTexture;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kTexture &&
                      stop.fault_width == 0x9C,
                  "short texture struct faults before any register write");
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerClamping — replaces only the
// CLAMP_X/Y/Z fields of word0, preserving every other bit. Distinct clamp
// values (2/3/4) into an all-ones word0 prove the field replace and preserve.
void test_init_sampler_clamping_replaces_clamp_fields() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerClampingImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerClamping"});
    image.memory.write32(kClampSampler + 0x00, 0xFFFFFFFF, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerClampingImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.gpr[4] = 2;  // clampX
    cpu.gpr[5] = 3;  // clampY
    cpu.gpr[6] = 4;  // clampZ
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerClamping returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x00, 0) == 0xFFFFFF1A,
                  "clamp X/Y/Z replaced, all other word0 bits preserved");
}

void test_init_sampler_clamping_fault_is_atomic() {
    auto image = make_image();
    image.imports.emplace(
        kInitSamplerClampingImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerClamping"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerClampingImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.gpr[4] = 2;
    cpu.gpr[5] = 2;
    cpu.gpr[6] = 2;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault &&
                      stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kClampSampler,
                  "unmapped sampler word0 faults before any write");
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerXYFilter — replaces only
// XY_MAG_FILTER / XY_MIN_FILTER / MAX_ANISO_RATIO in word0, preserving all
// other bits (verified against an all-ones word0).
void test_init_sampler_xy_filter_replaces_filter_fields() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerXYFilterImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerXYFilter"});
    image.memory.write32(kClampSampler + 0x00, 0xFFFFFFFF, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerXYFilterImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.gpr[4] = 1;  // filterMag = BILINEAR
    cpu.gpr[5] = 1;  // filterMin = BILINEAR
    cpu.gpr[6] = 0;  // maxAniso = 1:1
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerXYFilter returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x00, 0) == 0xFFC793FF,
                  "mag/min filter and aniso replaced, other word0 bits kept");
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerZMFilter — replaces only Z_FILTER
// / MIP_FILTER in word0, preserving all other bits.
void test_init_sampler_zm_filter_replaces_filter_fields() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerZMFilterImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerZMFilter"});
    image.memory.write32(kClampSampler + 0x00, 0xFFFFFFFF, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerZMFilterImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.gpr[4] = 0;  // filterZ = NONE
    cpu.gpr[5] = 2;  // filterMip = LINEAR
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerZMFilter returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x00, 0) == 0xFFFC7FFF,
                  "Z/MIP filter replaced, other word0 bits preserved");
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerLOD. Float args arrive in fpr1-3.
// Reached case: lodMin 0, lodMax 14, lodBias 0 -> MAX_LOD raw 896 (14*64).
void test_init_sampler_lod_packs_reached_fixed_point() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerLODImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerLOD"});
    image.memory.write32(kClampSampler + 0x04, 0x000FFC00, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerLODImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.fpr[1][0] = std::bit_cast<uint64_t>(0.0);
    cpu.fpr[2][0] = std::bit_cast<uint64_t>(14.0);
    cpu.fpr[3][0] = std::bit_cast<uint64_t>(0.0);
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerLOD returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x04, 0) == 0x000E0000,
                  "MAX_LOD packs 14.0 as ufixed_4_6 raw 896");
}

// Fractional + signed-bias case defends the *64 fixed-point scaling and the
// 12-bit two's-complement LOD_BIAS field: 0.25/1.5/-1.0 -> MIN 16, MAX 96,
// BIAS -64 (0xFC0).
void test_init_sampler_lod_scales_fractional_and_signed_bias() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerLODImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerLOD"});

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerLODImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.fpr[1][0] = std::bit_cast<uint64_t>(0.25);
    cpu.fpr[2][0] = std::bit_cast<uint64_t>(1.5);
    cpu.fpr[3][0] = std::bit_cast<uint64_t>(-1.0);
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerLOD returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x04, 0) == 0xFC018010,
                  "MIN/MAX_LOD scale by 64 and LOD_BIAS is signed 12-bit");
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerDepthCompare — replaces only
// DEPTH_COMPARE_FUNCTION (bits 26-28) of word0, preserving all other bits.
void test_init_sampler_depth_compare_replaces_field() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerDepthCompareImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerDepthCompare"});
    image.memory.write32(kClampSampler + 0x00, 0xFFFFFFFF, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerDepthCompareImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.gpr[4] = 6;  // GEQUAL
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerDepthCompare returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x00, 0) == 0xFBFFFFFF,
                  "depth-compare function replaced, other word0 bits preserved");
}

// Source: Decaf gx2_sampler.cpp GX2InitSamplerBorderType — replaces only
// BORDER_COLOR_TYPE (bits 22-23) of word0, preserving all other bits.
void test_init_sampler_border_type_replaces_field() {
    auto image = make_image();
    image.memory.map(kClampSampler, 0x0C, {true, true, false});
    image.imports.emplace(
        kInitSamplerBorderTypeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitSamplerBorderType"});
    image.memory.write32(kClampSampler + 0x00, 0xFFFFFFFF, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitSamplerBorderTypeImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kClampSampler;
    cpu.gpr[4] = 1;  // OPAQUE_BLACK
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitSamplerBorderType returns to caller");
    test::require(image.memory.read32(kClampSampler + 0x00, 0) == 0xFF7FFFFF,
                  "border-color type replaced, other word0 bits preserved");
}

void test_draw_indexed_records_validated_submission() {
    auto image = make_image();
    image.memory.map(kIndexData, 8, {true, true, false});
    image.imports.emplace(
        kDrawIndexedImport,
        nwii::runtime::ImportTarget{"gx2", "GX2DrawIndexedEx"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kDrawIndexedImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x13;
    cpu.gpr[4] = 4;
    cpu.gpr[5] = 4;
    cpu.gpr[6] = kIndexData;
    cpu.gpr[7] = 0;
    cpu.gpr[8] = 1;

    const auto stop = executor.run(cpu, 1);
    const auto& draw = runtime.gx2().state().last_draw;
    test::require(
        stop.category == StopCategory::instruction_budget && draw.valid &&
            draw.indexed && draw.mode == 0x13 && draw.count == 4 &&
            draw.index_type == 4 && draw.indices == kIndexData &&
            draw.instances == 1 && runtime.gx2().state().draw_count == 1,
        "GX2DrawIndexedEx records one validated indexed draw");
}

void test_set_attrib_buffer_tracks_vertex_data() {
    auto image = make_image();
    image.memory.map(kAttribData, 0x20, {true, true, false});
    image.imports.emplace(
        kSetAttribBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetAttribBuffer"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetAttribBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 2;
    cpu.gpr[4] = 0x20;
    cpu.gpr[5] = 8;
    cpu.gpr[6] = kAttribData;

    const auto stop = executor.run(cpu, 1);
    const auto& binding = runtime.gx2().state().attribute_buffers[2];
    test::require(
        stop.category == StopCategory::instruction_budget && binding.valid &&
            binding.address == kAttribData && binding.size == 0x20 &&
            binding.stride == 8,
        "GX2SetAttribBuffer tracks address, size, and stride by index");
}

void test_set_pixel_texture_tracks_descriptor_by_unit() {
    auto image = make_image();
    image.memory.map(kBoundTexture, 0x9C, {true, true, false});
    image.imports.emplace(
        kSetPixelTextureImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetPixelTexture"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetPixelTextureImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kBoundTexture;
    cpu.gpr[4] = 3;

    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().pixel_texture_addresses[3] == kBoundTexture,
        "GX2SetPixelTexture tracks the descriptor by texture unit");

    cpu.pc = kSetPixelTextureImport;
    cpu.gpr[3] = 0;
    cpu.instruction_count = 0;
    const auto unbind = executor.run(cpu, 1);
    test::require(unbind.category == StopCategory::instruction_budget,
                  "GX2SetPixelTexture null unbind returns to caller");
    test::require(runtime.gx2().state().pixel_texture_addresses[3] == 0,
                  "GX2SetPixelTexture accepts null to unbind a texture unit");
}

void test_set_vertex_uniform_reg_copies_guest_words() {
    auto image = make_image();
    image.memory.map(kUniformData, 16, {true, true, false});
    image.imports.emplace(
        kSetVertexUniformRegImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetVertexUniformReg"});
    constexpr std::array<uint32_t, 4> values{
        0x3F800000, 0x40000000, 0x40400000, 0x40800000};
    for (uint32_t index = 0; index < values.size(); ++index) {
        image.memory.write32(kUniformData + index * 4, values[index], 0);
    }
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetVertexUniformRegImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 28;
    cpu.gpr[4] = values.size();
    cpu.gpr[5] = kUniformData;

    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().vertex_uniforms_valid &&
            std::equal(values.begin(), values.end(),
                       runtime.gx2().state().vertex_uniform_registers.begin() +
                           28),
        "GX2SetVertexUniformReg copies big-endian guest constants");
}
void test_set_vertex_uniform_block_tracks_guest_buffer() {
    auto image = make_image();
    image.memory.map(kUniformData, 64, {true, true, false});
    image.imports.emplace(
        kSetVertexUniformBlockImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetVertexUniformBlock"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetVertexUniformBlockImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = 3;
    cpu.gpr[4] = 64;
    cpu.gpr[5] = kUniformData;

    const auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().vertex_uniform_blocks[3] ==
                Gx2UniformBlockState{true, kUniformData, 64},
        "GX2SetVertexUniformBlock records its guest buffer");
}


void test_set_shader_mode_records_reached_resources() {
    auto image = make_image();
    image.imports.emplace(
        kSetShaderModeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetShaderModeEx"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetShaderModeImport;
    cpu.lr = kReturn;
    constexpr std::array<uint32_t, 7> expected{0, 48, 64, 0, 0, 200, 192};
    for (uint32_t index = 0; index < expected.size(); ++index) {
        cpu.gpr[3 + index] = expected[index];
    }

    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().shader_mode.valid &&
                      runtime.gx2().state().shader_mode.args == expected,
                  "GX2SetShaderModeEx records shader resource allocation");
}

// Reached WWHD 1920x1080 FLOAT_R32 depth buffer with HiZ enabled.
void test_init_depth_buffer_regs_writes_reached_latte_words() {
    auto image = make_image();
    image.memory.map(kDepthBuffer, 0xAC, {true, true, false});
    image.imports.emplace(
        kInitDepthBufferRegsImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitDepthBufferRegs"});
    image.imports.emplace(
        kSetDepthBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetDepthBuffer"});
    image.memory.write32(kDepthBuffer + 0x00, 1, 0);
    image.memory.write32(kDepthBuffer + 0x04, 1920, 0);
    image.memory.write32(kDepthBuffer + 0x08, 1080, 0);
    image.memory.write32(kDepthBuffer + 0x0C, 1, 0);
    image.memory.write32(kDepthBuffer + 0x10, 1, 0);
    image.memory.write32(kDepthBuffer + 0x14, 0x80E, 0);
    image.memory.write32(kDepthBuffer + 0x18, 0, 0);
    image.memory.write32(kDepthBuffer + 0x1C, 4, 0);
    image.memory.write32(kDepthBuffer + 0x20, 0x7F8000, 0);
    image.memory.write32(kDepthBuffer + 0x30, 4, 0);
    image.memory.write32(kDepthBuffer + 0x34, 0xD0100, 0);
    image.memory.write32(kDepthBuffer + 0x38, 0x800, 0);
    image.memory.write32(kDepthBuffer + 0x3C, 1920, 0);
    image.memory.write32(kDepthBuffer + 0x7C, 1, 0);
    image.memory.write32(kDepthBuffer + 0x80, 0x2D000000, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitDepthBufferRegsImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDepthBuffer;
    auto stop = executor.run(cpu, 1);
    cpu.pc = kSetDepthBufferImport;
    stop = executor.run(cpu, 2);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kDepthBuffer + 0x90, 0) == 0x01FDFCEF &&
            image.memory.read32(kDepthBuffer + 0x94, 0) == 0 &&
            image.memory.read32(kDepthBuffer + 0x98, 0) == 0x0202000E &&
            image.memory.read32(kDepthBuffer + 0x9C, 0) == 0xB &&
            image.memory.read32(kDepthBuffer + 0xA0, 0) == 0x86 &&
            image.memory.read32(kDepthBuffer + 0xA4, 0) == 0x213C0000 &&
            image.memory.read32(kDepthBuffer + 0xA8, 0) == 0x1E9 &&
            runtime.gx2().state().depth_buffer_address == kDepthBuffer,
        "GX2 depth buffer initialization and binding preserve Latte state");
}

void test_clear_buffers_records_color_and_depth_commands() {
    auto image = make_image();
    map_rgba8_color_buffer(image);
    image.memory.map(kDepthBuffer, 0xAC, {true, true, false});
    image.imports.emplace(
        kClearBuffersImport,
        nwii::runtime::ImportTarget{"gx2", "GX2ClearBuffersEx"});
    constexpr std::array<float, 5> values{
        0.125f, 0.25f, 0.5f, 1.0f, 0.75f};
    CafeRuntime runtime(image);
    uint32_t color_clear_events = 0;
    runtime.gx2().attach_event_callback(
        &color_clear_events,
        [](void* context, nwii::runtime::Gx2Event event,
           const nwii::runtime::Gx2State&) {
            if (event == nwii::runtime::Gx2Event::clear_color) {
                ++*static_cast<uint32_t*>(context);
            }
        });
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kClearBuffersImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    cpu.gpr[4] = kDepthBuffer;
    cpu.gpr[5] = 0x5A;
    cpu.gpr[6] = 3;
    for (size_t index = 0; index < values.size(); ++index) {
        cpu.fpr[index + 1][0] =
            std::bit_cast<uint64_t>(static_cast<double>(values[index]));
    }

    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().last_color_clear.valid &&
            runtime.gx2().state().last_color_clear.args ==
                std::array<uint32_t, 5>{
                    kColorBuffer, std::bit_cast<uint32_t>(values[0]),
                    std::bit_cast<uint32_t>(values[1]),
                    std::bit_cast<uint32_t>(values[2]),
                    std::bit_cast<uint32_t>(values[3])} &&
            runtime.gx2().state().color_clear_count == 1,
        "GX2ClearBuffersEx records its color clear");
    test::require(
        runtime.gx2().state().last_depth_stencil_clear.valid &&
            runtime.gx2().state().last_depth_stencil_clear.args ==
                std::array<uint32_t, 4>{
                    kDepthBuffer, std::bit_cast<uint32_t>(values[4]), 0x5A, 3} &&
            runtime.gx2().state().depth_stencil_clear_count == 1,
        "GX2ClearBuffersEx records its depth-stencil clear");
    test::require(color_clear_events == 1,
                  "GX2ClearBuffersEx emits one color-clear event");
}

void test_clear_depth_stencil_records_command() {
    auto image = make_image();
    image.memory.map(kDepthBuffer, 0xAC, {true, true, false});
    image.imports.emplace(
        kClearDepthImport,
        nwii::runtime::ImportTarget{"gx2", "GX2ClearDepthStencilEx"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kClearDepthImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDepthBuffer;
    cpu.gpr[4] = 0x5A;
    cpu.gpr[5] = 3;
    constexpr float depth = 0.75f;
    cpu.fpr[1][0] =
        std::bit_cast<uint64_t>(static_cast<double>(depth));

    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().last_depth_stencil_clear.valid &&
            runtime.gx2().state().last_depth_stencil_clear.args ==
                std::array<uint32_t, 4>{
                    kDepthBuffer, std::bit_cast<uint32_t>(depth), 0x5A, 3} &&
            runtime.gx2().state().depth_stencil_clear_count == 1,
        "GX2ClearDepthStencilEx records its depth-stencil clear");
}

void test_clear_color_records_validated_command() {
    auto image = make_image();
    map_rgba8_color_buffer(image);
    image.imports.emplace(
        kClearColorImport,
        nwii::runtime::ImportTarget{"gx2", "GX2ClearColor"});
    constexpr std::array<float, 4> values{0.25f, 0.5f, 0.75f, 1.0f};
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kClearColorImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    for (size_t index = 0; index < values.size(); ++index) {
        cpu.fpr[index + 1][0] =
            std::bit_cast<uint64_t>(static_cast<double>(values[index]));
    }

    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().last_color_clear.valid &&
            runtime.gx2().state().last_color_clear.args ==
                std::array<uint32_t, 5>{
                    kColorBuffer, std::bit_cast<uint32_t>(values[0]),
                    std::bit_cast<uint32_t>(values[1]),
                    std::bit_cast<uint32_t>(values[2]),
                    std::bit_cast<uint32_t>(values[3])} &&
            runtime.gx2().state().color_clear_count == 1,
        "GX2ClearColor records a validated clear command");
    std::array<uint8_t, 16> pixels{};
    image.memory.read_bytes(kColorImage, pixels, 0);
    test::require(
        pixels == std::array<uint8_t, 16>{
                      64, 128, 191, 255, 64, 128, 191, 255,
                      64, 128, 191, 255, 64, 128, 191, 255},
        "GX2ClearColor writes RGBA8 surface memory");
}

void test_set_clear_depth_stencil_updates_descriptor() {
    auto image = make_image();
    image.memory.map(kDepthBuffer, 0xAC, {true, true, false});
    image.imports.emplace(
        kSetClearDepthStencilImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetClearDepthStencil"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetClearDepthStencilImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kDepthBuffer;
    cpu.gpr[4] = 0x5A;
    cpu.fpr[1][0] = std::bit_cast<uint64_t>(0.75);

    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kDepthBuffer + 0x88, 0) ==
                std::bit_cast<uint32_t>(0.75f) &&
            image.memory.read32(kDepthBuffer + 0x8C, 0) == 0x5A,
        "GX2SetClearDepthStencil updates both descriptor clear values");
}

void test_init_color_buffer_regs_uses_selected_mip_layout() {
    auto image = make_image();
    image.memory.map(kColorBuffer, 0x9C, {true, true, false});
    image.imports.emplace(
        kInitColorBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitColorBufferRegs"});
    image.memory.write32(kColorBuffer + 0x00, 1, 0);
    image.memory.write32(kColorBuffer + 0x04, 480, 0);
    image.memory.write32(kColorBuffer + 0x08, 270, 0);
    image.memory.write32(kColorBuffer + 0x0C, 1, 0);
    image.memory.write32(kColorBuffer + 0x10, 4, 0);
    image.memory.write32(kColorBuffer + 0x14, 0x816, 0);
    image.memory.write32(kColorBuffer + 0x18, 0, 0);
    image.memory.write32(kColorBuffer + 0x1C, 2, 0);
    image.memory.write32(kColorBuffer + 0x30, 4, 0);
    image.memory.write32(kColorBuffer + 0x34, 0xD0000, 0);
    image.memory.write32(kColorBuffer + 0x74, 1, 0);
    image.memory.write32(kColorBuffer + 0x7C, 1, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitColorBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2InitColorBufferRegs accepts the selected mip");
    test::require(image.memory.read32(kColorBuffer + 0x88, 0) == 0x0008FC1F,
                  "selected color mip uses its own pitch and height");
    test::require(image.memory.read32(kColorBuffer + 0x8C, 0) == 0x0A007458,
                  "selected color mip retains format register fields");
}

// Reached WWHD TV buffer: 1920x1080 UNORM_R10_G10_B10_A2 (0x19),
// macro-tiled at pitch 1920 and aligned height 1088.
void test_init_color_buffer_regs_writes_reached_latte_words() {
    auto image = make_image();
    image.memory.map(kColorBuffer, 0x9C, {true, true, false});
    image.imports.emplace(
        kInitColorBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitColorBufferRegs"});
    image.memory.write32(kColorBuffer + 0x00, 1, 0);
    image.memory.write32(kColorBuffer + 0x04, 1920, 0);
    image.memory.write32(kColorBuffer + 0x08, 1080, 0);
    image.memory.write32(kColorBuffer + 0x0C, 1, 0);
    image.memory.write32(kColorBuffer + 0x10, 1, 0);
    image.memory.write32(kColorBuffer + 0x14, 0x19, 0);
    image.memory.write32(kColorBuffer + 0x18, 0, 0);
    image.memory.write32(kColorBuffer + 0x1C, 2, 0);
    image.memory.write32(kColorBuffer + 0x20, 0x7F8000, 0);
    image.memory.write32(kColorBuffer + 0x30, 4, 0);
    image.memory.write32(kColorBuffer + 0x34, 0xD0000, 0);
    image.memory.write32(kColorBuffer + 0x38, 0x800, 0);
    image.memory.write32(kColorBuffer + 0x3C, 1920, 0);
    image.memory.write32(kColorBuffer + 0x7C, 1, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitColorBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kColorBuffer + 0x88, 0) == 0x01FDFCEF &&
            image.memory.read32(kColorBuffer + 0x8C, 0) == 0x0802046C &&
            image.memory.read32(kColorBuffer + 0x90, 0) == 0 &&
            image.memory.read32(kColorBuffer + 0x94, 0) == 0,
        "GX2InitColorBufferRegs emits the reached Latte register words");
}

void test_init_color_buffer_regs_supports_rgba8() {
    auto image = make_image();
    image.memory.map(kColorBuffer, 0x9C, {true, true, false});
    image.imports.emplace(
        kInitColorBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2InitColorBufferRegs"});
    image.memory.write32(kColorBuffer + 0x00, 1, 0);
    image.memory.write32(kColorBuffer + 0x04, 4, 0);
    image.memory.write32(kColorBuffer + 0x08, 4, 0);
    image.memory.write32(kColorBuffer + 0x0C, 1, 0);
    image.memory.write32(kColorBuffer + 0x10, 1, 0);
    image.memory.write32(kColorBuffer + 0x14, 0x1A, 0);
    image.memory.write32(kColorBuffer + 0x18, 0, 0);
    image.memory.write32(kColorBuffer + 0x1C, 2, 0);
    image.memory.write32(kColorBuffer + 0x20, 256, 0);
    image.memory.write32(kColorBuffer + 0x30, 2, 0);
    image.memory.write32(kColorBuffer + 0x38, 256, 0);
    image.memory.write32(kColorBuffer + 0x3C, 8, 0);
    image.memory.write32(kColorBuffer + 0x78, 2, 0);
    image.memory.write32(kColorBuffer + 0x7C, 3, 0);

    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kInitColorBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    const auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kColorBuffer + 0x88, 0) == 0 &&
            image.memory.read32(kColorBuffer + 0x8C, 0) == 0x08000268 &&
            image.memory.read32(kColorBuffer + 0x90, 0) == 0x00008002,
        "GX2InitColorBufferRegs derives registers for RGBA8");
}

void test_set_color_buffer_regs_tracks_bound_target() {
    auto image = make_image();
    image.memory.map(kColorBuffer, 0x9C, {true, true, false});
    image.imports.emplace(
        kSetColorBufferImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetColorBufferRegs"});
    image.imports.emplace(
        kSetColorBufferPublicImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetColorBuffer"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kSetColorBufferImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kColorBuffer;
    cpu.gpr[4] = 3;

    auto stop = executor.run(cpu, 1);
    cpu.pc = kSetColorBufferPublicImport;
    cpu.gpr[4] = 4;
    stop = executor.run(cpu, 2);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().color_buffers[3].valid &&
            runtime.gx2().state().color_buffers[3].args ==
                std::array<uint32_t, 2>{kColorBuffer, 3} &&
            runtime.gx2().state().color_buffers[4].valid &&
            runtime.gx2().state().color_buffers[4].args ==
                std::array<uint32_t, 2>{kColorBuffer, 4},
        "GX2 color buffer exports track descriptors by render target");
}

void test_flush_submits_pending_command_state() {
    auto image = make_image();
    image.imports.emplace(
        kFlushImport, nwii::runtime::ImportTarget{"gx2", "GX2Flush"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kFlushImport;
    cpu.lr = kReturn;

    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().flush_count == 1,
                  "GX2Flush records one submitted command batch");
}

void test_draw_done_flushes_and_reports_completion() {
    auto image = make_image();
    image.imports.emplace(
        kDrawDoneImport, nwii::runtime::ImportTarget{"gx2", "GX2DrawDone"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kDrawDoneImport;
    cpu.lr = kReturn;

    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 1 &&
                      runtime.gx2().state().flush_count == 1,
                  "GX2DrawDone flushes and completes without a host GPU");
}

void test_set_context_state_tracks_bound_shadow_state() {
    auto image = make_image();
    image.memory.map(kContextState, 0xA100, {true, true, false});
    image.imports.emplace(
        kSetContextStateImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SetContextState"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.lr = kReturn;
    cpu.gpr[3] = kContextState;
    cpu.pc = kSetContextStateImport;

    auto stop = executor.run(cpu, 1);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().context_state_address == kContextState,
        "GX2SetContextState binds the supplied shadow state");

    cpu.gpr[3] = 0;
    cpu.pc = kSetContextStateImport;
    stop = executor.run(cpu, 2);
    test::require(
        stop.category == StopCategory::instruction_budget &&
            runtime.gx2().state().context_state_address == 0,
        "GX2SetContextState null disables state shadowing");
}

void test_gpu_cycle_queries_return_retirable_values() {
    auto image = make_image();
    image.memory.map(kOutputs, 16, {true, true, false});
    image.imports.emplace(
        kSampleTopImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SampleTopGPUCycle"});
    image.imports.emplace(
        kSampleBottomImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SampleBottomGPUCycle"});
    image.imports.emplace(
        kGpuTimeImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GPUTimeToCPUTime"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.lr = kReturn;

    cpu.gpr[3] = kOutputs;
    cpu.pc = kSampleTopImport;
    auto stop = executor.run(cpu, 1);
    cpu.gpr[3] = kOutputs + 8;
    cpu.pc = kSampleBottomImport;
    stop = executor.run(cpu, 2);
    cpu.gpr[3] = 0x11223344;
    cpu.gpr[4] = 0x55667788;
    cpu.pc = kGpuTimeImport;
    stop = executor.run(cpu, 3);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read64(kOutputs, 0) == 1 &&
            image.memory.read64(kOutputs + 8, 0) == 1 &&
            cpu.gpr[3] == 0x11223344 && cpu.gpr[4] == 0x55667788,
        "GPU cycle samples are retirable and GPU-to-CPU time is identity");
}

void test_wait_for_vsync_completes_host_frame_interval() {
    auto image = make_image();
    image.imports.emplace(
        kWaitForVsyncImport,
        nwii::runtime::ImportTarget{"gx2", "GX2WaitForVsync"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kWaitForVsyncImport;
    cpu.lr = kReturn;

    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget &&
                      runtime.gx2().state().vsync_wait_count == 1,
                  "GX2WaitForVsync records one completed host interval");
}

void test_get_swap_status_reports_headless_initial_state() {
    auto image = make_image();
    image.memory.map(kOutputs, 24, {true, true, false});
    for (uint32_t offset = 0; offset < 24; offset += 4) {
        image.memory.write32(kOutputs + offset, UINT32_MAX, 0);
    }
    image.imports.emplace(
        kGetSwapStatusImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GetSwapStatus"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kGetSwapStatusImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kOutputs;
    cpu.gpr[4] = kOutputs + 4;
    cpu.gpr[5] = kOutputs + 8;
    cpu.gpr[6] = kOutputs + 16;

    auto stop = executor.run(cpu, 1);

    test::require(
        stop.category == StopCategory::instruction_budget &&
            image.memory.read32(kOutputs, 0) == 0 &&
            image.memory.read32(kOutputs + 4, 0) == 0 &&
            image.memory.read64(kOutputs + 8, 0) == 0 &&
            image.memory.read64(kOutputs + 16, 0) == 0,
        "GX2GetSwapStatus reports no headless swaps or flips");
    cpu.pc = kGetSwapStatusImport;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0;
    cpu.gpr[5] = 0;
    cpu.gpr[6] = 0;
    stop = executor.run(cpu, 2);
    test::require(stop.category == StopCategory::instruction_budget,
                  "GX2GetSwapStatus accepts null output pointers");
}

void test_swap_scan_buffers_advances_status() {
    auto image = make_image();
    image.memory.map(kOutputs, 24, {true, true, false});
    image.imports.emplace(
        kWaitForVsyncImport,
        nwii::runtime::ImportTarget{"gx2", "GX2WaitForVsync"});
    image.imports.emplace(
        kSwapScanBuffersImport,
        nwii::runtime::ImportTarget{"gx2", "GX2SwapScanBuffers"});
    image.imports.emplace(
        kGetSwapStatusImport,
        nwii::runtime::ImportTarget{"gx2", "GX2GetSwapStatus"});
    CafeRuntime runtime(image);
    Executor executor(image);
    runtime.register_imports(executor);

    CPUContext wait;
    wait.pc = kWaitForVsyncImport;
    wait.lr = kReturn;
    executor.run(wait, 1);
    CPUContext swap;
    swap.pc = kSwapScanBuffersImport;
    swap.lr = kReturn;
    executor.run(swap, 1);
    CPUContext status;
    status.pc = kGetSwapStatusImport;
    status.lr = kReturn;
    status.gpr[3] = kOutputs;
    status.gpr[4] = kOutputs + 4;
    status.gpr[5] = kOutputs + 8;
    status.gpr[6] = kOutputs + 16;
    executor.run(status, 1);

    test::require(
        image.memory.read32(kOutputs, 0) == 1 &&
            image.memory.read32(kOutputs + 4, 0) == 1 &&
            image.memory.read64(kOutputs + 8, 0) != 0 &&
            image.memory.read64(kOutputs + 16, 0) != 0,
        "GX2SwapScanBuffers advances completed headless presentation");
}

} // namespace

int main() {
    test_gx2_init_parses_attributes_and_initializes_subsystems();
    test_gx2_init_applies_modeled_default_state();
    test_gx2_init_attribute_fault_is_atomic();
    test_gx2_init_pool_fault_is_atomic();
    test_depth_stencil_control_reads_full_cafe_abi();
    test_depth_stencil_stack_fault_is_atomic();
    test_depth_stencil_stack_range_preflight();
    test_stencil_mask_updates_indexed_values();
    test_polygon_control_reads_overflow_argument();
    test_color_control_updates_state();
    test_blend_control_updates_target_state();
    test_blend_constant_uses_floating_argument_channel();
    test_viewport_uses_six_floating_arguments();
    test_alpha_test_uses_mixed_argument_channels();
    test_target_channel_masks_preserve_all_targets();
    test_alpha_to_mask_updates_state();
    test_setup_context_state_writes_valid_shadow_list();
    test_setup_context_state_range_preflight();
    test_setup_context_state_fault_is_atomic();
    test_calc_tv_size_writes_observed_scan_buffer_size();
    test_calc_tv_size_preflights_both_outputs();
    test_calc_tv_size_rejects_unsupported_layout();
    test_calc_drc_size_writes_observed_scan_buffer_size();
    test_set_tv_buffer_records_observed_scan_buffer();
    test_set_tv_buffer_preflights_complete_range();
    test_set_drc_buffer_records_observed_scan_buffer();
    test_set_scan_buffer_enable_state();
    test_copy_color_buffer_records_scan_target();
    test_set_tv_scale_records_observed_dimensions();
    test_set_drc_scale_records_observed_dimensions();
    test_set_swap_interval_records_observed_interval();
    test_temp_get_gpu_version_returns_latte_revision();
    test_invalidate_records_aligned_range();
    test_invalidate_none_mode_skips_validation();
    test_invalidate_unmapped_buffer_faults_atomically();
    test_calc_fetch_shader_size_matches_latte_formula();
    test_init_fetch_shader_emits_exact_latte_program();
    test_init_fetch_shader_empty_attrib_set();
    test_init_fetch_shader_struct_fault_is_atomic();
    test_init_fetch_shader_buffer_fault_is_atomic();
    test_init_fetch_shader_attribs_fault_is_atomic();
    test_init_fetch_shader_rejects_tessellation();
    test_init_sampler_emits_exact_sampler_registers();
    test_init_sampler_struct_fault_is_atomic();
    test_calc_gs_output_ring_size_matches_decaf_formula();
    test_calc_gs_input_ring_size_matches_decaf_formula();
    test_get_context_state_display_list_returns_shadow_list();
    test_get_context_state_display_list_skips_null_outputs();
    test_get_context_state_display_list_fault_is_atomic();
    test_calc_surface_writes_addrlib_size_and_alignment();
    test_calc_depth_hiz_info_writes_addrlib_size_and_alignment();
    test_set_surface_swizzle_replaces_only_swizzle_byte();
    test_init_depth_buffer_hiz_enable_replaces_only_enable_bit();
    test_shader_resource_queries_read_packed_resource_word();
    test_begin_display_list_tracks_user_buffer();
    test_copy_surface_linear_special_to_micro_tiled();
    test_copy_surface_reached_rg8_linear_special_to_micro_tiled();
    test_set_vertex_shader_tracks_packed_shader();
    test_calc_surface_cube_writes_six_slice_size();
    test_copy_surface_reached_rgba8_linear_special_to_macro_tiled();
    test_set_pixel_shader_tracks_shader();
    test_set_fetch_shader_tracks_shader();
    test_set_pixel_sampler_tracks_slot_and_rejects_invalid_id();
    test_get_current_display_list_returns_active_buffer();
    test_nested_display_list_restores_outer_buffer();
    test_calc_surface_struct_fault_is_atomic();
    test_end_display_list_returns_size_and_clears_active_buffer();
    test_display_list_counts_hle_commands();
    test_display_list_rerecord_reuses_transient_storage();
    test_display_list_captures_transient_index_data();
    test_display_list_payloads_survive_later_replays();
    test_display_list_captures_transient_uniform_data();
    test_call_display_list_validates_guest_commands();
    test_calc_surface_rejects_unreached_layout();
    test_init_texture_regs_emits_exact_resource_words();
    test_init_texture_regs_struct_fault_is_atomic();
    test_init_sampler_clamping_replaces_clamp_fields();
    test_draw_indexed_records_validated_submission();
    test_set_attrib_buffer_tracks_vertex_data();
    test_init_sampler_clamping_fault_is_atomic();
    test_init_sampler_xy_filter_replaces_filter_fields();
    test_init_sampler_zm_filter_replaces_filter_fields();
    test_init_sampler_lod_packs_reached_fixed_point();
    test_init_sampler_lod_scales_fractional_and_signed_bias();
    test_init_sampler_depth_compare_replaces_field();
    test_init_sampler_border_type_replaces_field();
    test_set_vertex_uniform_reg_copies_guest_words();
    test_set_vertex_uniform_block_tracks_guest_buffer();
    test_set_shader_mode_records_reached_resources();
    test_set_pixel_texture_tracks_descriptor_by_unit();
    test_init_depth_buffer_regs_writes_reached_latte_words();
    test_clear_buffers_records_color_and_depth_commands();
    test_clear_depth_stencil_records_command();
    test_clear_color_records_validated_command();
    test_set_clear_depth_stencil_updates_descriptor();
    test_init_color_buffer_regs_uses_selected_mip_layout();
    test_init_color_buffer_regs_writes_reached_latte_words();
    test_init_color_buffer_regs_supports_rgba8();
    test_set_color_buffer_regs_tracks_bound_target();
    test_gpu_cycle_queries_return_retirable_values();
    test_wait_for_vsync_completes_host_frame_interval();
    test_get_swap_status_reports_headless_initial_state();
    test_swap_scan_buffers_advances_status();
    test_set_context_state_tracks_bound_shadow_state();
    test_flush_submits_pending_command_state();
    test_draw_done_flushes_and_reports_completion();
    return 0;
}
