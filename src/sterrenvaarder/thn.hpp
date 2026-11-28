// SPDX-License-Identifier: MPL-2.0
#pragma once
// Scene scripts (.thn) and base rooms: entities with their placement, cameras and lights,
// the PetalDB name -> model table, and the room layout of each base.
#include <optional>
#include <array>
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
    // SOUND (template_name is the sound's nickname)
    float attenuation = 0;           // dB
    float dmin = 0, dmax = 0, pan = 0;  // distances in which it fades; pan -1..1

    Vec3 forward() const { return {-orient[6], -orient[7], -orient[8]}; }
    Vec3 up() const { return {orient[3], orient[4], orient[5]}; }
};

// A START_MOTION event: an animation script played on a character entity.
struct ThnMotion {
    float time = 0;            // when it starts, in the scene
    std::string target;        // the entity it moves
    std::string animation;     // the .anm script name (with its "Sc_" prefix)
    float start_time = 0, duration = 0, time_scale = 1;
    bool loop = false;          // event_flags LOOP
};

// A START_SPATIAL_PROP_ANIM event: an entity moves and/or turns to a new place over time.
struct ThnMove {
    float time = 0, duration = 0;
    std::string target;
    std::string to;  // the entity whose place it takes, when the event names one
    bool has_pos = false, has_orient = false;
    Vec3 pos;
    std::array<float, 4> q{1, 0, 0, 0};  // q_orient as stored: w, x, y, z
};

// A MOTION_PATH entity's path: a spline through points (with a rotation at each), in the
// path entity's own frame. path_data reads "OPEN|CLOSED, {x,y,z}, {w,x,y,z}, {x,y,z}, ...".
struct ThnPath {
    bool closed = false;
    std::vector<Vec3> points;
    std::vector<std::array<float, 4>> orients;  // w, x, y, z; may be empty
    static ThnPath parse(std::string_view data);
    // The point at 0..1 along the path (Catmull-Rom through the points).
    Vec3 at(float f) const;
};

// START_PATH_ANIMATION: an entity follows a path from start to stop percent over time.
struct ThnPathAnim {
    float time = 0, duration = 0;
    std::string target, path;
    float start = 0, stop = 1;  // fractions of the path
    Vec3 offset;
    // flags: POSITION moves it along the path; ORIENTATION turns it with the path's own
    // orientations; LOOK_AT faces it along the way it travels. No flags: position.
    bool position = true, orientation = false, look_along = false;
};

// ATTACH_ENTITY: from `time` on, `child` follows `parent` (flags POSITION, ORIENTATION;
// ENTITY_RELATIVE keeps the child's own offset in the parent's frame, ORIENTATION_RELATIVE
// its own turn on top of the parent's; LOOK_AT turns it to face the parent). Hardpoint
// and part targets are taken as the parent's root for now.
struct ThnAttach {
    float time = 0, duration = 0;
    std::string child, parent;
    Vec3 offset;
    bool position = false, orientation = false, relative = false, orientation_relative = false, look_at = false;
};

// START_SOUND: a SOUND entity plays from `time` on.
struct ThnSound {
    float time = 0, duration = 0, start_time = 0;
    std::string target;  // the SOUND entity
    bool loop = false;    // flags LOOP
};

// SET_CAMERA: from `time` on, the scene is seen through `camera`.
struct ThnCameraCut {
    float time = 0;
    std::string camera;
};

// START_CAMERA_PROP_ANIM: a camera's field of view changes over time.
struct ThnCameraProp {
    float time = 0, duration = 0;
    std::string target;
    float fovh = 0;  // 0: unchanged
};

struct ThnScript {
    double duration = 0;
    std::vector<ThnEntity> entities;
    LuaValue events;  // raw: {time, EVENT_TYPE, targets..., params}
    std::vector<ThnMotion> motions;  // the START_MOTION events, in order
    std::vector<ThnMove> moves;
    std::vector<ThnCameraCut> cuts;
    std::vector<ThnCameraProp> camera_props;
    std::vector<ThnPathAnim> path_anims;
    std::vector<ThnAttach> attachments;
    std::vector<ThnSound> sounds;
    std::unordered_map<std::string, ThnPath> paths;  // lower-case MOTION_PATH entity name
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
    std::vector<std::string> ship_placements;  // [ForSaleShipPlacement] markers (ship dealer)
    std::vector<RoomHotspot> hotspots;
    std::string music, ambient;  // [Room_Sound] sound nicknames (music may be empty: keep what plays)
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
