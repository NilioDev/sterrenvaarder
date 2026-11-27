// SPDX-License-Identifier: MPL-2.0
#pragma once
// Scene scripts (.thn) and base rooms: entities with their placement, cameras and lights,
// the PetalDB name -> model table, and the room layout of each base.
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/lua32.hpp"
#include "sterrenvaarder/math.hpp"
#include "sterrenvaarder/universe.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

struct ThnEntity {
    std::string name;
    std::string type;           // SCENE, COMPOUND, CAMERA, LIGHT, MARKER, DEFORMABLE, ...
    std::string template_name;  // PetalDB name for COMPOUND/PROP
    std::string flags;          // e.g. "HIDDEN", "LIT_DYNAMIC+LIT_AMBIENT"
    std::string category;       // userprops.category ("Room", "Character", ...)
    int light_group = 0, sort_group = 0;
    Vec3 pos;
    Mat3 orient = kIdentity3;  // rows: right, up, back (the entity looks down -Z)
    // CAMERA
    float fovh = 0, hvaspect = 0, near_plane = 0, far_plane = 0;
    // LIGHT
    bool light_on = true;
    std::string light_type;  // L_POINT, L_DIRECT, L_SPOT
    Color3 diffuse{1, 1, 1}, light_ambient;
    float range = 0;
    Vec3 atten{1, 0, 0}, direction{0, 0, 1};
    // SCENE
    Color3 ambient;  // 0..1

    Vec3 forward() const { return {-orient[6], -orient[7], -orient[8]}; }
    Vec3 up() const { return {orient[3], orient[4], orient[5]}; }
};

struct ThnScript {
    double duration = 0;
    std::vector<ThnEntity> entities;
    LuaValue events;  // raw: {time, EVENT_TYPE, targets..., params}
    const ThnEntity* entity(std::string_view name) const;
    static ThnScript parse(Bytes data);  // throws FormatError
};

// PetalDB.ini: room / prop / cart names -> model files.
class PetalDb {
public:
    explicit PetalDb(const Vfs& vfs, std::string_view path = "petaldb.ini");
    const std::string* model(std::string_view name) const;
    std::size_t size() const { return models_.size(); }

private:
    std::unordered_map<std::string, std::string> models_;
};

struct RoomHotspot {
    std::string name;      // IDS_HOTSPOT_BAR, ...
    std::string behavior;  // ExitDoor, StartDealer, StartShipDealer, StartEquipDealer, FrontDesk, ...
    std::string room_switch;
};

struct RoomInfo {
    std::string nickname;
    std::string set_script;            // hardpoint script: the set, cameras, markers
    std::vector<std::string> scenes;   // ambient scripts (lights, props, traffic)
    std::string camera;                // camera entity to view the room through
    std::vector<RoomHotspot> hotspots;
};

struct BaseRooms {
    std::string start_room;
    std::vector<RoomInfo> rooms;
    const RoomInfo* room(std::string_view nickname) const;
    // Reads the base file named in universe.ini ([Base] file) and its room files.
    static std::optional<BaseRooms> load(const Vfs& vfs, std::string_view base_file);
};

// The [Base] file path for a base nickname from universe.ini, or empty.
std::string base_file_of(const Vfs& vfs, std::string_view base);

}  // namespace sterrenvaarder
