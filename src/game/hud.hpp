// SPDX-License-Identifier: MPL-2.0
#pragma once
// Flight HUD. Every element is placed inside a centred "HUD canvas" with a fixed
// aspect ratio (16:9 by default), so on 21:9 and 32:9 screens the gauges stay in
// front of the player instead of drifting to the far edges. The 3D world still uses
// the full width (Hor+ field of view).
#include <string>
#include <vector>

#include "game/flight.hpp"
#include "render/hud_batch.hpp"
#include "render/renderer.hpp"

namespace sterrenvaarder::game {

struct HudLayout {
    float screen_w = 1920, screen_h = 1080;
    float canvas_x = 0, canvas_y = 0, canvas_w = 1920, canvas_h = 1080;
    float scale = 1;  // 1 at 1080 lines

    // max_aspect: widest canvas allowed (16:9 = 1.777). 0 stretches to the screen edges.
    static HudLayout make(float w, float h, float max_aspect = 16.f / 9.f);
    float cx() const { return canvas_x + canvas_w * 0.5f; }
    float cy() const { return canvas_y + canvas_h * 0.5f; }
};

struct HudContact {
    std::string name;
    DVec3 pos;
    float radius = 0;   // for bracket size
    bool selected = false;
};

struct HudState {
    float speed = 0, max_speed = 80, throttle = 0;
    float hull = 1, shield = 1;  // 0..1
    float cruise_charge = 0;     // 0..1
    bool cruising = false;
    bool mouse_flight = true;
    float cursor_x = 0, cursor_y = 0;  // pixels
    std::string system_name;
    std::vector<HudContact> contacts;  // nearest first
};

void build_flight_hud(render::HudBatch& out, const HudLayout& layout, const render::Camera& cam,
                      const DVec3& player_pos, const HudState& state);

}  // namespace sterrenvaarder::game
