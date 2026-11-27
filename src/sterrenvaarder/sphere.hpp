// SPDX-License-Identifier: MPL-2.0
#pragma once
// Sphere models (.sph): planets and suns. The file only stores a radius and up to seven
// material names (six cube faces + atmosphere); the mesh is generated.
#include <memory>
#include <string>
#include <vector>

#include "sterrenvaarder/utf.hpp"

namespace sterrenvaarder {

struct SphereModel {
    float radius = 0;
    std::vector<std::string> materials;  // M0..M5 faces (+X,-X? see sphere.cpp), M6 atmosphere

    static bool is_sphere(const UtfFile& f) { return f.root().child("Sphere") != nullptr; }
    static SphereModel load(const UtfFile& f);
    const std::string* face_material(int face) const;
    const std::string* atmosphere_material() const { return materials.size() >= 7 ? &materials[6] : nullptr; }
};

}  // namespace sterrenvaarder
