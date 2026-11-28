// SPDX-License-Identifier: MPL-2.0
#include "game/hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "sterrenvaarder/bytes.hpp"
#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder::game {

using render::HudBatch;
using render::HudColor;
using render::TextAlign;

namespace {

const HudColor kLine{0.55f, 0.85f, 1.0f, 0.85f};
const HudColor kDim{0.55f, 0.85f, 1.0f, 0.35f};
const HudColor kPanel{0.02f, 0.08f, 0.14f, 0.55f};
const HudColor kShield{0.35f, 0.75f, 1.0f, 0.95f};
const HudColor kHull{1.0f, 0.72f, 0.25f, 0.95f};
const HudColor kCruise{0.6f, 1.0f, 0.75f, 0.95f};
const HudColor kTarget{1.0f, 0.85f, 0.3f, 0.95f};
const HudColor kHostile{1.0f, 0.32f, 0.25f, 0.95f};
const HudColor kFriendly{0.45f, 1.0f, 0.5f, 0.95f};

// Contact colour by attitude; neutral contacts use `neutral`.
HudColor attitude_color(const HudContact& c, HudColor neutral) {
    return c.hostile ? kHostile : c.friendly ? kFriendly : neutral;
}

std::string fmt_distance(double m) {
    char b[32];
    if (m < 1000) std::snprintf(b, sizeof b, "%dM", static_cast<int>(m));
    else if (m < 100000) std::snprintf(b, sizeof b, "%.1fK", m / 1000.0);
    else std::snprintf(b, sizeof b, "%dK", static_cast<int>(m / 1000.0));
    return b;
}

void bar(HudBatch& h, float x, float y, float w, float bh, float frac, HudColor c, float s) {
    h.rect(x, y, w, bh, kPanel);
    h.rect(x, y, w * std::clamp(frac, 0.f, 1.f), bh, c);
    h.rect_outline(x - s, y - s, w + 2 * s, bh + 2 * s, s, kDim);
}

void panel(HudBatch& h, float x, float y, float w, float ph, float s) {
    h.rect(x, y, w, ph, kPanel);
    // Corner ticks instead of a full frame: reads as a HUD, not a window.
    float t = 12 * s, lw = 1.5f * s;
    for (int i = 0; i < 4; ++i) {
        float cx = (i & 1) ? x + w : x, cy = (i & 2) ? y + ph : y;
        float dx = (i & 1) ? -t : t, dy = (i & 2) ? -t : t;
        h.line(cx, cy, cx + dx, cy, lw, kLine);
        h.line(cx, cy, cx, cy + dy, lw, kLine);
    }
}

}  // namespace

HudLayout HudLayout::make(float w, float h, float max_aspect, float user_scale) {
    HudLayout l;
    l.screen_w = w;
    l.screen_h = h;
    // Wider than max_aspect (21:9, 32:9): a centred canvas. Narrower (4:3, 5:4, 16:10):
    // the whole screen, with the HUD scaled so the layout (designed to fit 4:3 at
    // 1440 x 1080 units) never overlaps or runs off the sides.
    l.canvas_h = h;
    l.canvas_w = max_aspect > 0 ? std::min(w, h * max_aspect) : w;
    l.canvas_x = (w - l.canvas_w) * 0.5f;
    l.canvas_y = 0;
    l.scale = std::min(h / 1080.f, l.canvas_w / 1440.f) * std::clamp(user_scale, 0.25f, 4.f);
    return l;
}

void HudLayout::place(const HudPanelConfig& p, float w, float h, float& x, float& y) const {
    using A = HudPanelConfig::Anchor;
    float ox = p.offset_x * scale, oy = p.offset_y * scale;
    int col = 0, row = 0;  // 0 = start, 1 = middle, 2 = end
    switch (p.anchor) {
        case A::TopLeft: col = 0; row = 0; break;
        case A::Top: col = 1; row = 0; break;
        case A::TopRight: col = 2; row = 0; break;
        case A::Left: col = 0; row = 1; break;
        case A::Center: col = 1; row = 1; break;
        case A::Right: col = 2; row = 1; break;
        case A::BottomLeft: col = 0; row = 2; break;
        case A::Bottom: col = 1; row = 2; break;
        case A::BottomRight: col = 2; row = 2; break;
    }
    x = col == 0 ? canvas_x + ox : col == 1 ? canvas_x + (canvas_w - w) * 0.5f + ox : canvas_x + canvas_w - w - ox;
    y = row == 0 ? canvas_y + oy : row == 1 ? canvas_y + (canvas_h - h) * 0.5f + oy : canvas_y + canvas_h - h - oy;
}

