#pragma once

// Presentation adapter for modular human packages with duplicated skin rigs.
// Each complete rig receives the shared MotionWeave walk generator output;
// partial head/groom roots receive the same root travel only.
#include "HumanPreviewAnimation.h"
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
                                             HumanWalkPreviewStats* report=nullptr) {
    using namespace render;
    AnimClip combined;
    combined.name = "vivid_generated_walk";
    combined.duration = settings.duration;
    HumanWalkPreviewStats stats;
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
        const std::array<const char*, 12> semantic = {
            "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
            "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r",
            "neck_01", "head"
        };
        std::array<int, 12> joints;
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
                              joints[8], joints[9], joints[10], joints[11]};
            countArm(classifyWalkArmRestPose(model, rig.leftShoulder, rig.leftElbow));
            countArm(classifyWalkArmRestPose(model, rig.rightShoulder, rig.rightElbow));
            stats.headChannels += int(rig.neck >= 0) + int(rig.head >= 0);
            AnimClip part = generateWalkCycle(model, rig, settings);
            combined.channels.insert(combined.channels.end(),
                                     std::make_move_iterator(part.channels.begin()),
                                     std::make_move_iterator(part.channels.end()));
            ++stats.completeRigs;
        } else {
            AnimChannel root;
            root.node = int(r);
            root.path = AnimChannel::Translation;
            root.comps = 3;
            for (int i = 0; i < settings.samples; ++i) {
                const float time = settings.duration * float(i) / float(settings.samples - 1);
                const auto base = model.nodes[r].t;
                root.times.push_back(time);
                root.values.insert(root.values.end(),
                                   {base.x, base.y, base.z + settings.rootSpeed * time});
            }
            combined.channels.push_back(std::move(root));
            appendWalkHeadMotion(model, joints[10], joints[11], settings, combined);
            stats.headChannels += int(joints[10] >= 0) + int(joints[11] >= 0);
        }
    }
    if (stats.completeRigs == 0) throw std::invalid_argument("walk preview: no complete rig");
    if(report)*report=stats;
    return combined;
}

} // namespace aiverse::vivid
