// SPDX-License-Identifier: MPL-2.0
#include "game/particles.hpp"

#include <algorithm>
#include <cmath>

namespace sterrenvaarder::game {

namespace {

constexpr float kDeg = 3.14159265f / 180.f;

float curve(const AleNode& n, std::uint32_t hash, float spark, float time, float fallback) {
    if (auto* c = n.get<AleCurve>(hash)) return c->eval(spark, time);
    return n.number(hash, fallback);
}

float anim(const AleNode* n, std::uint32_t hash, float spark, float t, float fallback) {
    if (!n) return fallback;
    if (auto* a = n->get<AleAnimFloat>(hash)) return a->eval(spark, t);
    return n->number(hash, fallback);
}

// The node's placement relative to the effect: translation and rotation (degrees, X then
// Y then Z) from its transform channels.
void node_placement(const AleNode& n, float spark, float time, Vec3& offset, Quat& rot) {
    offset = {};
    rot = Quat{};
    auto* t = n.get<AleTransform>(ale::kTransform);
    if (!t || !t->animated) return;
    offset = {t->channels[0].eval(spark, time), t->channels[1].eval(spark, time), t->channels[2].eval(spark, time)};
    float rx = t->channels[3].eval(spark, time) * kDeg;
    float ry = t->channels[4].eval(spark, time) * kDeg;
    float rz = t->channels[5].eval(spark, time) * kDeg;
    rot = Quat::axis_angle({0, 0, 1}, rz) * Quat::axis_angle({0, 1, 0}, ry) * Quat::axis_angle({1, 0, 0}, rx);
}

}  // namespace

EffectInstance::EffectInstance(ResolvedEffect fx, std::uint32_t seed) : fx_(std::move(fx)), rng_(seed * 747796405u + 2891336453u) {
    if (rng_ == 0) rng_ = 1;
    if (!fx_) return;
    std::unordered_map<std::uint32_t, const AleNode*> by_id;
    for (auto& r : fx_.effect->refs)
        if (r.node_crc != kAleRoot) by_id[r.id] = fx_.lib->node(r.node_crc);
    for (auto& [em, app] : fx_.effect->pairs) {
        auto e = by_id.find(em);
        auto a = by_id.find(app);
        if (e == by_id.end() || !e->second || !e->second->is_emitter()) continue;
        Emitter x;
        x.node = e->second;
        x.appearance = (a != by_id.end() && a->second && a->second->is_appearance()) ? a->second : nullptr;
        emitters_.push_back(x);
    }
}

float EffectInstance::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xffffff) / static_cast<float>(0x1000000);
}

void EffectInstance::spawn(Emitter& e, std::uint16_t index, DVec3 pos, const Quat& orient, DVec3 vel, float spark) {
    const AleNode& n = *e.node;
    float t = static_cast<float>(time_);
    Vec3 offset;
    Quat local;
    node_placement(n, spark, t, offset, local);
    Quat q = orient * local;

    float speed = curve(n, ale::kPressure, spark, t, 0);
    Vec3 start{}, dir{0, 1, 0};
    if (n.kind == "FxSphereEmitter") {
        float z = rand01() * 2 - 1, phi = rand01() * 6.2831853f, r = std::sqrt(std::max(0.f, 1 - z * z));
        dir = {r * std::cos(phi), r * std::sin(phi), z};
        float rmin = curve(n, ale::kSphereMinRadius, spark, t, 0), rmax = curve(n, ale::kSphereMaxRadius, spark, t, 0);
        start = dir * (rmin + (rmax - rmin) * rand01());
    } else {
        // Cone and cube emitters spray around their local +Y within a spread angle.
        bool cone = n.kind == "FxConeEmitter";
        float amin = curve(n, cone ? ale::kConeMinRadius : ale::kCubeMinSpread, spark, t, 0);
        float amax = curve(n, cone ? ale::kConeMaxRadius : ale::kCubeMaxSpread, spark, t, 0);
        float a = (amin + (amax - amin) * rand01()) * kDeg;
        float phi = rand01() * 6.2831853f;
        dir = {std::sin(a) * std::cos(phi), std::cos(a), std::sin(a) * std::sin(phi)};
        if (!cone) {
            start = {(rand01() - 0.5f) * curve(n, ale::kCubeWidth, spark, t, 0),
                     (rand01() - 0.5f) * curve(n, ale::kCubeHeight, spark, t, 0),
                     (rand01() - 0.5f) * curve(n, ale::kCubeDepth, spark, t, 0)};
        }
    }
    Particle p;
    p.pos = pos + to_d(orient.rotate(offset) + q.rotate(start));
    Vec3 wv = q.rotate(dir) * speed;
    p.vel = {static_cast<float>(vel.x) + wv.x, static_cast<float>(vel.y) + wv.y, static_cast<float>(vel.z) + wv.z};
    p.life = std::max(0.01f, curve(n, ale::kParticleLife, spark, t, 1));
    p.spin = rand01() * 6.2831853f;
    p.emitter = index;
    particles_.push_back(p);
    ++e.live;
}

