// SPDX-License-Identifier: MPL-2.0
// Sterrenvaarder: fly your own ship through a star system of your own game data.
//
//   sterrenvaarder --data <Freelancer DATA dir> [--mod <dir>]... [--system Li01]
//                  [--ship li_elite] [--width 2560 --height 1080] [--scale 0.77] [--msaa 4]
//                  [--lod-scale 1] [--hud-aspect 1.778] [--hud-config hud.ini] [--fov 70] [--free]
//                  [--screenshot out.png [--sim 4 --throttle 1 --cursor 0.3,-0.1]]
//                  [--camera x,y,z --look x,y,z]   (spawn point / free camera)
//
// Flight controls: mouse = steer (Space toggles mouse flight), W/S = throttle,
//   Shift+W = cruise, A/D = strafe, Q/E = roll, Z = stop engines, T = next target,
//   F11 = fullscreen, Esc = quit.
// Free camera (--free): right mouse = look, WASD/QE = move, Shift = boost, wheel = speed.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "game/flight.hpp"
#include "game/hud.hpp"
#include "render/renderer.hpp"
#include "sterrenvaarder/ships.hpp"
#include "sterrenvaarder/png.hpp"

using namespace sterrenvaarder;
using namespace sterrenvaarder::render;
using sterrenvaarder::game::Quat;

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
    bool free = false;
    std::string ship = "li_elite";
    float hud_aspect = -1;  // < 0: from the HUD config
    std::string hud_config;
    float fov = 0;  // horizontal FOV in degrees at 4:3; 0 = cameras.ini (original)
    double sim = 0;
    float throttle = 1;
    float cursor_x = 0, cursor_y = 0;
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
        else if (k == "--free") a.free = true;
        else if (k == "--ship") a.ship = next();
        else if (k == "--hud-aspect") a.hud_aspect = std::stof(next());
        else if (k == "--hud-config") a.hud_config = next();
        else if (k == "--fov") a.fov = std::stof(next());
        else if (k == "--sim") a.sim = std::stod(next());
        else if (k == "--throttle") a.throttle = std::stof(next());
        else if (k == "--cursor") std::sscanf(next().c_str(), "%f,%f", &a.cursor_x, &a.cursor_y);
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

// The player's ship, its flight model, the chase camera and the HUD state.
struct FlightSession {
    const ShipArchetype* arch = nullptr;
    game::FlightModel model{game::FlightParams{}};
    game::ShipState ship;
    game::FlightInput input;
    Renderer::ModelHandle mesh;
    Quat cam_orient;
    bool mouse_flight = true;
    int target = -1;  // index into contacts
    std::vector<game::HudContact> contacts;
    float win_fovx = 54.432f;  // cameras.ini [WinCamera]: the original interface view
    std::unordered_map<std::string, Renderer::ModelHandle> hud_models;
    bool hud_meshes_loaded = false;

    // Original interface models come from the player's own data, loaded on first use.
    Renderer::ModelHandle hud_model(Renderer& r, const std::string& path) {
        if (auto it = hud_models.find(path); it != hud_models.end()) return it->second;
        Renderer::ModelHandle h;
        try {
            if (!hud_meshes_loaded) {
                hud_meshes_loaded = true;
                r.add_mesh_library("interface/interface.generic.vms");
            }
            h = r.load_model(path, {"interface/hud/hud.txm"});
        } catch (const std::exception& e) {
            SDL_Log("HUD model %s: %s", path.c_str(), e.what());
        }
        return hud_models[path] = h;
    }

