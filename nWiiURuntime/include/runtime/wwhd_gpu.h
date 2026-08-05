#pragma once

#include "runtime/latte_shader.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace nwii::runtime {

class WwhdGpu {
public:
    struct Texture {
        SDL_GPUTexture* handle{};
        uint32_t width{};
        uint32_t height{};
    };

    struct VertexStream {
        uint32_t location{};
        SDL_GPUVertexElementFormat format{};
        uint32_t pitch{};
        std::vector<uint8_t> data;
    };

    WwhdGpu() {
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true,
                                      "vulkan");
        if (device_ == nullptr) {
            fail("SDL_CreateGPUDevice");
        }
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = SDL_GPU_FILTER_LINEAR;
        info.mag_filter = SDL_GPU_FILTER_LINEAR;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_ = SDL_CreateGPUSampler(device_, &info);
        if (sampler_ == nullptr) {
            SDL_DestroyGPUDevice(device_);
            device_ = nullptr;
            fail("SDL_CreateGPUSampler");
        }
    }

    void wait_idle() {
        if (!SDL_WaitForGPUIdle(device_)) {
            fail("SDL_WaitForGPUIdle");
        }
    }

    ~WwhdGpu() {
        SDL_WaitForGPUIdle(device_);
        for (const auto& [key, pipeline] : pipelines_) {
            static_cast<void>(key);
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        }
        SDL_ReleaseGPUSampler(device_, sampler_);
        SDL_DestroyGPUDevice(device_);
    }

    WwhdGpu(const WwhdGpu&) = delete;
    WwhdGpu& operator=(const WwhdGpu&) = delete;

    Texture create_target(uint32_t width, uint32_t height,
                          std::span<const uint8_t> pixels = {}) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                     SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        auto* texture = SDL_CreateGPUTexture(device_, &info);
        if (texture == nullptr) {
            fail("SDL_CreateGPUTexture target");
        }
        Texture result{texture, width, height};
        if (!pixels.empty()) {
            upload(result, pixels);
        }
        return result;
    }

    Texture create_texture(uint32_t width, uint32_t height,
                           std::span<const uint8_t> pixels) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        auto* texture = SDL_CreateGPUTexture(device_, &info);
        if (texture == nullptr) {
            fail("SDL_CreateGPUTexture sampled");
        }
        Texture result{texture, width, height};
        upload(result, pixels);
        return result;
    }

    void upload(Texture texture, std::span<const uint8_t> pixels) {
        auto* transfer = transfer_buffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                         pixels.size());
        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            fail("SDL_MapGPUTransferBuffer texture");
        }
        std::memcpy(mapped, pixels.data(), pixels.size());
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        auto* command = command_buffer();
        auto* copy = SDL_BeginGPUCopyPass(command);
        const SDL_GPUTextureTransferInfo source{
            transfer, 0, texture.width, texture.height};
        const SDL_GPUTextureRegion destination{
            texture.handle, 0, 0, 0, 0, 0, texture.width, texture.height, 1};
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        submit(command);
        wait_idle();
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
    }

    void release(Texture texture) {
        if (texture.handle != nullptr) {
            SDL_ReleaseGPUTexture(device_, texture.handle);
        }
    }

    void clear(Texture target, const std::array<float, 4>& color) {
        auto* command = command_buffer();
        const SDL_GPUColorTargetInfo info{
            target.handle, 0, 0,
            {color[0], color[1], color[2], color[3]},
            SDL_GPU_LOADOP_CLEAR, SDL_GPU_STOREOP_STORE,
            nullptr, 0, 0, false, false, 0, 0};
        auto* pass = SDL_BeginGPURenderPass(command, &info, 1, nullptr);
        SDL_EndGPURenderPass(pass);
        submit(command);
    }

    void draw(const std::tuple<uint32_t, uint32_t, uint32_t>& key,
              const LatteTranslation& translated, Texture target,
              std::span<const VertexStream> streams,
              std::span<const uint32_t> indices,
              std::span<const uint32_t> vertex_uniforms,
              std::span<const uint32_t> fragment_uniforms,
              std::span<const Texture> sampled) {
        auto* pipeline = graphics_pipeline(key, translated, streams);
        std::vector<SDL_GPUBuffer*> buffers;
        std::vector<SDL_GPUTransferBuffer*> transfers;
        std::vector<SDL_GPUBufferBinding> vertex_bindings;
        buffers.reserve(streams.size() + 1);
        transfers.reserve(streams.size() + 1);
        vertex_bindings.reserve(streams.size());

        auto* command = command_buffer();
        auto* copy = SDL_BeginGPUCopyPass(command);
        for (const auto& stream : streams) {
            auto [buffer, transfer] = upload_buffer(
                copy, SDL_GPU_BUFFERUSAGE_VERTEX, stream.data);
            buffers.push_back(buffer);
            transfers.push_back(transfer);
            vertex_bindings.push_back({buffer, 0});
        }
        const auto index_bytes = std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(indices.data()),
            indices.size_bytes()};
        auto [index_buffer, index_transfer] = upload_buffer(
            copy, SDL_GPU_BUFFERUSAGE_INDEX, index_bytes);
        buffers.push_back(index_buffer);
        transfers.push_back(index_transfer);
        SDL_EndGPUCopyPass(copy);

        SDL_PushGPUVertexUniformData(
            command, 0, vertex_uniforms.data(),
            static_cast<uint32_t>(vertex_uniforms.size_bytes()));
        SDL_PushGPUFragmentUniformData(
            command, 0, fragment_uniforms.data(),
            static_cast<uint32_t>(fragment_uniforms.size_bytes()));
        const SDL_GPUColorTargetInfo color{
            target.handle, 0, 0, {}, SDL_GPU_LOADOP_LOAD,
            SDL_GPU_STOREOP_STORE, nullptr, 0, 0, false, false, 0, 0};
        auto* pass = SDL_BeginGPURenderPass(command, &color, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, vertex_bindings.data(),
                                 vertex_bindings.size());
        const SDL_GPUBufferBinding index_binding{index_buffer, 0};
        SDL_BindGPUIndexBuffer(pass, &index_binding,
                               SDL_GPU_INDEXELEMENTSIZE_32BIT);
        std::vector<SDL_GPUTextureSamplerBinding> sampler_bindings;
        sampler_bindings.reserve(sampled.size());
        for (const auto texture : sampled) {
            sampler_bindings.push_back({texture.handle, sampler_});
        }
        if (!sampler_bindings.empty()) {
            SDL_BindGPUFragmentSamplers(pass, 0, sampler_bindings.data(),
                                        sampler_bindings.size());
        }
        SDL_DrawGPUIndexedPrimitives(pass, indices.size(), 1, 0, 0, 0);
        SDL_EndGPURenderPass(pass);
        submit(command);
        for (auto* transfer : transfers) {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
        }
        for (auto* buffer : buffers) {
            SDL_ReleaseGPUBuffer(device_, buffer);
        }
    }

    std::vector<uint8_t> download(Texture texture) {
        const size_t size = static_cast<size_t>(texture.width) *
                            texture.height * 4;
        auto* transfer = transfer_buffer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                                         size);
        auto* command = command_buffer();
        auto* copy = SDL_BeginGPUCopyPass(command);
        const SDL_GPUTextureRegion source{texture.handle, 0, 0, 0, 0, 0,
                                          texture.width, texture.height, 1};
        const SDL_GPUTextureTransferInfo destination{
            transfer, 0, texture.width, texture.height};
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        submit(command);
        if (!SDL_WaitForGPUIdle(device_)) {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            fail("SDL_WaitForGPUIdle");
        }
        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            fail("SDL_MapGPUTransferBuffer download");
        }
        std::vector<uint8_t> pixels(size);
        std::memcpy(pixels.data(), mapped, size);
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return pixels;
    }

