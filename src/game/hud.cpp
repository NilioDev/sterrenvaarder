// SPDX-License-Identifier: MPL-2.0
#include "game/hud.hpp"

#include <cmath>
#include <cstdio>

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

HudLayout HudLayout::make(float w, float h, float max_aspect) {
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
    l.scale = std::min(h / 1080.f, l.canvas_w / 1440.f);
    return l;
}

void build_flight_hud(HudBatch& h, const HudLayout& L, const render::Camera& cam, const DVec3& player_pos, const HudState& st) {
    const float s = L.scale;
    const float cx = L.screen_w * 0.5f, cy = L.screen_h * 0.5f;  // optical centre = screen centre
    const float lw = 1.5f * s;

    // ---- centre reticle
    h.circle(cx, cy, 14 * s, lw, kDim);
    for (int i = 0; i < 4; ++i) {
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

    // ---- contacts in the world: small markers, brackets on the selected one
    for (auto& c : st.contacts) {
        float px, py;
        bool front = cam.project(c.pos, L.screen_w, L.screen_h, px, py);
        double dist = (c.pos - player_pos).length();
        if (c.selected) {
            bool onscreen = front && px > 0 && px < L.screen_w && py > 0 && py < L.screen_h;
            if (onscreen) {
                double z = std::max(1.0, (c.pos - cam.pos).length());
                float rpx = static_cast<float>(c.radius / (z * std::tan(cam.fov_y * 0.5)) * L.screen_h * 0.5);
                float b = std::clamp(rpx, 18 * s, L.canvas_h * 0.4f), t = std::max(8 * s, b * 0.35f);
                for (int i = 0; i < 4; ++i) {
                    float sx = (i & 1) ? 1.f : -1.f, sy = (i & 2) ? 1.f : -1.f;
                    float x0 = px + sx * b, y0 = py + sy * b;
                    h.line(x0, y0, x0 - sx * t, y0, 2 * s, kTarget);
                    h.line(x0, y0, x0, y0 - sy * t, 2 * s, kTarget);
                }
                h.text(px, py + b + 8 * s, 13 * s, c.name, kTarget, TextAlign::Center);
                h.text(px, py + b + 28 * s, 11 * s, fmt_distance(dist), kTarget.with_alpha(0.8f), TextAlign::Center);
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
        } else if (front && dist < 60000) {
            float m = 4 * s;
            h.rect_outline(px - m, py - m, 2 * m, 2 * m, 1.2f * s, kDim);
        }
    }

    // The space below the reticle belongs to the ship, so the gauges sit in the two
    // lower corners of the canvas: own ship on the left, target and contacts right.
    const float pw = 340 * s, ph = 190 * s, margin = 40 * s;
    const float py0 = L.canvas_y + L.canvas_h - ph - margin;
    char buf[64];

    // ---- bottom left: own ship
    float px0 = L.canvas_x + margin;
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

    // ---- bottom right: target and nearest contacts
    float qx = L.canvas_x + L.canvas_w - pw - margin;
    panel(h, qx, py0, pw, ph, s);
    h.text(qx + 16 * s, py0 + 14 * s, 10 * s, "TARGET", kLine);
    for (auto& c : st.contacts) {
        if (!c.selected) continue;
        h.text(qx + 16 * s, py0 + 36 * s, 12 * s, c.name.size() > 22 ? c.name.substr(0, 22) : c.name, kTarget);
        h.text(qx + pw - 16 * s, py0 + 14 * s, 10 * s, fmt_distance((c.pos - player_pos).length()), kTarget, TextAlign::Right);
        break;
    }
    h.line(qx + 16 * s, py0 + 62 * s, qx + pw - 16 * s, py0 + 62 * s, 1.0f * s, kDim);
    float row = py0 + 74 * s;
    int shown = 0;
    for (auto& c : st.contacts) {
        if (shown++ >= 6) break;
        HudColor col = c.selected ? kTarget : kLine.with_alpha(0.8f);
        std::string name = c.name.size() > 24 ? c.name.substr(0, 24) : c.name;
        h.text(qx + 16 * s, row, 8.5f * s, name, col);
        h.text(qx + pw - 16 * s, row, 8.5f * s, fmt_distance((c.pos - player_pos).length()), col, TextAlign::Right);
        row += 17 * s;
    }

    // ---- top centre: system name
    if (!st.system_name.empty()) h.text(L.cx(), L.canvas_y + 30 * s, 12 * s, st.system_name, kLine, TextAlign::Center);
}

}  // namespace sterrenvaarder::game
