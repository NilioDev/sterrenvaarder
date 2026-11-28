// SPDX-License-Identifier: MPL-2.0
#pragma once
// Character parts and costumes: characters/bodyparts.ini names the mesh of every body,
// head, hand and accessory, and characters/costumes.ini puts them together.
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

struct BodyPart {
    enum class Kind { Body, Head, LeftHand, RightHand, Accessory } kind = Kind::Body;
    std::string nickname, mesh;
    std::string sex;  // from the [Skeleton] section before it: "male", "female"
    std::string hardpoint, body_hardpoint;  // accessories: where they attach
};

struct Costume {
    std::string nickname, head, body, left_hand, right_hand;
    std::vector<std::string> accessories;
};

class CharacterParts {
public:
    void add_body_parts(const IniFile& ini);
    void add_costumes(const IniFile& ini);
    const BodyPart* part(std::string_view nickname) const;
    const Costume* costume(std::string_view nickname) const;
    std::size_t part_count() const { return parts_.size(); }
    std::size_t costume_count() const { return costumes_.size(); }
    const std::vector<std::string>& animation_files() const { return anims_; }

private:
    std::unordered_map<std::string, BodyPart> parts_;    // lower-case nickname
    std::unordered_map<std::string, Costume> costumes_;  // lower-case nickname
    std::vector<std::string> anims_;
};

}  // namespace sterrenvaarder
