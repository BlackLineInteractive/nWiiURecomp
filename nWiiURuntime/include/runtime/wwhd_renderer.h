#pragma once

#include "runtime/cafe_runtime.h"
#include "runtime/execution_image.h"
#include "runtime/gx2_runtime.h"
#include "runtime/latte_shader.h"
#include "runtime/wwhd_gpu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace nwii::runtime {

class WwhdRenderer {
public:
    WwhdRenderer(ExecutionImage& image, CafeRuntime& cafe)
        : image_(image), cafe_(cafe), gx2_(cafe.gx2()) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
            fail("SDL_Init");
        }
        window_ = SDL_CreateWindow("The Wind Waker HD", 1280, 720,
                                   SDL_WINDOW_RESIZABLE);
        if (window_ == nullptr) {
            SDL_Quit();
            fail("SDL_CreateWindow");
        }
        renderer_ = SDL_CreateRenderer(window_, "vulkan");
        if (renderer_ == nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            SDL_Quit();
            fail("SDL Vulkan renderer");
        }
        SDL_AudioSpec audio_spec{};
        audio_spec.format = SDL_AUDIO_S16;
        audio_spec.channels = 2;
        audio_spec.freq = 48000;
        audio_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
        if (audio_ == nullptr || !SDL_ResumeAudioStreamDevice(audio_)) {
            SDL_DestroyAudioStream(audio_);
            audio_ = nullptr;
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            SDL_Quit();
            fail("SDL audio");
        }
        gpu_ = std::make_unique<WwhdGpu>();
        cafe_.set_vpad_buttons(0);
        gx2_.attach_event_callback(this, render_event);
    }

    ~WwhdRenderer() {
        gx2_.attach_event_callback(nullptr, nullptr);
        for (const auto& [address, target] : targets_) {
            (void)address;
            SDL_DestroyTexture(target.texture);
        }
        for (auto& scan : scan_targets_) {
            SDL_DestroyTexture(scan.texture);
        }
        SDL_DestroyTexture(feedback_target_.texture);
        for (const auto& [key, texture] : textures_) {
            (void)key;
            SDL_DestroyTexture(texture);
        }
        SDL_DestroyAudioStream(audio_);
        SDL_DestroyRenderer(renderer_);
        gpu_.reset();
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    WwhdRenderer(const WwhdRenderer&) = delete;
    WwhdRenderer& operator=(const WwhdRenderer&) = delete;

    bool pump_events() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                return false;
            }
        }
        const bool* key = SDL_GetKeyboardState(nullptr);
        uint32_t buttons = 0;
        if (key[SDL_SCANCODE_P]) {
            buttons |= 1u << 3;
        }
        if (key[SDL_SCANCODE_DOWN]) {
            buttons |= 1u << 8;
        }
        if (key[SDL_SCANCODE_UP]) {
            buttons |= 1u << 9;
        }
        if (key[SDL_SCANCODE_RIGHT]) {
            buttons |= 1u << 10;
        }
        if (key[SDL_SCANCODE_LEFT]) {
            buttons |= 1u << 11;
        }
        if (key[SDL_SCANCODE_ESCAPE]) {
            buttons |= 1u << 14;
        }
        if (key[SDL_SCANCODE_RETURN] || key[SDL_SCANCODE_SPACE]) {
            buttons |= 1u << 15;
        }
        if (std::getenv("NWIIU_AUTO_A") != nullptr && swap_count_ < 2 &&
            (SDL_GetTicks() / 500) % 2 == 0) {
            buttons |= 1u << 15;
        }
        if (std::getenv("NWIIU_AUTO_START") != nullptr &&
            (SDL_GetTicks() / 500) % 2 == 0) {
            buttons |= 1u << 3;
        }
        cafe_.set_vpad_buttons(buttons);
        return true;
    }