std::vector<std::string> HudConfig::apply_ini(std::string_view text) {
    using A = HudPanelConfig::Anchor;
    std::vector<std::string> warnings;
    auto anchor = [&](const std::string& v, A fallback) {
        static const std::pair<const char*, A> names[] = {
            {"top_left", A::TopLeft}, {"top", A::Top},         {"top_right", A::TopRight},
            {"left", A::Left},        {"center", A::Center},   {"right", A::Right},
            {"bottom_left", A::BottomLeft}, {"bottom", A::Bottom}, {"bottom_right", A::BottomRight}};
        for (auto& [n, a] : names)
            if (iequals(v, n)) return a;
        warnings.push_back("unknown anchor " + v);
        return fallback;
    };
    for (auto& sec : parse_text_ini(text)) {
        if (iequals(sec.name, "Canvas")) {
            if (auto e = sec.find("max_aspect"); e && !e->values.empty()) max_aspect = std::max(0.f, e->values[0].as_float());
            if (auto e = sec.find("scale"); e && !e->values.empty()) scale = e->values[0].as_float();
            if (auto e = sec.find("style"); e && !e->values.empty()) {
                auto v = e->values[0].as_string();
                if (iequals(v, "classic")) style = HudStyle::Classic;
                else if (iequals(v, "modern")) style = HudStyle::Modern;
                else warnings.push_back("unknown style " + v);
            }
        } else if (iequals(sec.name, "Panel")) {
            auto n = sec.find("name");
            if (!n || n->values.empty()) continue;
            std::string name = n->values[0].as_string();
            HudPanelConfig* p = iequals(name, "ship") ? &ship : iequals(name, "target") ? &target
                              : iequals(name, "system") ? &system : iequals(name, "gauges") ? &gauges : nullptr;
            if (!p) { warnings.push_back("unknown panel " + name); continue; }
            if (auto e = sec.find("anchor"); e && !e->values.empty()) p->anchor = anchor(e->values[0].as_string(), p->anchor);
            if (auto e = sec.find("offset"); e && e->values.size() >= 2) {
                p->offset_x = e->values[0].as_float();
                p->offset_y = e->values[1].as_float();
            }
            if (auto e = sec.find("scale"); e && !e->values.empty()) p->scale = std::clamp(e->values[0].as_float(), 0.25f, 4.f);
            if (auto e = sec.find("visible"); e && !e->values.empty()) p->visible = e->values[0].as_bool();
        }
    }
    return warnings;
}

namespace {

// Bounds of the retail interface models (x0, x1, y0, y1 in model units, y up), measured
// from the data. They only decide where a piece is anchored; modded models still draw.
struct Box { float x0, x1, y0, y1; };
constexpr Box kShipInfo{-0.119f, 0.127f, -0.106f, 0.106f};
constexpr Box kTargetBox{-0.127f, 0.119f, -0.106f, 0.106f};
constexpr Box kGaugeWindow{-0.103f, 0.103f, -0.025f, 0.019f};

float classic_ppu(const HudLayout& L, float win_fovx_deg) {
    // Height of the original 4:3 interface view at depth 1.
    float half_w = std::tan(win_fovx_deg * 3.14159265f / 360.f);
    float view_h = 2.f * half_w * 0.75f;
    return L.scale * 1080.f / view_h;
}

// Places a model by its bounds; returns the origin in pixels and the panel rect.
void place_box(const HudLayout& L, const HudPanelConfig& p, const Box& b, float ppu, float& ox, float& oy, float& x, float& y,
               float& w, float& h) {
    ppu *= p.scale;
    w = (b.x1 - b.x0) * ppu;
    h = (b.y1 - b.y0) * ppu;
    L.place(p, w, h, x, y);
    ox = x - b.x0 * ppu;
    oy = y + b.y1 * ppu;
}

}  // namespace

