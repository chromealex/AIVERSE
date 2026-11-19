// Humanoid avatar metadata, stable semantic names and conservative suggestions.
// This is an authoring helper; ambiguous matches MUST be reviewed by the user.
#pragma once
#include "HumanoidRetarget.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
namespace aiverse::lib::animation {
constexpr std::array<const char*,kHumanoidBoneCount> kHumanoidRoleNames={
    "Root","Hips","Spine","Chest","UpperChest","Neck","Head",
    "LeftShoulder","LeftUpperArm","LeftLowerArm","LeftHand",
    "RightShoulder","RightUpperArm","RightLowerArm","RightHand",
    "LeftUpperLeg","LeftLowerLeg","LeftFoot","LeftToes",
    "RightUpperLeg","RightLowerLeg","RightFoot","RightToes"};
inline std::string roleName(HumanoidBone role) {
    if(size_t(role)>=kHumanoidBoneCount)
        throw std::invalid_argument("invalid humanoid role");
    return kHumanoidRoleNames[size_t(role)];
}
inline HumanoidBone parseRole(std::string_view name) {
    for(size_t i=0;i<kHumanoidBoneCount;++i)
        if(name==kHumanoidRoleNames[i]) return HumanoidBone(i);
    throw std::invalid_argument("unknown humanoid role "+std::string(name));
}
inline std::string normalizedBoneName(const std::string& name) {
    std::string result;
    for(unsigned char c:name)if(std::isalnum(c))result+=char(std::tolower(c));
    return result;
}
inline std::map<HumanoidBone,std::string> suggestHumanoidRoles(const render::Model& model) {
    // Only exact normalized names or rig-prefix suffixes; no fuzzy guessing
    // of left/right. If multiple candidates compete, omit the role entirely.
    const std::array<std::vector<std::string>,kHumanoidBoneCount> aliases={{
        {"root","armature"},{"hips","pelvis","hip","ccbasehip"},
        {"spine","spine1"},{"chest","spine2","spine01"},
        {"upperchest","spine3"},{"neck","neck1"},{"head"},
        {"leftshoulder","lshoulder","shoulderl"},
        {"leftupperarm","leftarm","lupperarm","upperarml"},
        {"leftlowerarm","leftforearm","lforearm","lowerarml"},
        {"lefthand","lhand","handl"},
        {"rightshoulder","rshoulder","shoulderr"},
        {"rightupperarm","rightarm","rupperarm","upperarmr"},
        {"rightlowerarm","rightforearm","rforearm","lowerarmr"},
        {"righthand","rhand","handr"},
        {"leftupleg","leftupperleg","leftthigh","lthigh","thighl"},
        {"leftleg","leftlowerleg","leftshin","lshin","shinl"},
        {"leftfoot","lfoot","footl"},{"lefttoes","lefttoe","ltoe"},
        {"rightupleg","rightupperleg","rightthigh","rthigh","thighr"},
        {"rightleg","rightlowerleg","rightshin","rshin","shinr"},
        {"rightfoot","rfoot","footr"},{"righttoes","righttoe","rtoe"}
    }};
    std::map<HumanoidBone,std::string> roles;
    std::set<int> used;
    for(size_t role=0;role<kHumanoidBoneCount;++role) {
        int chosen=-1;
        for(const auto& alias:aliases[role]) {
            std::vector<int> found;
            for(size_t n=0;n<model.nodes.size();++n) {
                const auto key=normalizedBoneName(model.nodes[n].name);
                if(key==alias || (key.size()>alias.size() && key.ends_with(alias)))
                    found.push_back(int(n));
            }
            if(found.size()==1 && !used.count(found[0])) {
                chosen=found[0];break;
            }
        }
        if(chosen>=0) {
            roles[HumanoidBone(role)]=bonePath(model,chosen);
            used.insert(chosen);
        }
    }
    return roles;
}
inline Json humanoidProfileJson(const std::string& modelPath,
                    const std::map<HumanoidBone,std::string>& roles) {
    Json doc=Json::object();
    doc.set("format",Json::string("aiverse-humanoid-v1"));
    doc.set("model",Json::string(modelPath));
    Json bones=Json::object();
    for(const auto& [role,path]:roles)
        bones.set(roleName(role),Json::string(path));
    doc.set("bones",bones);return doc;
}
inline std::map<HumanoidBone,std::string> readHumanoidProfile(const Json& doc) {
    if(!doc.isObject() || doc.getStr("format")!="aiverse-humanoid-v1" ||
       !doc["bones"].isObject() || doc["bones"].size()>kHumanoidBoneCount)
        throw std::invalid_argument("invalid humanoid profile asset");
    std::map<HumanoidBone,std::string> roles;
    for(const auto& [name,val]:doc["bones"].members()) {
        if(!val.isString() || val.str().empty())
            throw std::invalid_argument("invalid humanoid bone path");
        const auto role=parseRole(name);
        roles.emplace(role,val.str());
    }
    return roles;
}
} // namespace aiverse::lib::animation
