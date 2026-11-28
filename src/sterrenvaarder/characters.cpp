// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/characters.hpp"

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {
namespace {

std::string first(const IniEntry& e) { return e.values.empty() ? std::string() : e.values[0].as_string(); }

}  // namespace

void CharacterParts::add_body_parts(const IniFile& ini) {
    std::string sex;
    for (auto& sec : ini) {
        if (iequals(sec.name, "Skeleton")) {
            if (auto* e = sec.find("sex")) sex = first(*e);
            continue;
        }
        if (iequals(sec.name, "Animations")) {
            for (auto* e : sec.find_all("anim")) anims_.push_back(first(*e));
            continue;
        }
        BodyPart p;
        if (iequals(sec.name, "Body")) p.kind = BodyPart::Kind::Body;
        else if (iequals(sec.name, "Head")) p.kind = BodyPart::Kind::Head;
        else if (iequals(sec.name, "LeftHand")) p.kind = BodyPart::Kind::LeftHand;
        else if (iequals(sec.name, "RightHand")) p.kind = BodyPart::Kind::RightHand;
        else if (iequals(sec.name, "Accessory")) p.kind = BodyPart::Kind::Accessory;
        else continue;
        p.sex = sex;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "nickname")) p.nickname = first(e);
            else if (iequals(e.key, "mesh")) p.mesh = first(e);
            else if (iequals(e.key, "hardpoint")) p.hardpoint = first(e);
            else if (iequals(e.key, "body_hardpoint")) p.body_hardpoint = first(e);
        }
        if (!p.nickname.empty()) parts_[to_lower(p.nickname)] = std::move(p);
    }
}

void CharacterParts::add_costumes(const IniFile& ini) {
    for (auto& sec : ini) {
        if (!iequals(sec.name, "Costume")) continue;
        Costume c;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "nickname")) c.nickname = first(e);
            else if (iequals(e.key, "head")) c.head = first(e);
            else if (iequals(e.key, "body")) c.body = first(e);
            else if (iequals(e.key, "lefthand")) c.left_hand = first(e);
            else if (iequals(e.key, "righthand")) c.right_hand = first(e);
            else if (iequals(e.key, "accessory")) c.accessories.push_back(first(e));
        }
        if (!c.nickname.empty()) costumes_[to_lower(c.nickname)] = std::move(c);
    }
}

const BodyPart* CharacterParts::part(std::string_view n) const {
    auto it = parts_.find(to_lower(n));
    return it == parts_.end() ? nullptr : &it->second;
}

const Costume* CharacterParts::costume(std::string_view n) const {
    auto it = costumes_.find(to_lower(n));
    return it == costumes_.end() ? nullptr : &it->second;
}

}  // namespace sterrenvaarder
