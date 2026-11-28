// SPDX-License-Identifier: MPL-2.0
#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>

#include "game/skin.hpp"
#include "sterrenvaarder/dfm.hpp"
#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/sphere.hpp"

namespace sterrenvaarder::render {

// ---------------------------------------------------------------- camera

Vec3 Camera::forward() const {
    if (has_basis) return normalize(basis_forward);
    return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}
Vec3 Camera::right() const { return normalize(cross(forward(), has_basis ? basis_up : Vec3{0, 1, 0})); }
Vec3 Camera::up() const { return cross(right(), forward()); }
bool Camera::project(DVec3 world, float out_w, float out_h, float& px, float& py) const {
    DVec3 d = world - pos;
    Vec3 f = forward(), r = right(), u = up();
    double z = d.x * f.x + d.y * f.y + d.z * f.z;
    if (z <= 0.1) return false;
    double x = d.x * r.x + d.y * r.y + d.z * r.z, y = d.x * u.x + d.y * u.y + d.z * u.z;
    double t = std::tan(fov_y * 0.5);
    double ndc_x = x / (z * t * (out_w / out_h)), ndc_y = y / (z * t);
    px = static_cast<float>((ndc_x * 0.5 + 0.5) * out_w);
    py = static_cast<float>((0.5 - ndc_y * 0.5) * out_h);
    return true;
}

void Camera::look_at(DVec3 t) {
    has_basis = false;
    DVec3 d = t - pos;
    double len = d.length();
    if (len <= 0) return;
    pitch = static_cast<float>(std::asin(d.y / len));
    yaw = static_cast<float>(std::atan2(-d.x, -d.z));
}

namespace {

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float tangent[4];
    std::uint32_t color;  // RGBA8
};
static_assert(sizeof(Vertex) == 52);

enum class Blend { Opaque, Alpha, Additive };

struct GpuMaterial {
    SDL_GPUTexture* diffuse = nullptr;
    SDL_GPUTexture* normal = nullptr;
    SDL_GPUTexture* emissive = nullptr;
    SDL_GPUTexture* detail = nullptr;
    Vec3 dc{1, 1, 1}, ec{0, 0, 0};
    float opacity = 1, detail_tile = 1, normal_scale = 1;
    Blend blend = Blend::Opaque;
    bool vertex_color = false, unlit = false, atmosphere = false;
};

struct DrawRange {
    std::uint32_t first_index = 0, index_count = 0;
    std::int32_t vertex_offset = 0;
    int material = 0;
};

struct GpuLod {
    std::vector<DrawRange> draws;
    float range_end = 0;  // 0 = unlimited (Switch2 data)
};

struct GpuPart {
    Mat4 local;
    std::vector<GpuLod> lods;
    std::vector<DrawRange> wires;  // line lists (VMeshWire), drawn only in the HUD pass
};

// A character mesh posed each frame: its own vertex buffer, refilled from the skin.
struct GpuSkin {
    DfmModel dfm;
    std::vector<Vertex> verts;               // bind pose, one per corner
    std::vector<std::uint32_t> corner_point;  // the mesh point of each vertex
    SDL_GPUTransferBuffer* tb = nullptr;
    bool dirty = false;
};

struct GpuModel {
    SDL_GPUBuffer* vb = nullptr;
    SDL_GPUBuffer* ib = nullptr;
    std::vector<GpuPart> parts;
    std::vector<GpuMaterial> materials;
    std::uint32_t triangles = 0;
    std::unique_ptr<GpuSkin> skin;  // character meshes loaded for posing
};

struct SceneObject {
    const SystemObject* obj = nullptr;
    GpuModel* model = nullptr;
    std::vector<float> lod_ranges;  // from the archetype (original draw distances)
    Mat4 rotation;
    const StarGlow* glow = nullptr;
    const StarGlow* center = nullptr;
    float star_radius = 0;
    bool sphere = false;
};

struct VertexUniforms {
    Mat4 view_proj, model, normal;
};

constexpr int kMaxLights = 8;
struct FragmentUniforms {
    float diffuse[4], emissive[4], ambient[4], params[4], params2[4];
    float light_pos[kMaxLights][4];
    float light_color[kMaxLights][4];
    float light_atten[kMaxLights][4];
};

std::uint32_t pack_color(float r, float g, float b, float a) {
    auto c = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

// Derived normal map: treats luminance as height and takes a Sobel gradient. Gives
// panels and rivets in the original art some relief under the new lighting.
Image derive_normal_map(const Image& src) {
    Image n;
    n.width = src.width;
    n.height = src.height;
    n.rgba.resize(src.rgba.size());
    const int w = static_cast<int>(src.width), h = static_cast<int>(src.height);
    std::vector<float> height(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < height.size(); ++i)
        height[i] = (0.2126f * src.rgba[i * 4] + 0.7152f * src.rgba[i * 4 + 1] + 0.0722f * src.rgba[i * 4 + 2]) / 255.f;
    auto H = [&](int x, int y) { return height[static_cast<std::size_t>((y + h) % h) * w + (x + w) % w]; };
    const float strength = 2.0f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float dx = (H(x + 1, y - 1) + 2 * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x - 1, y) + H(x - 1, y + 1));
            float dy = (H(x - 1, y + 1) + 2 * H(x, y + 1) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x, y - 1) + H(x + 1, y - 1));
            Vec3 v = normalize({-dx * strength, dy * strength, 1.0f});
            std::uint8_t* o = &n.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            o[0] = static_cast<std::uint8_t>((v.x * 0.5f + 0.5f) * 255.f);
            o[1] = static_cast<std::uint8_t>((v.y * 0.5f + 0.5f) * 255.f);
            o[2] = static_cast<std::uint8_t>((v.z * 0.5f + 0.5f) * 255.f);
            o[3] = 255;
        }
    return n;
}

void compute_tangents(std::vector<Vertex>& v, const std::vector<std::uint16_t>& idx, std::size_t vbase, std::size_t ibase,
                      std::size_t icount) {
    std::vector<Vec3> tan(v.size() - vbase), bit(v.size() - vbase);
    for (std::size_t i = 0; i + 2 < icount; i += 3) {
        std::size_t a = idx[ibase + i], b = idx[ibase + i + 1], c = idx[ibase + i + 2];
        if (a >= tan.size() || b >= tan.size() || c >= tan.size()) continue;
        const Vertex &A = v[vbase + a], &B = v[vbase + b], &C = v[vbase + c];
        Vec3 e1{B.pos[0] - A.pos[0], B.pos[1] - A.pos[1], B.pos[2] - A.pos[2]};
        Vec3 e2{C.pos[0] - A.pos[0], C.pos[1] - A.pos[1], C.pos[2] - A.pos[2]};
        float du1 = B.uv[0] - A.uv[0], dv1 = B.uv[1] - A.uv[1], du2 = C.uv[0] - A.uv[0], dv2 = C.uv[1] - A.uv[1];
        float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        float r = 1.0f / det;
        Vec3 t{(e1.x * dv2 - e2.x * dv1) * r, (e1.y * dv2 - e2.y * dv1) * r, (e1.z * dv2 - e2.z * dv1) * r};
        Vec3 bt{(e2.x * du1 - e1.x * du2) * r, (e2.y * du1 - e1.y * du2) * r, (e2.z * du1 - e1.z * du2) * r};
        for (auto k : {a, b, c}) {
            tan[k] = {tan[k].x + t.x, tan[k].y + t.y, tan[k].z + t.z};
            bit[k] = {bit[k].x + bt.x, bit[k].y + bt.y, bit[k].z + bt.z};
        }
    }
    for (std::size_t k = 0; k < tan.size(); ++k) {
        Vertex& vx = v[vbase + k];
        Vec3 nrm{vx.normal[0], vx.normal[1], vx.normal[2]};
        Vec3 t = tan[k];
        float d = t.x * nrm.x + t.y * nrm.y + t.z * nrm.z;
        t = normalize({t.x - nrm.x * d, t.y - nrm.y * d, t.z - nrm.z * d});
        Vec3 c = cross(nrm, t);
        float w = (c.x * bit[k].x + c.y * bit[k].y + c.z * bit[k].z) < 0 ? -1.f : 1.f;
        vx.tangent[0] = t.x;
        vx.tangent[1] = t.y;
        vx.tangent[2] = t.z;
        vx.tangent[3] = w;
    }
}

}  // namespace

// ---------------------------------------------------------------- implementation

struct Renderer::Impl {
    Gpu& gpu;
    const Vfs& vfs;
    const Universe& universe;

    SDL_GPUTextureFormat output_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_4;
    int msaa = 4;

    // Pipelines: scene (blend x depth-test), post-processing.
    SDL_GPUGraphicsPipeline* scene[3][2] = {};
    SDL_GPUGraphicsPipeline* glow = nullptr;
    SDL_GPUGraphicsPipeline* sprite_add = nullptr;
    SDL_GPUGraphicsPipeline* sprite_alpha = nullptr;
    SDL_GPUGraphicsPipeline* shield_pipe = nullptr;
    struct ShieldInstance {
        DVec3 center;
        Vec3 ax[3];
        Vec3 hit;
        Color3 color;
        float strength;
    };
    std::vector<ShieldInstance> shields;
    SDL_GPUGraphicsPipeline* bloom_down = nullptr;
    SDL_GPUGraphicsPipeline* bloom_up = nullptr;
    SDL_GPUGraphicsPipeline* tonemap = nullptr;
    SDL_GPUGraphicsPipeline* upscale = nullptr;
    SDL_GPUGraphicsPipeline* sharpen = nullptr;
    SDL_GPUGraphicsPipeline* blit = nullptr;

    // Render targets (recreated on resize).
    std::uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    SDL_GPUTexture* hdr_msaa = nullptr;
    SDL_GPUTexture* depth = nullptr;
    SDL_GPUTexture* hdr = nullptr;
    std::vector<SDL_GPUTexture*> bloom;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bloom_size;
    SDL_GPUTexture* ldr = nullptr;
    SDL_GPUTexture* upscaled = nullptr;

