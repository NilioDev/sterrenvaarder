// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/sphere.hpp"

namespace sterrenvaarder {

SphereModel SphereModel::load(const UtfFile& f) {
    SphereModel s;
    auto sp = f.root().child("Sphere");
    if (!sp) throw FormatError("not a sphere model");
    if (auto r = sp->child("Radius")) s.radius = r->as<float>();
    for (int i = 0; i < 7; ++i) {
        auto m = sp->child("M" + std::to_string(i));
        if (!m) break;
        s.materials.push_back(m->as_string());
    }
    return s;
}

const std::string* SphereModel::face_material(int face) const {
    if (materials.empty()) return nullptr;
    if (face < static_cast<int>(materials.size()) && face < 6) return &materials[face];
    return &materials[0];
}

}  // namespace sterrenvaarder
