#pragma once

// Presentation adapter for modular human packages with duplicated skin rigs.
// Each complete rig receives the shared MotionWeave walk generator output;
// partial head/groom roots receive matching pose tracks and root travel.
#include "HumanPreviewAnimation.h"
#include "engine/render/BvhWalkRetarget.h"
#include "engine/render/WalkCycleGenerator.h"
#include <array>
#include <iterator>

namespace aiverse::vivid {

struct HumanWalkPreviewStats {
    int completeRigs=0;
    int relaxedArms=0, aPoseArms=0, tPoseArms=0, unknownArms=0;
    int headChannels=0;
};

inline render::AnimClip humanWalkPreviewClip(const render::Model& model,
                                             const render::WalkCycleSettings& settings,
                                             HumanWalkPreviewStats* report=nullptr,
                                             const render::BvhMotion* motion=nullptr) {
    using namespace render;
    AnimClip combined;
    combined.name = "vivid_generated_walk";
    combined.duration = motion ? motion->duration() : settings.duration;
    HumanWalkPreviewStats stats;
    AnimClip referenceRig;
    std::vector<int> partialRoots;
    const auto countArm = [&](WalkArmRestPose pose) {
        switch(pose){
            case WalkArmRestPose::Relaxed: ++stats.relaxedArms; break;
            case WalkArmRestPose::A: ++stats.aPoseArms; break;
            case WalkArmRestPose::T: ++stats.tPoseArms; break;
            case WalkArmRestPose::Unknown: ++stats.unknownArms; break;
        }
    };
    const auto belongs = [&](int node, int root) {
        for (int cursor = node; cursor >= 0; cursor = model.nodes[size_t(cursor)].parent)
            if (cursor == root) return true;
        return false;
    };
    for (size_t r = 0; r < model.nodes.size(); ++r) {
        const auto rootName = humanBoneSemantic(model.nodes[r].name);
        if (rootName != "root" && rootName.rfind("root.", 0) != 0) continue;
        const std::array<const char*, 13> semantic = {
            "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
            "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r",
            "neck_01", "head", "spine_03"
        };
        std::array<int, 13> joints;
        joints.fill(-1);
        for (size_t n = 0; n < model.nodes.size(); ++n) {
            if (!belongs(int(n), int(r))) continue;
            const auto name = humanBoneSemantic(model.nodes[n].name);
            for (size_t k = 0; k < semantic.size(); ++k)
                if (name == semantic[k]) joints[k] = int(n);
        }
        const bool ready = std::all_of(joints.begin(), joints.begin() + 10,
                                       [](int id) { return id >= 0; });
        if (ready) {
            const WalkRig rig{int(r), joints[0], joints[1], joints[2],
                              joints[3], joints[4], joints[5], joints[6], joints[7],
                              joints[8], joints[9], joints[10], joints[11], joints[12]};
            countArm(classifyWalkArmRestPose(model, rig.leftShoulder, rig.leftElbow));
            countArm(classifyWalkArmRestPose(model, rig.rightShoulder, rig.rightElbow));
            stats.headChannels += int(rig.neck >= 0) + int(rig.head >= 0);
            AnimClip part;
            if (motion) {
                const auto findSemantic = [&](const char* semanticName) {
                    for (size_t n = 0; n < model.nodes.size(); ++n)
                        if (belongs(int(n), int(r)) && humanBoneSemantic(model.nodes[n].name) == semanticName)
                            return int(n);
                    return -1;
                };
                std::map<std::string, int> mapping;
                for (const auto& [source, target] : {
                         std::pair{"Hips", "pelvis"}, {"LeftUpLeg", "thigh_l"},
                         {"LeftLeg", "calf_l"}, {"LeftFoot", "foot_l"},
                         {"RightUpLeg", "thigh_r"}, {"RightLeg", "calf_r"},
                         {"RightFoot", "foot_r"}, {"LowerBack", "spine_01"},
                         {"Spine", "spine_02"}, {"Spine1", "spine_03"},
                         {"Neck", "neck_01"}, {"Head", "head"}}) {
                    const int targetNode = findSemantic(target);
                    if (targetNode >= 0) mapping.emplace(source, targetNode);
                }
                part = retargetBvhMotion(*motion, model, int(r), mapping);
                const AnimClip arms = generateWalkCycle(model, rig, settings);
                for (const AnimChannel& source : arms.channels) {
                    if (source.path != AnimChannel::Rotation ||
                        (source.node != rig.leftShoulder && source.node != rig.rightShoulder &&
                         source.node != rig.leftElbow && source.node != rig.rightElbow)) continue;
                    AnimChannel channel;
                    channel.node = source.node;
                    channel.path = AnimChannel::Rotation;
                    channel.comps = 4;
                    AnimClip one;
                    one.duration = arms.duration;
                    one.channels.push_back(source);
                    for (size_t i = 0; i < motion->frames.size(); ++i) {
                        Pose pose;
                        bindPose(model, pose);
                        const float time = float(i) * motion->frameTime;
                        sampleClip(one, std::fmod(time + 0.25f, arms.duration), pose);
                        const quat q = pose[size_t(source.node)].r;
                        channel.times.push_back(time);
                        channel.values.insert(channel.values.end(), {q.x, q.y, q.z, q.w});
                    }
                    part.channels.push_back(std::move(channel));
                }
            } else part = generateWalkCycle(model, rig, settings);
            if (referenceRig.channels.empty()) referenceRig = part;
            combined.channels.insert(combined.channels.end(),
                                     std::make_move_iterator(part.channels.begin()),
                                     std::make_move_iterator(part.channels.end()));
            ++stats.completeRigs;
        } else {
            partialRoots.push_back(int(r));
        }
    }
    for (int partialRoot : partialRoots) {
            AnimChannel root;
            for (const auto& source : referenceRig.channels)
                if (source.path == AnimChannel::Translation &&
                    humanBoneSemantic(model.nodes[size_t(source.node)].name) == "root") {
                    root = source;
                    const float3 sourceBase = model.nodes[size_t(source.node)].t;
                    const float3 base = model.nodes[size_t(partialRoot)].t;
                    for (size_t k = 0; k < root.times.size(); ++k) {
                        root.values[k * 3] += base.x - sourceBase.x;
                        root.values[k * 3 + 1] += base.y - sourceBase.y;
                        root.values[k * 3 + 2] += base.z - sourceBase.z;
                    }
                    break;
                }
            if (root.times.empty()) throw std::invalid_argument("walk preview: complete rig has no root motion");
            root.node = partialRoot;
            combined.channels.push_back(std::move(root));
            for (const auto& source : referenceRig.channels) {
                const std::string semantic = humanBoneSemantic(model.nodes[size_t(source.node)].name);
                if (semantic == "root") continue;
                for (size_t n = 0; n < model.nodes.size(); ++n) {
                    if (!belongs(int(n), partialRoot) ||
                        humanBoneSemantic(model.nodes[n].name) != semantic) continue;
                    AnimChannel channel = source;
                    channel.node = int(n);
                    combined.channels.push_back(std::move(channel));
                    if (semantic == "neck_01" || semantic == "head") ++stats.headChannels;
                    break;
                }
            }
    }
    if (stats.completeRigs == 0) throw std::invalid_argument("walk preview: no complete rig");
    if(report)*report=stats;
    return combined;
}

} // namespace aiverse::vivid