    SDL_GPUTexture* white = nullptr;
    SDL_GPUTexture* flat_normal = nullptr;
    std::unordered_map<std::string, SDL_GPUTexture*> textures;   // diffuse/emissive, by name
    std::unordered_map<std::string, SDL_GPUTexture*> normals;    // derived normal maps
    std::unordered_map<std::string, std::unique_ptr<GpuModel>> models;
    std::vector<SDL_GPUBuffer*> buffers;
    std::vector<SDL_GPUTexture*> owned_textures;
    SDL_GPUTexture* font_tex = nullptr;  // HUD font atlas (signed distance), when set

    std::vector<SceneObject> objects;
    std::vector<GpuModel*> background;
    SDL_GPUTexture* sphere_unused = nullptr;

    Impl(Gpu& g, const Vfs& v, const Universe& u) : gpu(g), vfs(v), universe(u) {
        Image w;
        w.width = w.height = 1;
        w.rgba = {255, 255, 255, 255};
        white = gpu.texture(w, false);
        Image n = w;
        n.rgba = {128, 128, 255, 255};
        flat_normal = gpu.texture(n, false);
        owned_textures = {white, flat_normal};
    }

    ~Impl() {
        auto* d = gpu.device();
        SDL_WaitForGPUIdle(d);
        release_targets();
        release_pipelines();
        for (auto* b : buffers) SDL_ReleaseGPUBuffer(d, b);
        if (hud_vb) SDL_ReleaseGPUBuffer(d, hud_vb);
        if (hud_tb) SDL_ReleaseGPUTransferBuffer(d, hud_tb);
        for (auto& [k, m] : models)
            if (m->skin && m->skin->tb) SDL_ReleaseGPUTransferBuffer(d, m->skin->tb);
        for (auto* t : owned_textures) SDL_ReleaseGPUTexture(d, t);
        if (font_tex) SDL_ReleaseGPUTexture(d, font_tex);
    }

    // ------------------------------------------------------------ pipelines

    void release_pipelines() {
        auto* d = gpu.device();
        for (auto& row : scene)
            for (auto*& p : row) {
                if (p) SDL_ReleaseGPUGraphicsPipeline(d, p);
                p = nullptr;
            }
        for (auto** p : {&glow, &sprite_add, &sprite_alpha, &shield_pipe, &bloom_down, &bloom_up, &tonemap, &upscale, &sharpen, &blit, &hud_pipe, &hud_mesh_pipe, &hud_wire_pipe}) {
            if (*p) SDL_ReleaseGPUGraphicsPipeline(d, *p);
            *p = nullptr;
        }
    }

    static SDL_GPUColorTargetBlendState blend_state(Blend b) {
        SDL_GPUColorTargetBlendState s{};
        if (b == Blend::Opaque) return s;
        s.enable_blend = true;
        s.color_blend_op = s.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        if (b == Blend::Alpha) {
            s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        } else {
            s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        }
        s.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        s.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        return s;
    }

