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
#include "render/hud_batch.hpp"
#include "render/mat.hpp"
#include "sterrenvaarder/model.hpp"
#include "sterrenvaarder/universe.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder::render {

struct Camera {
    DVec3 pos;
    float yaw = 0, pitch = 0;  // radians; yaw 0 looks down -Z
    float fov_y = 0.80f;       // vertical FOV (Hor+: wider screens see more, not less)
    // Full orientation (e.g. a chase camera that rolls with the ship); overrides yaw/pitch.
    bool has_basis = false;
    Vec3 basis_forward{0, 0, -1}, basis_up{0, 1, 0};
    void set_basis(Vec3 fwd, Vec3 up) { has_basis = true; basis_forward = fwd; basis_up = up; }
    // Projects a world point to output pixels. Returns false if it is behind the camera.
    bool project(DVec3 world, float out_w, float out_h, float& px, float& py) const;
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

    // Models outside the static system (ships, loot). Loaded once and cached.
    struct ModelHandle { void* p = nullptr; explicit operator bool() const { return p; } };
    ModelHandle load_model(const std::string& path, const std::vector<std::string>& material_libraries);
    // Shared mesh files (.vms) that models may reference, e.g. interface/interface.generic.vms.
    void add_mesh_library(const std::string& path);
    // Queues one instance for the next render() call. lod_ranges as in the archetype.
    void draw_model(ModelHandle m, DVec3 pos, const Mat4& rotation, const std::vector<float>& lod_ranges);
    // A camera-facing additive glow for the next render() call (weapon bolts, flashes,
    // explosions). Size is the half-width in metres; intensity scales HDR brightness.
    void draw_glow(DVec3 pos, float size, Color3 color, float intensity);
    // Draws a model as a flat HUD element for the next render() call (e.g. the original
    // interface models from the user's own data). The model's local x/y units are mapped
    // to pixels around (px, py), y up. Drawn unlit, before the vector HUD.
    void draw_hud_model(ModelHandle m, float px, float py, float pixels_per_unit, HudColor tint = {});
    // Optional overlay for the next render() call, in output pixels.
    void set_hud(const HudBatch* hud) { hud_ = hud; }

    struct Stats { int objects_drawn = 0, draw_calls = 0, triangles = 0, culled_by_lod = 0; };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    StarSystem system_;
    const HudBatch* hud_ = nullptr;
    Stats stats_;
};

}  // namespace sterrenvaarder::render
