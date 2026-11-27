// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/ale.hpp"

#include <algorithm>

#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/utf.hpp"

namespace sterrenvaarder {

namespace {

class Reader {
public:
    explicit Reader(Bytes b) : b_(b) {}
    template <typename T>
    T get() {
        T v = read_le<T>(b_, pos_);
        pos_ += sizeof(T);
        return v;
    }
    // u16 length (including the NUL), the bytes, padded to an even length.
    std::string str() {
        auto len = get<std::uint16_t>();
        if (pos_ + len > b_.size()) throw FormatError("ale: string past end");
        if (len == 0) return {};
        std::string s(read_cstr(b_.subspan(0, pos_ + len), pos_));
        pos_ += len + (len & 1u);
        return s;
    }
    std::size_t pos() const { return pos_; }
    bool done() const { return pos_ >= b_.size(); }

private:
    Bytes b_;
    std::size_t pos_ = 0;
};

AleKeys read_keys(Reader& r) {
    AleKeys k;
    k.mode = r.get<std::uint8_t>();
    auto n = r.get<std::uint8_t>();
    for (int i = 0; i < n; ++i) {
        float t = r.get<float>();
        float v = r.get<float>();
        k.keys.emplace_back(t, v);
    }
    return k;
}

AleColorKeys read_color_keys(Reader& r) {
    AleColorKeys k;
    k.mode = r.get<std::uint8_t>();
    auto n = r.get<std::uint8_t>();
    for (int i = 0; i < n; ++i) {
        float t = r.get<float>();
        Color3 c;
        c.r = r.get<float>();
        c.g = r.get<float>();
        c.b = r.get<float>();
        k.keys.emplace_back(t, c);
    }
    return k;
}

AleCurve read_curve(Reader& r) {
    AleCurve c;
    c.mode = r.get<std::uint8_t>();
    auto n = r.get<std::uint8_t>();
    for (int i = 0; i < n; ++i) {
        AleCurve::Entry e;
        e.param = r.get<float>();
        e.value = r.get<float>();
        e.flags = r.get<std::uint16_t>();
        auto m = r.get<std::uint16_t>();
        for (int k = 0; k < m; ++k) {
            AleCurve::Key key{};
            key.time = r.get<float>();
            key.value = r.get<float>();
            key.tan_in = r.get<float>();
            key.tan_out = r.get<float>();
            e.keys.push_back(key);
        }
        c.by_param.push_back(std::move(e));
    }
    return c;
}

AleValue read_value(Reader& r, std::uint16_t type) {
    switch (type) {
        case 0x0001: return false;
        case 0x8001: return true;
        case 0x0002: return r.get<std::int32_t>();
        case 0x0003: return r.get<float>();
        case 0x0103: return r.str();
        case 0x0104: {
            AleBlend b;
            b.src = r.get<std::uint32_t>();
            b.dst = r.get<std::uint32_t>();
            return b;
        }
        case 0x0105: {
            AleTransform t;
            r.get<std::uint16_t>();
            auto flags = r.get<std::uint16_t>();
            t.animated = (flags & 0x8000u) != 0;
            if (t.animated)
                for (auto& ch : t.channels) ch = read_curve(r);
            return t;
        }
        case 0x0200: {
            AleAnimFloat a;
            a.mode = r.get<std::uint8_t>();
            auto n = r.get<std::uint8_t>();
            for (int i = 0; i < n; ++i) {
                float p = r.get<float>();
                a.by_param.emplace_back(p, read_keys(r));
            }
            return a;
        }
        case 0x0201: {
            AleAnimColor a;
            a.mode = r.get<std::uint8_t>();
            auto n = r.get<std::uint8_t>();
            for (int i = 0; i < n; ++i) {
                float p = r.get<float>();
                a.by_param.emplace_back(p, read_color_keys(r));
            }
            return a;
        }
        case 0x0202: return read_curve(r);
        default: throw FormatError("ale: unknown property type " + std::to_string(type));
    }
}

template <typename V, typename Lerp>
V eval_keys(const std::vector<std::pair<float, V>>& keys, float t, V fallback, Lerp lerp) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().first) return keys.front().second;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].first) {
            float span = keys[i].first - keys[i - 1].first;
            float f = span > 0 ? (t - keys[i - 1].first) / span : 1.f;
            return lerp(keys[i - 1].second, keys[i].second, f);
        }
    }
    return keys.back().second;
}

float lerpf(float a, float b, float f) { return a + (b - a) * f; }
Color3 lerpc(Color3 a, Color3 b, float f) { return {lerpf(a.r, b.r, f), lerpf(a.g, b.g, f), lerpf(a.b, b.b, f)}; }

// Blends the per-parameter entries around `param`.
template <typename Entry, typename Eval, typename Lerp, typename V>
V eval_by_param(const std::vector<Entry>& entries, float param, Eval eval, Lerp lerp, V fallback) {
    if (entries.empty()) return fallback;
    auto pk = [](const Entry& e) { return e.first; };
    if (param <= pk(entries.front())) return eval(entries.front());
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (param <= pk(entries[i])) {
            float span = pk(entries[i]) - pk(entries[i - 1]);
            float f = span > 0 ? (param - pk(entries[i - 1])) / span : 1.f;
            return lerp(eval(entries[i - 1]), eval(entries[i]), f);
        }
    }
    return eval(entries.back());
}

}  // namespace