void EffectInstance::update(double dt, DVec3 pos, const Quat& orient, DVec3 vel, float spark, bool emitting) {
    spark_ = spark;
    float fdt = static_cast<float>(dt);
    for (auto& p : particles_) {
        p.age += fdt;
        p.pos = p.pos + to_d(p.vel * fdt);
    }
    for (auto& p : particles_)
        if (p.age >= p.life) --emitters_[p.emitter].live;
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life; });

    if (emitting) {
        for (std::size_t i = 0; i < emitters_.size(); ++i) {
            auto& e = emitters_[i];
            const AleNode& n = *e.node;
            float t = static_cast<float>(time_);
            if (time_ > n.number(ale::kLifespan, 1e30f)) continue;
            auto max_live = static_cast<std::size_t>(std::max(1.f, curve(n, ale::kMaxParticles, spark, t, 1000)));
            if (!e.started) {
                e.started = true;
                int initial = static_cast<int>(n.number(ale::kInitialParticles, 0));
                for (int k = 0; k < initial && e.live < max_live; ++k)
                    spawn(e, static_cast<std::uint16_t>(i), pos, orient, vel, spark);
            }
            e.accumulator += curve(n, ale::kFrequency, spark, t, 0) * fdt;
            int count = static_cast<int>(e.accumulator);
            e.accumulator -= static_cast<float>(count);
            for (int k = 0; k < count && e.live < max_live; ++k) {
                // Spread the new particles along this frame's movement so fast emitters
                // leave an even stream instead of clumps.
                double back = dt * (k + 0.5) / count;
                spawn(e, static_cast<std::uint16_t>(i), pos - vel * back, orient, vel, spark);
                particles_.back().age = static_cast<float>(back);
            }
        }
    }
    time_ += dt;
}

void EffectInstance::sprites(std::vector<Sprite>& out) const {
    for (auto& p : particles_) {
        const AleNode* a = emitters_[p.emitter].appearance;
        if (!a) continue;
        float t = std::clamp(p.age / p.life, 0.f, 1.f);
        Sprite s;
        s.pos = p.pos;
        s.size = anim(a, ale::kSize, spark_, t, anim(a, ale::kRectWidth, spark_, t, 1.f));
        s.alpha = anim(a, ale::kAlpha, spark_, t, 1.f);
        if (auto* c = a->get<AleAnimColor>(ale::kColor)) s.color = c->eval(spark_, t);
        s.rotation = p.spin + anim(a, ale::kRotate, spark_, t, 0.f) * kDeg;
        s.texture = a->get<std::string>(ale::kTexture);
        if (auto* b = a->get<AleBlend>(ale::kBlend)) s.additive = b->dst == 2;  // ...ONE: additive
        if (s.size <= 0 || s.alpha <= 0.002f) continue;
        out.push_back(s);
    }
}

bool EffectInstance::finished() const {
    if (!particles_.empty()) return false;
    for (auto& e : emitters_)
        if (time_ <= e.node->number(ale::kLifespan, 1e30f)) return false;
    return true;
}

}  // namespace sterrenvaarder::game