    SDL_GPUGraphicsPipeline* make_pipeline(const char* vs_name, std::uint32_t vs_ubos, const char* fs_name,
                                           std::uint32_t fs_samplers, std::uint32_t fs_ubos, SDL_GPUTextureFormat fmt,
                                           Blend blend, bool mesh_input, bool depth_test, bool depth_write,
                                           SDL_GPUSampleCount sc,
                                           SDL_GPUPrimitiveType prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST) {
        auto* d = gpu.device();
        auto* vs = gpu.shader(vs_name, 0, vs_ubos);
        auto* fs = gpu.shader(fs_name, fs_samplers, fs_ubos);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        SDL_GPUVertexBufferDescription vbd{};
        SDL_GPUVertexAttribute attrs[5]{};
        if (mesh_input) {
            vbd.slot = 0;
            vbd.pitch = sizeof(Vertex);
            vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, pos)};
            attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)};
            attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, uv)};
            attrs[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, tangent)};
            attrs[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Vertex, color)};
            pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
            pi.vertex_input_state.num_vertex_buffers = 1;
            pi.vertex_input_state.vertex_attributes = attrs;
            pi.vertex_input_state.num_vertex_attributes = 5;
        }
        pi.primitive_type = prim;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count = sc;
        SDL_GPUColorTargetDescription ctd{};
        ctd.format = fmt;
        ctd.blend_state = blend_state(blend);
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        if (depth_test || depth_write) {
            pi.target_info.has_depth_stencil_target = true;
            pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            pi.depth_stencil_state.enable_depth_test = depth_test;
            pi.depth_stencil_state.enable_depth_write = depth_write;
            pi.depth_stencil_state.compare_op = depth_test ? SDL_GPU_COMPAREOP_GREATER_OR_EQUAL : SDL_GPU_COMPAREOP_ALWAYS;
        }
        auto* p = SDL_CreateGPUGraphicsPipeline(d, &pi);
        SDL_ReleaseGPUShader(d, vs);
        SDL_ReleaseGPUShader(d, fs);
        if (!p) throw std::runtime_error(std::string("pipeline ") + fs_name + ": " + SDL_GetError());
        return p;
    }

    void build_pipelines() {
        release_pipelines();
        const auto hdr_fmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        for (int b = 0; b < 3; ++b)
            for (int dt = 0; dt < 2; ++dt) {
                bool write = (b == 0) && dt;
                // All scene pipelines share one depth attachment, so keep it bound and
                // disable the test for background layers instead.
                scene[b][dt] = dt ? make_pipeline("mesh.vert", 1, "mesh.frag", 4, 1, hdr_fmt, static_cast<Blend>(b), true, true,
                                                  write, samples)
                                  : make_background_pipeline(static_cast<Blend>(b));
            }
        glow = make_pipeline("glow.vert", 1, "glow.frag", 1, 1, hdr_fmt, Blend::Additive, false, true, false, samples);
        sprite_add = make_pipeline("sprite.vert", 1, "sprite.frag", 1, 1, hdr_fmt, Blend::Additive, false, true, false, samples);
        sprite_alpha = make_pipeline("sprite.vert", 1, "sprite.frag", 1, 1, hdr_fmt, Blend::Alpha, false, true, false, samples);
        shield_pipe = make_pipeline("shield.vert", 1, "shield.frag", 0, 1, hdr_fmt, Blend::Additive, false, true, false, samples);
        bloom_down = make_pipeline("fullscreen.vert", 0, "bloom_down.frag", 1, 1, hdr_fmt, Blend::Opaque, false, false, false,
                                   SDL_GPU_SAMPLECOUNT_1);
        bloom_up = make_pipeline("fullscreen.vert", 0, "bloom_up.frag", 1, 1, hdr_fmt, Blend::Additive, false, false, false,
                                 SDL_GPU_SAMPLECOUNT_1);
        tonemap = make_pipeline("fullscreen.vert", 0, "tonemap.frag", 2, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                Blend::Opaque, false, false, false, SDL_GPU_SAMPLECOUNT_1);
        upscale = make_pipeline("fullscreen.vert", 0, "upscale.frag", 1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                Blend::Opaque, false, false, false, SDL_GPU_SAMPLECOUNT_1);
        sharpen = make_pipeline("fullscreen.vert", 0, "sharpen.frag", 1, 1, output_format, Blend::Opaque, false, false, false,
                                SDL_GPU_SAMPLECOUNT_1);
        hud_pipe = make_hud_pipeline();
        hud_mesh_pipe = make_pipeline("mesh.vert", 1, "mesh.frag", 4, 1, output_format, Blend::Alpha, true, false, false,
                                      SDL_GPU_SAMPLECOUNT_1);
        hud_wire_pipe = make_pipeline("mesh.vert", 1, "mesh.frag", 4, 1, output_format, Blend::Alpha, true, false, false,
                                      SDL_GPU_SAMPLECOUNT_1, SDL_GPU_PRIMITIVETYPE_LINELIST);
    }

    SDL_GPUGraphicsPipeline* make_background_pipeline(Blend b) {
        // Depth attachment present (same render pass) but neither tested nor written.
        auto* d = gpu.device();
        auto* vs = gpu.shader("mesh.vert", 0, 1);
        auto* fs = gpu.shader("mesh.frag", 4, 1);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        SDL_GPUVertexBufferDescription vbd{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute attrs[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, pos)},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, uv)},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, tangent)},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Vertex, color)}};
        pi.vertex_input_state = {&vbd, 1, attrs, 5};
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = samples;
        SDL_GPUColorTargetDescription ctd{SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, blend_state(b)};
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pi.target_info.has_depth_stencil_target = true;
        pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        pi.depth_stencil_state.enable_depth_test = false;
        pi.depth_stencil_state.enable_depth_write = false;
        auto* p = SDL_CreateGPUGraphicsPipeline(d, &pi);
        SDL_ReleaseGPUShader(d, vs);
        SDL_ReleaseGPUShader(d, fs);
        if (!p) throw std::runtime_error(std::string("background pipeline: ") + SDL_GetError());
        return p;
    }

    // ------------------------------------------------------------ targets

    void release_targets() {
        auto* d = gpu.device();
        for (auto** t : {&hdr_msaa, &depth, &hdr, &ldr, &upscaled}) {
            if (*t) SDL_ReleaseGPUTexture(d, *t);
            *t = nullptr;
        }
        for (auto* t : bloom) SDL_ReleaseGPUTexture(d, t);
        bloom.clear();
        bloom_size.clear();
    }

    void ensure_targets(std::uint32_t render_w, std::uint32_t render_h, std::uint32_t out_w, std::uint32_t out_h, int want_msaa) {
        if (want_msaa != msaa || !scene[0][0]) {
            msaa = want_msaa;
            samples = msaa >= 8 ? SDL_GPU_SAMPLECOUNT_8 : msaa >= 4 ? SDL_GPU_SAMPLECOUNT_4 : msaa >= 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
            while (samples != SDL_GPU_SAMPLECOUNT_1 &&
                   !SDL_GPUTextureSupportsSampleCount(gpu.device(), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, samples))
                samples = static_cast<SDL_GPUSampleCount>(samples - 1);
            build_pipelines();
            rw = 0;  // force target rebuild
        }
        if (render_w == rw && render_h == rh && out_w == ow && out_h == oh) return;
        release_targets();
        rw = render_w;
        rh = render_h;
        ow = out_w;
        oh = out_h;
        const auto hdr_fmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        if (samples != SDL_GPU_SAMPLECOUNT_1) hdr_msaa = gpu.render_target(hdr_fmt, rw, rh, samples, false);
        depth = gpu.render_target(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, rw, rh, samples, false);
        hdr = gpu.render_target(hdr_fmt, rw, rh);
        std::uint32_t bw = rw, bh = rh;
        for (int i = 0; i < 6 && bw > 8 && bh > 8; ++i) {
            bw = std::max(1u, bw / 2);
            bh = std::max(1u, bh / 2);
            bloom.push_back(gpu.render_target(hdr_fmt, bw, bh));
            bloom_size.emplace_back(bw, bh);
        }
        ldr = gpu.render_target(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rw, rh);
        if (rw != ow || rh != oh) upscaled = gpu.render_target(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, ow, oh);
    }

    // ------------------------------------------------------------ assets

    SDL_GPUTexture* texture(const MaterialLibrary& lib, const std::string& name, bool srgb, bool want_normal,
                            SDL_GPUTexture** normal_out) {
        if (name.empty()) return white;
        auto key = to_lower(name);
        if (auto it = textures.find(key); it != textures.end()) {
            if (normal_out) *normal_out = normals.count(key) ? normals[key] : flat_normal;
            return it->second;
        }
        const UtfNode* node = lib.texture(name);
        if (!node || is_animation_node(*node)) {
            if (normal_out) *normal_out = flat_normal;
            return white;
        }
        Image img;
        try {
            img = decode_texture_node(*node);
        } catch (const std::exception&) {
            if (normal_out) *normal_out = flat_normal;
            return white;
        }
        auto* tex = gpu.texture(img, srgb);
        owned_textures.push_back(tex);
        textures[key] = tex;
        if (want_normal) {
            auto* nt = gpu.texture(derive_normal_map(img), false);
            owned_textures.push_back(nt);
            normals[key] = nt;
            if (normal_out) *normal_out = nt;
        } else if (normal_out) {
            *normal_out = flat_normal;
        }
        return tex;
    }

    GpuMaterial make_material(const MaterialLibrary& lib, const Material* m, bool background) {
        GpuMaterial g;
        g.diffuse = white;
        g.normal = flat_normal;
        g.emissive = white;
        g.detail = white;
        if (!m) return g;
        g.dc = m->diffuse;
        g.ec = m->emissive;
        g.opacity = m->opacity;
        g.diffuse = texture(lib, m->diffuse_texture, true, !background, &g.normal);
        if (!m->emissive_texture.empty()) g.emissive = texture(lib, m->emissive_texture, true, false, nullptr);
        if (!m->detail_texture.empty()) g.detail = texture(lib, m->detail_texture, false, false, nullptr);
        g.detail_tile = m->detail_tile;
        // Planet surfaces are painted at a much larger scale; keep their relief subtle.
        if (m->type.find("DetailMap") != std::string::npos) g.normal_scale = 0.3f;
        g.blend = m->alpha_blend ? Blend::Alpha : Blend::Opaque;
        if (m->type.find("Nebula") != std::string::npos) g.blend = Blend::Additive;
        if (background) {
            g.unlit = true;
            if (g.blend == Blend::Opaque) g.blend = Blend::Additive;
        }
        if (m->type.find("Atmosphere") != std::string::npos) {
            g.atmosphere = true;
            g.blend = Blend::Alpha;
            g.opacity = m->alpha;
        }
        return g;
    }

    MaterialLibrary material_libs(const std::vector<std::string>& libs) {
        MaterialLibrary lib;
        for (auto& l : libs) {
            try {
                lib.add_from(vfs.load_with_fallback(l, [](std::vector<std::uint8_t> b) { return UtfFile::parse(std::move(b)); }));
            } catch (const std::exception&) {
            }
        }
        return lib;
    }

    // A character instance (`instance` not empty) gets a model of its own that can be posed.
    GpuModel* load_model(const std::string& path, const std::vector<std::string>& libs, bool background,
                         const std::string& instance = {}) {
        std::string key = to_lower(path) + (background ? "#bg" : "") + (instance.empty() ? "" : "#skin:" + instance);
        if (auto it = models.find(key); it != models.end()) return it->second.get();
        auto file = vfs.load_with_fallback(path, [](std::vector<std::uint8_t> b) { return UtfFile::parse(std::move(b)); });
        auto gm = std::make_unique<GpuModel>();
        std::vector<Vertex> verts;
        std::vector<std::uint16_t> indices;
        MaterialLibrary lib = material_libs(libs);
        lib.add_from(file);
        std::map<std::uint32_t, int> mat_index;
        auto material = [&](std::uint32_t crc) {
            if (auto it = mat_index.find(crc); it != mat_index.end()) return it->second;
            gm->materials.push_back(make_material(lib, lib.material(crc), background));
            return mat_index[crc] = static_cast<int>(gm->materials.size() - 1);
        };

        if (SphereModel::is_sphere(*file)) {
            build_sphere(*gm, SphereModel::load(*file), lib, verts, indices, background);
        } else if (DfmModel::is_dfm(file->root())) {
            // Character mesh, in its bind pose until posed (pose_character).
            DfmModel dfm = DfmModel::load(file->root());
            const DfmMesh& mesh = dfm.levels.front();
            if (!instance.empty()) gm->skin = std::make_unique<GpuSkin>();
            GpuPart gp;
            GpuLod gl;
            gl.range_end = 1e9f;
            for (auto& g : mesh.groups) {
                DrawRange dr;
                dr.vertex_offset = static_cast<std::int32_t>(verts.size());
                dr.first_index = static_cast<std::uint32_t>(indices.size());
                const Material* mat = lib.material_by_name(g.material);
                dr.material = material(mat ? fl_model_crc(mat->name) : 0);
                // One vertex per corner, as corners carry their own uv.
                for (std::size_t c = 0; c < mesh.point_index.size(); ++c) {
                    Vertex vx{};
                    const Vec3& p = mesh.points[mesh.point_index[c]];
                    vx.pos[0] = p.x * dfm.scale;
                    vx.pos[1] = p.y * dfm.scale;
                    vx.pos[2] = p.z * dfm.scale;
                    if (mesh.point_index[c] < mesh.normals.size()) {
                        const Vec3& n = mesh.normals[mesh.point_index[c]];
                        vx.normal[0] = n.x;
                        vx.normal[1] = n.y;
                        vx.normal[2] = n.z;
                    }
                    if (c < mesh.uv_index.size() && mesh.uv_index[c] < mesh.uvs.size()) {
                        vx.uv[0] = mesh.uvs[mesh.uv_index[c]][0];
                        vx.uv[1] = mesh.uvs[mesh.uv_index[c]][1];
                    }
                    vx.color = 0xFFFFFFFFu;
                    verts.push_back(vx);
                    if (gm->skin) gm->skin->corner_point.push_back(mesh.point_index[c]);
                }
                for (auto i : mesh.triangles(g)) indices.push_back(static_cast<std::uint16_t>(i));
                dr.index_count = static_cast<std::uint32_t>(indices.size()) - dr.first_index;
                compute_tangents(verts, indices, dr.vertex_offset, dr.first_index, dr.index_count);
                gm->triangles += dr.index_count / 3;
                gl.draws.push_back(dr);
            }
            gp.lods.push_back(std::move(gl));
            gm->parts.push_back(std::move(gp));
            if (gm->skin) gm->skin->dfm = std::move(dfm);
        } else {
            Model model = Model::load(file);
            MeshLibrary meshes;
            meshes.add_from(file);
            auto find_mesh = [&](std::uint32_t id) -> const VMeshData* {
                if (auto* m = meshes.mesh(id)) return m;
                return shared_meshes.mesh(id);
            };
            // Part transforms: walk joints from the root.
            std::map<std::string, Mat4> world;
            if (!model.parts.empty()) world[to_lower(model.parts[0].name)] = Mat4{};
            for (int pass = 0; pass < 8; ++pass)
                for (auto& j : model.joints) {
                    auto p = world.find(to_lower(j.parent));
                    if (p == world.end() || world.count(to_lower(j.child))) continue;
                    world[to_lower(j.child)] = p->second * Mat4::translate(j.origin.x, j.origin.y, j.origin.z) *
                                               Mat4::from_rows3(j.rotation);
                }
            for (auto& part : model.parts) {
                GpuPart gp;
                if (auto w = world.find(to_lower(part.name)); w != world.end()) gp.local = w->second;
                for (auto& lod : part.lods) {
                    GpuLod gl;
                    gl.range_end = lod.range_end;
                    const VMeshData* vm = find_mesh(lod.ref.mesh_id);
                    if (!vm) continue;
                    std::size_t cursor = lod.ref.start_index;
                    for (std::size_t g = lod.ref.start_group; g < std::size_t(lod.ref.start_group) + lod.ref.group_count; ++g) {
                        const auto& grp = vm->groups.at(g);
                        std::size_t base = std::size_t(lod.ref.start_vertex) + grp.start_vertex;
                        std::size_t n = std::size_t(grp.end_vertex) - grp.start_vertex + 1;
                        DrawRange dr;
                        dr.vertex_offset = static_cast<std::int32_t>(verts.size());
                        dr.first_index = static_cast<std::uint32_t>(indices.size());
                        dr.index_count = grp.index_count;
                        dr.material = material(grp.material_crc);
                        for (std::size_t v = base; v < base + n && v < vm->positions.size(); ++v) {
                            Vertex vx{};
                            vx.pos[0] = vm->positions[v].x;
                            vx.pos[1] = vm->positions[v].y;
                            vx.pos[2] = vm->positions[v].z;
                            if (!vm->normals.empty()) {
                                vx.normal[0] = vm->normals[v].x;
                                vx.normal[1] = vm->normals[v].y;
                                vx.normal[2] = vm->normals[v].z;
                            } else {
                                vx.normal[1] = 1;
                            }
                            if (!vm->uv0.empty()) {
                                vx.uv[0] = vm->uv0[v].x;
                                vx.uv[1] = vm->uv0[v].y;
                            }
                            if (!vm->colors.empty()) {
                                std::uint32_t c = vm->colors[v];  // D3DCOLOR ARGB
                                vx.color = ((c >> 16) & 0xFF) | (c & 0xFF00) | ((c & 0xFF) << 16) | (c & 0xFF000000u);
                                gm->materials[dr.material].vertex_color = true;
                            } else {
                                vx.color = 0xFFFFFFFFu;
                            }
                            verts.push_back(vx);
                        }
                        for (std::size_t k = 0; k < grp.index_count; ++k) indices.push_back(vm->indices.at(cursor + k));
                        if (!vm->normals.empty() && !vm->uv0.empty())
                            compute_tangents(verts, indices, dr.vertex_offset, dr.first_index, dr.index_count);
                        cursor += grp.index_count;
                        gm->triangles += grp.index_count / 3;
                        gl.draws.push_back(dr);
                    }
                    gp.lods.push_back(std::move(gl));
                }
                if (part.wire) {
                    const auto& w = *part.wire;
                    if (const VMeshData* vm = find_mesh(w.mesh_id)) {
                        DrawRange dr;
                        dr.vertex_offset = static_cast<std::int32_t>(verts.size());
                        dr.first_index = static_cast<std::uint32_t>(indices.size());
                        dr.material = material(0);
                        for (std::size_t v = w.vertex_offset; v < std::size_t(w.vertex_offset) + w.vertex_count && v < vm->positions.size(); ++v) {
                            Vertex vx{};
                            vx.pos[0] = vm->positions[v].x;
                            vx.pos[1] = vm->positions[v].y;
                            vx.pos[2] = vm->positions[v].z;
                            vx.normal[2] = 1;
                            vx.color = 0xFFFFFFFFu;
                            verts.push_back(vx);
                        }
                        std::uint16_t limit = w.vertex_count;
                        for (std::size_t k = 0; k + 1 < w.indices.size(); k += 2)
                            if (w.indices[k] < limit && w.indices[k + 1] < limit) {
                                indices.push_back(w.indices[k]);
                                indices.push_back(w.indices[k + 1]);
                            }
                        dr.index_count = static_cast<std::uint32_t>(indices.size()) - dr.first_index;
                        if (dr.index_count) gp.wires.push_back(dr);
                    }
                }
                gm->parts.push_back(std::move(gp));
            }
        }
        if (verts.empty()) verts.push_back(Vertex{});
        if (indices.empty()) indices.push_back(0);
        gm->vb = gpu.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, verts.data(), static_cast<std::uint32_t>(verts.size() * sizeof(Vertex)));
        gm->ib = gpu.buffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), static_cast<std::uint32_t>(indices.size() * 2));
        buffers.push_back(gm->vb);
        buffers.push_back(gm->ib);
        if (gm->skin) {
            gm->skin->verts = verts;  // after tangents: posing keeps uv and tangent space of the bind pose
            SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                               static_cast<std::uint32_t>(verts.size() * sizeof(Vertex)), 0};
            gm->skin->tb = SDL_CreateGPUTransferBuffer(gpu.device(), &ti);
        }
        auto* raw = gm.get();
        models[key] = std::move(gm);
        return raw;
    }

    // Planets and suns: a subdivided cube projected onto a sphere, one material per face
    // (M0..M3 around the equator, M4 top, M5 bottom) plus an atmosphere shell (M6).
    void build_sphere(GpuModel& gm, const SphereModel& sph, const MaterialLibrary& lib, std::vector<Vertex>& verts,
                      std::vector<std::uint16_t>& indices, bool background) {
        if (sph.materials.empty() || iequals(sph.materials[0], "none")) return;
        const int N = 32;
        struct Face { Vec3 n, u, v; };
        const Face faces[6] = {
            {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
            {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        };
        GpuPart part;
        GpuLod lod;
        auto add_shell = [&](float radius, int face, const Material* m, bool atmosphere) {
            DrawRange dr;
            dr.vertex_offset = static_cast<std::int32_t>(verts.size());
            dr.first_index = static_cast<std::uint32_t>(indices.size());
            GpuMaterial gmat = make_material(lib, m, background);
            if (atmosphere) {
                gmat.atmosphere = true;
                gmat.blend = Blend::Alpha;
                if (m) gmat.opacity = m->alpha > 0 ? m->alpha : 0.5f;
            }
            gm.materials.push_back(gmat);
            dr.material = static_cast<int>(gm.materials.size() - 1);
            const Face& f = faces[face];
            for (int y = 0; y <= N; ++y)
                for (int x = 0; x <= N; ++x) {
                    float s = 2.f * x / N - 1.f, t = 2.f * y / N - 1.f;
                    Vec3 p = normalize({f.n.x + f.u.x * s + f.v.x * t, f.n.y + f.u.y * s + f.v.y * t, f.n.z + f.u.z * s + f.v.z * t});
                    Vertex vx{};
                    vx.pos[0] = p.x * radius;
                    vx.pos[1] = p.y * radius;
                    vx.pos[2] = p.z * radius;
                    vx.normal[0] = p.x;
                    vx.normal[1] = p.y;
                    vx.normal[2] = p.z;
                    vx.uv[0] = static_cast<float>(x) / N;
                    vx.uv[1] = 1.f - static_cast<float>(y) / N;
                    vx.color = 0xFFFFFFFFu;
                    verts.push_back(vx);
                }
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x) {
                    auto i0 = static_cast<std::uint16_t>(y * (N + 1) + x);
                    auto i1 = static_cast<std::uint16_t>(i0 + 1);
                    auto i2 = static_cast<std::uint16_t>(i0 + N + 1);
                    auto i3 = static_cast<std::uint16_t>(i2 + 1);
                    indices.insert(indices.end(), {i0, i1, i3, i0, i3, i2});
                }
            dr.index_count = static_cast<std::uint32_t>(indices.size()) - dr.first_index;
            compute_tangents(verts, indices, dr.vertex_offset, dr.first_index, dr.index_count);
            gm.triangles += dr.index_count / 3;
            lod.draws.push_back(dr);
        };
        for (int face = 0; face < 6; ++face) {
            const std::string* name = sph.face_material(face);
            add_shell(sph.radius, face, name ? lib.material_by_name(*name) : nullptr, false);
        }
        if (auto atm = sph.atmosphere_material()) {
            const Material* m = lib.material_by_name(*atm);
            float scale = (m && m->scale > 1.0f) ? m->scale : 1.03f;
            for (int face = 0; face < 6; ++face) add_shell(sph.radius * scale, face, m, true);
        }
        part.lods.push_back(std::move(lod));
        gm.parts.push_back(std::move(part));
    }

    void load_system(const StarSystem& sys) {
        objects.clear();
        background.clear();
        for (auto* p : {&sys.basic_stars, &sys.complex_stars, &sys.nebulae}) {
            if (p->empty()) continue;
            try {
                background.push_back(load_model(*p, {}, true));
            } catch (const std::exception& e) {
                SDL_Log("background %s: %s", p->c_str(), e.what());
            }
        }
        for (auto& o : sys.objects) {
            const SolarArchetype* arch = universe.solar(o.archetype);
            if (!arch || arch->model.empty()) continue;
            SceneObject so;
            so.obj = &o;
            so.lod_ranges = arch->lod_ranges;
            so.rotation = Mat4::euler_deg(o.rotate_deg);
            try {
                so.model = load_model(arch->model, arch->material_libraries, false);
            } catch (const std::exception& e) {
                SDL_Log("object %s (%s): %s", o.nickname.c_str(), arch->model.c_str(), e.what());
                continue;
            }
            if (!o.star.empty()) {
                if (const StarArchetype* st = universe.star(o.star)) {
                    so.glow = universe.star_glow(st->glow);
                    so.center = universe.star_glow(st->center);
                    so.star_radius = st->radius > 0 ? st->radius : arch->solar_radius;
                }
            }
            objects.push_back(std::move(so));
        }
        // Star glow textures live in the star texture library (fx\sun.txm).
        if (!universe.star_texture_library().empty()) {
            try {
                star_lib = std::make_unique<MaterialLibrary>(material_libs({universe.star_texture_library()}));
                for (auto& so : objects)  // upload now: no copies are allowed inside the render pass
                    for (const StarGlow* g : {so.glow, so.center})
                        if (g) texture(*star_lib, g->shape, true, false, nullptr);
            } catch (const std::exception&) {
            }
        }
    }
    std::unique_ptr<MaterialLibrary> star_lib;

    struct DynamicInstance {
        GpuModel* model;
        DVec3 pos;
        Mat4 rotation;
        std::vector<float> lod_ranges;
        int only_part = -1;  // >= 0: draw just this part, centred on pos
    };
    std::vector<DynamicInstance> dynamic;
    MeshLibrary shared_meshes;

    struct GlowInstance {
        DVec3 pos;
        float size;
        Color3 color;
        float intensity;
    };
    std::vector<GlowInstance> glows;

    // Particle sprites from effects, with the effect texture libraries they draw from.
    struct SpriteInstance {
        DVec3 pos;
        float size, rotation;
        float color[4];
        SDL_GPUTexture* tex;
        bool additive;
        float uv[4];
        bool streak = false;
        Vec3 axis{};  // streaks: half the segment, centre to end
        float color2[4] = {};  // streaks: colour at the far end (sprites: same as color)
        float taper = 1;       // streaks: far-end width relative to the near end
        bool flat = false;     // lies in the plane of ax_u, ax_v
        Vec3 ax_u{}, ax_v{};
    };
    std::vector<SpriteInstance> sprites;
    // Animated effect textures: a node with "Frame count" and "Frame rects" whose frames are
    // rectangles in the images <name>_0, <name>_1, ...
    struct Flipbook {
        std::vector<SDL_GPUTexture*> images;
        std::vector<std::array<float, 5>> frames;  // image index, u0, v0, u1, v1 (v up)
    };
    std::unordered_map<std::string, std::optional<Flipbook>> flipbooks;

    const Flipbook* flipbook(const std::string& name) {
        auto key = to_lower(name);
        if (auto it = flipbooks.find(key); it != flipbooks.end()) return it->second ? &*it->second : nullptr;
        auto& slot = flipbooks[key];
        const UtfNode* node = fx_lib ? fx_lib->texture(name) : nullptr;
        if (!node || !is_animation_node(*node)) return nullptr;
        const UtfNode* rects = node->child("Frame rects");
        const UtfNode* count = node->child("Texture count");
        if (!rects) return nullptr;
        Flipbook fb;
        int images = count && count->data.size() >= 4 ? std::max(1, count->as<std::int32_t>()) : 1;
        for (int i = 0; i < images; ++i) fb.images.push_back(texture(*fx_lib, node->name + "_" + std::to_string(i), true, false, nullptr));
        for (std::size_t o = 0; o + 20 <= rects->data.size(); o += 20) {
            std::array<float, 5> f;
            f[0] = static_cast<float>(read_le<std::uint32_t>(rects->data, o));
            for (int k = 0; k < 4; ++k) f[k + 1] = read_le<float>(rects->data, o + 4 + k * 4);
            fb.frames.push_back(f);
        }
        if (fb.frames.empty()) return nullptr;
        slot = std::move(fb);
        return &*slot;
    }
    std::vector<std::string> fx_lib_paths;
    std::unique_ptr<MaterialLibrary> fx_lib;
    std::unordered_map<std::string, SDL_GPUTexture*> builtin_fx;

    // The beam textures (star, ball, wide, thin) are drawn by the game itself, not stored in
    // a texture library, so we draw our own; any other missing effect texture gets a soft dot
    // rather than a hard white square.
    SDL_GPUTexture* builtin_fx_texture(const std::string& name) {
        auto key = to_lower(name);
        if (auto it = builtin_fx.find(key); it != builtin_fx.end()) return it->second;
        constexpr int N = 64;
        Image img;
        img.width = img.height = N;
        img.has_alpha = true;
        img.rgba.resize(N * N * 4);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float u = (x + 0.5f) / N * 2 - 1, v = (y + 0.5f) / N * 2 - 1;
                float r = std::sqrt(u * u + v * v), a;
                if (key == "star") {
                    float core = std::max(0.f, 1 - r);
                    float rays = std::max(std::max(0.f, 1 - std::abs(u) * 16) * std::max(0.f, 1 - std::abs(v) * 1.4f),
                                          std::max(0.f, 1 - std::abs(v) * 16) * std::max(0.f, 1 - std::abs(u) * 1.4f));
                    // A round glow with faint short rays, as the heads look in the original.
                    a = std::min(1.f, core * core + rays * rays * 0.35f);
                } else if (key == "wide" || key == "thin") {
                    // Across the beam (u): bright core fading to the edges; along it (v): even.
                    float w = key == "wide" ? 1.f : 0.45f;
                    float c = std::max(0.f, 1 - std::abs(u) / w);
                    a = c * c;
                } else {
                    float c = std::max(0.f, 1 - r);
                    a = c * c;
                }
                auto b = static_cast<std::uint8_t>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
                auto* px = &img.rgba[(y * N + x) * 4];
                px[0] = px[1] = px[2] = b;
                px[3] = b;
            }
        auto* tex = gpu.texture(img, false);
        owned_textures.push_back(tex);
        builtin_fx[key] = tex;
        return tex;
    }

    struct HudModel {
        GpuModel* model;
        float px, py, ppu;
        HudColor tint;
    };
    std::vector<HudModel> hud_models;
    SDL_GPUGraphicsPipeline* hud_mesh_pipe = nullptr;
    SDL_GPUGraphicsPipeline* hud_wire_pipe = nullptr;

    // HUD overlay: one growable vertex buffer, uploaded before the render passes.
    SDL_GPUGraphicsPipeline* hud_pipe = nullptr;
    SDL_GPUBuffer* hud_vb = nullptr;
    SDL_GPUTransferBuffer* hud_tb = nullptr;
    std::uint32_t hud_capacity = 0;

    void pose_character(GpuModel* m, const AnmScript* anim, float time) {
        if (!m || !m->skin || !m->skin->tb || m->skin->dfm.levels.empty()) return;
        auto& sk = *m->skin;
        const DfmMesh& mesh = sk.dfm.levels.front();
        auto pose = game::pose_bones(sk.dfm, anim, time);
        auto pts = game::skin_points(sk.dfm, mesh, pose);
        auto nrm = game::skin_normals(sk.dfm, mesh, pose);
        auto* map = static_cast<Vertex*>(SDL_MapGPUTransferBuffer(gpu.device(), sk.tb, true));
        if (!map) return;
        for (std::size_t v = 0; v < sk.verts.size(); ++v) {
            Vertex vx = sk.verts[v];
            std::uint32_t p = v < sk.corner_point.size() ? sk.corner_point[v] : 0;
            if (p < pts.size()) {
                vx.pos[0] = pts[p].x * sk.dfm.scale;
                vx.pos[1] = pts[p].y * sk.dfm.scale;
                vx.pos[2] = pts[p].z * sk.dfm.scale;
            }
            if (p < nrm.size()) {
                vx.normal[0] = nrm[p].x;
                vx.normal[1] = nrm[p].y;
                vx.normal[2] = nrm[p].z;
            }
            map[v] = vx;
        }
        SDL_UnmapGPUTransferBuffer(gpu.device(), sk.tb);
        sk.dirty = true;
    }

    void upload_skins(SDL_GPUCommandBuffer* cmd) {
        SDL_GPUCopyPass* cp = nullptr;
        for (auto& [k, m] : models) {
            if (!m->skin || !m->skin->dirty) continue;
            if (!cp) cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTransferBufferLocation src{m->skin->tb, 0};
            SDL_GPUBufferRegion dst{m->vb, 0, static_cast<std::uint32_t>(m->skin->verts.size() * sizeof(Vertex))};
            SDL_UploadToGPUBuffer(cp, &src, &dst, true);
            m->skin->dirty = false;
        }
        if (cp) SDL_EndGPUCopyPass(cp);
    }

    std::uint32_t upload_hud(SDL_GPUCommandBuffer* cmd, const HudBatch* hud) {
        if (!hud || hud->vertices().empty()) return 0;
        auto* d = gpu.device();
        std::uint32_t bytes = static_cast<std::uint32_t>(hud->vertices().size() * sizeof(HudVertex));
        if (bytes > hud_capacity) {
            if (hud_vb) SDL_ReleaseGPUBuffer(d, hud_vb);
            if (hud_tb) SDL_ReleaseGPUTransferBuffer(d, hud_tb);
            hud_capacity = std::max<std::uint32_t>(bytes * 2, 64 * 1024);
            SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_VERTEX, hud_capacity, 0};
            hud_vb = SDL_CreateGPUBuffer(d, &bi);
            SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, hud_capacity, 0};
            hud_tb = SDL_CreateGPUTransferBuffer(d, &ti);
        }
        void* map = SDL_MapGPUTransferBuffer(d, hud_tb, true);
        std::memcpy(map, hud->vertices().data(), bytes);
        SDL_UnmapGPUTransferBuffer(d, hud_tb);
        auto* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{hud_tb, 0};
        SDL_GPUBufferRegion dst{hud_vb, 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
        SDL_EndGPUCopyPass(cp);
        return static_cast<std::uint32_t>(hud->vertices().size());
    }

    SDL_GPUGraphicsPipeline* make_hud_pipeline() {
        auto* d = gpu.device();
        auto* vs = gpu.shader("hud.vert", 0, 1);
        auto* fs = gpu.shader("hud.frag", 1, 0);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        SDL_GPUVertexBufferDescription vbd{0, sizeof(HudVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute attrs[3] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(HudVertex, x)},
                                           {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(HudVertex, rgba)},
                                           {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(HudVertex, u)}};
        pi.vertex_input_state = {&vbd, 1, attrs, 3};
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUColorTargetDescription ctd{output_format, blend_state(Blend::Alpha)};
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        auto* p = SDL_CreateGPUGraphicsPipeline(d, &pi);
        SDL_ReleaseGPUShader(d, vs);
        SDL_ReleaseGPUShader(d, fs);
        if (!p) throw std::runtime_error(std::string("hud pipeline: ") + SDL_GetError());
        return p;
    }

    // ------------------------------------------------------------ drawing

    struct PendingDraw {
        GpuModel* model;
        const DrawRange* range;
        Mat4 world;
        float dist;
        std::uint32_t pipeline_blend;
    };

    void bind_material(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const GpuMaterial& m, const FragmentUniforms& base,
                       const RenderSettings& s) {
        FragmentUniforms fu = base;
        fu.diffuse[0] = m.dc.x;
        fu.diffuse[1] = m.dc.y;
        fu.diffuse[2] = m.dc.z;
        fu.diffuse[3] = m.opacity;
        fu.emissive[0] = m.ec.x;
        fu.emissive[1] = m.ec.y;
        fu.emissive[2] = m.ec.z;
        fu.emissive[3] = m.emissive != white ? 1.f : 0.f;
        fu.params[0] = s.normal_strength * m.normal_scale;
        fu.params[1] = m.vertex_color ? 1.f : 0.f;
        fu.params[2] = m.unlit ? 1.f : 0.f;
        fu.params[3] = m.detail_tile;
        fu.params2[0] = m.detail != white ? 1.f : 0.f;
        fu.params2[1] = 0.25f;
        fu.params2[2] = 1.0f;
        fu.params2[3] = m.atmosphere ? 1.f : 0.f;
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        SDL_GPUTextureSamplerBinding b[4] = {{m.diffuse, gpu.linear_sampler()},
                                             {m.normal, gpu.linear_sampler()},
                                             {m.emissive, gpu.linear_sampler()},
                                             {m.detail, gpu.linear_sampler()}};
        SDL_BindGPUFragmentSamplers(pass, 0, b, 4);
    }

    void fullscreen(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUGraphicsPipeline* pipe,
                    std::initializer_list<SDL_GPUTexture*> inputs, const void* ubo, std::uint32_t ubo_size, bool load = false) {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = target;
        ct.load_op = load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        std::vector<SDL_GPUTextureSamplerBinding> b;
        for (auto* t : inputs) b.push_back({t, gpu.clamp_sampler()});
        SDL_BindGPUFragmentSamplers(pass, 0, b.data(), static_cast<std::uint32_t>(b.size()));
        if (ubo) SDL_PushGPUFragmentUniformData(cmd, 0, ubo, ubo_size);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    // Returns the LOD index to draw, or -1 if the object is beyond its original draw distance.
    static int pick_lod(const std::vector<float>& r, const GpuPart& part, float dist, float lod_scale) {
        if (part.lods.empty()) return -1;
        if (r.size() >= 2) {
            for (std::size_t i = 0; i + 1 < r.size(); ++i)
                if (dist < r[i + 1] * lod_scale) return static_cast<int>(std::min(i, part.lods.size() - 1));
            return -1;
        }
        for (std::size_t i = 0; i < part.lods.size(); ++i)
            if (part.lods[i].range_end <= 0 || dist < part.lods[i].range_end * lod_scale) return static_cast<int>(i);
        return -1;
    }

    void render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, std::uint32_t out_w, std::uint32_t out_h, const Camera& cam,
                const RenderSettings& s, double time, const StarSystem& sys, Stats& stats, const HudBatch* hud,
                const RoomLighting* room) {
        std::uint32_t w = std::max(16u, static_cast<std::uint32_t>(out_w * std::clamp(s.render_scale, 0.25f, 2.0f)));
        std::uint32_t h = std::max(16u, static_cast<std::uint32_t>(out_h * std::clamp(s.render_scale, 0.25f, 2.0f)));
        ensure_targets(w, h, out_w, out_h, s.msaa);
        stats = {};
        std::uint32_t hud_verts = upload_hud(cmd, hud);
        upload_skins(cmd);

        const float aspect = static_cast<float>(out_w) / static_cast<float>(out_h);
        Mat4 proj = Mat4::perspective_reverse_z(cam.fov_y, aspect, std::max(0.01f, cam.near_plane));
        Vec3 fwd = cam.forward(), right = cam.right(), up = cam.up();
        Mat4 view = Mat4::view_rotation(right, up, fwd);
        Mat4 view_proj = proj * view;

        FragmentUniforms base{};
        Color3 amb = room ? room->ambient : sys.ambient;
        base.ambient[0] = amb.r;
        base.ambient[1] = amb.g;
        base.ambient[2] = amb.b;
        base.ambient[3] = room ? 1.f : 0.f;
        // Lights: nearest first.
        std::vector<const SystemLight*> lights;
        for (auto& l : room ? room->lights : sys.lights) lights.push_back(&l);
        auto light_dist = [&](const SystemLight* l) { return (DVec3{l->pos.x, l->pos.y, l->pos.z} - cam.pos).length(); };
        std::sort(lights.begin(), lights.end(), [&](auto* a, auto* b) { return light_dist(a) < light_dist(b); });
        for (std::size_t i = 0; i < lights.size() && i < kMaxLights; ++i) {
            DVec3 rel = DVec3{lights[i]->pos.x, lights[i]->pos.y, lights[i]->pos.z} - cam.pos;
            base.light_pos[i][0] = static_cast<float>(rel.x);
            base.light_pos[i][1] = static_cast<float>(rel.y);
            base.light_pos[i][2] = static_cast<float>(rel.z);
            base.light_pos[i][3] = lights[i]->range;
            base.light_color[i][0] = lights[i]->color.r;
            base.light_color[i][1] = lights[i]->color.g;
            base.light_color[i][2] = lights[i]->color.b;
            base.light_color[i][3] = 1;
            base.light_atten[i][0] = lights[i]->atten.x;
            base.light_atten[i][1] = lights[i]->atten.y;
            base.light_atten[i][2] = lights[i]->atten.z;
        }

        // ---- scene pass
        SDL_GPUColorTargetInfo ct{};
        Color3 clear = room ? room->clear : sys.space_color;
        ct.clear_color = {clear.r, clear.g, clear.b, 1};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        if (hdr_msaa) {
            ct.texture = hdr_msaa;
            ct.store_op = SDL_GPU_STOREOP_RESOLVE;
            ct.resolve_texture = hdr;
        } else {
            ct.texture = hdr;
            ct.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = depth;
        dt.clear_depth = 0.0f;  // reverse-Z
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);

        auto draw_range = [&](GpuModel* m, const DrawRange& dr, const Mat4& world, bool bg, Blend override_blend, bool use_override) {
            const GpuMaterial& mat = m->materials[dr.material];
            Blend b = use_override ? override_blend : mat.blend;
            SDL_BindGPUGraphicsPipeline(pass, scene[static_cast<int>(b)][bg ? 0 : 1]);
            VertexUniforms vu{view_proj, world, world.normal_matrix()};
            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
            bind_material(cmd, pass, mat, base, s);
            SDL_GPUBufferBinding vb{m->vb, 0}, ib{m->ib, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, dr.index_count, 1, dr.first_index, dr.vertex_offset, 0);
            ++stats.draw_calls;
            stats.triangles += static_cast<int>(dr.index_count / 3);
        };

        // Background layers: centred on the camera, scaled to stay well inside the far range.
        for (auto* bgm : background)
            if (!room) for (auto& part : bgm->parts)
                if (!part.lods.empty())
                    for (auto& dr : part.lods[0].draws) draw_range(bgm, dr, Mat4::scale(1000.f) * part.local, true, Blend::Opaque, false);

        std::vector<PendingDraw> transparent;
        for (auto& so : objects) {
            if (room) break;
            DVec3 opos{so.obj->pos.x, so.obj->pos.y, so.obj->pos.z};
            DVec3 rel = opos - cam.pos;
            float dist = static_cast<float>(rel.length());
            double angle = time * so.obj->spin.y * 6.2831853;  // spin is in turns per second about Y
            Mat4 obj_world = Mat4::translate(static_cast<float>(rel.x), static_cast<float>(rel.y), static_cast<float>(rel.z)) *
                             so.rotation * Mat4::rotate_y(static_cast<float>(angle));
            bool any = false;
            for (auto& part : so.model->parts) {
                int lod = pick_lod(so.lod_ranges, part, dist, s.lod_scale);
                if (lod < 0) continue;
                any = true;
                Mat4 world = obj_world * part.local;
                for (auto& dr : part.lods[lod].draws) {
                    if (so.model->materials[dr.material].blend == Blend::Opaque)
                        draw_range(so.model, dr, world, false, Blend::Opaque, false);
                    else
                        transparent.push_back({so.model, &dr, world, dist, 0});
                }
            }
            if (any) ++stats.objects_drawn;
            else ++stats.culled_by_lod;
        }
        for (auto& inst : dynamic) {
            DVec3 rel = inst.pos - cam.pos;
            float dist = static_cast<float>(rel.length());
            Mat4 obj_world =
                Mat4::translate(static_cast<float>(rel.x), static_cast<float>(rel.y), static_cast<float>(rel.z)) * inst.rotation;
            for (std::size_t pi = 0; pi < inst.model->parts.size(); ++pi) {
                if (inst.only_part >= 0 && pi != static_cast<std::size_t>(inst.only_part)) continue;
                auto& part = inst.model->parts[pi];
                int lod = pick_lod(inst.lod_ranges, part, dist, s.lod_scale);
                if (lod < 0) continue;
                Mat4 world = obj_world * part.local;
                if (inst.only_part >= 0)  // the part's own origin sits at pos
                    world = obj_world * Mat4::translate(-part.local.at(0, 3), -part.local.at(1, 3), -part.local.at(2, 3)) * part.local;
                for (auto& dr : part.lods[lod].draws) {
                    if (inst.model->materials[dr.material].blend == Blend::Opaque)
                        draw_range(inst.model, dr, world, false, Blend::Opaque, false);
                    else
                        transparent.push_back({inst.model, &dr, world, dist, 0});
                }
            }
        }
        dynamic.clear();
        std::sort(transparent.begin(), transparent.end(), [](auto& a, auto& b) { return a.dist > b.dist; });
        for (auto& t : transparent) draw_range(t.model, *t.range, t.world, false, Blend::Opaque, false);

        // Star glows.
        if (star_lib && !room) {
            SDL_BindGPUGraphicsPipeline(pass, glow);
            for (auto& so : objects) {
                for (const StarGlow* g : {so.glow, so.center}) {
                    if (!g) continue;
                    auto it = textures.find(to_lower(g->shape));
                    if (it == textures.end()) continue;
                    SDL_GPUTexture* tex = it->second;
                    DVec3 rel = DVec3{so.obj->pos.x, so.obj->pos.y, so.obj->pos.z} - cam.pos;
                    struct { Mat4 vp; float center[4], r[4], u[4]; } gu{};
                    gu.vp = view_proj;
                    gu.center[0] = static_cast<float>(rel.x);
                    gu.center[1] = static_cast<float>(rel.y);
                    gu.center[2] = static_cast<float>(rel.z);
                    gu.center[3] = so.star_radius * g->scale * 0.4f;
                    gu.r[0] = right.x; gu.r[1] = right.y; gu.r[2] = right.z;
                    gu.u[0] = up.x; gu.u[1] = up.y; gu.u[2] = up.z;
                    SDL_PushGPUVertexUniformData(cmd, 0, &gu, sizeof gu);
                    struct { float inner[4], outer[4]; } fu{{g->inner.r, g->inner.g, g->inner.b, g == so.center ? 6.0f : 2.0f}, {g->outer.r, g->outer.g, g->outer.b, 0}};
                    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
                    SDL_GPUTextureSamplerBinding b{tex, gpu.clamp_sampler()};
                    SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
                    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
                }
            }
        }
        // Dynamic glows (weapon bolts, explosions).
        if (!glows.empty()) {
            SDL_BindGPUGraphicsPipeline(pass, glow);
            for (auto& g : glows) {
                DVec3 rel = g.pos - cam.pos;
                struct { Mat4 vp; float center[4], r[4], u[4]; } gu{};
                gu.vp = view_proj;
                gu.center[0] = static_cast<float>(rel.x);
                gu.center[1] = static_cast<float>(rel.y);
                gu.center[2] = static_cast<float>(rel.z);
                gu.center[3] = g.size;
                gu.r[0] = right.x; gu.r[1] = right.y; gu.r[2] = right.z;
                gu.u[0] = up.x; gu.u[1] = up.y; gu.u[2] = up.z;
                SDL_PushGPUVertexUniformData(cmd, 0, &gu, sizeof gu);
                struct { float inner[4], outer[4]; } fu{{1, 1, 1, g.intensity}, {g.color.r, g.color.g, g.color.b, 0}};
                SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
                SDL_GPUTextureSamplerBinding b{white, gpu.clamp_sampler()};
                SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
                SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
            }
            glows.clear();
        }
        // Shield bubbles flashing after a hit.
        if (!shields.empty()) {
            SDL_BindGPUGraphicsPipeline(pass, shield_pipe);
            for (auto& sh : shields) {
                DVec3 rel = sh.center - cam.pos;
                struct { Mat4 vp; float center[4], ax[3][4]; } vu{};
                vu.vp = view_proj;
                vu.center[0] = static_cast<float>(rel.x);
                vu.center[1] = static_cast<float>(rel.y);
                vu.center[2] = static_cast<float>(rel.z);
                for (int k = 0; k < 3; ++k) {
                    vu.ax[k][0] = sh.ax[k].x;
                    vu.ax[k][1] = sh.ax[k].y;
                    vu.ax[k][2] = sh.ax[k].z;
                }
                SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
                float fu[8] = {sh.color.r, sh.color.g, sh.color.b, sh.strength, sh.hit.x, sh.hit.y, sh.hit.z, 10.f};
                SDL_PushGPUFragmentUniformData(cmd, 0, fu, sizeof fu);
                SDL_DrawGPUPrimitives(pass, 32 * 16 * 6, 1, 0, 0);
            }
            shields.clear();
        }
        // Effect particles: farthest first so alpha-blended smoke layers correctly.
        if (!sprites.empty()) {
            std::sort(sprites.begin(), sprites.end(), [&](const SpriteInstance& a, const SpriteInstance& b) {
                return (a.pos - cam.pos).length() > (b.pos - cam.pos).length();
            });
            SDL_GPUGraphicsPipeline* bound = nullptr;
            for (auto& sp : sprites) {
                SDL_GPUGraphicsPipeline* want = sp.additive ? sprite_add : sprite_alpha;
                if (want != bound) {
                    SDL_BindGPUGraphicsPipeline(pass, want);
                    bound = want;
                }
                DVec3 rel = sp.pos - cam.pos;
                struct { Mat4 vp; float center[4], r[4], u[4], rot[4], uv[4], taper[4]; } gu{};
                gu.taper[0] = sp.streak ? sp.taper : 1.f;
                std::copy(std::begin(sp.uv), std::end(sp.uv), gu.uv);
                gu.vp = view_proj;
                gu.center[0] = static_cast<float>(rel.x);
                gu.center[1] = static_cast<float>(rel.y);
                gu.center[2] = static_cast<float>(rel.z);
                gu.center[3] = sp.size;
                if (sp.flat) {
                    gu.r[0] = sp.ax_u.x; gu.r[1] = sp.ax_u.y; gu.r[2] = sp.ax_u.z;
                    gu.u[0] = sp.ax_v.x; gu.u[1] = sp.ax_v.y; gu.u[2] = sp.ax_v.z;
                    gu.rot[0] = 1;
                    gu.rot[1] = 0;
                } else if (sp.streak) {
                    // A quad along the segment, turned to face the camera: the texture's u runs
                    // across it and v along it.
                    Vec3 view{static_cast<float>(rel.x), static_cast<float>(rel.y), static_cast<float>(rel.z)};
                    Vec3 side = cross(sp.axis, view);
                    float len = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
                    float k = len > 1e-6f ? sp.size / len : 0.f;
                    side = len > 1e-6f ? Vec3{side.x * k, side.y * k, side.z * k}
                                       : Vec3{right.x * sp.size, right.y * sp.size, right.z * sp.size};
                    gu.center[3] = 1;
                    gu.r[0] = side.x; gu.r[1] = side.y; gu.r[2] = side.z;
                    gu.u[0] = sp.axis.x; gu.u[1] = sp.axis.y; gu.u[2] = sp.axis.z;
                    gu.rot[0] = 1;
                    gu.rot[1] = 0;
                } else {
                    gu.r[0] = right.x; gu.r[1] = right.y; gu.r[2] = right.z;
                    gu.u[0] = up.x; gu.u[1] = up.y; gu.u[2] = up.z;
                    gu.rot[0] = std::cos(sp.rotation);
                    gu.rot[1] = std::sin(sp.rotation);
                }
                SDL_PushGPUVertexUniformData(cmd, 0, &gu, sizeof gu);
                float fu[8];
                std::copy(std::begin(sp.color), std::end(sp.color), fu);
                const float* far = sp.streak ? sp.color2 : sp.color;
                std::copy(far, far + 4, fu + 4);
                // Fade out what comes right up to the camera, so an effect on our own ship
                // (a shield hit behind us) doesn't cover the whole screen.
                if (!sp.streak) {
                    double dist = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
                    float near_fade = static_cast<float>(std::clamp((dist - sp.size) / (2.0 * sp.size + 1.0), 0.0, 1.0));
                    fu[3] *= near_fade;
                    fu[7] *= near_fade;
                }
                SDL_PushGPUFragmentUniformData(cmd, 0, fu, sizeof fu);
                SDL_GPUTextureSamplerBinding b{sp.tex, gpu.clamp_sampler()};
                SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
                SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
            }
            sprites.clear();
        }
        SDL_EndGPURenderPass(pass);

        // ---- bloom
        SDL_GPUTexture* src = hdr;
        std::uint32_t sw = rw, sh = rh;
        for (std::size_t i = 0; i < bloom.size(); ++i) {
            float u[4] = {1.f / sw, 1.f / sh, 1.0f, i == 0 ? 1.f : 0.f};
            fullscreen(cmd, bloom[i], bloom_down, {src}, u, sizeof u);
            src = bloom[i];
            sw = bloom_size[i].first;
            sh = bloom_size[i].second;
        }
        for (std::size_t i = bloom.size(); i-- > 1;) {
            float u[4] = {1.f / bloom_size[i].first, 1.f / bloom_size[i].second, 1.0f, 0};
            fullscreen(cmd, bloom[i - 1], bloom_up, {bloom[i]}, u, sizeof u, true);
        }

        // ---- tonemap, upscale, sharpen
        const PostFx& pf = s.post;
        float glow = bloom.empty() ? 0.f : s.bloom + (pf.enabled ? pf.bloom : 0.f);
        float tu[16] = {s.exposure, glow, static_cast<float>(std::fmod(time, 1000.0)), pf.enabled ? 1.f : 0.f,
                        pf.saturation, pf.vibrance, pf.contrast, pf.brightness,
                        pf.temperature, pf.vignette, pf.grain, pf.chromatic,
                        static_cast<float>(rw) / std::max(1u, rh), 0, 0, 0};
        fullscreen(cmd, ldr, tonemap, {hdr, bloom.empty() ? hdr : bloom[0]}, tu, sizeof tu);
        SDL_GPUTexture* final_src = ldr;
        if (upscaled) {
            float uu[4] = {static_cast<float>(rw), static_cast<float>(rh), 1.f / rw, 1.f / rh};
            fullscreen(cmd, upscaled, upscale, {ldr}, uu, sizeof uu);
            final_src = upscaled;
        }
        float sharp = std::clamp(s.sharpness + (pf.enabled ? pf.sharpen : 0.f), 0.f, 1.f);
        float su[4] = {1.f / out_w, 1.f / out_h, sharp, 0};
        fullscreen(cmd, target, sharpen, {final_src}, su, sizeof su);

        // ---- HUD, at output resolution
        if (hud_verts || !hud_models.empty()) {
            SDL_GPUColorTargetInfo hc{};
            hc.texture = target;
            hc.load_op = SDL_GPU_LOADOP_LOAD;
            hc.store_op = SDL_GPU_STOREOP_STORE;
            auto* hp = SDL_BeginGPURenderPass(cmd, &hc, 1, nullptr);
            if (!hud_models.empty()) {
                SDL_BindGPUGraphicsPipeline(hp, hud_mesh_pipe);
                FragmentUniforms hb{};
                for (auto& hm : hud_models) {
                    // Orthographic: model units -> pixels -> clip space; flat depth.
                    Mat4 ortho;
                    ortho.at(0, 0) = hm.ppu * 2.f / out_w;
                    ortho.at(1, 1) = hm.ppu * 2.f / out_h;
                    ortho.at(2, 2) = 0;
                    ortho.at(0, 3) = hm.px * 2.f / out_w - 1.f;
                    ortho.at(1, 3) = 1.f - hm.py * 2.f / out_h;
                    ortho.at(2, 3) = 0.5f;
                    for (auto& part : hm.model->parts) {
                        if (part.lods.empty()) continue;
                        for (auto& dr : part.lods[0].draws) {
                            VertexUniforms vu{ortho, part.local, part.local.normal_matrix()};
                            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
                            GpuMaterial m = hm.model->materials[dr.material];
                            m.unlit = true;
                            m.dc = {m.dc.x * hm.tint.r, m.dc.y * hm.tint.g, m.dc.z * hm.tint.b};
                            m.opacity *= hm.tint.a;
                            bind_material(cmd, hp, m, hb, s);
                            SDL_GPUBufferBinding mvb{hm.model->vb, 0}, mib{hm.model->ib, 0};
                            SDL_BindGPUVertexBuffers(hp, 0, &mvb, 1);
                            SDL_BindGPUIndexBuffer(hp, &mib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                            SDL_DrawGPUIndexedPrimitives(hp, dr.index_count, 1, dr.first_index, dr.vertex_offset, 0);
                        }
                    }
                }
                // Line geometry (reticles): same transforms, line-list pipeline.
                SDL_BindGPUGraphicsPipeline(hp, hud_wire_pipe);
                for (auto& hm : hud_models) {
                    Mat4 ortho;
                    ortho.at(0, 0) = hm.ppu * 2.f / out_w;
                    ortho.at(1, 1) = hm.ppu * 2.f / out_h;
                    ortho.at(2, 2) = 0;
                    ortho.at(0, 3) = hm.px * 2.f / out_w - 1.f;
                    ortho.at(1, 3) = 1.f - hm.py * 2.f / out_h;
                    ortho.at(2, 3) = 0.5f;
                    for (auto& part : hm.model->parts)
                        for (auto& dr : part.wires) {
                            VertexUniforms vu{ortho, part.local, part.local.normal_matrix()};
                            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
                            GpuMaterial m = hm.model->materials[dr.material];
                            m.unlit = true;
                            m.dc = {m.dc.x * hm.tint.r, m.dc.y * hm.tint.g, m.dc.z * hm.tint.b};
                            m.opacity *= hm.tint.a;
                            bind_material(cmd, hp, m, hb, s);
                            SDL_GPUBufferBinding mvb{hm.model->vb, 0}, mib{hm.model->ib, 0};
                            SDL_BindGPUVertexBuffers(hp, 0, &mvb, 1);
                            SDL_BindGPUIndexBuffer(hp, &mib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                            SDL_DrawGPUIndexedPrimitives(hp, dr.index_count, 1, dr.first_index, dr.vertex_offset, 0);
                        }
                }
                hud_models.clear();
            }
            SDL_BindGPUGraphicsPipeline(hp, hud_pipe);
            float su2[4] = {2.f / out_w, 2.f / out_h, 0, 0};
            SDL_PushGPUVertexUniformData(cmd, 0, su2, sizeof su2);
            SDL_GPUBufferBinding vb{hud_vb, 0};
            SDL_BindGPUVertexBuffers(hp, 0, &vb, 1);
            SDL_GPUTextureSamplerBinding fb{font_tex ? font_tex : white, gpu.linear_sampler()};
            SDL_BindGPUFragmentSamplers(hp, 0, &fb, 1);
            if (hud_verts) SDL_DrawGPUPrimitives(hp, hud_verts, 1, 0, 0);
            SDL_EndGPURenderPass(hp);
        }
    }
};

Renderer::Renderer(Gpu& gpu, const Vfs& vfs, const Universe& universe)
    : impl_(std::make_unique<Impl>(gpu, vfs, universe)) {}
Renderer::~Renderer() = default;

void Renderer::set_hud_font(const FontAtlas* font) {
    auto* d = impl_->gpu.device();
    if (impl_->font_tex) {
        SDL_WaitForGPUIdle(d);
        SDL_ReleaseGPUTexture(d, impl_->font_tex);
        impl_->font_tex = nullptr;
    }
    HudBatch::set_font(font);
    if (!font) return;
    Image img;
    img.width = static_cast<std::uint32_t>(font->width);
    img.height = static_cast<std::uint32_t>(font->height);
    img.rgba.resize(font->pixels.size() * 4);
    for (std::size_t i = 0; i < font->pixels.size(); ++i)
        for (int c = 0; c < 4; ++c) img.rgba[i * 4 + c] = font->pixels[i];
    impl_->font_tex = impl_->gpu.texture(img, false);
}

void Renderer::set_output_format(SDL_GPUTextureFormat fmt) {
    if (fmt == impl_->output_format) return;
    impl_->output_format = fmt;
    impl_->msaa = -1;  // pipelines are rebuilt on the next frame
}

void Renderer::load_system(std::string_view nickname) {
    auto sys = impl_->universe.load_system(nickname);
    if (!sys) throw std::runtime_error("unknown system " + std::string(nickname));
    system_ = std::move(*sys);
    impl_->load_system(system_);
}

void Renderer::render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, std::uint32_t out_w, std::uint32_t out_h,
                      const Camera& cam, const RenderSettings& s, double time_seconds) {
    impl_->render(cmd, target, out_w, out_h, cam, s, time_seconds, system_, stats_, hud_, room_);
    hud_ = nullptr;
}