float AleKeys::eval(float t) const { return eval_keys(keys, t, 0.f, lerpf); }
Color3 AleColorKeys::eval(float t) const { return eval_keys(keys, t, Color3{1, 1, 1}, lerpc); }

float AleAnimFloat::eval(float param, float t) const {
    return eval_by_param(by_param, param, [&](const auto& e) { return e.second.eval(t); }, lerpf, 0.f);
}

Color3 AleAnimColor::eval(float param, float t) const {
    return eval_by_param(by_param, param, [&](const auto& e) { return e.second.eval(t); }, lerpc, Color3{1, 1, 1});
}

float AleCurve::Entry::eval(float time) const {
    if (keys.empty()) return value;
    // Keys with tangents; we interpolate linearly between key values for now.
    if (time <= keys.front().time) return keys.front().value;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (time <= keys[i].time) {
            float span = keys[i].time - keys[i - 1].time;
            float f = span > 0 ? (time - keys[i - 1].time) / span : 1.f;
            return lerpf(keys[i - 1].value, keys[i].value, f);
        }
    }
    return keys.back().value;
}

float AleCurve::eval(float param, float time) const {
    if (by_param.empty()) return 0;
    if (param <= by_param.front().param) return by_param.front().eval(time);
    for (std::size_t i = 1; i < by_param.size(); ++i) {
        if (param <= by_param[i].param) {
            float span = by_param[i].param - by_param[i - 1].param;
            float f = span > 0 ? (param - by_param[i - 1].param) / span : 1.f;
            return lerpf(by_param[i - 1].eval(time), by_param[i].eval(time), f);
        }
    }
    return by_param.back().eval(time);
}

float AleNode::number(std::uint32_t hash, float fallback) const {
    auto it = props.find(hash);
    if (it == props.end()) return fallback;
    if (auto* f = std::get_if<float>(&it->second)) return *f;
    if (auto* i = std::get_if<std::int32_t>(&it->second)) return static_cast<float>(*i);
    if (auto* c = std::get_if<AleCurve>(&it->second)) return c->eval(0, 0);
    if (auto* a = std::get_if<AleAnimFloat>(&it->second)) return a->eval(0, 0);
    return fallback;
}

bool AleNode::flag(std::uint32_t hash, bool fallback) const {
    auto* b = get<bool>(hash);
    return b ? *b : fallback;
}

bool AleNode::is_emitter() const { return kind.ends_with("Emitter"); }
bool AleNode::is_appearance() const { return kind.ends_with("Appearance"); }
bool AleNode::is_field() const { return kind.ends_with("Field"); }

void AleLibrary::parse_nodes(Bytes blob) {
    Reader r(blob);
    r.get<float>();  // version 1.1
    auto count = r.get<std::uint32_t>();
    for (std::uint32_t i = 0; i < count; ++i) {
        AleNode n;
        n.kind = r.str();
        for (;;) {
            auto type = r.get<std::uint16_t>();
            if (type == 0) break;
            auto hash = r.get<std::uint32_t>();
            n.props.emplace(hash, read_value(r, type));
        }
        if (auto* s = n.get<std::string>(ale::kName)) n.name = *s;
        n.crc = fl_model_crc_exact(n.name);
        by_crc_[n.crc] = nodes_.size();
        nodes_.push_back(std::move(n));
    }
}

void AleLibrary::parse_effects(Bytes blob) {
    Reader r(blob);
    float version = r.get<float>();
    auto count = r.get<std::uint32_t>();
    for (std::uint32_t i = 0; i < count; ++i) {
        AleEffect e;
        e.name = r.str();
        if (version > 1.05f)  // version 1.1 adds four floats per effect
            for (float& h : e.header) h = r.get<float>();
        auto n = r.get<std::uint32_t>();
        for (std::uint32_t k = 0; k < n; ++k) {
            AleEffect::Ref ref;
            ref.flags = r.get<std::uint32_t>();
            ref.node_crc = r.get<std::uint32_t>();
            ref.parent = r.get<std::uint32_t>();
            ref.id = r.get<std::uint32_t>();
            e.refs.push_back(ref);
        }
        auto pairs = r.get<std::uint32_t>();
        for (std::uint32_t k = 0; k < pairs; ++k) {
            auto a = r.get<std::uint32_t>();
            auto b = r.get<std::uint32_t>();
            e.pairs.emplace_back(a, b);
        }
        effects_.push_back(std::move(e));
    }
}

AleLibrary AleLibrary::parse(Bytes utf_file) {
    auto utf = UtfFile::parse(std::vector<std::uint8_t>(utf_file.begin(), utf_file.end()));
    AleLibrary lib;
    const UtfNode* nodes = utf->root().find("AlchemyNodeLibrary/AlchemyNodeLibrary");
    const UtfNode* effects = utf->root().find("ALEffectLib/ALEffectLib");
    if (!nodes || !effects) throw FormatError("ale: missing effect or node library");
    lib.parse_nodes(nodes->data);
    lib.parse_effects(effects->data);
    return lib;
}

const AleEffect* AleLibrary::effect(std::string_view name) const {
    for (auto& e : effects_)
        if (iequals(e.name, name)) return &e;
    return nullptr;
}

const AleNode* AleLibrary::node(std::uint32_t crc) const {
    auto it = by_crc_.find(crc);
    return it == by_crc_.end() ? nullptr : &nodes_[it->second];
}

}  // namespace sterrenvaarder
