// SPDX-License-Identifier: MPL-2.0
#include "render/gpu.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace sterrenvaarder::render {

namespace {
struct EmbeddedShader {
    const char* name;
    const unsigned char* data;
    std::size_t size;
};
#include "render/shaders_spirv.inc"

std::runtime_error sdl_error(const char* what) { return std::runtime_error(std::string(what) + ": " + SDL_GetError()); }
}  // namespace

Gpu::Gpu(SDL_Window* window, bool debug) : window_(window) {
    dev_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, debug, nullptr);
    if (!dev_) throw sdl_error("SDL_CreateGPUDevice");
    if (window_ && !SDL_ClaimWindowForGPUDevice(dev_, window_)) throw sdl_error("SDL_ClaimWindowForGPUDevice");

    SDL_GPUSamplerCreateInfo s{};
    s.min_filter = SDL_GPU_FILTER_LINEAR;
    s.mag_filter = SDL_GPU_FILTER_LINEAR;
    s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    s.address_mode_u = s.address_mode_v = s.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    s.enable_anisotropy = true;
    s.max_anisotropy = 16;
    s.max_lod = 1000;
    linear_ = SDL_CreateGPUSampler(dev_, &s);
    s.address_mode_u = s.address_mode_v = s.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s.enable_anisotropy = false;
    s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    s.max_lod = 0;
    clamp_ = SDL_CreateGPUSampler(dev_, &s);
    if (!linear_ || !clamp_) throw sdl_error("SDL_CreateGPUSampler");
}

Gpu::~Gpu() {
    if (!dev_) return;
    SDL_WaitForGPUIdle(dev_);
    SDL_ReleaseGPUSampler(dev_, linear_);
    SDL_ReleaseGPUSampler(dev_, clamp_);
    if (window_) SDL_ReleaseWindowFromGPUDevice(dev_, window_);
    SDL_DestroyGPUDevice(dev_);
}

SDL_GPUShader* Gpu::shader(std::string_view name, std::uint32_t samplers, std::uint32_t uniform_buffers) {
    for (auto& s : k_shaders) {
        if (name != s.name) continue;
        SDL_GPUShaderCreateInfo info{};
        info.code = s.data;
        info.code_size = s.size;
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = name.ends_with(".vert") ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = samplers;
        info.num_uniform_buffers = uniform_buffers;
        auto* sh = SDL_CreateGPUShader(dev_, &info);
        if (!sh) throw sdl_error("SDL_CreateGPUShader");
        return sh;
    }
    throw std::runtime_error("unknown shader " + std::string(name));
}

SDL_GPUBuffer* Gpu::buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::uint32_t size) {
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = size;
    auto* buf = SDL_CreateGPUBuffer(dev_, &bi);
    if (!buf) throw sdl_error("SDL_CreateGPUBuffer");
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    auto* tb = SDL_CreateGPUTransferBuffer(dev_, &ti);
    std::memcpy(SDL_MapGPUTransferBuffer(dev_, tb, false), data, size);
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    auto* cmd = SDL_AcquireGPUCommandBuffer(dev_);
    auto* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion dst{buf, 0, size};
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev_, tb);
    return buf;
}

SDL_GPUTexture* Gpu::texture(const Image& img, bool srgb) {
    std::uint32_t levels = 1 + static_cast<std::uint32_t>(std::floor(std::log2(std::max(img.width, img.height))));
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = img.width;
    ci.height = img.height;
    ci.layer_count_or_depth = 1;
    ci.num_levels = levels;
    auto* tex = SDL_CreateGPUTexture(dev_, &ci);
    if (!tex) throw sdl_error("SDL_CreateGPUTexture");
    auto size = static_cast<std::uint32_t>(img.rgba.size());
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    auto* tb = SDL_CreateGPUTransferBuffer(dev_, &ti);
    std::memcpy(SDL_MapGPUTransferBuffer(dev_, tb, false), img.rgba.data(), size);
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    auto* cmd = SDL_AcquireGPUCommandBuffer(dev_);
    auto* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
    dst.w = img.width;
    dst.h = img.height;
    dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    if (levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, tex);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev_, tb);
    return tex;
}

SDL_GPUTexture* Gpu::render_target(SDL_GPUTextureFormat fmt, std::uint32_t w, std::uint32_t h, SDL_GPUSampleCount samples,
                                   bool sampled) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    bool depth = fmt == SDL_GPU_TEXTUREFORMAT_D32_FLOAT || fmt == SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    ci.usage = depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (sampled && samples == SDL_GPU_SAMPLECOUNT_1) ci.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = samples;
    auto* t = SDL_CreateGPUTexture(dev_, &ci);
    if (!t) throw sdl_error("render target");
    return t;
}

std::vector<std::uint8_t> Gpu::download_rgba8(SDL_GPUTexture* tex, std::uint32_t w, std::uint32_t h) {
    std::uint32_t size = w * h * 4;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = size;
    auto* tb = SDL_CreateGPUTransferBuffer(dev_, &ti);
    auto* cmd = SDL_AcquireGPUCommandBuffer(dev_);
    auto* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = tex;
    src.w = w;
    src.h = h;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(dev_, true, &fence, 1);
    SDL_ReleaseGPUFence(dev_, fence);
    std::vector<std::uint8_t> out(size);
    std::memcpy(out.data(), SDL_MapGPUTransferBuffer(dev_, tb, false), size);
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    SDL_ReleaseGPUTransferBuffer(dev_, tb);
    return out;
}

}  // namespace sterrenvaarder::render