Renderer::ModelHandle Renderer::load_model(const std::string& path, const std::vector<std::string>& libs) {
    return ModelHandle{impl_->load_model(path, libs, false)};
}

Renderer::ModelHandle Renderer::load_character(const std::string& path, const std::vector<std::string>& libs,
                                               const std::string& instance) {
    return ModelHandle{impl_->load_model(path, libs, false, instance.empty() ? std::string("0") : instance)};
}

void Renderer::pose_character(ModelHandle m, const AnmScript* anim, float time) {
    impl_->pose_character(static_cast<GpuModel*>(m.p), anim, time);
}

void Renderer::add_mesh_library(const std::string& path) {
    impl_->shared_meshes.add_from(
        impl_->vfs.load_with_fallback(path, [](std::vector<std::uint8_t> b) { return UtfFile::parse(std::move(b)); }));
}

void Renderer::draw_glow(DVec3 pos, float size, Color3 color, float intensity) {
    impl_->glows.push_back({pos, size, color, intensity});
}

void Renderer::add_effect_textures(const std::string& txm_path) {
    std::string key = to_lower(txm_path);
    auto& paths = impl_->fx_lib_paths;
    if (std::find(paths.begin(), paths.end(), key) != paths.end()) return;
    paths.push_back(key);
    if (!impl_->fx_lib) impl_->fx_lib = std::make_unique<MaterialLibrary>();
    try {
        impl_->fx_lib->add_from(
            impl_->vfs.load_with_fallback(key, [](std::vector<std::uint8_t> b) { return UtfFile::parse(std::move(b)); }));
    } catch (const std::exception&) {
    }
}

