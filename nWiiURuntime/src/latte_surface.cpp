#include "runtime/latte_surface.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace nwii::runtime {
LatteByteCountResult
checked_surface_byte_count(uint32_t width, uint32_t height, uint32_t depth,
                           uint32_t bytes_per_element, uint32_t samples) {
    uint64_t size = 1;
    for (const uint32_t factor :
         std::array{width, height, depth, bytes_per_element, samples}) {
        if (factor != 0 &&
            size > std::numeric_limits<uint64_t>::max() / factor) {
            return LatteSurfaceErrorCode::overflow;
        }
        size *= factor;
    }
    if (size > std::numeric_limits<uint32_t>::max()) {
        return LatteSurfaceErrorCode::overflow;
    }
    return static_cast<uint32_t>(size);
}

namespace {
LatteSurfaceError error(LatteSurfaceErrorCode code, uint32_t mode,
                        uint32_t format, uint32_t buffering) {
    return {code, mode, format, buffering};
}
LatteScanBufferResult calculate_scan_buffer(uint32_t width, uint32_t height,
                                             uint32_t mode, uint32_t format,
                                             uint32_t buffering) {
    if ((format & 0x3Fu) != 0x1A) {
        return error(LatteSurfaceErrorCode::unsupported_format, mode, format,
                     buffering);
    }
    if (buffering < 1 || buffering > 3) {
        return error(LatteSurfaceErrorCode::unsupported_buffering, mode, format,
                     buffering);
    }
    constexpr uint32_t bytes_per_element = 4;
    const auto size =
        checked_surface_byte_count(width, height, 1, bytes_per_element,
                                   buffering);
    if (const auto* code = std::get_if<LatteSurfaceErrorCode>(&size)) {
        return error(*code, mode, format, buffering);
    }
    return LatteScanBufferLayout{width,
                                 height,
                                 bytes_per_element,
                                 buffering,
                                 std::get<uint32_t>(size),
                                 0};
}
} // namespace

LatteScanBufferResult calculate_tv_scan_buffer(uint32_t mode, uint32_t format,
                                                uint32_t buffering) {
    std::pair<uint32_t, uint32_t> dimensions;
    switch (mode) {
    case 1:
        dimensions = {640, 480};
        break;
    case 2:
        dimensions = {854, 480};
        break;
    case 3:
    case 4:
        dimensions = {1280, 720};
        break;
    case 5:
        dimensions = {1920, 1080};
        break;
    default:
        return error(LatteSurfaceErrorCode::unsupported_mode, mode, format,
                     buffering);
    }
    return calculate_scan_buffer(dimensions.first, dimensions.second, mode,
                                 format, buffering);
}

LatteScanBufferResult calculate_drc_scan_buffer(uint32_t mode, uint32_t format,
                                                 uint32_t buffering) {
    if (mode != 1 && mode != 2) {
        return error(LatteSurfaceErrorCode::unsupported_mode, mode, format,
                     buffering);
    }
    return calculate_scan_buffer(864, 480, mode, format, buffering);
}

