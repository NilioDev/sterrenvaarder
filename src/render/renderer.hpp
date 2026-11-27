// SPDX-License-Identifier: MPL-2.0
#pragma once
// System renderer: loads a star system from the game data and draws it with a modern
// HDR pipeline (MSAA, bloom, ACES tonemapping, upscaling + adaptive sharpening).
#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/gpu.hpp"
#include "render/mat.hpp"
#include "sterrenvaarder/model.hpp"
#include "sterrenvaarder/universe.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder::render {

struct Camera {
    DVec3 pos;
    float yaw = 0, pitch = 0;  // radians; yaw 0 looks down -Z
    float fov_y = 0.80f;       // vertical FOV (Hor+: wider screens see more, not less)
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    void look_at(DVec3 target);
};

struct RenderSettings {
    float render_scale = 1.0f;  // < 1 renders below output resolution and upscales
    float sharpness = 0.5f;
    int msaa = 4;               // 1, 2, 4 or 8
    float exposure = 1.1f;
    float bloom = 0.06f;
    float normal_strength = 1.0f;  // derived normal maps (0 = off)
    float lod_scale = 1.0f;        // 1 = original draw distances; > 1 extends them
};

class Renderer {
public:
    Renderer(Gpu& gpu, const Vfs& vfs, const Universe& universe);
    ~Renderer();

    void load_system(std::string_view nickname);
    const StarSystem& system() const { return system_; }

    // Renders one frame into `target` (format must match `target_format` given at resize).
    void render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, std::uint32_t out_w, std::uint32_t out_h,
                const Camera& cam, const RenderSettings& s, double time_seconds);
    void set_output_format(SDL_GPUTextureFormat fmt);

    struct Stats { int objects_drawn = 0, draw_calls = 0, triangles = 0, culled_by_lod = 0; };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    StarSystem system_;
    Stats stats_;
};

}  // namespace sterrenvaarder::render
