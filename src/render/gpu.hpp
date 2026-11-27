// SPDX-License-Identifier: MPL-2.0
#pragma once
// Thin helpers over SDL_GPU: one API for Vulkan, Direct3D 12 and Metal.
#include <SDL3/SDL.h>

#include <cstdint>
#include <string_view>
#include <vector>

#include "sterrenvaarder/texture.hpp"

namespace sterrenvaarder::render {

class Gpu {
public:
    // window may be null for offscreen rendering (screenshots, tests).
    explicit Gpu(SDL_Window* window, bool debug = false);
    ~Gpu();
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;

    SDL_GPUDevice* device() const { return dev_; }
    SDL_Window* window() const { return window_; }
    const char* driver() const { return SDL_GetGPUDeviceDriver(dev_); }

    SDL_GPUShader* shader(std::string_view name, std::uint32_t samplers, std::uint32_t uniform_buffers);
    SDL_GPUBuffer* buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::uint32_t size);
    // RGBA8 texture with a full mip chain generated on the GPU.
    SDL_GPUTexture* texture(const Image& img, bool srgb);
    SDL_GPUTexture* render_target(SDL_GPUTextureFormat fmt, std::uint32_t w, std::uint32_t h,
                                  SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1, bool sampled = true);
    SDL_GPUSampler* linear_sampler() const { return linear_; }
    SDL_GPUSampler* clamp_sampler() const { return clamp_; }

    // Reads back an RGBA8 texture (blocking).
    std::vector<std::uint8_t> download_rgba8(SDL_GPUTexture* tex, std::uint32_t w, std::uint32_t h);

private:
    SDL_GPUDevice* dev_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_GPUSampler* linear_ = nullptr;
    SDL_GPUSampler* clamp_ = nullptr;
};

}  // namespace sterrenvaarder::render