namespace {

// Text and live values drawn over the original interface models.
void build_classic_overlay(HudBatch& h, const HudLayout& L, const HudConfig& cfg, const DVec3& player_pos, const HudState& st) {
    float ppu = classic_ppu(L, 54.432f), ox, oy, x, y, w, hh;
    char buf[64];
    if (cfg.gauges.visible) {
        place_box(L, cfg.gauges, kGaugeWindow, ppu, ox, oy, x, y, w, hh);
        float p = ppu * cfg.gauges.scale;
        // Shield and hull fills inside the gauge window.
        float gx = ox - 0.083f * p, gw = 0.166f * p, gh = 0.0045f * p;
        auto fill = [&](float model_y, float frac, HudColor c) {
            float yy = oy - model_y * p - gh * 0.5f;
            h.rect(gx, yy, gw * std::clamp(frac, 0.f, 1.f), gh, c);
        };
        fill(0.0035f, st.shield, kShield);
        fill(-0.0055f, st.hull, kHull);
        // Speed and throttle in the two number boxes either side.
        float ts = 0.014f * p;
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::round(st.speed)));
        float sts = std::strlen(buf) > 3 ? ts * 0.6f : ts;  // lane and cruise speeds fit the box
        h.text(ox - 0.145f * p, oy + 0.004f * p - sts * 0.5f, sts, buf, st.cruising ? kCruise : kLine, TextAlign::Center);
        std::snprintf(buf, sizeof buf, "%d%%", static_cast<int>(std::round(std::fabs(st.throttle) * 100)));
        float tts = std::strlen(buf) > 3 ? ts * 0.75f : ts;
        h.text(ox + 0.147f * p, oy + 0.004f * p - tts * 0.5f, tts, buf, st.throttle < 0 ? kHull : kLine, TextAlign::Center);
        if (st.cruising || st.cruise_charge > 0) {
            float a0 = -1.5707963f, cr = 0.012f * p;
            float cx = ox, cy = oy + 0.045f * p;
            h.circle(cx, cy, cr, 1.f * L.scale, kDim);
            h.arc(cx, cy, cr, a0, a0 + 6.2831853f * (st.cruising ? 1.f : st.cruise_charge), 3.f * L.scale, kCruise);
        }
    }
    if (cfg.ship.visible) {
        // Left window: the contact list.
        place_box(L, cfg.ship, kShipInfo, ppu, ox, oy, x, y, w, hh);
        float sc = cfg.ship.scale * L.scale;
        float row = y + 0.062f * ppu * cfg.ship.scale;
        int shown = 0;
        for (auto& c : st.contacts) {
            if (shown++ >= 8) break;
            HudColor col = attitude_color(c, c.selected ? kTarget : kLine).with_alpha(c.selected ? 1.f : 0.8f);
            std::string name = c.display().size() > 22 ? c.display().substr(0, 22) : c.display();
            h.text(x + 18 * sc, row, 8.5f * sc, name, col);
            h.text(x + w - 18 * sc, row, 8.5f * sc, fmt_distance((c.pos - player_pos).length()), col, TextAlign::Right);
            row += 17 * sc;
        }
    }
    if (cfg.target.visible) {
        place_box(L, cfg.target, kTargetBox, ppu, ox, oy, x, y, w, hh);
        float sc = cfg.target.scale * L.scale;
        for (auto& c : st.contacts) {
            if (!c.selected) continue;
            float ty = y + 0.035f * ppu * cfg.target.scale;
            h.text(x + w * 0.5f, ty, 10 * sc, c.display().size() > 24 ? c.display().substr(0, 24) : c.display(), attitude_color(c, kTarget), TextAlign::Center);
            if (!c.faction.empty())
                h.text(x + w * 0.5f, ty + 16 * sc, 8 * sc, c.faction.size() > 30 ? c.faction.substr(0, 30) : c.faction, kDim, TextAlign::Center);
            h.text(x + w * 0.5f, y + hh - 34 * sc, 10 * sc, fmt_distance((c.pos - player_pos).length()), kLine, TextAlign::Center);
            if (c.hull >= 0) {
                float bw = w * 0.6f, bx = x + (w - bw) * 0.5f, bh = 6 * sc;
                bar(h, bx, y + hh - 70 * sc, bw, bh, c.shield, kShield, sc);
                bar(h, bx, y + hh - 56 * sc, bw, bh, c.hull, kHull, sc);
            }
            break;
        }
    }
    if (!st.status.empty())
        h.text(L.cx(), L.canvas_y + L.canvas_h * 0.28f, 14 * L.scale, st.status, kCruise, TextAlign::Center);
    if (cfg.system.visible && !st.system_name.empty()) {
        float ts = 12 * L.scale * cfg.system.scale;
        float tw = HudBatch::text_width(ts, st.system_name);
        L.place(cfg.system, tw, ts, x, y);
        h.text(x, y, ts, st.system_name, kLine);
    }
}

}  // namespace

std::vector<HudModelDraw> classic_hud_models(const HudLayout& L, const HudConfig& cfg, float win_fovx_deg) {
    std::vector<HudModelDraw> out;
    float ppu = classic_ppu(L, win_fovx_deg), ox, oy, x, y, w, h;
    if (cfg.ship.visible) {
        place_box(L, cfg.ship, kShipInfo, ppu, ox, oy, x, y, w, h);
        out.push_back({"interface/hud/hud_shipinfo.cmp", ox, oy, ppu * cfg.ship.scale});
    }
    if (cfg.target.visible) {
        place_box(L, cfg.target, kTargetBox, ppu, ox, oy, x, y, w, h);
        out.push_back({"interface/hud/hud_target.cmp", ox, oy, ppu * cfg.target.scale});
    }
    if (cfg.gauges.visible) {
        place_box(L, cfg.gauges, kGaugeWindow, ppu, ox, oy, x, y, w, h);
        float p = ppu * cfg.gauges.scale;
        out.push_back({"interface/hud/hud_guagewindow.cmp", ox, oy, p});
        out.push_back({"interface/hud/hud_numberboxes.cmp", ox, oy, p});
    }
    // Centre reticle (line model), always at the optical centre of the screen.
    out.push_back({"interface/hud/hud_reticle.3db", L.screen_w * 0.5f, L.screen_h * 0.5f, ppu, kLine});
    return out;
}