    void spawn(const StarSystem& sys, const Args& args) {
        // Default: in front of the first station, facing it.
        DVec3 focus{0, 0, 0};
        bool found = false;
        for (auto& o : sys.objects)
            if (o.archetype.find("station") != std::string::npos || o.archetype.find("Station") != std::string::npos) {
                focus = {o.pos.x, o.pos.y, o.pos.z};
                found = true;
                break;
            }
        if (!found && !sys.objects.empty()) focus = {sys.objects[0].pos.x, sys.objects[0].pos.y, sys.objects[0].pos.z};
        ship.pos = focus + DVec3{0, 150, 2200};
        if (args.has_camera) ship.pos = args.camera;
        DVec3 look = args.has_look ? args.look : focus;
        DVec3 d = look - ship.pos;
        ship.orient = Quat::look({static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)});
        cam_orient = ship.orient;
    }

    void update_contacts(const StarSystem& sys) {
        std::string selected = target >= 0 && target < static_cast<int>(contacts.size()) ? contacts[target].name : "";
        contacts.clear();
        for (auto& o : sys.objects) {
            game::HudContact c;
            c.name = o.nickname;
            c.pos = {o.pos.x, o.pos.y, o.pos.z};
            c.radius = 200;
            contacts.push_back(std::move(c));
        }
        std::sort(contacts.begin(), contacts.end(), [&](auto& a, auto& b) {
            return (a.pos - ship.pos).length() < (b.pos - ship.pos).length();
        });
        target = -1;
        for (std::size_t i = 0; i < contacts.size(); ++i)
            if (contacts[i].name == selected) target = static_cast<int>(i);
        if (target >= 0) contacts[target].selected = true;
    }

    void next_target() {
        if (contacts.empty()) return;
        target = (target + 1) % static_cast<int>(std::min<std::size_t>(contacts.size(), 10));
        for (auto& c : contacts) c.selected = false;
        contacts[target].selected = true;
    }

    // Chase camera: trails the ship's orientation with a little lag, from the ship's
    // own camera_offset (up, back), so turns read on screen as in the original.
    void update_camera(Camera& cam, double dt) {
        float k = 1.f - std::exp(-static_cast<float>(dt) * 7.f);
        cam_orient = Quat::slerp(cam_orient, ship.orient, k).normalized();
        Vec3 off = cam_orient.rotate({0, arch ? arch->camera_offset.x : 8.f, arch ? arch->camera_offset.y : 34.f});
        cam.pos = ship.pos + game::to_d(off);
        cam.set_basis(cam_orient.forward(), cam_orient.up());
    }

    void draw(Renderer& r) {
        Mat4 rot = ship.orient.matrix() * Mat4::rotate_z(ship.bank);
        r.draw_model(mesh, ship.pos, rot, arch ? arch->lod_ranges : std::vector<float>{});
    }

    game::HudState hud_state(const StarSystem& sys, float cursor_x, float cursor_y) const {
        game::HudState h;
        h.speed = ship.speed();
        h.max_speed = model.params().max_speed();
        h.throttle = input.throttle;
        h.cruise_charge = model.params().cruise_charge_time > 0 ? ship.cruise_charge / model.params().cruise_charge_time : 0;
        h.cruising = ship.cruising;
        h.mouse_flight = mouse_flight;
        h.cursor_x = cursor_x;
        h.cursor_y = cursor_y;
        h.system_name = sys.nickname;
        h.contacts = contacts;
        return h;
    }
};

