#include "runtime/latte_surface.h"
#include "test_support.h"

#include <cstdint>
#include <limits>
#include <variant>

namespace {
using nwii::runtime::LatteScanBufferLayout;
using nwii::runtime::LatteSurfaceComputeError;
using nwii::runtime::LatteSurfaceComputeErrorCode;
using nwii::runtime::LatteSurfaceDescriptor;
using nwii::runtime::LatteSurfaceError;
using nwii::runtime::LatteSurfaceErrorCode;
using nwii::runtime::LatteSurfaceLayout;
using nwii::runtime::calculate_surface_size_and_alignment;
using nwii::runtime::calculate_tv_scan_buffer;
using nwii::runtime::calculate_drc_scan_buffer;
using nwii::runtime::checked_surface_byte_count;

void test_wide_1080p_rgba8_double_scan_buffer() {
    const auto result = calculate_tv_scan_buffer(5, 0x41A, 2);
    test::require(std::holds_alternative<LatteScanBufferLayout>(result),
                  "observed TV scan-buffer layout is supported");
    const auto& layout = std::get<LatteScanBufferLayout>(result);
    test::require(layout.width == 1920 && layout.height == 1080 &&
                      layout.bytes_per_element == 4 &&
                      layout.buffer_count == 2 && layout.size == 0x00FD2000 &&
                      layout.alignment == 0,
                  "GX2CalcTVSize matches WUT/Decaf for observed arguments");
}

void test_double_drc_rgba8_scan_buffer() {
    const auto result = calculate_drc_scan_buffer(1, 0x1A, 2);
    test::require(std::holds_alternative<LatteScanBufferLayout>(result),
                  "observed DRC scan-buffer layout is supported");
    const auto& layout = std::get<LatteScanBufferLayout>(result);
    test::require(layout.width == 864 && layout.height == 480 &&
                      layout.bytes_per_element == 4 &&
                      layout.buffer_count == 2 && layout.size == 0x0032A000 &&
                      layout.alignment == 0,
                  "GX2CalcDRCSize matches WUT/Decaf for observed arguments");
}

void test_scan_buffer_rejects_unobserved_layout_enums() {
    const auto mode = calculate_tv_scan_buffer(0, 0x1A, 2);
    const auto format = calculate_tv_scan_buffer(5, 0x19, 2);
    const auto buffering = calculate_tv_scan_buffer(5, 0x1A, 0);
    const auto drc_mode = calculate_drc_scan_buffer(3, 0x1A, 2);

    test::require(
        std::get<LatteSurfaceError>(mode).code ==
                LatteSurfaceErrorCode::unsupported_mode &&
            std::get<LatteSurfaceError>(format).code ==
                LatteSurfaceErrorCode::unsupported_format &&
            std::get<LatteSurfaceError>(buffering).code ==
                LatteSurfaceErrorCode::unsupported_buffering &&
            std::get<LatteSurfaceError>(drc_mode).code ==
                LatteSurfaceErrorCode::unsupported_mode,
        "scan sizing returns structured errors for unsupported enums");
}

void test_surface_byte_count_rejects_overflow() {
    const auto result = checked_surface_byte_count(
        std::numeric_limits<uint32_t>::max(), 2, 1, 1, 1);
    test::require(
        std::holds_alternative<LatteSurfaceErrorCode>(result) &&
            std::get<LatteSurfaceErrorCode>(result) ==
                LatteSurfaceErrorCode::overflow,
        "surface sizing rejects uint32 overflow");
}

// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on a 4x4
// UNORM_R8_G8_B8_A8 (0x1A) 2D texture with tileMode Default. Decaf promotes
// Default -> Tiled2DThin1; AddrLib macro alignment (pitchAlign 32, heightAlign
// 16) is larger than the surface, so GX2's own degrade check downgrades to
// Tiled1DThin1 and recomputes. Ground truth captured against the real
// decaf-emu/addrlib (gbAddrConfig 0x44902, chip R7XX).
void test_surface_reached_rgba8_4x4_degrades_to_micro_tiled() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 4, 4, 1, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x4 RGBA8 surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{2, 0, 256, 0, 256, 8, 1, {}},
                  "4x4 RGBA8 degrades to Tiled1DThin1 with size/align 256");
}

void test_surface_reached_4x4_mip_chain_clamps_to_three_levels() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 4, 4, 1, 13, 0x07, 0, 1, 2, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x4 R8_G8 mip chain is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout.image_size == 256 && layout.mipmap_size == 512 &&
                      layout.mip_levels == 3 &&
                      layout.mip_level_offsets[0] == 256,
                  "mip count, image/mipmap sizes, and first offset match AddrLib");
}

void test_surface_reached_bc4_mip_chain_uses_block_dimensions() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 512, 512, 1, 10, 0x34, 0, 1, 4, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 512x512 BC4 mip chain is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(
        layout.image_size == 131072 && layout.mipmap_size == 46080 &&
            layout.alignment == 4096 && layout.pitch == 128 &&
            layout.mip_levels == 10 && layout.swizzle == 0x30000 &&
            layout.mip_level_offsets[0] == 131072 &&
            layout.mip_level_offsets[2] == 40960 &&
            layout.mip_level_offsets[8] == 45568,
        "BC4 block sizing and level-3 micro-tile transition match AddrLib");
}