void build_flight_hud(HudBatch& h, const HudLayout& L, const HudConfig& cfg, const render::Camera& cam, const DVec3& player_pos,
                      const HudState& st) {
    const float s = L.scale;
    const float cx = L.screen_w * 0.5f, cy = L.screen_h * 0.5f;  // optical centre = screen centre
    const float lw = 1.5f * s;

    // ---- centre reticle (the classic style uses the original reticle model)
    if (cfg.style == HudStyle::Modern) h.circle(cx, cy, 14 * s, lw, kDim);
    for (int i = 0; cfg.style == HudStyle::Modern && i < 4; ++i) {
        float a = i * 1.5707963f;
        float dx = std::cos(a), dy = std::sin(a);
        h.line(cx + dx * 20 * s, cy + dy * 20 * s, cx + dx * 30 * s, cy + dy * 30 * s, lw, kLine);
    }
    // ---- flight cursor (mouse) and a faint steering line
    if (st.mouse_flight) {
        float mx = st.cursor_x, my = st.cursor_y, r = 9 * s;
        float dx = mx - cx, dy = my - cy;
        if (dx * dx + dy * dy > (40 * s) * (40 * s)) h.line(cx, cy, mx, my, 1.0f * s, kDim.with_alpha(0.18f));
        h.line(mx - r, my, mx, my - r, lw, kLine);
        h.line(mx, my - r, mx + r, my, lw, kLine);
        h.line(mx + r, my, mx, my + r, lw, kLine);
        h.line(mx, my + r, mx - r, my, lw, kLine);
    }

    // ---- mission waypoint: a marker with its name and distance, or an arrow when off-screen
    if (st.has_waypoint) {
        const HudColor wc{0.55f, 0.85f, 1.f, 0.95f};
        float px, py;
        bool front = cam.project(st.waypoint, L.screen_w, L.screen_h, px, py);
        double dist = (st.waypoint - player_pos).length();
        if (front && px > 0 && px < L.screen_w && py > 0 && py < L.screen_h) {
            float r = 11 * s;
            h.line(px - r, py, px, py - r, 2 * s, wc);
            h.line(px, py - r, px + r, py, 2 * s, wc);
            h.line(px + r, py, px, py + r, 2 * s, wc);
            h.line(px, py + r, px - r, py, 2 * s, wc);
            h.circle(px, py, r * 1.6f, 1.2f * s, wc.with_alpha(0.6f));
            h.text(px, py + r * 1.6f + 8 * s, 12 * s, st.waypoint_label, wc, TextAlign::Center);
            h.text(px, py + r * 1.6f + 26 * s, 11 * s, fmt_distance(dist), wc.with_alpha(0.8f), TextAlign::Center);
        } else {
            Vec3 d{static_cast<float>(st.waypoint.x - cam.pos.x), static_cast<float>(st.waypoint.y - cam.pos.y),
                   static_cast<float>(st.waypoint.z - cam.pos.z)};
            Vec3 r = cam.right(), u = cam.up();
            float ax = d.x * r.x + d.y * r.y + d.z * r.z, ay = -(d.x * u.x + d.y * u.y + d.z * u.z);
            float len = std::sqrt(ax * ax + ay * ay);
            if (len > 0) {
                ax /= len;
                ay /= len;
                float rr = L.canvas_h * 0.34f, tx = cx + ax * rr, ty = cy + ay * rr, k = 10 * s;
                h.line(tx - ay * k, ty + ax * k, tx + ax * k, ty + ay * k, 2 * s, wc);
                h.line(tx + ay * k, ty - ax * k, tx + ax * k, ty + ay * k, 2 * s, wc);
                h.line(tx - ay * k, ty + ax * k, tx + ay * k, ty - ax * k, 2 * s, wc);
            }
        }
    }

    // ---- contacts in the world: small markers, brackets on the selected one
    for (auto& c : st.contacts) {
        float px, py;
        bool front = cam.project(c.pos, L.screen_w, L.screen_h, px, py);
        double dist = (c.pos - player_pos).length();
        if (c.selected) {
            HudColor bc = attitude_color(c, kTarget);
            bool onscreen = front && px > 0 && px < L.screen_w && py > 0 && py < L.screen_h;
            if (onscreen) {
                double z = std::max(1.0, (c.pos - cam.pos).length());
                float rpx = static_cast<float>(c.radius / (z * std::tan(cam.fov_y * 0.5)) * L.screen_h * 0.5);
                float b = std::clamp(rpx, 18 * s, L.canvas_h * 0.22f), t = std::max(8 * s, b * 0.35f);
                for (int i = 0; i < 4; ++i) {
                    float sx = (i & 1) ? 1.f : -1.f, sy = (i & 2) ? 1.f : -1.f;
                    float x0 = px + sx * b, y0 = py + sy * b;
                    h.line(x0, y0, x0 - sx * t, y0, 2 * s, bc);
                    h.line(x0, y0, x0, y0 - sy * t, 2 * s, bc);
                }
                h.text(px, py + b + 8 * s, 13 * s, c.display(), bc, TextAlign::Center);
                h.text(px, py + b + 28 * s, 11 * s, fmt_distance(dist), bc.with_alpha(0.8f), TextAlign::Center);
            } else {
                // Off-screen: an arrow on a ring around the reticle pointing towards it.
                Vec3 d{static_cast<float>(c.pos.x - cam.pos.x), static_cast<float>(c.pos.y - cam.pos.y),
                       static_cast<float>(c.pos.z - cam.pos.z)};
                Vec3 r = cam.right(), u = cam.up();
                float ax = d.x * r.x + d.y * r.y + d.z * r.z, ay = -(d.x * u.x + d.y * u.y + d.z * u.z);
                float len = std::sqrt(ax * ax + ay * ay);
                if (len > 0) {
                    ax /= len;
                    ay /= len;
                    float rr = L.canvas_h * 0.3f;
                    float tx = cx + ax * rr, ty = cy + ay * rr;
                    float nx = -ay, ny = ax, k = 12 * s;
                    h.line(tx + ax * k, ty + ay * k, tx + nx * k, ty + ny * k, 2.5f * s, kTarget);
                    h.line(tx + ax * k, ty + ay * k, tx - nx * k, ty - ny * k, 2.5f * s, kTarget);
                }
            }
        } else if (c.marked && front) {
            // A mission marker: a diamond with the name and distance, at any range.
            HudColor mc = attitude_color(c, kTarget).with_alpha(0.9f);
            float m = 9 * s;
            h.line(px, py - m, px + m, py, 1.8f * s, mc);
            h.line(px + m, py, px, py + m, 1.8f * s, mc);
            h.line(px, py + m, px - m, py, 1.8f * s, mc);
            h.line(px - m, py, px, py - m, 1.8f * s, mc);
            h.text(px, py + m + 6 * s, 11 * s, c.display() + "  " + fmt_distance(dist), mc, TextAlign::Center);
        } else if (front && dist < 60000) {
            float m = 4 * s;
            h.rect_outline(px - m, py - m, 2 * m, 2 * m, 1.2f * s, kDim);
        }
    }

    // The space below the reticle belongs to the ship, so the gauges sit in the two
    // lower corners of the canvas: own ship on the left, target and contacts right.
    char buf[64];

    if (cfg.style == HudStyle::Classic) {
        build_classic_overlay(h, L, cfg, player_pos, st);
    }

    // ---- own ship (default: bottom left)
    if (cfg.style == HudStyle::Modern && cfg.ship.visible) {
    const float s0 = s;
    {
    const float s = s0 * cfg.ship.scale;
    const float pw = 340 * s, ph = 190 * s;
    float px0, py0;
    L.place(cfg.ship, pw, ph, px0, py0);
    panel(h, px0, py0, pw, ph, s);
    h.text(px0 + 16 * s, py0 + 14 * s, 10 * s, "SHIP", kLine);
    std::snprintf(buf, sizeof buf, "%03d", static_cast<int>(std::round(st.speed)));
    h.text(px0 + 120 * s, py0 + 44 * s, 30 * s, buf, st.cruising ? kCruise : kLine, TextAlign::Right);
    h.text(px0 + 130 * s, py0 + 64 * s, 9 * s, "M/S", kDim);
    // Throttle: a vertical bar at the panel's left edge.
    float tx = px0 + 18 * s, ty = py0 + 40 * s, th = 50 * s;
    h.rect(tx, ty, 8 * s, th, kPanel);
    float f = std::clamp(st.throttle, -1.f, 1.f);
    if (f >= 0) h.rect(tx, ty + th * (1 - f), 8 * s, th * f, kLine);
    else h.rect(tx, ty, 8 * s, th * -f, kHull);
    h.rect_outline(tx - s, ty - s, 10 * s, th + 2 * s, s, kDim);
    // Cruise state on the right of the speed.
    float ccx = px0 + pw - 50 * s, ccy = py0 + 62 * s;
    if (st.cruising) {
        h.text(px0 + pw - 16 * s, py0 + 56 * s, 11 * s, "CRUISE", kCruise, TextAlign::Right);
    } else if (st.cruise_charge > 0) {
        float a0 = -1.5707963f;
        h.circle(ccx, ccy, 16 * s, 1.0f * s, kDim);
        h.arc(ccx, ccy, 16 * s, a0, a0 + 6.2831853f * st.cruise_charge, 3 * s, kCruise);
    }
    float bx = px0 + 70 * s, bw = pw - 86 * s, bh = 8 * s;
    h.text(px0 + 16 * s, py0 + 118 * s, 10 * s, "SHD", kShield);
    bar(h, bx, py0 + 120 * s, bw, bh, st.shield, kShield, s);
    h.text(px0 + 16 * s, py0 + 148 * s, 10 * s, "HULL", kHull);
    bar(h, bx, py0 + 150 * s, bw, bh, st.hull, kHull, s);
    }
    }

    // ---- target and nearest contacts (default: bottom right)
    if (cfg.style == HudStyle::Modern && cfg.target.visible) {
    const float s0 = s;
    {
    const float s = s0 * cfg.target.scale;
    const float pw = 340 * s, ph = 190 * s;
    float qx, py0;
    L.place(cfg.target, pw, ph, qx, py0);
    panel(h, qx, py0, pw, ph, s);
    h.text(qx + 16 * s, py0 + 14 * s, 10 * s, "TARGET", kLine);
    for (auto& c : st.contacts) {
        if (!c.selected) continue;
        h.text(qx + 16 * s, py0 + 36 * s, 11 * s, c.display().size() > 26 ? c.display().substr(0, 26) : c.display(), kTarget);
        h.text(qx + pw - 16 * s, py0 + 14 * s, 10 * s, fmt_distance((c.pos - player_pos).length()), kTarget, TextAlign::Right);
        break;
    }
    h.line(qx + 16 * s, py0 + 62 * s, qx + pw - 16 * s, py0 + 62 * s, 1.0f * s, kDim);
    float row = py0 + 74 * s;
    int shown = 0;
    for (auto& c : st.contacts) {
        if (shown++ >= 6) break;
        HudColor col = attitude_color(c, c.selected ? kTarget : kLine).with_alpha(c.selected ? 1.f : 0.8f);
        std::string name = c.display().size() > 24 ? c.display().substr(0, 24) : c.display();
        h.text(qx + 16 * s, row, 8.5f * s, name, col);
        h.text(qx + pw - 16 * s, row, 8.5f * s, fmt_distance((c.pos - player_pos).length()), col, TextAlign::Right);
        row += 17 * s;
    }
    }
    }

    // ---- top centre: system name
    if (cfg.style == HudStyle::Modern && !st.status.empty())
        h.text(L.cx(), L.canvas_y + L.canvas_h * 0.28f, 14 * s, st.status, kCruise, TextAlign::Center);
    if (cfg.style == HudStyle::Modern && cfg.system.visible && !st.system_name.empty()) {
        float ts = 12 * s * cfg.system.scale;
        float w = HudBatch::text_width(ts, st.system_name), x, y;
        L.place(cfg.system, w, ts, x, y);
        h.text(x, y, ts, st.system_name, kLine);
    }
}

