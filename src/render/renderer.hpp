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
    float near_plane = 0.5f;   // metres (rooms use the scene camera's own)
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

// Lighting for an interior scene (base rooms): replaces the star system's sky, objects
// and lights while it is set; models are then drawn with draw_model.
struct RoomLighting {
    Color3 ambient;
    std::vector<SystemLight> lights;
    Color3 clear{0, 0, 0};
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
    // Effect particles: textures come from the .txm libraries named by the effect.
    void add_effect_textures(const std::string& txm_path);
    // frame (0..1) picks the frame of an animated texture over its frames.
    void draw_sprite(DVec3 pos, float size, float rotation, Color3 color, float alpha, const std::string& texture,
                     bool additive, float frame = 0);
    // A sprite lying in the plane spanned by the unit vectors u and v instead of facing the camera.
    void draw_flat_sprite(DVec3 pos, float size, Vec3 u, Vec3 v, Color3 color, float alpha, const std::string& texture,
                          bool additive, float frame = 0);
    // A shield bubble: an ellipsoid with the given axes (orientation times radius) around a
    // ship, glowing at its rim and brightest around hit_dir (in the ellipsoid's own frame).
    void draw_shield(DVec3 center, Vec3 ax0, Vec3 ax1, Vec3 ax2, Vec3 hit_dir, Color3 color, float strength);
    // A textured band from one point to another, facing the camera; half-width, colour and
    // alpha blend smoothly from the first end to the second.
    void draw_streak(DVec3 from, DVec3 to, float width_from, float width_to, Color3 color_from, float alpha_from,
                     Color3 color_to, float alpha_to, const std::string& texture, bool additive);
    // Draws a model as a flat HUD element for the next render() call (e.g. the original
    // interface models from the user's own data). The model's local x/y units are mapped
    // to pixels around (px, py), y up. Drawn unlit, before the vector HUD.
    void draw_hud_model(ModelHandle m, float px, float py, float pixels_per_unit, HudColor tint = {});
    // Optional overlay for the next render() call, in output pixels.
    void set_hud(const HudBatch* hud) { hud_ = hud; }
    void set_room(const RoomLighting* room) { room_ = room; }  // nullptr = space

    struct Stats { int objects_drawn = 0, draw_calls = 0, triangles = 0, culled_by_lod = 0; };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    StarSystem system_;
    const RoomLighting* room_ = nullptr;
    const HudBatch* hud_ = nullptr;
    Stats stats_;
};

}  // namespace sterrenvaarder::render