void Renderer::draw_sprite(DVec3 pos, float size, float rotation, Color3 color, float alpha, const std::string& texture,
                           bool additive, float frame) {
    if (!impl_->fx_lib || alpha <= 0 || size <= 0) return;
    Impl::SpriteInstance sp{pos, size, rotation, {color.r, color.g, color.b, std::min(alpha, 1.f)}, nullptr, additive, {0, 0, 1, 1}};
    if (auto* fb = impl_->flipbook(texture)) {
        auto n = fb->frames.size();
        auto i = static_cast<std::size_t>(std::clamp(frame, 0.f, 1.f) * static_cast<float>(n));
        const auto& f = fb->frames[std::min(i, n - 1)];
        auto img = static_cast<std::size_t>(f[0]);
        sp.tex = fb->images[std::min(img, fb->images.size() - 1)];
        // Frame rects count v upwards; our images run top-down.
        sp.uv[0] = f[1];
        sp.uv[1] = 1 - f[2];
        sp.uv[2] = f[3];
        sp.uv[3] = 1 - f[4];
    } else {
        sp.tex = impl_->texture(*impl_->fx_lib, texture, true, false, nullptr);
        if (sp.tex == impl_->white) sp.tex = impl_->builtin_fx_texture(texture);
    }
    impl_->sprites.push_back(sp);
}