// A larger 2D surface exceeds the macro alignment, so it stays Tiled2DThin1
// and keeps the macro swizzle bit; exercises the non-degrade branch.
void test_surface_large_rgba8_stays_macro_tiled() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 64, 64, 1, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "64x64 RGBA8 surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(
        layout == LatteSurfaceLayout{4, 0xD0000, 16384, 0, 2048, 64, 1, {}},
        "64x64 RGBA8 stays Tiled2DThin1 with macro size/align");
}

// An 8bpp (UNORM_R8, 0x01) 4x4 texture uses a wider macro pitch alignment
// (128), still degrades to Tiled1DThin1, but with the 8bpp micro pitch (32).
void test_surface_r8_4x4_micro_pitch_matches_bpp() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 4, 4, 1, 1, 0x01, 0, 1, 0, 0});
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{2, 0, 256, 0, 256, 32, 1, {}},
                  "4x4 R8 degrades to Tiled1DThin1 with 8bpp micro pitch");
}

// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on a 4x1
// UNORM_R8_G8_B8_A8 (0x1A) Texture1D (dim=0). A plain 1D non-AA non-depth
// surface is NOT promoted (Decaf's dim||aa||depth test is false), so it stays
// LinearAligned with no tile-mode-changed degrade. Ground truth from the real
// decaf-emu/addrlib: pitch 64, height 1, 32bpp -> 64*1*4 = 256 bytes.
void test_surface_reached_1d_4x1_stays_linear_aligned() {
    const auto result = calculate_surface_size_and_alignment(
        {0, 4, 1, 1, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x1 RGBA8 1D surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{1, 0, 256, 0, 256, 64, 1, {}},
                  "1D texture stays LinearAligned pitch 64 size 256");
}

// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on a 4x4x6
// UNORM_R8_G8_B8_A8 (0x1A) TextureCube (dim=3). Decaf's getSurfaceInfo treats
// the 6 faces as array slices (numSlices=max(6,depth)) with the AddrLib cube
// flag set; Default promotes to Tiled2DThin1 then degrades to Tiled1DThin1 for
// the tiny faces. Ground truth captured against the real decaf-emu/addrlib
// (gbAddrConfig 0x44902, chip R7XX): pitch 8, height 8, 6 slices, 32bpp ->
// 8*8*6*4 = 1536 bytes.
void test_surface_reached_cube_4x4x6_uses_six_slices() {
    const auto result = calculate_surface_size_and_alignment(
        {3, 4, 4, 6, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x4x6 RGBA8 cube surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{2, 0, 1536, 0, 256, 8, 1, {}},
                  "cube degrades to Tiled1DThin1 with 6-slice size 1536");
}

// depth < 6 is still promoted to 6 faces: a cube with depth 1 must size the
// full 6 slices (numSlices = max(6, depth)).
void test_surface_cube_depth_below_six_promotes_to_six() {
    const auto small = calculate_surface_size_and_alignment(
        {3, 4, 4, 1, 1, 0x1A, 0, 1, 0, 0});
    const auto six = calculate_surface_size_and_alignment(
        {3, 4, 4, 6, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::get<LatteSurfaceLayout>(small) ==
                      std::get<LatteSurfaceLayout>(six),
                  "cube depth<6 sizes the same as 6 faces");
}

// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on a Texture1DArray
// (dim=4) 4x4x1 RGBA8. Decaf's getSurfaceInfo forces height 1 and uses depth
// slices; unlike a plain 1D texture this dim is non-zero so it promotes to
// Tiled2DThin1 then degrades to Tiled1DThin1 for the tiny face. Ground truth
// from the real decaf-emu/addrlib: pitch 8, height 8, 1 slice, 32bpp -> 256.
void test_surface_reached_1d_array_forces_height_one() {
    const auto result = calculate_surface_size_and_alignment(
        {4, 4, 4, 1, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x4x1 RGBA8 1D-array surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{2, 0, 256, 0, 256, 8, 1, {}},
                  "1D array forces height 1 and degrades to Tiled1DThin1");
}

// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on a Texture2DArray
// (dim=5) 4x4x1 RGBA8. Decaf's getSurfaceInfo keeps the real height and uses
// depth slices; promotes to Tiled2DThin1 then degrades to Tiled1DThin1 for the
// tiny face. Ground truth from the real decaf-emu/addrlib: pitch 8, height 8,
// 1 slice, 32bpp -> 256.
void test_surface_reached_2d_array_uses_depth_slices() {
    const auto result = calculate_surface_size_and_alignment(
        {5, 4, 4, 1, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x4x1 RGBA8 2D-array surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{2, 0, 256, 0, 256, 8, 1, {}},
                  "2D array keeps height and degrades to Tiled1DThin1");
}

// Reached authenticated case: GX2CalcSurfaceSizeAndAlignment on a Texture3D
// (dim=2) 4x4x1 RGBA8 non-color surface. Decaf promotes a 3D non-color Default
// surface to Tiled2DThick, which degrades to Tiled1DThick for the tiny face.
// The thick path (thickness 4) pads the slice count up to 4, so the size is
// 8*8*4*4 = 1024. Ground truth from the real decaf-emu/addrlib.
void test_surface_reached_3d_uses_thick_tiling() {
    const auto result = calculate_surface_size_and_alignment(
        {2, 4, 4, 1, 1, 0x1A, 0, 1, 0, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached 4x4x1 RGBA8 3D surface is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout == LatteSurfaceLayout{3, 0, 1024, 0, 256, 8, 1, {}},
                  "3D degrades to Tiled1DThick with slice-padded size 1024");
}

void test_surface_reached_explicit_1d_thin_16x8() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 16, 8, 1, 1, 0x1A, 0, 1, 2, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached explicit Tiled1DThin1 surface is supported");
    test::require(
        std::get<LatteSurfaceLayout>(result) ==
            LatteSurfaceLayout{2, 0, 512, 0, 256, 16, 1, {}},
        "explicit Tiled1DThin1 keeps its resolved AddrLib layout");
}

void test_surface_reached_linear_special_16x8_is_tightly_packed() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 16, 8, 1, 1, 0x1A, 0, 1, 16, 0xD1234});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached LinearSpecial surface is supported");
    test::require(
        std::get<LatteSurfaceLayout>(result) ==
            LatteSurfaceLayout{16, 0xD1234 & 0xFF00FFFF, 512, 0, 1, 16, 1, {}},
        "LinearSpecial keeps tight pitch, byte alignment, and masked swizzle");
}

void test_surface_reached_explicit_2d_mip_chain() {
    const auto result = calculate_surface_size_and_alignment(
        {1, 256, 2, 1, 13, 0x080E, 0, 1, 4, 0});
    test::require(std::holds_alternative<LatteSurfaceLayout>(result),
                  "reached explicit Tiled2DThin1 mip chain is supported");
    const auto& layout = std::get<LatteSurfaceLayout>(result);
    test::require(layout.tile_mode == 4 && layout.image_size == 16384 &&
                      layout.alignment == 2048 && layout.pitch == 256 &&
                      layout.mip_levels == 9,
                  "explicit Tiled2DThin1 level zero matches AddrLib");
}

void test_surface_rejects_unreached_combos() {
    const auto aa = calculate_surface_size_and_alignment(
        {1, 4, 4, 1, 1, 0x1A, 1, 1, 0, 0});
    const auto tile = calculate_surface_size_and_alignment(
        {1, 4, 4, 1, 1, 0x1A, 0, 1, 5, 0});
    const auto format = calculate_surface_size_and_alignment(
        {1, 4, 4, 1, 1, 0x24, 0, 1, 0, 0});
    const auto dim = calculate_surface_size_and_alignment(
        {6, 4, 4, 1, 1, 0x1A, 0, 1, 0, 0});  // Texture2DMSAA (unreached)
    test::require(
        std::get<LatteSurfaceComputeError>(aa).code ==
                LatteSurfaceComputeErrorCode::unsupported_aa &&
            std::get<LatteSurfaceComputeError>(tile).code ==
                LatteSurfaceComputeErrorCode::unsupported_tile_mode &&
            std::get<LatteSurfaceComputeError>(format).code ==
                LatteSurfaceComputeErrorCode::unsupported_format &&
            std::get<LatteSurfaceComputeError>(dim).code ==
                LatteSurfaceComputeErrorCode::unsupported_dim,
        "surface sizing returns structured errors for unreached combos");
}
} // namespace

int main() {
    test_wide_1080p_rgba8_double_scan_buffer();
    test_double_drc_rgba8_scan_buffer();
    test_scan_buffer_rejects_unobserved_layout_enums();
    test_surface_byte_count_rejects_overflow();
    test_surface_reached_rgba8_4x4_degrades_to_micro_tiled();
    test_surface_large_rgba8_stays_macro_tiled();
    test_surface_r8_4x4_micro_pitch_matches_bpp();
    test_surface_reached_1d_4x1_stays_linear_aligned();
    test_surface_reached_4x4_mip_chain_clamps_to_three_levels();
    test_surface_reached_bc4_mip_chain_uses_block_dimensions();
    test_surface_reached_cube_4x4x6_uses_six_slices();
    test_surface_cube_depth_below_six_promotes_to_six();
    test_surface_reached_1d_array_forces_height_one();
    test_surface_reached_2d_array_uses_depth_slices();
    test_surface_reached_3d_uses_thick_tiling();
    test_surface_reached_explicit_1d_thin_16x8();
    test_surface_reached_linear_special_16x8_is_tightly_packed();
    test_surface_reached_explicit_2d_mip_chain();
    test_surface_rejects_unreached_combos();
    return 0;
}