std::string format_money(std::int64_t v) {
    std::string digits = std::to_string(v < 0 ? -v : v), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (v < 0 ? "-$" : "$") + out;
}

double nav_map_extent(const std::vector<NavMapItem>& items) {
    double e = 0;
    for (auto& i : items) e = std::max({e, std::abs(i.pos.x), std::abs(i.pos.z)});
    e *= 1.08;  // a margin round the edge
    return std::max(20000.0, std::ceil(e / 10000.0) * 10000.0);
}

std::string nav_map_cell(const NavMap& m, const DVec3& p) {
    auto cell = [&](double v) { return std::clamp(static_cast<int>(std::floor((v + m.extent) / (2 * m.extent) * 8)), 0, 7); };
    std::string out;
    out += static_cast<char>('A' + cell(p.x));
    out += static_cast<char>('1' + cell(p.z));
    return out;
}

void build_nav_map(HudBatch& h, const HudLayout& L, const NavMap& m) {
    float s = L.scale;
    float side = std::min(L.canvas_h * 0.82f, L.canvas_w - 40 * s);
    float x0 = L.cx() - side * 0.5f, y0 = L.cy() - side * 0.5f + 10 * s;
    h.rect(x0 - 30 * s, y0 - 54 * s, side + 60 * s, side + 84 * s, {0.01f, 0.03f, 0.07f, 0.9f});
    panel(h, x0 - 30 * s, y0 - 54 * s, side + 60 * s, side + 84 * s, s);
    h.text(L.cx(), y0 - 42 * s, 16 * s, m.title, kTarget, TextAlign::Center);
    auto to_map = [&](const DVec3& p, float& mx, float& my) {
        mx = x0 + static_cast<float>((p.x + m.extent) / (2 * m.extent)) * side;
        my = y0 + static_cast<float>((p.z + m.extent) / (2 * m.extent)) * side;
    };
    // Grid with its letters and numbers.
    float cellw = side / 8;
    for (int i = 0; i <= 8; ++i) {
        float t = static_cast<float>(i) * cellw;
        h.line(x0 + t, y0, x0 + t, y0 + side, 1 * s, kDim.with_alpha(0.25f));
        h.line(x0, y0 + t, x0 + side, y0 + t, 1 * s, kDim.with_alpha(0.25f));
        if (i < 8) {
            char c[2] = {static_cast<char>('A' + i), 0};
            h.text(x0 + t + cellw * 0.5f, y0 - 16 * s, 10 * s, c, kDim, TextAlign::Center);
            c[0] = static_cast<char>('1' + i);
            h.text(x0 - 14 * s, y0 + t + cellw * 0.5f - 5 * s, 10 * s, c, kDim, TextAlign::Center);
        }
    }
    auto inside = [&](float mx, float my) { return mx >= x0 && mx <= x0 + side && my >= y0 && my <= y0 + side; };
    // Trade lanes as thin lines.
    for (auto& [a, b] : m.lanes) {
        float ax, ay, bx, by;
        to_map(a, ax, ay);
        to_map(b, bx, by);
        if (inside(ax, ay) && inside(bx, by)) h.line(ax, ay, bx, by, 1.5f * s, kLine.with_alpha(0.45f));
    }
    // Objects: a ring sized to the object (planets, stars) or a small mark, and the name.
    // Names that would overlap one already written are left out; bases, planets and gates
    // go first so they keep theirs.
    struct Box { float x0, y0, x1, y1; };
    std::vector<Box> used;
    auto place_label = [&](float lx, float ly, float size, const std::string& text) {
        Box b{lx, ly - 2 * s, lx + HudBatch::text_width(size, text), ly + size + 2 * s};
        for (auto& u : used)
            if (b.x0 < u.x1 && u.x0 < b.x1 && b.y0 < u.y1 && u.y0 < b.y1) return false;
        used.push_back(b);
        return true;
    };
    std::vector<const NavMapItem*> order;
    for (auto& it : m.items) order.push_back(&it);
    auto rank = [](NavMapItem::Kind k) {
        switch (k) {
            case NavMapItem::Kind::Star: return 0;
            case NavMapItem::Kind::Planet: return 1;
            case NavMapItem::Kind::Base: return 2;
            case NavMapItem::Kind::Gate: return 3;
            default: return 4;
        }
    };
    std::stable_sort(order.begin(), order.end(), [&](auto* a, auto* b) { return rank(a->kind) < rank(b->kind); });
    for (auto* itp : order) {
        auto& it = *itp;
        float mx, my;
        to_map(it.pos, mx, my);
        if (!inside(mx, my)) continue;
        float r = std::max(3.f * s, static_cast<float>(it.radius / (2 * m.extent)) * side);
        HudColor c = kLine;
        switch (it.kind) {
            case NavMapItem::Kind::Star: c = {1.f, 0.85f, 0.45f, 0.9f}; h.circle(mx, my, r, 1.5f * s, c, 24); break;
            case NavMapItem::Kind::Planet: h.circle(mx, my, r, 1.5f * s, c, 24); break;
            case NavMapItem::Kind::Base: h.rect(mx - 3 * s, my - 3 * s, 6 * s, 6 * s, c); break;
            case NavMapItem::Kind::Gate:
                c = {0.75f, 0.6f, 1.f, 0.95f};
                h.line(mx - 5 * s, my, mx, my - 5 * s, 1.5f * s, c);
                h.line(mx, my - 5 * s, mx + 5 * s, my, 1.5f * s, c);
                h.line(mx + 5 * s, my, mx, my + 5 * s, 1.5f * s, c);
                h.line(mx, my + 5 * s, mx - 5 * s, my, 1.5f * s, c);
                break;
            default: h.rect(mx - 1.5f * s, my - 1.5f * s, 3 * s, 3 * s, kDim); break;
        }
        if (!it.label.empty() && place_label(mx + r + 5 * s, my - 5 * s, 10 * s, it.label))
            h.text(mx + r + 5 * s, my - 5 * s, 10 * s, it.label, c.with_alpha(0.85f));
    }
    // Mission waypoint.
    if (m.has_waypoint) {
        float mx, my;
        to_map(m.waypoint, mx, my);
        if (inside(mx, my)) {
            const HudColor wc{0.55f, 0.85f, 1.f, 0.95f};
            h.circle(mx, my, 8 * s, 1.5f * s, wc, 20);
            h.line(mx - 12 * s, my, mx + 12 * s, my, 1 * s, wc);
            h.line(mx, my - 12 * s, mx, my + 12 * s, 1 * s, wc);
            if (!m.waypoint_label.empty() && place_label(mx + 14 * s, my + 4 * s, 10 * s, m.waypoint_label))
                h.text(mx + 14 * s, my + 4 * s, 10 * s, m.waypoint_label, wc);
        }
    }
    // The player: an arrow pointing where the ship heads.
    {
        float mx, my;
        to_map(m.player, mx, my);
        mx = std::clamp(mx, x0, x0 + side);
        my = std::clamp(my, y0, y0 + side);
        float a = m.player_heading, k = 9 * s;
        float fx = std::sin(a), fy = -std::cos(a);  // forward on the map
        float rx = -fy, ry = fx;
        float tipx = mx + fx * k, tipy = my + fy * k;
        float lx = mx - fx * k * 0.7f + rx * k * 0.6f, ly = my - fy * k * 0.7f + ry * k * 0.6f;
        float qx = mx - fx * k * 0.7f - rx * k * 0.6f, qy = my - fy * k * 0.7f - ry * k * 0.6f;
        h.line(tipx, tipy, lx, ly, 2 * s, kTarget);
        h.line(lx, ly, qx, qy, 2 * s, kTarget);
        h.line(qx, qy, tipx, tipy, 2 * s, kTarget);
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "YOU ARE IN %s     SCALE %.0f K PER SQUARE     M = CLOSE", nav_map_cell(m, m.player).c_str(),
                  m.extent / 4000.0);
    h.text(L.cx(), y0 + side + 10 * s, 10 * s, buf, kDim, TextAlign::Center);
}