void Renderer::draw_hud_model(ModelHandle m, float px, float py, float ppu, HudColor tint) {
    if (!m) return;
    impl_->hud_models.push_back({static_cast<GpuModel*>(m.p), px, py, ppu, tint});
}

int Renderer::model_part_count(ModelHandle m) const {
    return m ? static_cast<int>(static_cast<GpuModel*>(m.p)->parts.size()) : 0;
}

Vec3 Renderer::model_part_offset(ModelHandle m, int part) const {
    if (!m || part < 0 || part >= model_part_count(m)) return {};
    const Mat4& l = static_cast<GpuModel*>(m.p)->parts[static_cast<std::size_t>(part)].local;
    return {l.at(0, 3), l.at(1, 3), l.at(2, 3)};
}

void Renderer::draw_model_part(ModelHandle m, int part, DVec3 pos, const Mat4& rotation,
                               const std::vector<float>& lod_ranges) {
    if (!m || part < 0 || part >= model_part_count(m)) return;
    impl_->dynamic.push_back({static_cast<GpuModel*>(m.p), pos, rotation, lod_ranges, part});
}

void Renderer::draw_model(ModelHandle m, DVec3 pos, const Mat4& rotation, const std::vector<float>& lod_ranges) {
    if (!m) return;
    impl_->dynamic.push_back({static_cast<GpuModel*>(m.p), pos, rotation, lod_ranges});
}

