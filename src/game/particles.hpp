// SPDX-License-Identifier: MPL-2.0
#pragma once
// Particle effects from the original Alchemy (.ale) data: emitters spawn particles at their
// frequency with their speed and lifetime, shaped by the emitter kind (sphere, cone, cube);
// each particle is drawn by the appearance its emitter feeds, with size, colour and alpha
// over its life. The spark parameter (engine throttle, for instance) blends the values.
#include <cstdint>
#include <string>
#include <vector>

#include "game/quat.hpp"
#include "sterrenvaarder/effects.hpp"

namespace sterrenvaarder::game {

struct Sprite {
    DVec3 pos;
    float size = 1;      // half-size in metres
    float rotation = 0;  // radians, around the view axis
    Color3 color{1, 1, 1};
    float alpha = 1;
    const std::string* texture = nullptr;
    bool additive = true;
    float frame = 0;  // 0..1 through an animated texture's frames
};

class EffectInstance {
public:
    EffectInstance() = default;
    EffectInstance(ResolvedEffect fx, std::uint32_t seed);

    // Advances the effect placed at pos/orient, moving with vel. emitting = false lets the
    // live particles fade out without new ones.
    void update(double dt, DVec3 pos, const Quat& orient, DVec3 vel, float spark, bool emitting = true);
    void sprites(std::vector<Sprite>& out) const;
    // True once nothing is left to draw and no emitter will spawn again.
    bool finished() const;
    std::size_t particle_count() const { return particles_.size(); }
    const ResolvedEffect& effect() const { return fx_; }

private:
    struct Emitter {
        const AleNode* node = nullptr;
        const AleNode* appearance = nullptr;
        float accumulator = 0;
        bool started = false;
        std::size_t live = 0;
    };
    struct Particle {
        DVec3 pos;
        Vec3 vel;
        float age = 0, life = 1;
        float spin = 0;
        std::uint16_t emitter = 0;
    };

    ResolvedEffect fx_;
    std::vector<Emitter> emitters_;
    std::vector<Particle> particles_;
    double time_ = 0;
    float spark_ = 0;
    std::uint32_t rng_ = 1;

    float rand01();
    void spawn(Emitter& e, std::uint16_t index, DVec3 pos, const Quat& orient, DVec3 vel, float spark);
};

}  // namespace sterrenvaarder::game