void build_base_screen(HudBatch& h, const HudLayout& L, const BaseScreen& b) {
    float s = L.scale;
    float w = std::min(L.canvas_w - 40 * s, 900 * s), ph = 560 * s;
    float x = L.cx() - w * 0.5f, y = L.canvas_y + L.canvas_h * 0.1f;
    panel(h, x, y, w, ph, s);
    float pad = 26 * s;
    h.text(x + pad, y + pad, 18 * s, b.title, kTarget);
    h.text(x + w - pad, y + pad, 12 * s, b.system, kDim, TextAlign::Right);
    char buf[96];
    h.text(x + pad, y + pad + 34 * s, 12 * s, "CREDITS  " + format_money(b.credits), kLine);
    std::snprintf(buf, sizeof buf, "CARGO  %d / %d", static_cast<int>(std::lround(b.hold_used)), static_cast<int>(std::lround(b.hold_size)));
    h.text(x + w - pad, y + pad + 34 * s, 12 * s, buf, kLine, TextAlign::Right);
    bar(h, x + pad, y + pad + 56 * s, w - 2 * pad, 4 * s, b.hold_size > 0 ? b.hold_used / b.hold_size : 0, kHull, s);

    // Tabs
    float tx = x + pad, tyy = y + pad + 74 * s;
    for (std::size_t i = 0; i < b.tabs.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%zu  %s", i + 1, b.tabs[i].c_str());
        bool on = static_cast<int>(i) == b.tab;
        float tw = HudBatch::text_width(11 * s, buf);
        if (on) h.rect(tx - 8 * s, tyy - 6 * s, tw + 16 * s, 23 * s, kLine.with_alpha(0.16f));
        h.text(tx, tyy, 11 * s, buf, on ? kTarget : kDim);
        tx += tw + 34 * s;
    }

    float ty = tyy + 36 * s;
    float c_price = x + w * 0.60f, c3 = x + w * 0.78f, c4 = x + w - pad;
    h.text(x + pad + 14 * s, ty, 10 * s, b.columns[0], kDim);
    h.text(c_price, ty, 10 * s, b.columns[1], kDim, TextAlign::Right);
    h.text(c3, ty, 10 * s, b.columns[2], kDim, TextAlign::Right);
    h.text(c4, ty, 10 * s, b.columns[3], kDim, TextAlign::Right);
    ty += 24 * s;
    const float row_h = 22 * s;
    const int visible = std::max(1, static_cast<int>((y + ph - 70 * s - ty) / row_h));
    int first = std::clamp(b.selected - visible / 2, 0, std::max(0, static_cast<int>(b.rows.size()) - visible));
    for (int i = first; i < static_cast<int>(b.rows.size()) && i < first + visible; ++i) {
        auto& r = b.rows[i];
        if (r.heading) {
            h.text(x + pad, ty + 2 * s, 10 * s, r.name, kDim);
            h.line(x + pad + HudBatch::text_width(10 * s, r.name) + 10 * s, ty + 7 * s, x + w - pad, ty + 7 * s, 1.f * s, kDim);
            ty += row_h;
            continue;
        }
        bool sel = i == b.selected;
        HudColor col = sel ? kTarget : (r.dim ? kDim : kLine);
        if (sel) h.rect(x + pad, ty - 5 * s, w - 2 * pad, row_h, kTarget.with_alpha(0.12f));
        std::string name = r.name.size() > 30 ? r.name.substr(0, 30) : r.name;
        h.text(x + pad + 14 * s, ty, 11 * s, name, col);
        h.text(c_price, ty, 11 * s, r.price, col, TextAlign::Right);
        h.text(c3, ty, 11 * s, r.col3, col, TextAlign::Right);
        h.text(c4, ty, 11 * s, r.col4, col, TextAlign::Right);
        ty += row_h;
    }
    if (b.rows.empty()) h.text(x + pad + 14 * s, ty, 11 * s, "NOTHING FOR SALE HERE", kDim);
    float fy = y + ph - pad - 10 * s;
    h.text(x + pad, fy, 10 * s, b.help, kDim);
    if (!b.message.empty()) h.text(x + w - pad, fy - 24 * s, 11 * s, b.message, kCruise, TextAlign::Right);
}

}  // namespace sterrenvaarder::game