private:
    struct Target {
        SDL_Texture* texture{};
        uint32_t width{};
        uint32_t height{};
    };

    struct Surface {
        uint32_t width{};
        uint32_t height{};
        uint32_t format{};
        uint32_t image_size{};
        uint32_t image{};
        uint32_t mode{};
        uint32_t swizzle{};
        uint32_t pitch{};
        uint32_t component_map{};
    };

    [[noreturn]] static void fail(const char* operation) {
        throw std::runtime_error(std::string{operation} + ": " +
                                 SDL_GetError());
    }

    static float as_float(uint32_t value) {
        return std::bit_cast<float>(value);
    }

    Surface surface(uint32_t descriptor) const {
        auto& memory = image_.memory;
        return {
            memory.read32(descriptor + 0x04, 0),
            memory.read32(descriptor + 0x08, 0),
            memory.read32(descriptor + 0x14, 0),
            memory.read32(descriptor + 0x20, 0),
            memory.read32(descriptor + 0x24, 0),
            memory.read32(descriptor + 0x30, 0),
            memory.read32(descriptor + 0x34, 0),
            memory.read32(descriptor + 0x3C, 0),
            memory.read32(descriptor + 0x84, 0),
        };
    }
    using TargetKey =
        std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                   uint32_t, uint32_t, uint32_t>;
    using TextureKey = std::pair<TargetKey, uint32_t>;

    static TargetKey target_key(const Surface& value) {
        return {value.image, value.width, value.height, value.format,
                value.image_size, value.mode, value.swizzle, value.pitch};
    }
    static TextureKey texture_key(const Surface& value) {
        return {target_key(value), value.component_map};
    }

    static uint32_t tiled_offset(uint32_t x, uint32_t y, uint32_t pitch,
                                 uint32_t mode, uint32_t swizzle,
                                 uint32_t bytes_per_element) {
        if (mode != 2 && mode != 4) {
            return (y * pitch + x) * bytes_per_element;
        }
        const uint32_t x0 = x & 1u;
        const uint32_t x1 = (x >> 1) & 1u;
        const uint32_t x2 = (x >> 2) & 1u;
        const uint32_t y0 = y & 1u;
        const uint32_t y1 = (y >> 1) & 1u;
        const uint32_t y2 = (y >> 2) & 1u;
        uint32_t pixel_index;
        if (bytes_per_element == 8) {
            pixel_index = x0 | (y0 << 1) | (x1 << 2) | (x2 << 3) |
                          (y1 << 4) | (y2 << 5);
        } else if (bytes_per_element == 16) {
            pixel_index = y0 | (x0 << 1) | (x1 << 2) | (x2 << 3) |
                          (y1 << 4) | (y2 << 5);
        } else {
            pixel_index = x0 | (x1 << 1) | (y0 << 2) | (x2 << 3) |
                          (y1 << 4) | (y2 << 5);
        }
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
            (512u * bytes_per_element) *
            ((x >> 5) + (pitch >> 5) * (y >> 4));
        const uint32_t total =
            pixel_index * bytes_per_element + (macro_tile >> 3);
        return ((total & ~0xFFu) << 3) | ((bank_pipe >> 1) << 9) |
               ((bank_pipe & 1u) << 8) | (total & 0xFFu);
    }

    static std::array<uint8_t, 4> rgb565(uint16_t value) {
        return {
            static_cast<uint8_t>(((value >> 11) & 31u) * 255u / 31u),
            static_cast<uint8_t>(((value >> 5) & 63u) * 255u / 63u),
            static_cast<uint8_t>((value & 31u) * 255u / 31u),
            255,
        };
    }

    void decode_bc1(const Surface& surface, std::vector<uint8_t>& pixels) const {
        const uint32_t block_pitch = surface.pitch;
        const uint32_t block_width = (surface.width + 3) / 4;
        const uint32_t block_height = (surface.height + 3) / 4;
        for (uint32_t by = 0; by < block_height; ++by) {
            for (uint32_t bx = 0; bx < block_width; ++bx) {
                const uint32_t offset = tiled_offset(
                    bx, by, block_pitch, surface.mode, surface.swizzle, 8);
                if (offset + 8 > surface.image_size) {
                    throw std::runtime_error("BC1 block exceeds GX2 surface");
                }
                std::array<uint8_t, 8> block{};
                for (uint32_t i = 0; i < block.size(); ++i) {
                    block[i] =
                        image_.memory.read8(surface.image + offset + i, 0);
                }
                const uint16_t c0 =
                    static_cast<uint16_t>(block[0] | (block[1] << 8));
                const uint16_t c1 =
                    static_cast<uint16_t>(block[2] | (block[3] << 8));
                std::array<std::array<uint8_t, 4>, 4> colors{
                    rgb565(c0), rgb565(c1), {}, {}};
                for (uint32_t component = 0; component < 3; ++component) {
                    if (c0 > c1) {
                        colors[2][component] = static_cast<uint8_t>(
                            (2u * colors[0][component] +
                             colors[1][component]) /
                            3u);
                        colors[3][component] = static_cast<uint8_t>(
                            (colors[0][component] +
                             2u * colors[1][component]) /
                            3u);
                    } else {
                        colors[2][component] = static_cast<uint8_t>(
                            (colors[0][component] +
                             colors[1][component]) /
                            2u);
                        colors[3][component] = 0;
                    }
                }
                colors[2][3] = 255;
                colors[3][3] = c0 > c1 ? 255 : 0;
                const uint32_t indices =
                    static_cast<uint32_t>(block[4]) |
                    (static_cast<uint32_t>(block[5]) << 8) |
                    (static_cast<uint32_t>(block[6]) << 16) |
                    (static_cast<uint32_t>(block[7]) << 24);
                for (uint32_t y = 0; y < 4; ++y) {
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint32_t px = bx * 4 + x;
                        const uint32_t py = by * 4 + y;
                        if (px >= surface.width || py >= surface.height) {
                            continue;
                        }
                        const auto& color =
                            colors[(indices >> (2u * (y * 4 + x))) & 3u];
                        const size_t destination =
                            (static_cast<size_t>(py) * surface.width + px) * 4;
                        std::copy(color.begin(), color.end(),
                                  pixels.begin() + destination);
                    }
                }
            }
        }
    }

    void decode_bc3(const Surface& surface, std::vector<uint8_t>& pixels) const {
        const uint32_t block_width = (surface.width + 3) / 4;
        const uint32_t block_height = (surface.height + 3) / 4;
        for (uint32_t by = 0; by < block_height; ++by) {
            for (uint32_t bx = 0; bx < block_width; ++bx) {
                const uint32_t offset = tiled_offset(
                    bx, by, surface.pitch, surface.mode, surface.swizzle, 16);
                if (offset + 16 > surface.image_size) {
                    throw std::runtime_error("BC3 block exceeds GX2 surface");
                }
                std::array<uint8_t, 16> block{};
                for (uint32_t i = 0; i < block.size(); ++i) {
                    block[i] =
                        image_.memory.read8(surface.image + offset + i, 0);
                }

                std::array<uint8_t, 8> alpha{block[0], block[1]};
                if (alpha[0] > alpha[1]) {
                    for (uint32_t i = 1; i <= 6; ++i) {
                        alpha[i + 1] = static_cast<uint8_t>(
                            ((7u - i) * alpha[0] + i * alpha[1]) / 7u);
                    }
                } else {
                    for (uint32_t i = 1; i <= 4; ++i) {
                        alpha[i + 1] = static_cast<uint8_t>(
                            ((5u - i) * alpha[0] + i * alpha[1]) / 5u);
                    }
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
                uint64_t alpha_indices = 0;
                for (uint32_t i = 0; i < 6; ++i) {
                    alpha_indices |=
                        static_cast<uint64_t>(block[i + 2]) << (8u * i);
                }

                const uint16_t c0 =
                    static_cast<uint16_t>(block[8] | (block[9] << 8));
                const uint16_t c1 =
                    static_cast<uint16_t>(block[10] | (block[11] << 8));
                std::array<std::array<uint8_t, 4>, 4> colors{
                    rgb565(c0), rgb565(c1), {}, {}};
                for (uint32_t component = 0; component < 3; ++component) {
                    colors[2][component] = static_cast<uint8_t>(
                        (2u * colors[0][component] + colors[1][component]) /
                        3u);
                    colors[3][component] = static_cast<uint8_t>(
                        (colors[0][component] + 2u * colors[1][component]) /
                        3u);
                }
                const uint32_t color_indices =
                    static_cast<uint32_t>(block[12]) |
                    (static_cast<uint32_t>(block[13]) << 8) |
                    (static_cast<uint32_t>(block[14]) << 16) |
                    (static_cast<uint32_t>(block[15]) << 24);
                for (uint32_t y = 0; y < 4; ++y) {
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint32_t px = bx * 4 + x;
                        const uint32_t py = by * 4 + y;
                        if (px >= surface.width || py >= surface.height) {
                            continue;
                        }
                        const uint32_t pixel = y * 4 + x;
                        const auto& color =
                            colors[(color_indices >> (2u * pixel)) & 3u];
                        const size_t destination =
                            (static_cast<size_t>(py) * surface.width + px) * 4;
                        std::copy_n(color.begin(), 3,
                                    pixels.begin() + destination);
                        pixels[destination + 3] =
                            alpha[(alpha_indices >> (3u * pixel)) & 7u];
                    }
                }
            }
        }
    }

    void decode_bc4(const Surface& surface, std::vector<uint8_t>& pixels) const {

        const uint32_t block_pitch = surface.pitch;
        const uint32_t block_width = (surface.width + 3) / 4;
        const uint32_t block_height = (surface.height + 3) / 4;
        for (uint32_t by = 0; by < block_height; ++by) {
            for (uint32_t bx = 0; bx < block_width; ++bx) {
                const uint32_t offset = tiled_offset(
                    bx, by, block_pitch, surface.mode, surface.swizzle, 8);
                if (offset + 8 > surface.image_size) {
                    throw std::runtime_error("BC4 block exceeds GX2 surface");
                }
                std::array<uint8_t, 8> block{};
                for (uint32_t i = 0; i < block.size(); ++i) {
                    block[i] =
                        image_.memory.read8(surface.image + offset + i, 0);
                }
                std::array<uint8_t, 8> values{block[0], block[1]};
                if (block[0] > block[1]) {
                    for (uint32_t i = 1; i <= 6; ++i) {
                        values[i + 1] = static_cast<uint8_t>(
                            ((7u - i) * block[0] + i * block[1]) / 7u);
                    }
                } else {
                    for (uint32_t i = 1; i <= 4; ++i) {
                        values[i + 1] = static_cast<uint8_t>(
                            ((5u - i) * block[0] + i * block[1]) / 5u);
                    }
                    values[6] = 0;
                    values[7] = 255;
                }
                uint64_t indices = 0;
                for (uint32_t i = 0; i < 6; ++i) {
                    indices |= static_cast<uint64_t>(block[i + 2]) << (8u * i);
                }
                for (uint32_t y = 0; y < 4; ++y) {
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint32_t px = bx * 4 + x;
                        const uint32_t py = by * 4 + y;
                        if (px >= surface.width || py >= surface.height) {
                            continue;
                        }
                        const uint8_t value =
                            values[(indices >> (3u * (y * 4 + x))) & 7u];
                        const size_t destination =
                            (static_cast<size_t>(py) * surface.width + px) * 4;
                        pixels[destination + 0] = value;
                        pixels[destination + 1] = 0;
                        pixels[destination + 2] = 0;
                        pixels[destination + 3] = 255;
                    }
                }
            }
        }
    }

    void decode_bc5(const Surface& surface, std::vector<uint8_t>& pixels) const {
        const uint32_t block_width = (surface.width + 3) / 4;
        const uint32_t block_height = (surface.height + 3) / 4;
        for (uint32_t by = 0; by < block_height; ++by) {
            for (uint32_t bx = 0; bx < block_width; ++bx) {
                const uint32_t offset = tiled_offset(
                    bx, by, surface.pitch, surface.mode, surface.swizzle, 16);
                if (offset + 16 > surface.image_size) {
                    throw std::runtime_error("BC5 block exceeds GX2 surface");
                }
                std::array<uint8_t, 16> block{};
                for (uint32_t i = 0; i < block.size(); ++i) {
                    block[i] =
                        image_.memory.read8(surface.image + offset + i, 0);
                }
                const auto channel = [&block](uint32_t base) {
                    std::array<uint8_t, 8> values{
                        block[base], block[base + 1]};
                    if (values[0] > values[1]) {
                        for (uint32_t i = 1; i <= 6; ++i) {
                            values[i + 1] = static_cast<uint8_t>(
                                ((7u - i) * values[0] + i * values[1]) / 7u);
                        }
                    } else {
                        for (uint32_t i = 1; i <= 4; ++i) {
                            values[i + 1] = static_cast<uint8_t>(
                                ((5u - i) * values[0] + i * values[1]) / 5u);
                        }
                        values[6] = 0;
                        values[7] = 255;
                    }
                    uint64_t indices = 0;
                    for (uint32_t i = 0; i < 6; ++i) {
                        indices |= static_cast<uint64_t>(block[base + 2 + i])
                                   << (8u * i);
                    }
                    return std::pair{values, indices};
                };
                const auto [red, red_indices] = channel(0);
                const auto [green, green_indices] = channel(8);
                for (uint32_t y = 0; y < 4; ++y) {
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint32_t px = bx * 4 + x;
                        const uint32_t py = by * 4 + y;
                        if (px >= surface.width || py >= surface.height) {
                            continue;
                        }
                        const uint32_t shift = 3u * (y * 4 + x);
                        const size_t destination =
                            (static_cast<size_t>(py) * surface.width + px) * 4;
                        pixels[destination + 0] =
                            red[(red_indices >> shift) & 7u];
                        pixels[destination + 1] =
                            green[(green_indices >> shift) & 7u];
                        pixels[destination + 2] = 0;
                        pixels[destination + 3] = 255;
                    }
                }
            }
        }
    }

    void decode_rgba8(const Surface& surface,
                      std::vector<uint8_t>& pixels) const {
        for (uint32_t y = 0; y < surface.height; ++y) {
            for (uint32_t x = 0; x < surface.width; ++x) {
                const uint32_t source = tiled_offset(
                    x, y, surface.pitch, surface.mode, surface.swizzle, 4);
                if (source + 4 > surface.image_size) {
                    throw std::runtime_error(
                        "RGBA8 pixel exceeds GX2 surface");
                }
                const size_t destination =
                    (static_cast<size_t>(y) * surface.width + x) * 4;
                const uint8_t c0 =
                    image_.memory.read8(surface.image + source, 0);
                const uint8_t c1 =
                    image_.memory.read8(surface.image + source + 1, 0);
                const uint8_t c2 =
                    image_.memory.read8(surface.image + source + 2, 0);
                pixels[destination] =
                    (surface.format & 0x3Fu) == 0x19 ? c2 : c0;
                pixels[destination + 1] = c1;
                pixels[destination + 2] =
                    (surface.format & 0x3Fu) == 0x19 ? c0 : c2;
                pixels[destination + 3] =
                    image_.memory.read8(surface.image + source + 3, 0);
            }
        }
    }

    void decode_rg8(const Surface& surface,
                    std::vector<uint8_t>& pixels) const {
        for (uint32_t y = 0; y < surface.height; ++y) {
            for (uint32_t x = 0; x < surface.width; ++x) {
                const uint32_t source = tiled_offset(
                    x, y, surface.pitch, surface.mode, surface.swizzle, 2);
                if (source + 2 > surface.image_size) {
                    throw std::runtime_error("RG8 pixel exceeds GX2 surface");
                }
                const size_t destination =
                    (static_cast<size_t>(y) * surface.width + x) * 4;
                pixels[destination + 0] =
                    image_.memory.read8(surface.image + source, 0);
                pixels[destination + 1] =
                    image_.memory.read8(surface.image + source + 1, 0);
                pixels[destination + 2] = 0;
                pixels[destination + 3] = 255;
            }
        }
    }

    void decode_r8(const Surface& surface,
                   std::vector<uint8_t>& pixels) const {
        for (uint32_t y = 0; y < surface.height; ++y) {
            for (uint32_t x = 0; x < surface.width; ++x) {
                const uint32_t source = tiled_offset(
                    x, y, surface.pitch, surface.mode, surface.swizzle, 1);
                if (source >= surface.image_size) {
                    throw std::runtime_error("R8 pixel exceeds GX2 surface");
                }
                const size_t destination =
                    (static_cast<size_t>(y) * surface.width + x) * 4;
                pixels[destination + 0] =
                    image_.memory.read8(surface.image + source, 0);
                pixels[destination + 1] = 0;
                pixels[destination + 2] = 0;
                pixels[destination + 3] = 255;
            }
        }
    }

    void decode_r32_float(const Surface& surface,
                          std::vector<uint8_t>& pixels) const {
        for (uint32_t y = 0; y < surface.height; ++y) {
            for (uint32_t x = 0; x < surface.width; ++x) {
                const uint32_t source = tiled_offset(
                    x, y, surface.pitch, surface.mode, surface.swizzle, 4);
                if (source + 4 > surface.image_size) {
                    throw std::runtime_error(
                        "R32 float pixel exceeds GX2 surface");
                }
                const float sample = std::bit_cast<float>(
                    image_.memory.read32(surface.image + source, 0));
                const uint8_t value = static_cast<uint8_t>(
                    std::clamp(sample, 0.0f, 1.0f) * 255.0f);
                const size_t destination =
                    (static_cast<size_t>(y) * surface.width + x) * 4;
                pixels[destination + 0] = value;
                pixels[destination + 1] = 0;
                pixels[destination + 2] = 0;
                pixels[destination + 3] = 255;
            }
        }
    }

    static void apply_component_map(uint32_t component_map,
                                    std::vector<uint8_t>& pixels) {
        const std::array<uint32_t, 4> selectors{
            (component_map >> 24) & 0xFFu,
            (component_map >> 16) & 0xFFu,
            (component_map >> 8) & 0xFFu,
            component_map & 0xFFu,
        };
        for (size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            const std::array<uint8_t, 6> source{
                pixels[pixel + 0], pixels[pixel + 1], pixels[pixel + 2],
                pixels[pixel + 3], 0, 255};
            for (uint32_t component = 0; component < 4; ++component) {
                pixels[pixel + component] =
                    selectors[component] < source.size()
                        ? source[selectors[component]]
                        : 0;
            }
        }
    }

    SDL_Texture* sampled_texture(uint32_t descriptor) {
        if (descriptor == 0) {
            return nullptr;
        }
        const auto value = surface(descriptor);
        if (const auto target = targets_.find(target_key(value));
            target != targets_.end()) {
            return target->second.texture;
        }
        const auto key = texture_key(value);
        if (const auto texture = textures_.find(key);
            texture != textures_.end()) {
            return texture->second;
        }
        std::vector<uint8_t> pixels(
            static_cast<size_t>(value.width) * value.height * 4);
        switch (value.format & 0x3Fu) {
        case 0x01:
            decode_r8(value, pixels);
            break;
        case 0x07:
            decode_rg8(value, pixels);
            break;
        case 0x19:
        case 0x1A:
            decode_rgba8(value, pixels);
            break;
        case 0x0E:
            decode_r32_float(value, pixels);
            break;
        case 0x31:
            decode_bc1(value, pixels);
            break;
        case 0x33:
            decode_bc3(value, pixels);
            break;
        case 0x34:
            decode_bc4(value, pixels);
            break;
        case 0x35:
            decode_bc5(value, pixels);
            break;
        default:
            throw std::runtime_error(
                "unsupported sampled GX2 texture format " +
                std::to_string(value.format));
        }
        apply_component_map(value.component_map, pixels);
        if (value.image == 0x1EB65000 &&
            std::getenv("NWIIU_SHADER_DUMP") != nullptr &&
            dumped_.insert("splash-texture").second) {
            std::ofstream out(
                std::filesystem::path(std::getenv("NWIIU_SHADER_DUMP")) /
                    "splash.ppm",
                std::ios::binary);
            out << "P6\n" << value.width << ' ' << value.height << "\n255\n";
            for (size_t i = 0; i < pixels.size(); i += 4) {
                out.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
            }
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr) {
            uint64_t sum_rgb = 0;
            uint64_t sum_a = 0;
            for (size_t i = 0; i < pixels.size(); i += 4) {
                sum_rgb += pixels[i] + pixels[i + 1] + pixels[i + 2];
                sum_a += pixels[i + 3];
            }
            const size_t count = pixels.size() / 4;
            std::fprintf(stderr,
                         "TEXDECODE image=%08X %ux%u fmt=%08X comp=%08X "
                         "mean_rgb=%llu mean_a=%llu\n",
                         value.image, value.width, value.height, value.format,
                         value.component_map,
                         static_cast<unsigned long long>(
                             count ? sum_rgb / (3 * count) : 0),
                         static_cast<unsigned long long>(
                             count ? sum_a / count : 0));
        }
        SDL_Texture* texture = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
            static_cast<int>(value.width), static_cast<int>(value.height));
        if (texture == nullptr ||
            !SDL_UpdateTexture(texture, nullptr, pixels.data(),
                               static_cast<int>(value.width * 4)) ||
            !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) ||
            !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
            SDL_DestroyTexture(texture);
            fail("SDL GX2 texture upload");
        }
        textures_.emplace(key, texture);
        return texture;
    }

    void trace_shader(const Gx2State& state) {
        if (std::getenv("NWIIU_SHADER_TRACE") == nullptr ||
            state.vertex_shader_address == 0 ||
            state.pixel_shader_address == 0 ||
            state.fetch_shader_address == 0) {
            return;
        }
        const auto key = std::tuple{
            state.vertex_shader_address, state.pixel_shader_address,
            state.fetch_shader_address};
        if (shader_traces_.size() >= 256 || !shader_traces_.insert(key).second) {
            return;
        }
        const auto words = [this](uint32_t address, uint32_t count) {
            std::vector<uint32_t> values(count);
            for (uint32_t i = 0; i < count; ++i) {
                values[i] = image_.memory.read32(address + i * 4, 0);
            }
            return values;
        };
        const auto bytes = [this](uint32_t descriptor, uint32_t size_offset,
                                  uint32_t data_offset) {
            const uint32_t size =
                image_.memory.read32(descriptor + size_offset, 0);
            const uint32_t data =
                image_.memory.read32(descriptor + data_offset, 0);
            std::vector<uint8_t> values(size);
            image_.memory.read_bytes(data, values, 0);
            return values;
        };
        try {
            const auto vs_regs = words(state.vertex_shader_address, 0xD0 / 4);
            const auto ps_regs = words(state.pixel_shader_address, 0xA4 / 4);
            const auto vs =
                bytes(state.vertex_shader_address, 0xD0, 0xD4);
            const auto ps =
                bytes(state.pixel_shader_address, 0xA4, 0xA8);
            const auto fs =
                bytes(state.fetch_shader_address, 0x08, 0x0C);
            const auto translated = translate_latte(
                {vs_regs, vs, ps_regs, ps, fs});
            if (const char* dir = std::getenv("NWIIU_SHADER_DUMP");
                dir != nullptr) {
                std::filesystem::create_directories(dir);
                const auto stem =
                    std::filesystem::path(dir) /
                    ("translated_" +
                     std::to_string(state.vertex_shader_address) + "_" +
                     std::to_string(state.pixel_shader_address));
                if (dumped_.insert(stem.string()).second) {
                    std::ofstream(stem.string() + ".vert.glsl")
                        << translated.vertex_glsl;
                    std::ofstream(stem.string() + ".frag.glsl")
                        << translated.fragment_glsl;
                    std::ofstream uniforms(
                        stem.string() + ".uniforms.bin", std::ios::binary);
                    uniforms.write(
                        reinterpret_cast<const char*>(
                            state.vertex_uniform_registers.data()),
                        static_cast<std::streamsize>(
                            state.vertex_uniform_registers.size() *
                            sizeof(state.vertex_uniform_registers[0])));
                    std::ofstream pixel_uniforms(
                        stem.string() + ".pixel-uniforms.bin",
                        std::ios::binary);
                    pixel_uniforms.write(
                        reinterpret_cast<const char*>(
                            state.pixel_uniform_registers.data()),
                        static_cast<std::streamsize>(
                            state.pixel_uniform_registers.size() *
                            sizeof(state.pixel_uniform_registers[0])));
                }
            }
            std::fprintf(stderr,
                         "SHADER-OK vs=%08X ps=%08X fs=%08X inputs=%zu "
                         "samplers=%zu\n",
                         state.vertex_shader_address,
                         state.pixel_shader_address,
                         state.fetch_shader_address,
                         translated.vertex_inputs.size(),
                         translated.ps_sampler_slots.size());
            if (dumped_.insert("shader-ok-glsl").second) {
                std::fprintf(stderr, "VERTEX-GLSL\n%sFRAGMENT-GLSL\n%s",
                             translated.vertex_glsl.c_str(),
                             translated.fragment_glsl.c_str());
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SHADER-FAIL vs=%08X ps=%08X fs=%08X %s\n",
                         state.vertex_shader_address,
                         state.pixel_shader_address,
                         state.fetch_shader_address, error.what());
        }
    }

    Target& target(uint32_t descriptor) {
        const auto value = surface(descriptor);
        if (auto found = targets_.find(target_key(value));
            found != targets_.end()) {
            return found->second;
        }
        SDL_Texture* texture = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            static_cast<int>(value.width), static_cast<int>(value.height));
        if (texture == nullptr ||
            !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) ||
            !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
            SDL_DestroyTexture(texture);
            fail("SDL GX2 color target");
        }
        return targets_
            .emplace(target_key(value),
                     Target{texture, value.width, value.height})
            .first->second;
    }

    static void append_triangle(std::vector<int>& triangles, uint32_t a,
                                uint32_t b, uint32_t c) {
        triangles.push_back(static_cast<int>(a));
        triangles.push_back(static_cast<int>(b));
        triangles.push_back(static_cast<int>(c));
    }

    std::vector<uint32_t> draw_indices(const Gx2DrawState& draw) {
        std::vector<uint32_t> indices(draw.count);
        if (!draw.indexed) {
            for (uint32_t index = 0; index < draw.count; ++index) {
                indices[index] = index + draw.offset;
            }
            return indices;
        }
        const uint32_t size =
            draw.index_type == 0 || draw.index_type == 4 ? 2u : 4u;
        for (uint32_t index = 0; index < draw.count; ++index) {
            const uint32_t address = draw.indices + index * size;
            if (draw.index_type == 0) {
                indices[index] = image_.memory.read8(address, 0) |
                                 (image_.memory.read8(address + 1, 0) << 8);
            } else if (draw.index_type == 4) {
                indices[index] = image_.memory.read16(address, 0);
            } else if (draw.index_type == 1) {
                indices[index] = image_.memory.read8(address, 0) |
                                 (image_.memory.read8(address + 1, 0) << 8) |
                                 (image_.memory.read8(address + 2, 0) << 16) |
                                 (image_.memory.read8(address + 3, 0) << 24);
            } else {
                indices[index] = image_.memory.read32(address, 0);
            }
            indices[index] += draw.offset;
        }
        return indices;
    }

    static std::vector<int>
    triangle_indices(uint32_t mode, const std::vector<uint32_t>& source) {
        std::vector<int> triangles;
        if (mode == 4) {
            triangles.reserve(source.size());
            for (uint32_t index : source) {
                triangles.push_back(static_cast<int>(index));
            }
        } else if (mode == 6) {
            for (size_t i = 2; i < source.size(); ++i) {
                if ((i & 1u) == 0) {
                    append_triangle(triangles, source[i - 2], source[i - 1],
                                    source[i]);
                } else {
                    append_triangle(triangles, source[i - 1], source[i - 2],
                                    source[i]);
                }
            }
        } else if (mode == 0x13) {
            for (size_t i = 0; i + 3 < source.size(); i += 4) {
                append_triangle(triangles, source[i], source[i + 1],
                                source[i + 2]);
                append_triangle(triangles, source[i], source[i + 2],
                                source[i + 3]);
            }
        } else {
            throw std::runtime_error("unsupported GX2 primitive mode " +
                                     std::to_string(mode));
        }
        return triangles;
    }

    WwhdGpu::Texture& gpu_target(uint32_t descriptor,
                                 SDL_Texture* software_target) {
        const auto value = surface(descriptor);
        const auto key = target_key(value);
        if (const auto found = gpu_targets_.find(key);
            found != gpu_targets_.end()) {
            return found->second;
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(value.width) *
                                    value.height * 4);
        if (!SDL_SetRenderTarget(renderer_, software_target)) {
            fail("SDL_SetRenderTarget");
        }
        SDL_Surface* snapshot = SDL_RenderReadPixels(renderer_, nullptr);
        if (snapshot == nullptr) {
            fail("SDL_RenderReadPixels");
        }
        SDL_Surface* rgba =
            SDL_ConvertSurface(snapshot, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(snapshot);
        if (rgba == nullptr) {
            fail("SDL_ConvertSurface");
        }
        if (rgba->w != static_cast<int>(value.width) ||
            rgba->h != static_cast<int>(value.height)) {
            SDL_DestroySurface(rgba);
            throw std::runtime_error("SDL GPU seed dimensions");
        }
        for (uint32_t row = 0; row < value.height; ++row) {
            std::memcpy(pixels.data() + static_cast<size_t>(row) *
                                            value.width * 4,
                        static_cast<const uint8_t*>(rgba->pixels) +
                            static_cast<size_t>(row) * rgba->pitch,
                        static_cast<size_t>(value.width) * 4);
        }
        SDL_DestroySurface(rgba);
        return gpu_targets_
            .emplace(key, gpu_->create_target(value.width, value.height, pixels))
            .first->second;
    }

    std::vector<uint8_t> decoded_pixels(const Surface& value) {
        std::vector<uint8_t> pixels(
            static_cast<size_t>(value.width) * value.height * 4);
        switch (value.format & 0x3Fu) {
        case 0x01: decode_r8(value, pixels); break;
        case 0x07: decode_rg8(value, pixels); break;
        case 0x19:
        case 0x1A: decode_rgba8(value, pixels); break;
        case 0x0E: decode_r32_float(value, pixels); break;
        case 0x31: decode_bc1(value, pixels); break;
        case 0x33: decode_bc3(value, pixels); break;
        case 0x34: decode_bc4(value, pixels); break;
        case 0x35: decode_bc5(value, pixels); break;
        default:
            throw std::runtime_error(
                "unsupported sampled GX2 texture format " +
                std::to_string(value.format));
        }
        apply_component_map(value.component_map, pixels);
        return pixels;
    }

    WwhdGpu::Texture gpu_sampled_texture(uint32_t descriptor) {
        if (descriptor == 0) {
            return {};
        }
        const auto value = surface(descriptor);
        const auto target_key_value = target_key(value);
        if (const auto target = gpu_targets_.find(target_key_value);
            target != gpu_targets_.end()) {
            // ponytail: CPU snapshot avoids render-target sampling hazards;
            // replace with a GPU copy when this path is performance-critical.
            const auto pixels = gpu_->download(target->second);
            if (std::getenv("NWIIU_GPU_SOURCE_TRACE") != nullptr) {
                std::fprintf(stderr,
                             "GPU-SOURCE target image=%08X first=%u,%u,%u,%u\n",
                             value.image, pixels[0], pixels[1], pixels[2],
                             pixels[3]);
            }
            if (auto snapshot = gpu_sample_copies_.find(target_key_value);
                snapshot != gpu_sample_copies_.end()) {
                gpu_->upload(snapshot->second, pixels);
                return snapshot->second;
            }
            return gpu_sample_copies_
                .emplace(target_key_value,
                         gpu_->create_texture(value.width, value.height, pixels))
                .first->second;
        }
        if (value.image == 0) {
            if (gpu_null_texture_.handle == nullptr) {
                static constexpr std::array<uint8_t, 4> transparent{};
                gpu_null_texture_ = gpu_->create_texture(1, 1, transparent);
            }
            return gpu_null_texture_;
        }
        const auto key = texture_key(value);
        if (const auto found = gpu_textures_.find(key);
            found != gpu_textures_.end()) {
            if (std::getenv("NWIIU_GPU_SOURCE_TRACE") != nullptr) {
                std::fprintf(stderr, "GPU-SOURCE cached image=%08X\n",
                             value.image);
            }
            return found->second;
        }
        auto pixels = decoded_pixels(value);
        if (std::getenv("NWIIU_GPU_DEBUG_SAMPLE") != nullptr &&
            value.image == 0x1EB65000) {
            for (size_t offset = 0; offset < pixels.size(); offset += 4) {
                pixels[offset] = 255;
                pixels[offset + 1] = 0;
                pixels[offset + 2] = 0;
                pixels[offset + 3] = 255;
            }
        }
        if (std::getenv("NWIIU_GPU_SOURCE_TRACE") != nullptr) {
            std::fprintf(stderr,
                         "GPU-SOURCE static image=%08X first=%u,%u,%u,%u\n",
                         value.image, pixels[0], pixels[1], pixels[2],
                         pixels[3]);
        }
        return gpu_textures_
            .emplace(key, gpu_->create_texture(value.width, value.height,
                                               pixels))
            .first->second;
    }
    std::vector<uint32_t> gpu_uniforms(
        std::span<const uint32_t> registers,
        std::span<const Gx2UniformBlockState> blocks,
        std::span<const LatteUniformBlockRef> refs) {
        std::vector<uint32_t> values(registers.begin(), registers.end());
        if (refs.empty()) {
            values.resize(values.size() + 4);
            return values;
        }
        values.reserve(values.size() + refs.size() * 4);
        for (const auto ref : refs) {
            if (ref.block >= blocks.size() || !blocks[ref.block].valid ||
                blocks[ref.block].size < 16 ||
                ref.vector > (blocks[ref.block].size - 16) / 16) {
                throw std::runtime_error("unbound GX2 uniform block");
            }
            const uint32_t address =
                blocks[ref.block].address + ref.vector * 16;
            for (uint32_t component = 0; component < 4; ++component) {
                values.push_back(SDL_Swap32(
                    image_.memory.read32(address + component * 4, 0)));
            }
        }
        return values;
    }


    LatteTranslation translate_shader(const Gx2State& state) {
        const auto words = [this](uint32_t address, uint32_t count) {
            std::vector<uint32_t> values(count);
            for (uint32_t i = 0; i < count; ++i) {
                values[i] = image_.memory.read32(address + i * 4, 0);
            }
            return values;
        };
        const auto bytes = [this](uint32_t descriptor, uint32_t size_offset,
                                  uint32_t data_offset) {
            const uint32_t size =
                image_.memory.read32(descriptor + size_offset, 0);
            const uint32_t data =
                image_.memory.read32(descriptor + data_offset, 0);
            std::vector<uint8_t> values(size);
            image_.memory.read_bytes(data, values, 0);
            return values;
        };
        const auto vs_regs = words(state.vertex_shader_address, 0xD0 / 4);
        const auto ps_regs = words(state.pixel_shader_address, 0xA4 / 4);
        const auto vs = bytes(state.vertex_shader_address, 0xD0, 0xD4);
        const auto ps = bytes(state.pixel_shader_address, 0xA4, 0xA8);
        const auto fs = bytes(state.fetch_shader_address, 0x08, 0x0C);
        return translate_latte({vs_regs, vs, ps_regs, ps, fs});
    }

    static std::pair<SDL_GPUVertexElementFormat, uint32_t>
    gpu_vertex_format(uint32_t format) {
        switch (format) {
        case 13:
        case 14: return {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 1};
        case 29:
        case 30: return {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 2};
        case 47:
        case 48: return {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 3};
        case 34:
        case 35: return {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 4};
        default:
            throw std::runtime_error("unsupported GPU vertex format " +
                                     std::to_string(format));
        }
    }

    bool gpu_draw(const Gx2State& state, SDL_Texture* software_target) {
        if (state.vertex_shader_address == 0 ||
            state.pixel_shader_address == 0 ||
            state.fetch_shader_address == 0) {
            return false;
        }
        if (const char* skip = std::getenv("NWIIU_GPU_SKIP_SHADER");
            skip != nullptr) {
            const uint32_t address =
                static_cast<uint32_t>(std::strtoul(skip, nullptr, 16));
            if (state.vertex_shader_address == address ||
                state.pixel_shader_address == address) {
                return false;
            }
        }
        LatteTranslation translated;
        try {
            translated = translate_shader(state);
        } catch (const std::exception&) {
            return false;
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            state.draw_count <= 40) {
            std::fprintf(
                stderr,
                "GPU-DRAW draw#%llu vs=%08X ps=%08X fs=%08X inputs=%zu "
                "samplers=%zu\n",
                static_cast<unsigned long long>(state.draw_count),
                state.vertex_shader_address, state.pixel_shader_address,
                state.fetch_shader_address, translated.vertex_inputs.size(),
                translated.ps_sampler_slots.size());
        }

        std::vector<WwhdGpu::VertexStream> streams;
        streams.reserve(translated.vertex_inputs.size());
        for (const auto& input : translated.vertex_inputs) {
            if (input.buffer >= state.attribute_buffers.size()) {
                return false;
            }
            const auto& source = state.attribute_buffers[input.buffer];
            if (!source.valid || source.stride == 0) {
                return false;
            }
            const auto [format, components] =
                gpu_vertex_format(input.data_format);
            const uint32_t vertices = source.size / source.stride;
            WwhdGpu::VertexStream stream{
                input.semantic, format, components * 4, {}};
            stream.data.resize(static_cast<size_t>(vertices) *
                               stream.pitch);
            for (uint32_t vertex = 0; vertex < vertices; ++vertex) {
                for (uint32_t component = 0; component < components;
                     ++component) {
                    const uint32_t value = decode_latte_vertex_word(
                        image_.memory.read32(
                            source.address + vertex * source.stride +
                                input.offset + component * 4,
                            0),
                        input.endian);
                    std::memcpy(stream.data.data() +
                                    static_cast<size_t>(vertex) * stream.pitch +
                                    component * 4,
                                &value, 4);
                }
            }
            streams.push_back(std::move(stream));
        }
        if (std::getenv("NWIIU_GPU_DRAW_TRACE") != nullptr &&
            state.draw_count <= 20) {
            for (size_t stream_index = 0; stream_index < streams.size();
                 ++stream_index) {
                const auto& stream = streams[stream_index];
                const size_t vertices =
                    std::min<size_t>(4, stream.data.size() / stream.pitch);
                for (size_t vertex = 0; vertex < vertices; ++vertex) {
                    std::fprintf(stderr, "GPU-VERT draw#%llu s=%zu v=%zu",
                                 static_cast<unsigned long long>(
                                     state.draw_count),
                                 stream_index, vertex);
                    for (uint32_t component = 0;
                         component < stream.pitch / 4; ++component) {
                        float value{};
                        std::memcpy(&value,
                                    stream.data.data() +
                                        vertex * stream.pitch + component * 4,
                                    sizeof(value));
                        std::fprintf(stderr, " %g", value);
                    }
                    std::fputc('\n', stderr);
                }
            }
        }

        const auto source_indices = draw_indices(state.last_draw);
        const auto triangles =
            triangle_indices(state.last_draw.mode, source_indices);
        std::vector<uint32_t> indices;
        indices.reserve(triangles.size());
        for (const int index : triangles) {
            indices.push_back(static_cast<uint32_t>(index));
        }
        std::vector<WwhdGpu::Texture> sampled;
        sampled.reserve(translated.ps_sampler_slots.size());
        for (const uint32_t slot : translated.ps_sampler_slots) {
            if (slot >= state.pixel_texture_addresses.size() ||
                state.pixel_texture_addresses[slot] == 0) {
                return false;
            }
            try {
                sampled.push_back(gpu_sampled_texture(
                    state.pixel_texture_addresses[slot]));
            } catch (const std::exception& error) {
                if (std::getenv("NWIIU_RENDER_TRACE") != nullptr) {
                    const auto value =
                        surface(state.pixel_texture_addresses[slot]);
                    std::fprintf(
                        stderr,
                        "GPU-FALLBACK draw#%llu slot=%u descriptor=%08X "
                        "image=%08X %ux%u pitch=%u size=%u fmt=%X mode=%u %s\n",
                        static_cast<unsigned long long>(state.draw_count),
                        slot, state.pixel_texture_addresses[slot], value.image,
                        value.width, value.height, value.pitch, value.image_size,
                        value.format, value.mode, error.what());
                }
                return false;
            }
        }
        if (std::getenv("NWIIU_GPU_DRAW_TRACE") != nullptr &&
            state.draw_count >= 15 && state.draw_count <= 19) {
            for (size_t index = 0; index < sampled.size(); ++index) {
                const auto pixels = gpu_->download(sampled[index]);
                uint8_t max_rgb = 0;
                uint8_t max_alpha = 0;
                size_t colored = 0;
                uint64_t rgb_sum = 0;
                for (size_t offset = 0; offset < pixels.size(); offset += 4) {
                    const auto rgb = std::max(
                        pixels[offset],
                        std::max(pixels[offset + 1], pixels[offset + 2]));
                    max_rgb = std::max(max_rgb, rgb);
                    max_alpha = std::max(max_alpha, pixels[offset + 3]);
                    colored += rgb != 0;
                    rgb_sum += rgb;
                }
                const auto slot = translated.ps_sampler_slots[index];
                const auto descriptor = state.pixel_texture_addresses[slot];
                const auto value = surface(descriptor);
                std::fprintf(
                    stderr,
                    "GPU-SAMPLE draw#%llu index=%zu slot=%u descriptor=%08X "
                    "image=%08X %ux%u rgb=%u alpha=%u colored=%zu mean=%llu "
                    "first=%u,%u,%u,%u\n",
                    static_cast<unsigned long long>(state.draw_count), index,
                    slot, descriptor, value.image, sampled[index].width,
                    sampled[index].height, max_rgb, max_alpha, colored,
                    static_cast<unsigned long long>(
                        rgb_sum / (pixels.size() / 4)),
                    pixels[0], pixels[1], pixels[2], pixels[3]);
            }
        }
        std::vector<uint32_t> vertex_uniforms;
        std::vector<uint32_t> pixel_uniforms;
        try {
            vertex_uniforms =
                gpu_uniforms(state.vertex_uniform_registers,
                             state.vertex_uniform_blocks,
                             translated.vertex_uniform_blocks);
            pixel_uniforms =
                gpu_uniforms(state.pixel_uniform_registers,
                             state.pixel_uniform_blocks,
                             translated.fragment_uniform_blocks);
        } catch (const std::exception& error) {
            if (std::getenv("NWIIU_RENDER_TRACE") != nullptr) {
                std::fprintf(stderr, "GPU-FALLBACK draw#%llu %s\n",
                             static_cast<unsigned long long>(state.draw_count),
                             error.what());
            }
            return false;
        }
        auto& output =
            gpu_target(state.color_buffers[0].args[0], software_target);
        if (const auto clear = pending_clears_.find(software_target);
            clear != pending_clears_.end()) {
            gpu_->clear(output, clear->second);
            pending_clears_.erase(clear);
        }
        if (std::getenv("NWIIU_GPU_SOURCE_TRACE") != nullptr &&
            state.draw_count >= 15 && state.draw_count <= 17) {
            std::fprintf(stderr, "GPU-BIND draw#%llu output=%p",
                         static_cast<unsigned long long>(state.draw_count),
                         static_cast<void*>(output.handle));
            for (const auto texture : sampled) {
                std::fprintf(stderr, " sampled=%p",
                             static_cast<void*>(texture.handle));
            }
            std::fputc('\n', stderr);
        }
        gpu_->draw(
            {state.vertex_shader_address, state.pixel_shader_address,
             state.fetch_shader_address},
            translated, output, streams, indices,
            vertex_uniforms, pixel_uniforms,
            sampled);
        if (std::getenv("NWIIU_GPU_OUTPUT_TRACE") != nullptr &&
            state.draw_count <= 40) {
            const auto pixels = gpu_->download(output);
            uint8_t max_rgb = 0;
            uint8_t max_alpha = 0;
            size_t visible = 0;
            for (size_t offset = 0; offset < pixels.size(); offset += 4) {
                max_rgb = std::max(
                    max_rgb,
                    std::max(pixels[offset],
                             std::max(pixels[offset + 1], pixels[offset + 2])));
                max_alpha = std::max(max_alpha, pixels[offset + 3]);
                visible += pixels[offset] != 0 || pixels[offset + 1] != 0 ||
                           pixels[offset + 2] != 0;
            }
            const auto target_value =
                surface(state.color_buffers[0].args[0]);
            std::fprintf(
                stderr,
                "GPU-OUTPUT draw#%llu descriptor=%08X image=%08X "
                "rgb=%u alpha=%u visible=%zu\n",
                static_cast<unsigned long long>(state.draw_count),
                state.color_buffers[0].args[0], target_value.image,
                max_rgb, max_alpha, visible);
        }
        return true;
    }

    void clear(const Gx2State& state) {
        auto& output = target(state.last_color_clear.args[0]);
        // GX2 fast clears only touch CMASK metadata; the color data stays
        // intact until rendering resumes. Defer the clear to the next draw
        // on this target so scan copies read the completed frame.
        pending_clears_[output.texture] = {
            as_float(state.last_color_clear.args[1]),
            as_float(state.last_color_clear.args[2]),
            as_float(state.last_color_clear.args[3]),
            as_float(state.last_color_clear.args[4]),
        };
    }

    void apply_pending_clear(SDL_Texture* texture) {
        const auto pending = pending_clears_.find(texture);
        if (pending == pending_clears_.end()) {
            return;
        }
        const auto color = pending->second;
        pending_clears_.erase(pending);
        if (!SDL_SetRenderTarget(renderer_, texture) ||
            !SDL_SetRenderDrawColorFloat(renderer_, color[0], color[1],
                                         color[2], color[3]) ||
            !SDL_RenderClear(renderer_)) {
            fail("SDL GX2 clear");
        }
    }

    void sync_gpu_target(const Surface& value, SDL_Texture* texture) {
        const auto found = gpu_targets_.find(target_key(value));
        if (found == gpu_targets_.end()) {
            return;
        }
        const auto pixels = gpu_->download(found->second);
        if (!SDL_UpdateTexture(texture, nullptr, pixels.data(),
                               static_cast<int>(value.width * 4))) {
            fail("SDL GPU target download");
        }
        gpu_->release(found->second);
        gpu_targets_.erase(found);
    }

    void draw(const Gx2State& state) {
        if (const char* render = std::getenv("NWIIU_GPU_RENDER_SWAP");
            render != nullptr &&
            swap_count_ != std::strtoull(render, nullptr, 10)) {
            return;
        }
        if (const char* skip = std::getenv("NWIIU_GPU_SKIP_TO_SWAP");
            skip != nullptr &&
            swap_count_ < std::strtoull(skip, nullptr, 10)) {
            return;
        }
        if (!state.color_buffers[0].valid ||
            !state.attribute_buffers[0].valid) {
            return;
        }
        trace_shader(state);
        auto& output = target(state.color_buffers[0].args[0]);
        if (gpu_draw(state, output.texture)) {
            return;
        }
        sync_gpu_target(surface(state.color_buffers[0].args[0]),
                        output.texture);
        apply_pending_clear(output.texture);
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            (state.draw_count <= 40 || state.draw_count % 2000 == 0)) {
            const auto value = surface(state.color_buffers[0].args[0]);
            std::fprintf(
                stderr,
                "DRAWTGT draw#%llu tex=%p image=%08X %ux%u "
                "ab=%08X+%u/%u mode=%u count=%u indices=%08X type=%u\n",
                static_cast<unsigned long long>(state.draw_count),
                static_cast<void*>(output.texture), value.image, value.width,
                value.height, state.attribute_buffers[0].address,
                state.attribute_buffers[0].size,
                state.attribute_buffers[0].stride, state.last_draw.mode,
                state.last_draw.count, state.last_draw.indices,
                state.last_draw.index_type);
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            state.last_draw.mode == 0x13 && quad_logs_ < 24) {
            ++quad_logs_;
            const auto& ab = state.attribute_buffers[0];
            std::fprintf(stderr, "QUADVERT draw#%llu ab=%08X stride=%u",
                         static_cast<unsigned long long>(state.draw_count),
                         ab.address, ab.stride);
            const uint32_t floats =
                std::min<uint32_t>(16, ab.size / 4);
            for (uint32_t i = 0; i < floats; ++i) {
                std::fprintf(stderr, " %g",
                             as_float(image_.memory.read32(
                                 ab.address + i * 4, 0)));
            }
            std::fprintf(stderr, " tex=%08X\n",
                         state.pixel_texture_addresses[0]);
        }
        const auto& attribute = state.attribute_buffers[0];
        if (attribute.stride != 8 && attribute.stride != 32) {
            // ponytail: SDL_RenderGeometry is only the 2D fallback; skip 3D
            // layouts until the existing Latte translator is wired to SDL_GPU.
            return;
        }
        const uint32_t buffer_vertex_count = attribute.size / attribute.stride;
        const float normalized_scale =
            state.last_draw.count == 3 && buffer_vertex_count == 4 ? 2.0f
                                                                   : 1.0f;
        const auto source_indices = draw_indices(state.last_draw);
        const auto indices =
            triangle_indices(state.last_draw.mode, source_indices);
        uint32_t vertex_count = buffer_vertex_count;
        for (const int index : indices) {
            vertex_count =
                std::max(vertex_count, static_cast<uint32_t>(index) + 1u);
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            state.draw_count <= 40) {
            std::fprintf(stderr, "DRAWARGS draw#%llu vertices=%u source=",
                         static_cast<unsigned long long>(state.draw_count),
                         vertex_count);
            for (uint32_t index : source_indices) {
                std::fprintf(stderr, "%u,", index);
            }
            std::fprintf(stderr, " texture=%08X\n",
                         state.pixel_texture_addresses[0]);
        }
        std::vector<SDL_Vertex> vertices(vertex_count);
        for (uint32_t index = 0; index < vertex_count; ++index) {
            const uint32_t address =
                attribute.address + index * attribute.stride;
            const float x = as_float(image_.memory.read32(address, 0));
            const float y = as_float(image_.memory.read32(address + 4, 0));
            float screen_x;
            float screen_y;
            float u;
            float v;
            if (attribute.stride == 8) {
                screen_x = x * output.width * normalized_scale;
                screen_y = y * output.height * normalized_scale;
                u = x * normalized_scale;
                v = y * normalized_scale;
            } else {
                screen_x = (x * 0.5f + 0.5f) * output.width;
                screen_y = (0.5f - y * 0.5f) * output.height;
                u = as_float(image_.memory.read32(address + 24, 0));
                v = as_float(image_.memory.read32(address + 28, 0));
            }
            vertices[index] = {
                {screen_x, screen_y},
                {1.0f, 1.0f, 1.0f, 1.0f},
                {u, v},
            };
        }
        SDL_Texture* texture =
            sampled_texture(state.pixel_texture_addresses[0]);
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            overlay_logs_ < 64) {
            for (size_t slot = 0;
                 slot < state.pixel_texture_addresses.size(); ++slot) {
                if (state.pixel_texture_addresses[slot] == 0) {
                    continue;
                }
                const auto bound =
                    surface(state.pixel_texture_addresses[slot]);
                if (bound.image != 0x1EBDE000u) {
                    continue;
                }
                ++overlay_logs_;
                const auto out_surface =
                    surface(state.color_buffers[0].args[0]);
                std::fprintf(stderr,
                             "OVERLAYDRAW draw#%llu slot=%zu out=%08X/%ux%u "
                             "mode=%u count=%u stride=%u v0=(%g,%g)\n",
                             static_cast<unsigned long long>(
                                 state.draw_count),
                             slot, out_surface.image, output.width,
                             output.height, state.last_draw.mode,
                             state.last_draw.count, attribute.stride,
                             as_float(image_.memory.read32(
                                 attribute.address, 0)),
                             as_float(image_.memory.read32(
                                 attribute.address + 4, 0)));
                break;
            }
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            surface(state.color_buffers[0].args[0]).image == 0xF5807800u &&
            composite_logs_ < 24) {
            ++composite_logs_;
            const auto tex_surface =
                state.pixel_texture_addresses[0] != 0
                    ? surface(state.pixel_texture_addresses[0])
                    : Surface{};
            std::string slots;
            for (size_t slot = 0;
                 slot < state.pixel_texture_addresses.size(); ++slot) {
                if (state.pixel_texture_addresses[slot] != 0) {
                    const auto bound =
                        surface(state.pixel_texture_addresses[slot]);
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), " t%zu=%08X/%ux%u",
                                  slot, bound.image, bound.width,
                                  bound.height);
                    slots += buffer;
                }
            }
            std::fprintf(
                stderr,
                "COMPOSITE draw#%llu out=%ux%u self=%d verts=%zu idx=%zu%s\n",
                static_cast<unsigned long long>(state.draw_count),
                output.width, output.height,
                texture == output.texture ? 1 : 0, vertices.size(),
                indices.size(), slots.c_str());
        }
        if (texture == output.texture) {
            if (feedback_target_.texture == nullptr ||
                feedback_target_.width != output.width ||
                feedback_target_.height != output.height) {
                SDL_DestroyTexture(feedback_target_.texture);
                feedback_target_ = {
                    SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET,
                                      static_cast<int>(output.width),
                                      static_cast<int>(output.height)),
                    output.width,
                    output.height,
                };
                if (feedback_target_.texture == nullptr ||
                    !SDL_SetTextureScaleMode(feedback_target_.texture,
                                             SDL_SCALEMODE_LINEAR)) {
                    fail("SDL GX2 feedback target");
                }
            }
            if (!SDL_SetTextureBlendMode(output.texture, SDL_BLENDMODE_NONE) ||
                !SDL_SetRenderTarget(renderer_, feedback_target_.texture) ||
                !SDL_RenderTexture(renderer_, output.texture, nullptr,
                                   nullptr) ||
                !SDL_SetTextureBlendMode(output.texture,
                                         SDL_BLENDMODE_BLEND)) {
                fail("SDL GX2 feedback copy");
            }
            texture = feedback_target_.texture;
        }
        if (!SDL_SetRenderTarget(renderer_, output.texture) ||
            !SDL_RenderGeometry(renderer_, texture, vertices.data(),
                                static_cast<int>(vertices.size()),
                                indices.data(),
                                static_cast<int>(indices.size()))) {
            const auto max_index =
                indices.empty()
                    ? 0
                    : *std::max_element(indices.begin(), indices.end());
            throw std::runtime_error(
                "SDL GX2 draw: " + std::string{SDL_GetError()} +
                " [draw#" + std::to_string(state.draw_count) +
                " mode=" + std::to_string(state.last_draw.mode) +
                " count=" + std::to_string(state.last_draw.count) +
                " offset=" + std::to_string(state.last_draw.offset) +
                " vertices=" + std::to_string(vertices.size()) +
                " max_index=" + std::to_string(max_index) +
                " ab0_size=" + std::to_string(attribute.size) +
                " ab0_stride=" + std::to_string(attribute.stride) + "]");
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            surface(state.color_buffers[0].args[0]).image == 0xF5807800u &&
            probe_logs_ < 8) {
            ++probe_logs_;
            SDL_SetRenderTarget(renderer_, output.texture);
            const SDL_Rect probe{static_cast<int>(output.width / 2),
                                 static_cast<int>(output.height / 2), 4, 4};
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer_, &probe);
                shot != nullptr) {
                const auto* px = static_cast<const uint8_t*>(shot->pixels);
                std::fprintf(stderr,
                             "PROBE draw#%llu fmt=%s px=%02X%02X%02X%02X\n",
                             static_cast<unsigned long long>(
                                 state.draw_count),
                             SDL_GetPixelFormatName(shot->format), px[0],
                             px[1], px[2], px[3]);
                SDL_DestroySurface(shot);
            }
        }
    }

    void copy_to_scan(const Gx2State& state) {
        const uint32_t descriptor = state.last_scan_copy.args[0];
        const uint32_t scan_target = state.last_scan_copy.args[1];
        if (descriptor == 0 || scan_target >= scan_targets_.size()) {
            return;
        }
        const auto value = surface(descriptor);
        if (const auto gpu_source = gpu_targets_.find(target_key(value));
            gpu_source != gpu_targets_.end()) {
            auto& destination = scan_targets_[scan_target];
            if (destination.texture == nullptr ||
                destination.width != value.width ||
                destination.height != value.height) {
                SDL_DestroyTexture(destination.texture);
                destination.texture = SDL_CreateTexture(
                    renderer_, SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STATIC,
                    static_cast<int>(value.width),
                    static_cast<int>(value.height));
                destination.width = value.width;
                destination.height = value.height;
                if (destination.texture == nullptr ||
                    !SDL_SetTextureScaleMode(destination.texture,
                                             SDL_SCALEMODE_LINEAR)) {
                    fail("SDL GPU scan target");
                }
            }
            const auto pixels = gpu_->download(gpu_source->second);
            if (!SDL_UpdateTexture(destination.texture, nullptr,
                                   pixels.data(),
                                   static_cast<int>(value.width * 4))) {
                fail("SDL GPU scan download");
            }
            return;
        }
        const auto source = targets_.find(target_key(value));
        if (source == targets_.end()) {
            if (std::getenv("NWIIU_RENDER_TRACE") != nullptr) {
                std::fprintf(stderr,
                             "SCANMISS image=%08X %ux%u fmt=%08X scan=%u "
                             "targets=%zu\n",
                             value.image, value.width, value.height,
                             value.format, scan_target, targets_.size());
            }
            return;
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr &&
            (scan_copies_ < 40 || state.scan_copy_count % 500 == 0)) {
            ++scan_copies_;
            std::fprintf(stderr,
                         "SCANCOPY image=%08X %ux%u -> scan=%u tex=%p\n",
                         value.image, value.width, value.height, scan_target,
                         static_cast<void*>(source->second.texture));
        }
        auto& destination = scan_targets_[scan_target];
        if (destination.texture == nullptr ||
            destination.width != source->second.width ||
            destination.height != source->second.height) {
            SDL_DestroyTexture(destination.texture);
            destination.texture = SDL_CreateTexture(
                renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                static_cast<int>(source->second.width),
                static_cast<int>(source->second.height));
            destination.width = source->second.width;
            destination.height = source->second.height;
            if (destination.texture == nullptr ||
                !SDL_SetTextureScaleMode(destination.texture,
                                         SDL_SCALEMODE_LINEAR)) {
                fail("SDL GX2 scan target");
            }
        }
        if (!SDL_SetTextureBlendMode(source->second.texture,
                                     SDL_BLENDMODE_NONE) ||
            !SDL_SetRenderTarget(renderer_, destination.texture) ||
            !SDL_RenderTexture(renderer_, source->second.texture, nullptr,
                               nullptr) ||
            !SDL_SetTextureBlendMode(source->second.texture,
                                     SDL_BLENDMODE_BLEND)) {
            fail("SDL GX2 scan copy");
        }
        if (std::getenv("NWIIU_RENDER_TRACE") != nullptr && scan_target == 1 &&
            (scan_probe_logs_ < 8 || state.scan_copy_count % 500 == 0)) {
            ++scan_probe_logs_;
            const SDL_Rect probe{
                static_cast<int>(destination.width / 2),
                static_cast<int>(destination.height / 2), 1, 1};
            SDL_SetRenderTarget(renderer_, destination.texture);
            uint32_t dst_px = 0;
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer_, &probe);
                shot != nullptr) {
                dst_px = *static_cast<const uint32_t*>(shot->pixels);
                SDL_DestroySurface(shot);
            }
            SDL_SetRenderTarget(renderer_, source->second.texture);
            uint32_t src_px = 0;
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer_, &probe);
                shot != nullptr) {
                src_px = *static_cast<const uint32_t*>(shot->pixels);
                SDL_DestroySurface(shot);
            }
            std::fprintf(stderr, "SCANPROBE scancopy#%llu src=%08X dst=%08X\n",
                         static_cast<unsigned long long>(state.scan_copy_count), src_px,
                         dst_px);
        }
    }

    void swap(const Gx2State& state) {
        ++swap_count_;
        const auto& source = scan_targets_[1];
        static const uint64_t dump_interval = [] {
            const char* value = std::getenv("NWIIU_FRAME_DUMP_INTERVAL");
            return value == nullptr
                       ? uint64_t{64}
                       : std::max(uint64_t{1}, static_cast<uint64_t>(
                                                       std::strtoull(
                                                           value, nullptr, 10)));
        }();
        if (const char* dir = std::getenv("NWIIU_FRAME_DUMP");
            dir != nullptr && swap_count_ % dump_interval == 0) {
            std::filesystem::create_directories(dir);
            std::fprintf(stderr,
                         "FRAME swap=%llu scan1=%p %ux%u draw_count=%llu "
                         "scan_copy_count=%llu flush_count=%u "
                         "vsync_wait_count=%llu\n",
                         static_cast<unsigned long long>(swap_count_),
                         static_cast<void*>(source.texture), source.width,
                         source.height,
                         static_cast<unsigned long long>(state.draw_count),
                         static_cast<unsigned long long>(
                             state.scan_copy_count),
                         state.flush_count,
                         static_cast<unsigned long long>(
                             state.vsync_wait_count));
            auto dump_texture = [&](SDL_Texture* texture,
                                    const std::string& name) {
                if (texture == nullptr) {
                    return;
                }
                SDL_SetRenderTarget(renderer_, texture);
                if (SDL_Surface* shot =
                        SDL_RenderReadPixels(renderer_, nullptr);
                    shot != nullptr) {
                    const auto path =
                        std::filesystem::path(dir) /
                        (name + "_" + std::to_string(swap_count_) + ".bmp");
                    SDL_SaveBMP(shot, path.string().c_str());
                    SDL_DestroySurface(shot);
                }
            };
            dump_texture(source.texture, "frame");
            if (swap_count_ == 128) {
                size_t index = 0;
                for (const auto& [key, target] : targets_) {
                    char name[64];
                    std::snprintf(name, sizeof(name), "target%zu_%08X_%ux%u",
                                  index++, std::get<0>(key), target.width,
                                  target.height);
                    dump_texture(target.texture, name);
                }
            }
        }
        if (source.texture == nullptr) {
            return;
        }
        if (!SDL_SetRenderTarget(renderer_, nullptr) ||
            !SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) ||
            !SDL_RenderClear(renderer_) ||
            !SDL_RenderTexture(renderer_, source.texture, nullptr, nullptr) ||
            !SDL_RenderPresent(renderer_)) {
            fail("SDL GX2 present");
        }
    }

    void dump_draw(const Gx2State& state) {
        const char* dir = std::getenv("NWIIU_SHADER_DUMP");
        if (dir == nullptr) {
            return;
        }
        if (const char* render = std::getenv("NWIIU_GPU_RENDER_SWAP");
            render != nullptr &&
            swap_count_ != std::strtoull(render, nullptr, 10)) {
            return;
        }
        std::filesystem::create_directories(dir);
        auto& memory = image_.memory;
        auto dump_range = [&](const char* name, uint32_t address,
                              uint32_t size) {
            const auto path = std::filesystem::path(dir) / name;
            if (!dumped_.insert(path.string()).second) {
                return;
            }
            std::vector<uint8_t> bytes(size);
            for (uint32_t i = 0; i < size; ++i) {
                bytes[i] = memory.read8(address + i, 0);
            }
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        };
        auto dump_program = [&](const char* kind, uint32_t shader,
                                uint32_t size_offset, uint32_t data_offset) {
            if (shader == 0) {
                return std::string{"none"};
            }
            const uint32_t size = memory.read32(shader + size_offset, 0);
            const uint32_t data = memory.read32(shader + data_offset, 0);
            char name[64];
            std::snprintf(name, sizeof(name), "%s_%08X_%u.bin", kind, data,
                          size);
            const auto path = std::filesystem::path(dir) / name;
            if (!dumped_.insert(path.string()).second) {
                return std::string{name};
            }
            std::vector<uint8_t> bytes(size);
            for (uint32_t i = 0; i < size; ++i) {
                bytes[i] = memory.read8(data + i, 0);
            }
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            return std::string{name};
        };
        const auto vs = dump_program("vs", state.vertex_shader_address, 0xD0,
                                     0xD4);
        const auto ps = dump_program("ps", state.pixel_shader_address, 0xA4,
                                     0xA8);
        const auto fs = dump_program("fs", state.fetch_shader_address, 0x08,
                                     0x0C);
        if (state.vertex_shader_address != 0) {
            dump_range("vs_regs.bin", state.vertex_shader_address, 0xD0);
        }
        if (state.pixel_shader_address != 0) {
            dump_range("ps_regs.bin", state.pixel_shader_address, 0xA4);
        }
        std::ofstream log(std::filesystem::path(dir) / "draws.log",
                          std::ios::app);
        log << "draw#" << state.draw_count << " vs=" << vs << " ps=" << ps
            << " fs=" << fs << " mode=" << state.last_draw.mode
            << " count=" << state.last_draw.count
            << " indexed=" << state.last_draw.indexed
            << " index_type=" << state.last_draw.index_type;
        if (state.fetch_shader_address != 0) {
            log << " fs_attribs="
                << memory.read32(state.fetch_shader_address + 0x10, 0);
        }
        for (size_t i = 0; i < state.attribute_buffers.size(); ++i) {
            const auto& buffer = state.attribute_buffers[i];
            if (buffer.valid) {
                log << " ab" << i << "=@" << std::hex << buffer.address
                    << std::dec << "+" << buffer.size << "/" << buffer.stride;
            }
        }
        for (size_t i = 0; i < state.pixel_texture_addresses.size(); ++i) {
            if (state.pixel_texture_addresses[i] != 0) {
                log << " tex" << i << "=@" << std::hex
                    << state.pixel_texture_addresses[i] << std::dec;
            }
        }
        log << '\n';
    }

    void evict_invalidated_textures() {
        const auto ranges = gx2_.take_texture_invalidates();
        if (ranges.empty()) {
            return;
        }
        for (auto it = textures_.begin(); it != textures_.end();) {
            const uint32_t image = std::get<0>(it->first.first);
            const uint32_t recorded_size = std::get<4>(it->first.first);
            const uint32_t image_size =
                recorded_size != 0 ? recorded_size : 0x1000u;
            bool overlaps = false;
            for (const auto& [buffer, size] : ranges) {
                if (size == 0xFFFFFFFFu) {
                    overlaps = true;
                    break;
                }
                if (image < buffer + size && buffer < image + image_size) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) {
                SDL_DestroyTexture(it->second);
                it = textures_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void queue_audio() {
        std::array<int16_t, CafeRuntime::kAudioFrameSamples> samples;
        while (cafe_.pop_audio_frame(samples)) {
            if (!SDL_PutAudioStreamData(
                    audio_, samples.data(),
                    static_cast<int>(samples.size() * sizeof(samples[0])))) {
                fail("SDL_PutAudioStreamData");
            }
        }
    }

    static void render_event(void* context, Gx2Event event,
                             const Gx2State& state) {
        auto& self = *static_cast<WwhdRenderer*>(context);
        const bool dumping = std::getenv("NWIIU_SHADER_DUMP") != nullptr;
        try {
            switch (event) {
            case Gx2Event::clear_color:
                self.clear(state);
                break;
            case Gx2Event::draw:
                self.dump_draw(state);
                self.draw(state);
                break;
            case Gx2Event::copy_scan_buffer:
                self.copy_to_scan(state);
                break;
            case Gx2Event::swap_scan_buffers:
                self.swap(state);
                self.evict_invalidated_textures();
                self.queue_audio();
                self.cafe_.service_audio_frame();
                break;
            }
        } catch (const std::exception& error) {
            if (!dumping) {
                throw;
            }
            std::fprintf(stderr, "DUMP-SKIP: %s\n", error.what());
        }
    }

    ExecutionImage& image_;
    CafeRuntime& cafe_;
    Gx2Runtime& gx2_;
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_AudioStream* audio_{};
    std::unique_ptr<WwhdGpu> gpu_;
    WwhdGpu::Texture gpu_null_texture_;
    std::map<TargetKey, WwhdGpu::Texture> gpu_targets_;
    std::map<TextureKey, WwhdGpu::Texture> gpu_textures_;
    std::map<TargetKey, WwhdGpu::Texture> gpu_sample_copies_;
    std::map<TargetKey, Target> targets_;
    std::map<TextureKey, SDL_Texture*> textures_;
    std::array<Target, 5> scan_targets_{};
    Target feedback_target_;
    std::set<std::string> dumped_;
    uint64_t swap_count_{};
    uint64_t scan_copies_{};
    uint64_t composite_logs_{};
    uint64_t probe_logs_{};
    uint64_t scan_probe_logs_{};
    uint64_t clear_logs_{};
    uint64_t quad_logs_{};
    uint64_t overlay_logs_{};
    std::map<SDL_Texture*, std::array<float, 4>> pending_clears_;
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> shader_traces_;
};

} // namespace nwii::runtime
