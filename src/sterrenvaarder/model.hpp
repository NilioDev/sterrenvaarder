// SPDX-License-Identifier: MPL-2.0
#pragma once
// Rigid models: compound (.cmp) and single-part (.3db), plus their materials/textures.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/math.hpp"
#include "sterrenvaarder/texture.hpp"
#include "sterrenvaarder/utf.hpp"
#include "sterrenvaarder/vmesh.hpp"

namespace sterrenvaarder {

struct Material {
    std::string name;
    std::string type;  // e.g. "DcDt", "DcDtOcOt", "DcDtEc", "Nebula"
    Vec3 diffuse{1, 1, 1};
    Vec3 emissive{0, 0, 0};
    float opacity = 1.0f;
    std::string diffuse_texture, emissive_texture;
    std::string detail_texture;  // Dm_name / Dm0_name (planets)
    float detail_tile = 1.0f;    // TileRate / TileRate0
    Vec3 ambient{1, 1, 1};       // Ac
    float alpha = 1.0f;          // Alpha (atmospheres)
    float scale = 1.0f;          // Scale (atmosphere radius factor)
    bool two_sided = false;
    bool alpha_blend = false;
};

// Materials and textures gathered from any number of UTF files (the model itself,
// plus .mat/.txm libraries named in the game INIs). Later additions win, like mods.
class MaterialLibrary {
public:
    void add_from(std::shared_ptr<UtfFile> file);
    const Material* material(std::uint32_t crc) const;
    const Material* material_by_name(std::string_view name) const;
    const UtfNode* texture(std::string_view name) const;

private:
    std::vector<std::shared_ptr<UtfFile>> files_;
    std::unordered_map<std::uint32_t, Material> materials_;
    std::unordered_map<std::string, const UtfNode*> textures_;
};

enum class JointType { Fixed, Revolute, Prismatic, Sphere, Loose };

struct Joint {
    JointType type = JointType::Fixed;
    std::string parent, child;
    Vec3 origin;
    Mat3 rotation = kIdentity3;
    Vec3 axis{0, 0, 1};
    float min = 0, max = 0;
};

struct Lod {
    VMeshRef ref;
    float range_end = 0;  // switch distance; 0 = unlimited
};

struct ModelPart {
    std::string name;  // object name ("Root", "Li_Engine01", ...)
    std::vector<Lod> lods;
    std::vector<std::string> hardpoints;
};

class MeshLibrary {
public:
    void add_from(std::shared_ptr<UtfFile> file);
    const VMeshData* mesh(std::uint32_t id) const;

private:
    std::vector<std::shared_ptr<UtfFile>> files_;
    std::unordered_map<std::uint32_t, VMeshData> meshes_;
};

struct Model {
    std::vector<ModelPart> parts;  // parts[0] is the root
    std::vector<Joint> joints;
    std::shared_ptr<UtfFile> file;

    static Model load(std::shared_ptr<UtfFile> file);
    const ModelPart* part(std::string_view name) const;
};

}  // namespace sterrenvaarder
