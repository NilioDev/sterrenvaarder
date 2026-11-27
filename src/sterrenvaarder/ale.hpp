// SPDX-License-Identifier: MPL-2.0
#pragma once
// Alchemy effect files (.ale): a UTF file holding an effect library (which nodes make up
// each effect, and which emitter feeds which appearance) and a node library (emitters,
// fields and appearances, each a list of typed, hashed properties). The layout was worked
// out by observing the files of the player's own install; see docs/formats.md. Property
// names are our own descriptive names for the hashes, chosen from the values they hold.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "sterrenvaarder/bytes.hpp"
#include "sterrenvaarder/math.hpp"

namespace sterrenvaarder {

// A keyed float curve over [0, 1] (usually a particle's normalised age).
struct AleKeys {
    std::uint8_t mode = 0;
    std::vector<std::pair<float, float>> keys;  // (t, value), t ascending
    float eval(float t) const;
};
struct AleColorKeys {
    std::uint8_t mode = 0;
    std::vector<std::pair<float, Color3>> keys;
    Color3 eval(float t) const;
};

// Animated by the effect's "spark parameter" (e.g. engine throttle): one keyed curve per
// parameter value, blended between neighbours.
struct AleAnimFloat {
    std::uint8_t mode = 0;
    std::vector<std::pair<float, AleKeys>> by_param;
    float eval(float param, float t) const;
};
struct AleAnimColor {
    std::uint8_t mode = 0;
    std::vector<std::pair<float, AleColorKeys>> by_param;
    Color3 eval(float param, float t) const;
};

// A value per spark parameter that may change over the effect's running time, given as
// (time, value, tangent in, tangent out) keys; with no keys the base value holds.
struct AleCurve {
    struct Key { float time, value, tan_in, tan_out; };
    struct Entry {
        float param = 0, value = 0;
        std::uint16_t flags = 0;
        std::vector<Key> keys;
        float eval(float time) const;
    };
    std::uint8_t mode = 0;
    std::vector<Entry> by_param;
    float eval(float param, float time) const;
};

// Placement of a node: 3 translation, 3 rotation (degrees), 3 scale channels.
struct AleTransform {
    bool animated = false;
    AleCurve channels[9];
};

struct AleBlend { std::uint32_t src = 5, dst = 2; };  // Direct3D blend factors

using AleValue = std::variant<bool, std::int32_t, float, std::string, AleBlend, AleTransform, AleAnimFloat,
                              AleAnimColor, AleCurve>;

struct AleNode {
    std::string kind;  // FxBasicAppearance, FxSphereEmitter, ...
    std::string name;
    std::uint32_t crc = 0;  // case-sensitive model CRC of the name, as effects refer to it
    std::unordered_map<std::uint32_t, AleValue> props;

    template <typename T>
    const T* get(std::uint32_t hash) const {
        auto it = props.find(hash);
        return it == props.end() ? nullptr : std::get_if<T>(&it->second);
    }
    float number(std::uint32_t hash, float fallback) const;
    bool flag(std::uint32_t hash, bool fallback) const;
    bool is_emitter() const;
    bool is_appearance() const;
    bool is_field() const;
};

struct AleEffect {
    std::string name;
    struct Ref { std::uint32_t flags = 0, node_crc = 0, parent = 0, id = 0; };
    std::vector<Ref> refs;                                // the root has node_crc kAleRoot
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;  // emitter id -> appearance id
    float header[4] = {};
};

class AleLibrary {
public:
    static AleLibrary parse(Bytes utf_file);  // throws FormatError
    // Parse just the two blobs (used by tests).
    void parse_effects(Bytes blob);
    void parse_nodes(Bytes blob);

    const AleEffect* effect(std::string_view name) const;
    const AleNode* node(std::uint32_t crc) const;
    const std::vector<AleEffect>& effects() const { return effects_; }
    const std::vector<AleNode>& nodes() const { return nodes_; }

private:
    std::vector<AleEffect> effects_;
    std::vector<AleNode> nodes_;
    std::unordered_map<std::uint32_t, std::size_t> by_crc_;
};

// The effect library's root entry.
constexpr std::uint32_t kAleRoot = 0xEE223B51u;

// Property hashes, named after what their values do (observed, not official names).
namespace ale {
constexpr std::uint32_t kName = 0xEFA8CE01u;
constexpr std::uint32_t kLifespan = 0xF27FDE7Du;  // node lifetime in seconds (FLT_MAX = forever)
constexpr std::uint32_t kTransform = 0xE13A59A1u;
// Emitters
constexpr std::uint32_t kEmitLod = 0xFE077E05u;
constexpr std::uint32_t kInitialParticles = 0x0F9A9D52u;
constexpr std::uint32_t kFrequency = 0x023C350Cu;        // particles per second
constexpr std::uint32_t kMaxParticles = 0xF5042852u;
constexpr std::uint32_t kEmitCount = 0xE7221F95u;
constexpr std::uint32_t kParticleLife = 0x0A635880u;     // seconds
constexpr std::uint32_t kPressure = 0x0AB180C5u;         // initial speed, m/s
constexpr std::uint32_t kVelocityApproach = 0x0681233Eu;
constexpr std::uint32_t kSphereMinRadius = 0xF231FF60u;
constexpr std::uint32_t kSphereMaxRadius = 0x1874DE74u;
constexpr std::uint32_t kConeMinRadius = 0x1C73D16Bu;
constexpr std::uint32_t kConeMaxRadius = 0xF636F07Fu;    // spread, degrees
constexpr std::uint32_t kConeMinSpread = 0x100E7F55u;
constexpr std::uint32_t kConeMaxSpread = 0xFA4B5E41u;
constexpr std::uint32_t kCubeWidth = 0xFD22A11Cu;
constexpr std::uint32_t kCubeHeight = 0xEFB0F8CFu;
constexpr std::uint32_t kCubeDepth = 0x14B74EB2u;
constexpr std::uint32_t kCubeMinSpread = 0xE282006Bu;
constexpr std::uint32_t kCubeMaxSpread = 0x08C7217Fu;
// Appearances
constexpr std::uint32_t kAppLod = 0x0C53B008u;
constexpr std::uint32_t kColor = 0x048767E8u;
constexpr std::uint32_t kAlpha = 0x04FC9016u;
constexpr std::uint32_t kSize = 0xF7C2EBA9u;
constexpr std::uint32_t kAspect = 0xE0E97650u;
constexpr std::uint32_t kRotate = 0x05DB630Eu;
constexpr std::uint32_t kTexture = 0x1BA23359u;
constexpr std::uint32_t kBlend = 0x1506EB6Cu;
constexpr std::uint32_t kTexFrame = 0x015A6F47u;
constexpr std::uint32_t kFlipU = 0xF3D52EE4u;
constexpr std::uint32_t kFlipV = 0xE8DC7F5Eu;
constexpr std::uint32_t kRectWidth = 0xE48DDAE2u;
constexpr std::uint32_t kRectLength = 0xFBA203B8u;
constexpr std::uint32_t kRectScale = 0x1266735Fu;
}  // namespace ale

}  // namespace sterrenvaarder
