#pragma once

#include <array>
#include <cstdint>
#include <variant>

namespace nwii::runtime {

enum class LatteSurfaceErrorCode {
    unsupported_mode,
    unsupported_format,
    unsupported_buffering,
    overflow,
};
using LatteByteCountResult =
    std::variant<uint32_t, LatteSurfaceErrorCode>;

struct LatteSurfaceError {
    LatteSurfaceErrorCode code{};
    uint32_t mode{};
    uint32_t format{};
    uint32_t buffering{};

    bool operator==(const LatteSurfaceError&) const = default;
};

struct LatteScanBufferLayout {
    uint32_t width{};
    uint32_t height{};
    uint32_t bytes_per_element{};
    uint32_t buffer_count{};
    uint32_t size{};
    uint32_t alignment{};

    bool operator==(const LatteScanBufferLayout&) const = default;
};

using LatteScanBufferResult =
    std::variant<LatteScanBufferLayout, LatteSurfaceError>;

// GX2CalcSurfaceSizeAndAlignment / AddrLib surface sizing.
enum class LatteSurfaceComputeErrorCode {
    unsupported_dim,
    unsupported_format,
    unsupported_aa,
    unsupported_tile_mode,
    unsupported_mip_levels,
    overflow,
};

// Guest GX2Surface fields consumed by GX2CalcSurfaceSizeAndAlignment.
struct LatteSurfaceDescriptor {
    uint32_t dim{};
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{};
    uint32_t mip_levels{};
    uint32_t format{};
    uint32_t aa{};
    uint32_t use{};
    uint32_t tile_mode{};
    uint32_t swizzle{};

    bool operator==(const LatteSurfaceDescriptor&) const = default;
};

// Fields GX2CalcSurfaceSizeAndAlignment writes back into the guest struct.
struct LatteSurfaceLayout {
    uint32_t tile_mode{};
    uint32_t swizzle{};
    uint32_t image_size{};
    uint32_t mipmap_size{};
    uint32_t alignment{};
    uint32_t pitch{};
    uint32_t mip_levels{};
    std::array<uint32_t, 13> mip_level_offsets{};

    bool operator==(const LatteSurfaceLayout&) const = default;
};

struct LatteSurfaceComputeError {
    LatteSurfaceComputeErrorCode code{};
    uint32_t dim{};
    uint32_t format{};
    uint32_t aa{};
    uint32_t tile_mode{};
    uint32_t mip_levels{};

    bool operator==(const LatteSurfaceComputeError&) const = default;
};

using LatteSurfaceComputeResult =
    std::variant<LatteSurfaceLayout, LatteSurfaceComputeError>;

LatteSurfaceComputeResult
calculate_surface_size_and_alignment(const LatteSurfaceDescriptor& surface);

LatteByteCountResult
checked_surface_byte_count(uint32_t width, uint32_t height, uint32_t depth,
                           uint32_t bytes_per_element, uint32_t samples);

LatteScanBufferResult calculate_tv_scan_buffer(uint32_t mode, uint32_t format,
                                                uint32_t buffering);
LatteScanBufferResult calculate_drc_scan_buffer(uint32_t mode, uint32_t format,
                                                 uint32_t buffering);

} // namespace nwii::runtime