void Renderer::draw_streak(DVec3 from, DVec3 to, float width_from, float width_to, Color3 color_from, float alpha_from,
                           Color3 color_to, float alpha_to, const std::string& texture, bool additive) {
    std::size_t before = impl_->sprites.size();
    draw_sprite((from + to) * 0.5, width_from, 0, color_from, std::max(alpha_from, 1e-4f), texture, additive);
    if (impl_->sprites.size() == before) return;
    auto& sp = impl_->sprites.back();
    DVec3 half = (to - from) * 0.5;
    sp.streak = true;
    sp.axis = {static_cast<float>(half.x), static_cast<float>(half.y), static_cast<float>(half.z)};
    sp.color[3] = std::clamp(alpha_from, 0.f, 1.f);
    sp.color2[0] = color_to.r;
    sp.color2[1] = color_to.g;
    sp.color2[2] = color_to.b;
    sp.color2[3] = std::clamp(alpha_to, 0.f, 1.f);
    sp.taper = width_from > 0 ? width_to / width_from : 1.f;
}

void Renderer::draw_flat_sprite(DVec3 pos, float size, Vec3 u, Vec3 v, Color3 color, float alpha,
                                const std::string& texture, bool additive, float frame) {
    std::size_t before = impl_->sprites.size();
    draw_sprite(pos, size, 0, color, alpha, texture, additive, frame);
    if (impl_->sprites.size() == before) return;
    auto& sp = impl_->sprites.back();
    sp.flat = true;
    sp.ax_u = u;
    sp.ax_v = v;
}

void Renderer::draw_shield(DVec3 center, Vec3 ax0, Vec3 ax1, Vec3 ax2, Vec3 hit_dir, Color3 color, float strength) {
    if (strength <= 0) return;
    impl_->shields.push_back({center, {ax0, ax1, ax2}, hit_dir, color, std::min(strength, 1.f)});
}

}  // namespace sterrenvaarder::render