private:
    [[noreturn]] static void fail(const char* operation) {
        throw std::runtime_error(std::string{operation} + ": " +
                                 SDL_GetError());
    }

    SDL_GPUCommandBuffer* command_buffer() {
        auto* command = SDL_AcquireGPUCommandBuffer(device_);
        if (command == nullptr) {
            fail("SDL_AcquireGPUCommandBuffer");
        }
        return command;
    }

    void submit(SDL_GPUCommandBuffer* command) {
        if (!SDL_SubmitGPUCommandBuffer(command)) {
            fail("SDL_SubmitGPUCommandBuffer");
        }
    }

    SDL_GPUTransferBuffer* transfer_buffer(SDL_GPUTransferBufferUsage usage,
                                            size_t size) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = usage;
        info.size = static_cast<uint32_t>(size);
        auto* transfer = SDL_CreateGPUTransferBuffer(device_, &info);
        if (transfer == nullptr) {
            fail("SDL_CreateGPUTransferBuffer");
        }
        return transfer;
    }

    std::pair<SDL_GPUBuffer*, SDL_GPUTransferBuffer*> upload_buffer(
        SDL_GPUCopyPass* copy, SDL_GPUBufferUsageFlags usage,
        std::span<const uint8_t> bytes) {
        SDL_GPUBufferCreateInfo buffer_info{};
        buffer_info.usage = usage;
        buffer_info.size = static_cast<uint32_t>(bytes.size());
        auto* buffer = SDL_CreateGPUBuffer(device_, &buffer_info);
        if (buffer == nullptr) {
            fail("SDL_CreateGPUBuffer");
        }
        auto* transfer = transfer_buffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                         bytes.size());
        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            SDL_ReleaseGPUBuffer(device_, buffer);
            fail("SDL_MapGPUTransferBuffer buffer");
        }
        std::memcpy(mapped, bytes.data(), bytes.size());
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        const SDL_GPUTransferBufferLocation source{transfer, 0};
        const SDL_GPUBufferRegion destination{
            buffer, 0, static_cast<uint32_t>(bytes.size())};
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);
        return {buffer, transfer};
    }

    SDL_GPUGraphicsPipeline* graphics_pipeline(
        const std::tuple<uint32_t, uint32_t, uint32_t>& key,
        const LatteTranslation& translated,
        std::span<const VertexStream> streams) {
        if (const auto found = pipelines_.find(key);
            found != pipelines_.end()) {
            return found->second;
        }
        const SDL_GPUShaderCreateInfo vertex_info{
            translated.vertex_spirv.size() * sizeof(uint32_t),
            reinterpret_cast<const uint8_t*>(translated.vertex_spirv.data()),
            "main", SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERSTAGE_VERTEX,
            0, 0, 0, 1, 0};
        const SDL_GPUShaderCreateInfo fragment_info{
            translated.fragment_spirv.size() * sizeof(uint32_t),
            reinterpret_cast<const uint8_t*>(translated.fragment_spirv.data()),
            "main", SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERSTAGE_FRAGMENT,
            static_cast<uint32_t>(translated.ps_sampler_slots.size()),
            0, 0, 1, 0};
        auto* vertex = SDL_CreateGPUShader(device_, &vertex_info);
        auto* fragment = SDL_CreateGPUShader(device_, &fragment_info);
        if (vertex == nullptr || fragment == nullptr) {
            SDL_ReleaseGPUShader(device_, vertex);
            SDL_ReleaseGPUShader(device_, fragment);
            fail("SDL_CreateGPUShader");
        }
        std::vector<SDL_GPUVertexBufferDescription> descriptions;
        std::vector<SDL_GPUVertexAttribute> attributes;
        descriptions.reserve(streams.size());
        attributes.reserve(streams.size());
        for (uint32_t slot = 0; slot < streams.size(); ++slot) {
            descriptions.push_back({slot, streams[slot].pitch,
                                    SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
            attributes.push_back({streams[slot].location, slot,
                                  streams[slot].format, 0});
        }
        const SDL_GPUColorTargetDescription color{
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            {SDL_GPU_BLENDFACTOR_SRC_ALPHA,
             SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
             SDL_GPU_BLENDOP_ADD,
             SDL_GPU_BLENDFACTOR_ONE,
             SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
             SDL_GPU_BLENDOP_ADD,
             0, false, false, 0, 0}};
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex;
        info.fragment_shader = fragment;
        info.vertex_input_state = {descriptions.data(),
                                   static_cast<uint32_t>(descriptions.size()),
                                   attributes.data(),
                                   static_cast<uint32_t>(attributes.size())};
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info = {&color, 1, SDL_GPU_TEXTUREFORMAT_INVALID,
                            false, 0, 0, 0};
        auto* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
        SDL_ReleaseGPUShader(device_, vertex);
        SDL_ReleaseGPUShader(device_, fragment);
        if (pipeline == nullptr) {
            fail("SDL_CreateGPUGraphicsPipeline");
        }
        pipelines_.emplace(key, pipeline);
        return pipeline;
    }

    SDL_GPUDevice* device_{};
    SDL_GPUSampler* sampler_{};
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>,
             SDL_GPUGraphicsPipeline*>
        pipelines_;
};

} // namespace nwii::runtime
