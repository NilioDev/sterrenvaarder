// SPDX-License-Identifier: MPL-2.0
#pragma once
// Flight HUD. Every element is placed inside a centred "HUD canvas" with a fixed
// aspect ratio (16:9 by default), so on 21:9 and 32:9 screens the gauges stay in
// front of the player instead of drifting to the far edges. The 3D world still uses
// the full width (Hor+ field of view).
#include <string>
#include <string_view>
#include <vector>

#include "game/flight.hpp"
#include "render/hud_batch.hpp"
#include "render/renderer.hpp"

namespace sterrenvaarder::game {

// Placement of one HUD panel inside the canvas. Offsets are in 1080p units, measured
// inwards from the anchor (so "bottom_right" with 40,40 sits 40 units from both edges).
struct HudPanelConfig {
    enum class Anchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
    Anchor anchor = Anchor::BottomLeft;
    float offset_x = 40, offset_y = 40;
    float scale = 1;
    bool visible = true;
};

// User and mod adjustable HUD settings, read from a small INI file:
//
//   [Canvas]
//   style = classic         ; classic (original HUD models) or modern
//   max_aspect = 1.7778     ; widest HUD area; 0 = use the full screen width
//   scale = 1.0             ; overall HUD size
//
//   [Panel]
//   name = target           ; ship, target, system, gauges
//   anchor = bottom_right   ; top_left, top, top_right, left, center, right,
//                           ; bottom_left, bottom, bottom_right
//   offset = 40, 40
//   scale = 1.0
//   visible = true
enum class HudStyle {
    Classic,  // the original interface models from the player's own data (default)
    Modern,   // Sterrenvaarder's own vector HUD, no game files needed
};

struct HudConfig {
    HudStyle style = HudStyle::Classic;
    float max_aspect = 16.f / 9.f;
    float scale = 1;
    HudPanelConfig ship{HudPanelConfig::Anchor::BottomLeft, 40, 40, 1, true};
    HudPanelConfig target{HudPanelConfig::Anchor::BottomRight, 40, 40, 1, true};
    HudPanelConfig system{HudPanelConfig::Anchor::Top, 0, 30, 1, true};
    HudPanelConfig gauges{HudPanelConfig::Anchor::Bottom, 0, 24, 1, true};  // classic style

    // Applies the settings in `text` on top of the current values. Unknown keys and
    // panels are ignored so newer files still load. Returns warnings, if any.
    std::vector<std::string> apply_ini(std::string_view text);
};

struct HudLayout {
    float screen_w = 1920, screen_h = 1080;
    float canvas_x = 0, canvas_y = 0, canvas_w = 1920, canvas_h = 1080;
    float scale = 1;  // 1 at 1080 lines

    // max_aspect: widest canvas allowed (16:9 = 1.777). 0 stretches to the screen edges.
    static HudLayout make(float w, float h, float max_aspect = 16.f / 9.f, float user_scale = 1.f);
    // Top-left corner (pixels) of a panel of size (w, h) in pixels, placed per `p`.
    void place(const HudPanelConfig& p, float w, float h, float& x, float& y) const;
    float cx() const { return canvas_x + canvas_w * 0.5f; }
    float cy() const { return canvas_y + canvas_h * 0.5f; }
};

struct HudContact {
    std::string name;
    DVec3 pos;
    float radius = 0;   // for bracket size
    bool selected = false;
    bool hostile = false;
    float hull = -1, shield = -1;  // 0..1 for ships, < 0 for objects without them
};

struct HudState {
    float speed = 0, max_speed = 80, throttle = 0;
    float hull = 1, shield = 1;  // 0..1
    float cruise_charge = 0;     // 0..1
    bool cruising = false;
    bool mouse_flight = true;
    float cursor_x = 0, cursor_y = 0;  // pixels
    std::string system_name;
    std::string status;  // navigation state ("TRADE LANE", "DOCKED: ...", prompts)
    std::vector<HudContact> contacts;  // nearest first
};

// One original interface model to draw (classic style), loaded from the player's data.
struct HudModelDraw {
    const char* model;  // game path
    float px, py;       // model origin in output pixels
    float ppu;          // pixels per model unit
    render::HudColor tint{};
};

// Classic style: where the original interface models go. Model units are those of the
// original 4:3 interface camera (cameras.ini [WinCamera] fovx), so at 4:3 each piece is
// exactly its original size; on wider screens the pieces sit on the centred canvas.
std::vector<HudModelDraw> classic_hud_models(const HudLayout& layout, const HudConfig& config, float win_fovx_deg = 54.432f);

void build_flight_hud(render::HudBatch& out, const HudLayout& layout, const HudConfig& config,
                      const render::Camera& cam, const DVec3& player_pos, const HudState& state);

}  // namespace sterrenvaarder::game