namespace {
// Latte R7XX AddrLib configuration for the Wii U GPU. Source: decaf-emu
// gpu_tiling.cpp initAddrLib (gbAddrConfig 0x44902, chipRevision 71) decoded by
// r600addrlib.cpp DecodeGbRegs -> pipes 2, banks 4, pipe-interleave 256 B,
// sample-split 2048 B. The R7XX family disables the R6XX dual base/pitch
// alignment and macro-tile doubling branches.
constexpr uint32_t kNumPipes = 2;
constexpr uint32_t kNumBanks = 4;
constexpr uint32_t kPipeInterleaveBytes = 256;
constexpr uint32_t kSampleSplitBytes = 2048;

// GX2TileMode values (wut gx2/enum.h).
constexpr uint32_t kTileDefault = 0;
constexpr uint32_t kTileLinearAligned = 1;
constexpr uint32_t kTile1DThin1 = 2;
constexpr uint32_t kTile1DThick = 3;
constexpr uint32_t kTile2DThin1 = 4;
constexpr uint32_t kTile2DThick = 7;
constexpr uint32_t kTileLinearSpecial = 16;

// GX2SurfaceDim / GX2SurfaceUse (wut gx2/enum.h).
constexpr uint32_t kDimTexture1D = 0;
constexpr uint32_t kDimTexture2D = 1;
constexpr uint32_t kDimTexture3D = 2;
constexpr uint32_t kDimTextureCube = 3;
constexpr uint32_t kDimTexture1DArray = 4;
constexpr uint32_t kDimTexture2DArray = 5;
constexpr uint32_t kCubeMinSlices = 6;
constexpr uint32_t kUseColorBuffer = 1u << 1;
constexpr uint32_t kUseDepthBuffer = 1u << 2;

uint32_t pow2_align(uint32_t value, uint32_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

// AddrLib element bits-per-pixel after format expansion. BC formats use one
// 64- or 128-bit element per 4x4 block; all other supported formats are
// uncompressed packed elements.
std::optional<uint32_t> surface_element_bits(uint32_t format) {
    switch (format & 0x3Fu) {
    case 0x01: case 0x02: case 0x03:
        return 8u;
    case 0x05: case 0x06: case 0x07: case 0x08:
    case 0x09: case 0x0A: case 0x0B: case 0x0C:
        return 16u;
    case 0x0D: case 0x0E: case 0x0F: case 0x10:
    case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18:
    case 0x19: case 0x1A: case 0x1B:
        return 32u;
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20:
        return 64u;
    case 0x22: case 0x23:
        return 128u;
    case 0x31: case 0x34:
        return 64u;
    case 0x32: case 0x33: case 0x35:
        return 128u;
    default:
        return std::nullopt;
    }
}

bool is_block_compressed(uint32_t format) {
    const uint32_t base = format & 0x3Fu;
    return base >= 0x31 && base <= 0x35;
}

struct AddrSurfaceInfo {
    uint32_t tile_mode{};
    uint32_t pitch{};
    uint32_t height{};
    uint64_t surf_size{};
    uint32_t base_align{};
    uint32_t pitch_align{};
    uint32_t height_align{};
};

// AddrLib R600 ComputeSurfaceInfo, level 0, single sample. Source:
// r600addrlib.cpp ComputeSurfaceInfo{Linear,MicroTiled,MacroTiled}, their
// alignment helpers, and core addrlib.cpp PadDimensions/ComputeSurfaceThickness.
// Restricted to the reached feature subset: one sample (numSamples 1), R7XX
// config, no bank swap (only non-2B thin/thick modes are reached). Thick modes
// (1D/2D thick) use thickness 4 and pad the slice count to a multiple of it.
AddrSurfaceInfo compute_addr_surface_info(uint32_t tile_mode, uint32_t bpp,
                                          uint32_t width, uint32_t height,
                                          uint32_t num_slices) {
    constexpr uint32_t num_samples = 1;
    const uint32_t thickness =
        (tile_mode == kTile1DThick || tile_mode == kTile2DThick) ? 4u : 1u;
    AddrSurfaceInfo info;
    info.tile_mode = tile_mode;

    if (tile_mode == kTile1DThin1 || tile_mode == kTile1DThick) {
        // ComputeSurfaceAlignmentsMicrotiled.
        info.base_align = kPipeInterleaveBytes;
        info.pitch_align = std::max<uint32_t>(
            8u, kPipeInterleaveBytes / bpp / num_samples / thickness);
        info.height_align = 8u;
    } else if (tile_mode == kTile2DThin1 || tile_mode == kTile2DThick) {
        // ComputeSurfaceAlignmentsMacrotiled (aspectRatio 1).
        constexpr uint32_t aspect_ratio = 1;
        const uint32_t macro_tile_width = 8u * kNumBanks / aspect_ratio;
        const uint32_t macro_tile_height = aspect_ratio * 8u * kNumPipes;
        info.pitch_align = std::max<uint32_t>(
            macro_tile_width,
            macro_tile_width *
                (kPipeInterleaveBytes / bpp / (8u * thickness) / num_samples));
        info.height_align = macro_tile_height;
        if (thickness == 1) {
            const uint32_t macro_tile_bytes =
                num_samples * (bpp * macro_tile_height * macro_tile_width / 8u);
            info.base_align = std::max<uint32_t>(
                macro_tile_bytes,
                num_samples * info.height_align * bpp * info.pitch_align / 8u);
        } else {
            info.base_align = std::max<uint32_t>(
                kPipeInterleaveBytes,
                4u * info.height_align * bpp * info.pitch_align / 8u);
        }
        // base_align /= numSlicesPerMicroTile (microTileBytes vs sample split).
        const uint32_t micro_tile_bytes =
            thickness * num_samples * bpp * 64u / 8u;
        const uint32_t slices_per_micro_tile =
            (micro_tile_bytes >= kSampleSplitBytes)
                ? micro_tile_bytes / kSampleSplitBytes
                : 1u;
        info.base_align /= slices_per_micro_tile;
    } else {  // LinearAligned
        info.base_align = kPipeInterleaveBytes;
        info.pitch_align =
            std::max<uint32_t>(64u, 8u * kPipeInterleaveBytes / bpp);
        info.height_align = 1u;
    }

    info.pitch = pow2_align(width, info.pitch_align);
    info.height = pow2_align(height, info.height_align);
    // PadDimensions pads the slice count to a multiple of the tile thickness.
    const uint32_t slices =
        (thickness > 1) ? pow2_align(num_slices, thickness) : num_slices;
    info.surf_size = static_cast<uint64_t>(info.height) * info.pitch * slices *
                     bpp / 8u;
    return info;
}

LatteSurfaceComputeError surface_error(LatteSurfaceComputeErrorCode code,
                                       const LatteSurfaceDescriptor& s) {
    return {code, s.dim, s.format, s.aa, s.tile_mode, s.mip_levels};
}
}  // namespace

// Source: Decaf gx2_surface.cpp GX2CalcSurfaceSizeAndAlignment + gx2_addrlib.cpp
// internal::getSurfaceInfo. Reproduces the Default -> Tiled2DThin1 promotion,
// the AddrLib macro alignment, and GX2's own too-small-surface degrade to
// Tiled1DThin1, writing back the computed image size / alignment / pitch /
// tile mode / swizzle. Only the authenticated reached feature subset is
// implemented; anything else returns a structured unsupported result.
LatteSurfaceComputeResult
calculate_surface_size_and_alignment(const LatteSurfaceDescriptor& surface) {
    if (surface.aa != 0) {
        return surface_error(LatteSurfaceComputeErrorCode::unsupported_aa,
                             surface);
    }
    if (surface.dim != kDimTexture1D && surface.dim != kDimTexture2D &&
        surface.dim != kDimTexture3D && surface.dim != kDimTextureCube &&
        surface.dim != kDimTexture1DArray &&
        surface.dim != kDimTexture2DArray) {
        return surface_error(LatteSurfaceComputeErrorCode::unsupported_dim,
                             surface);
    }
    if (surface.tile_mode != kTileDefault &&
        surface.tile_mode != kTile1DThin1 &&
        surface.tile_mode != kTile2DThin1 &&
        surface.tile_mode != kTileLinearSpecial) {
        return surface_error(LatteSurfaceComputeErrorCode::unsupported_tile_mode,
                             surface);
    }
    const auto bits = surface_element_bits(surface.format);
    if (!bits) {
        return surface_error(LatteSurfaceComputeErrorCode::unsupported_format,
                             surface);
    }
    const uint32_t bpp = *bits;
    const bool is_depth = (surface.use & kUseDepthBuffer) != 0;
    const bool is_color = (surface.use & kUseColorBuffer) != 0;

    uint32_t max_dimension = std::max<uint32_t>(1u, surface.width);
    if (surface.dim != kDimTexture1D &&
        surface.dim != kDimTexture1DArray) {
        max_dimension =
            std::max(max_dimension, std::max<uint32_t>(1u, surface.height));
    }
    if (surface.dim == kDimTexture3D) {
        max_dimension =
            std::max(max_dimension, std::max<uint32_t>(1u, surface.depth));
    }
    uint32_t max_levels = 0;
    for (; max_dimension != 0; max_dimension >>= 1) {
        ++max_levels;
    }
    const uint32_t mip_levels =
        std::min<uint32_t>(14u, std::min(std::max(surface.mip_levels, 1u),
                                        max_levels));

    uint32_t tile_mode = surface.tile_mode;
    bool tile_mode_changed = false;
    if (tile_mode == kTileDefault) {
        tile_mode = kTileLinearAligned;
        if (surface.dim != kDimTexture1D || is_depth) {
            tile_mode = (surface.dim != kDimTexture3D || is_color)
                            ? kTile2DThin1
                            : kTile2DThick;
            tile_mode_changed = true;
        }
    }

    uint32_t swizzle = surface.swizzle & 0xFF00FFFFu;
    if (tile_mode >= kTile2DThin1 && tile_mode != kTileLinearSpecial) {
        swizzle |= 0xD0000u;
    }

    const bool block_compressed = is_block_compressed(surface.format);
    const auto level_info = [&](uint32_t level_tile_mode, uint32_t level) {
        uint32_t width = std::max<uint32_t>(1u, surface.width >> level);
        uint32_t height = 1u;
        uint32_t num_slices = 1u;
        switch (surface.dim) {
        case kDimTexture1D:
            break;
        case kDimTexture2D:
            height = std::max<uint32_t>(1u, surface.height >> level);
            break;
        case kDimTexture3D:
            height = std::max<uint32_t>(1u, surface.height >> level);
            num_slices = std::max<uint32_t>(1u, surface.depth >> level);
            break;
        case kDimTextureCube:
            height = std::max<uint32_t>(1u, surface.height >> level);
            num_slices = std::max<uint32_t>(kCubeMinSlices, surface.depth);
            break;
        case kDimTexture1DArray:
            num_slices = std::max<uint32_t>(1u, surface.depth);
            break;
        case kDimTexture2DArray:
            height = std::max<uint32_t>(1u, surface.height >> level);
            num_slices = std::max<uint32_t>(1u, surface.depth);
            break;
        }
        if (block_compressed) {
            width = 1u + (width - 1u) / 4u;
            height = 1u + (height - 1u) / 4u;
        }
        if (level_tile_mode != kTileLinearSpecial) {
            return compute_addr_surface_info(level_tile_mode, bpp, width,
                                             height, num_slices);
        }
        AddrSurfaceInfo info;
        info.tile_mode = level_tile_mode;
        info.pitch = width;
        info.height = height;
        info.surf_size = static_cast<uint64_t>(width) * height * num_slices *
                         (bpp / 8u);
        info.base_align = 1u;
        info.pitch_align = 1u;
        info.height_align = 1u;
        return info;
    };

    auto info = level_info(tile_mode, 0);
    if (tile_mode_changed &&
        surface.width < info.pitch_align &&
        surface.height < info.height_align) {
        tile_mode =
            (tile_mode == kTile2DThick) ? kTile1DThick : kTile1DThin1;
        info = level_info(tile_mode, 0);
        swizzle &= 0xFF00FFFFu;
    }
    if (info.surf_size > std::numeric_limits<uint32_t>::max()) {
        return surface_error(LatteSurfaceComputeErrorCode::overflow, surface);
    }

    LatteSurfaceLayout layout{tile_mode,
                              swizzle,
                              static_cast<uint32_t>(info.surf_size),
                              0u,
                              info.base_align,
                              info.pitch,
                              mip_levels,
                              {}};
    uint32_t last_tile_mode = tile_mode;
    uint64_t previous_size = info.surf_size;
    uint32_t offset0 = 0;
    for (uint32_t level = 1; level < mip_levels; ++level) {
        auto level_layout = level_info(last_tile_mode, level);
        uint64_t pad = 0;
        if (last_tile_mode >= kTile2DThin1 &&
            last_tile_mode != kTileLinearSpecial) {
            uint32_t width =
                std::max<uint32_t>(1u, surface.width >> level);
            uint32_t height =
                std::max<uint32_t>(1u, surface.height >> level);
            if (block_compressed) {
                width = 1u + (width - 1u) / 4u;
                height = 1u + (height - 1u) / 4u;
            }
            if (width < level_layout.pitch_align ||
                height < level_layout.height_align) {
                last_tile_mode = (last_tile_mode == kTile2DThick)
                                     ? kTile1DThick
                                     : kTile1DThin1;
                level_layout = level_info(last_tile_mode, level);
                layout.swizzle =
                    (level << 16) | (layout.swizzle & 0xFF00FFFFu);
                if (level > 1) {
                    pad = layout.swizzle & 0xFFFFu;
                }
            }
        }
        pad += (level_layout.base_align -
                (previous_size % level_layout.base_align)) %
               level_layout.base_align;
        const uint64_t offset =
            pad + previous_size +
            (level > 1 ? layout.mip_level_offsets[level - 2] : 0u);
        if (offset > std::numeric_limits<uint32_t>::max() ||
            level_layout.surf_size > std::numeric_limits<uint32_t>::max()) {
            return surface_error(LatteSurfaceComputeErrorCode::overflow,
                                 surface);
        }
        if (level == 1) {
            offset0 = static_cast<uint32_t>(offset);
        } else {
            layout.mip_level_offsets[level - 1] =
                static_cast<uint32_t>(offset);
        }
        previous_size = level_layout.surf_size;
    }
    if (mip_levels > 1) {
        layout.mip_level_offsets[0] = offset0;
        const uint64_t mipmap_size =
            previous_size + layout.mip_level_offsets[mip_levels - 2];
        if (mipmap_size > std::numeric_limits<uint32_t>::max()) {
            return surface_error(LatteSurfaceComputeErrorCode::overflow,
                                 surface);
        }
        layout.mipmap_size = static_cast<uint32_t>(mipmap_size);
    }
    return layout;
}

} // namespace nwii::runtime
