// SPDX-License-Identifier: MPL-2.0
// Sterrenvaarder system viewer: fly freely through a star system of your own game data.
//
//   sterrenvaarder --data <Freelancer DATA dir> [--mod <dir>]... [--system Li01]
//                  [--width 2560 --height 1080] [--scale 0.77] [--msaa 4] [--lod-scale 1]
//                  [--screenshot out.png --camera x,y,z --look x,y,z]
//
// Controls: right mouse = look, WASD/QE = move, Shift = boost, wheel = speed,
//           F = toggle fullscreen, Esc = quit.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "render/renderer.hpp"
#include "sterrenvaarder/png.hpp"

using namespace sterrenvaarder;
using namespace sterrenvaarder::render;

namespace {

DVec3 parse_vec(const std::string& s) {
    DVec3 v;
    std::sscanf(s.c_str(), "%lf,%lf,%lf", &v.x, &v.y, &v.z);
    return v;
}

struct Args {
    std::vector<std::string> mounts;
    std::string system = "Li01";
    int width = 1920, height = 1080;
    RenderSettings settings;
    std::string screenshot;
    bool has_camera = false, has_look = false;
    DVec3 camera, look;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
            return argv[++i];
        };
        if (k == "--data" || k == "--mod") a.mounts.push_back(next());
        else if (k == "--system") a.system = next();
        else if (k == "--width") a.width = std::stoi(next());
        else if (k == "--height") a.height = std::stoi(next());
        else if (k == "--scale") a.settings.render_scale = std::stof(next());
        else if (k == "--sharpness") a.settings.sharpness = std::stof(next());
        else if (k == "--msaa") a.settings.msaa = std::stoi(next());
        else if (k == "--exposure") a.settings.exposure = std::stof(next());
        else if (k == "--lod-scale") a.settings.lod_scale = std::stof(next());
        else if (k == "--normal-strength") a.settings.normal_strength = std::stof(next());
        else if (k == "--screenshot") a.screenshot = next();
        else if (k == "--camera") { a.camera = parse_vec(next()); a.has_camera = true; }
        else if (k == "--look") { a.look = parse_vec(next()); a.has_look = true; }
        else throw std::runtime_error("unknown option " + k);
    }
    if (a.mounts.empty()) throw std::runtime_error("usage: sterrenvaarder --data <Freelancer DATA dir> [--system Li01]");
    return a;
}

void place_default_camera(Camera& cam, const StarSystem& sys) {
    // Start near the first planet or station, looking at it.
    for (auto& o : sys.objects) {
        if (o.archetype.find("planet") == std::string::npos) continue;
        DVec3 p{o.pos.x, o.pos.y, o.pos.z};
        cam.pos = p + DVec3{0, 2500, 12000};
        cam.look_at(p);
        return;
    }
}

int run(const Args& args) {
    Vfs vfs;
    for (std::size_t i = 0; i < args.mounts.size(); ++i) vfs.mount(args.mounts[i], i == 0 ? "base" : args.mounts[i]);
    Universe universe(vfs);

    const bool offscreen = !args.screenshot.empty();
    if (offscreen) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());

    SDL_Window* window = nullptr;
    if (!offscreen) {
        window = SDL_CreateWindow("Sterrenvaarder", args.width, args.height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) throw std::runtime_error(SDL_GetError());
    }
    int rc = 0;
    {
        Gpu gpu(window);
        SDL_Log("GPU driver: %s", gpu.driver());
        Renderer renderer(gpu, vfs, universe);
        Uint64 t0 = SDL_GetTicksNS();
        renderer.load_system(args.system);
        SDL_Log("loaded %s in %.2f s: %zu objects", args.system.c_str(), (SDL_GetTicksNS() - t0) / 1e9,
                renderer.system().objects.size());

        Camera cam;
        place_default_camera(cam, renderer.system());
        if (args.has_camera) cam.pos = args.camera;
        if (args.has_look) cam.look_at(args.look);
        RenderSettings settings = args.settings;

        if (offscreen) {
            renderer.set_output_format(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            auto* out = gpu.render_target(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, args.width, args.height);
            auto* cmd = SDL_AcquireGPUCommandBuffer(gpu.device());
            renderer.render(cmd, out, args.width, args.height, cam, settings, 0.0);
            SDL_SubmitGPUCommandBuffer(cmd);
            Image img;
            img.width = args.width;
            img.height = args.height;
            img.rgba = gpu.download_rgba8(out, args.width, args.height);
            auto png = encode_png(img);
            std::ofstream(args.screenshot, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            auto& st = renderer.stats();
            SDL_Log("wrote %s (%dx%d): %d objects drawn, %d beyond draw distance, %d draw calls, %d triangles",
                    args.screenshot.c_str(), args.width, args.height, st.objects_drawn, st.culled_by_lod, st.draw_calls, st.triangles);
            SDL_ReleaseGPUTexture(gpu.device(), out);
        } else {
            renderer.set_output_format(SDL_GetGPUSwapchainTextureFormat(gpu.device(), window));
            bool running = true, looking = false;
            double speed = 300.0;
            Uint64 last = SDL_GetTicksNS();
            double time = 0;
            while (running) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_EVENT_QUIT) running = false;
                    else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
                    else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F)
                        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
                        looking = true;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                        looking = false;
                        SDL_SetWindowRelativeMouseMode(window, false);
                    } else if (e.type == SDL_EVENT_MOUSE_MOTION && looking) {
                        cam.yaw -= e.motion.xrel * 0.003f;
                        cam.pitch = std::clamp(cam.pitch - e.motion.yrel * 0.003f, -1.55f, 1.55f);
                    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                        speed = std::clamp(speed * (e.wheel.y > 0 ? 1.25 : 0.8), 10.0, 200000.0);
                    }
                }
                Uint64 now = SDL_GetTicksNS();
                double dt = (now - last) / 1e9;
                last = now;
                time += dt;
                const bool* keys = SDL_GetKeyboardState(nullptr);
                double v = speed * dt * (keys[SDL_SCANCODE_LSHIFT] ? 10.0 : 1.0);
                Vec3 f = cam.forward(), r = cam.right();
                auto move = [&](Vec3 d, double amt) { cam.pos = cam.pos + DVec3{d.x, d.y, d.z} * amt; };
                if (keys[SDL_SCANCODE_W]) move(f, v);
                if (keys[SDL_SCANCODE_S]) move(f, -v);
                if (keys[SDL_SCANCODE_D]) move(r, v);
                if (keys[SDL_SCANCODE_A]) move(r, -v);
                if (keys[SDL_SCANCODE_E]) move({0, 1, 0}, v);
                if (keys[SDL_SCANCODE_Q]) move({0, 1, 0}, -v);

                auto* cmd = SDL_AcquireGPUCommandBuffer(gpu.device());
                SDL_GPUTexture* swap = nullptr;
                Uint32 w = 0, h = 0;
                if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap && w && h)
                    renderer.render(cmd, swap, w, h, cam, settings, time);
                SDL_SubmitGPUCommandBuffer(cmd);
            }
        }
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