void render_frame(Gpu& gpu, Renderer& renderer, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, Uint32 w, Uint32 h,
                  const Camera& cam, const RenderSettings& settings, double time, FlightSession* fs, const game::HudConfig& hud_cfg,
                  float cursor_x, float cursor_y, HudBatch& hud) {
    (void)gpu;
    if (fs) {
        fs->draw(renderer);
        hud.clear();
        auto layout = game::HudLayout::make(static_cast<float>(w), static_cast<float>(h), hud_cfg.max_aspect, hud_cfg.scale);
        game::build_flight_hud(hud, layout, hud_cfg, cam, fs->ship.pos, fs->hud_state(renderer.system(), cursor_x, cursor_y));
        renderer.set_hud(&hud);
        if (hud_cfg.style == game::HudStyle::Classic)
            for (auto& d : game::classic_hud_models(layout, hud_cfg, fs->win_fovx))
                renderer.draw_hud_model(fs->hud_model(renderer, d.model), d.px, d.py, d.ppu, d.tint);
    }
    renderer.render(cmd, target, w, h, cam, settings, time);
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

        std::unique_ptr<ShipDatabase> ships;
        std::unique_ptr<FlightSession> fs;
        if (!args.free) {
            ships = std::make_unique<ShipDatabase>(vfs);
            fs = std::make_unique<FlightSession>();
            fs->arch = ships->ship(args.ship);
            if (!fs->arch) throw std::runtime_error("unknown ship " + args.ship);
            const EngineArchetype* engine = nullptr;
            if (auto* lo = ships->loadout_for_ship(args.ship)) engine = ships->engine_of(*lo);
            fs->model = game::FlightModel(game::FlightParams::from(*fs->arch, engine, ships->cruise_speed()));
            fs->mesh = renderer.load_model(fs->arch->model, fs->arch->material_libraries);
            // Field of view: the chase camera's fovx from cameras.ini was defined for 4:3.
            // Keep that vertical angle and let wider screens see more to the sides (Hor+).
            float fovx = 70;
            if (vfs.exists("cameras.ini"))
                for (auto& sec : parse_ini(vfs.read("cameras.ini")))
                    if (auto* e = sec.find("fovx"); e && !e->values.empty()) {
                        if (iequals(sec.name, "ChaseCamera")) fovx = e->values[0].as_float();
                        else if (iequals(sec.name, "WinCamera")) fs->win_fovx = e->values[0].as_float();
                    }
            if (args.fov > 0) fovx = std::clamp(args.fov, 40.f, 120.f);
            cam.fov_y = 2.f * std::atan(std::tan(fovx * 3.14159265f / 360.f) * 0.75f);
            fs->spawn(renderer.system(), args);
            fs->update_contacts(renderer.system());
            fs->next_target();
            fs->update_camera(cam, 1.0);
            SDL_Log("ship %s: top speed %.0f m/s, cruise %.0f m/s", fs->arch->nickname.c_str(), fs->model.params().max_speed(),
                    fs->model.params().cruise_speed);
        }
        HudBatch hud;
        // HUD layout: built-in defaults, then sterrenvaarder/hud.ini from the data layers
        // (so a mod can ship one), then --hud-config, then --hud-aspect.
        game::HudConfig hud_cfg;
        auto apply_hud = [&](std::string_view text, const std::string& from) {
            for (auto& w : hud_cfg.apply_ini(text)) SDL_Log("%s: %s", from.c_str(), w.c_str());
        };
        if (vfs.exists("sterrenvaarder/hud.ini")) {
            auto b = vfs.read("sterrenvaarder/hud.ini");
            apply_hud(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()), "hud.ini");
        }
        if (!args.hud_config.empty()) {
            std::ifstream f(args.hud_config, std::ios::binary);
            if (!f) throw std::runtime_error("cannot read " + args.hud_config);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            apply_hud(text, args.hud_config);
        }
        if (args.hud_aspect >= 0) hud_cfg.max_aspect = args.hud_aspect;

        if (offscreen) {
            float cx = args.width * 0.5f + args.cursor_x * args.height * 0.5f;
            float cy = args.height * 0.5f + args.cursor_y * args.height * 0.5f;
            if (fs) {
                // Scripted flight so screenshots show the ship in motion.
                fs->input.throttle = args.throttle;
                fs->input.steer = game::mouse_steer(args.cursor_x, args.cursor_y);
                for (double t = 0; t < args.sim; t += 1.0 / 60) {
                    fs->model.update(fs->ship, fs->input, 1.0 / 60);
                    fs->update_camera(cam, 1.0 / 60);
                }
                fs->update_contacts(renderer.system());
            }
            renderer.set_output_format(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            auto* out = gpu.render_target(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, args.width, args.height);
            auto* cmd = SDL_AcquireGPUCommandBuffer(gpu.device());
            render_frame(gpu, renderer, cmd, out, args.width, args.height, cam, settings, args.sim, fs.get(), hud_cfg, cx, cy,
                         hud);
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
            if (fs) SDL_Log("ship speed %.1f m/s", fs->ship.speed());
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
                    else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_F11 || (args.free && e.key.key == SDLK_F)))
                        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                    else if (fs && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                        if (e.key.key == SDLK_SPACE) fs->mouse_flight = !fs->mouse_flight;
                        else if (e.key.key == SDLK_T) fs->next_target();
                        else if (e.key.key == SDLK_Z) { fs->input.throttle = 0; fs->input.cruise = false; }
                        else if (e.key.key == SDLK_W && (e.key.mod & SDL_KMOD_SHIFT)) fs->input.cruise = !fs->input.cruise;
                        else if (e.key.key == SDLK_S) fs->input.cruise = false;
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
                        looking = true;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                        looking = false;
                        SDL_SetWindowRelativeMouseMode(window, false);
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_MOTION && looking) {
                        cam.yaw -= e.motion.xrel * 0.003f;
                        cam.pitch = std::clamp(cam.pitch - e.motion.yrel * 0.003f, -1.55f, 1.55f);
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_WHEEL) {
                        speed = std::clamp(speed * (e.wheel.y > 0 ? 1.25 : 0.8), 10.0, 200000.0);
                    }
                }
                Uint64 now = SDL_GetTicksNS();
                double dt = (now - last) / 1e9;
                last = now;
                time += dt;
                const bool* keys = SDL_GetKeyboardState(nullptr);

                int ww = 0, wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                float mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                if (fs) {
                    auto& in = fs->input;
                    bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                    if (keys[SDL_SCANCODE_W] && !shift) in.throttle = std::min(1.f, in.throttle + static_cast<float>(dt) * 1.2f);
                    if (keys[SDL_SCANCODE_S]) in.throttle = std::max(-1.f, in.throttle - static_cast<float>(dt) * 1.2f);
                    in.strafe_x = (keys[SDL_SCANCODE_D] ? 1.f : 0.f) - (keys[SDL_SCANCODE_A] ? 1.f : 0.f);
                    float roll = (keys[SDL_SCANCODE_Q] ? 1.f : 0.f) - (keys[SDL_SCANCODE_E] ? 1.f : 0.f);
                    float half = wh > 0 ? wh * 0.5f : 1.f;
                    in.steer = fs->mouse_flight ? game::mouse_steer((mx - ww * 0.5f) / half, (my - wh * 0.5f) / half) : Vec3{};
                    in.steer.z = roll;
                    fs->model.update(fs->ship, in, dt);
                    fs->update_camera(cam, dt);
                    fs->update_contacts(renderer.system());
                } else {
                    double v = speed * dt * (keys[SDL_SCANCODE_LSHIFT] ? 10.0 : 1.0);
                    Vec3 f = cam.forward(), r = cam.right();
                    auto move = [&](Vec3 d, double amt) { cam.pos = cam.pos + DVec3{d.x, d.y, d.z} * amt; };
                    if (keys[SDL_SCANCODE_W]) move(f, v);
                    if (keys[SDL_SCANCODE_S]) move(f, -v);
                    if (keys[SDL_SCANCODE_D]) move(r, v);
                    if (keys[SDL_SCANCODE_A]) move(r, -v);
                    if (keys[SDL_SCANCODE_E]) move({0, 1, 0}, v);
                    if (keys[SDL_SCANCODE_Q]) move({0, 1, 0}, -v);
                }

                auto* cmd = SDL_AcquireGPUCommandBuffer(gpu.device());
                SDL_GPUTexture* swap = nullptr;
                Uint32 w = 0, h = 0;
                if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap && w && h) {
                    // Mouse is in window points; the swapchain may be in pixels (HiDPI).
                    float sx = ww > 0 ? static_cast<float>(w) / ww : 1.f, sy = wh > 0 ? static_cast<float>(h) / wh : 1.f;
                    render_frame(gpu, renderer, cmd, swap, w, h, cam, settings, time, fs.get(), hud_cfg, mx * sx, my * sy,
                                 hud);
                }
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
