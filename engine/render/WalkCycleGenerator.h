#pragma once

#include "Model.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace aiverse::render {

// Named by the caller after rig inspection. The generator never guesses bone names.
struct WalkRig {
    int root = -1;
    int leftHip = -1, leftKnee = -1, leftAnkle = -1;
    int rightHip = -1, rightKnee = -1, rightAnkle = -1;
    int leftShoulder = -1, rightShoulder = -1;
};

struct WalkCycleSettings {
    float duration = 1.0f;
    float hipSwing = 0.35f;       // radians
    float kneeLift = 0.55f;       // radians during swing
    float armSwing = 0.22f;       // radians, opposite the legs
    float rootBob = 0.025f;      // model units
    float rootSpeed = 0.0f;      // model units/s along local +Z; 0 = in-place
    int samples = 33;            // includes duplicate closing pose
};

// Generates a looping AnimClip with optional forward root motion. The rig must face local +Z with
// local +X as the pitch axis; callers may retarget the resulting clip afterward.
inline AnimClip generateWalkCycle(const Model& model, const WalkRig& rig,
                                  const WalkCycleSettings& settings = {}) {
    const auto valid = [&](int bone) { return bone >= 0 && static_cast<size_t>(bone) < model.nodes.size(); };
    if (!valid(rig.root) || !valid(rig.leftHip) || !valid(rig.leftKnee) ||
        !valid(rig.leftAnkle) || !valid(rig.rightHip) || !valid(rig.rightKnee) ||
        !valid(rig.rightAnkle) || !valid(rig.leftShoulder) || !valid(rig.rightShoulder))
        throw std::invalid_argument("walk cycle: missing rig joint");
    if (rig.leftHip == rig.rightHip || rig.leftKnee == rig.rightKnee ||
        rig.leftAnkle == rig.rightAnkle || rig.leftShoulder == rig.rightShoulder ||
        model.nodes[size_t(rig.leftKnee)].parent != rig.leftHip ||
        model.nodes[size_t(rig.leftAnkle)].parent != rig.leftKnee ||
        model.nodes[size_t(rig.rightKnee)].parent != rig.rightHip ||
        model.nodes[size_t(rig.rightAnkle)].parent != rig.rightKnee)
        throw std::invalid_argument("walk cycle: invalid leg chains");
    if (!std::isfinite(settings.duration) || settings.duration <= 0 ||
        !std::isfinite(settings.hipSwing) || settings.hipSwing < 0 || settings.hipSwing > 1.2f ||
        !std::isfinite(settings.kneeLift) || settings.kneeLift < 0 || settings.kneeLift > 1.5f ||
        !std::isfinite(settings.armSwing) || settings.armSwing < 0 || settings.armSwing > 1.2f ||
        !std::isfinite(settings.rootBob) || settings.rootBob < 0 || settings.rootBob > 1.0f ||
        !std::isfinite(settings.rootSpeed) || settings.rootSpeed < 0 || settings.rootSpeed > 5.0f ||
        settings.samples < 5 || settings.samples > 257)
        throw std::invalid_argument("walk cycle: invalid settings");

    AnimClip clip;
    clip.name = "GeneratedWalk";
    clip.duration = settings.duration;
    const int bones[] = {rig.leftHip, rig.leftKnee, rig.leftAnkle,
                         rig.rightHip, rig.rightKnee, rig.rightAnkle,
                         rig.leftShoulder, rig.rightShoulder};
    clip.channels.reserve(9);
    for (int bone : bones) {
        AnimChannel channel;
        channel.node = bone;
        channel.path = AnimChannel::Rotation;
        channel.comps = 4;
        channel.times.reserve(size_t(settings.samples));
        channel.values.reserve(size_t(settings.samples) * 4);
        clip.channels.push_back(std::move(channel));
    }
    AnimChannel bob;
    bob.node = rig.root;
    bob.path = AnimChannel::Translation;
    bob.comps = 3;
    bob.times.reserve(size_t(settings.samples));
    bob.values.reserve(size_t(settings.samples) * 3);
    clip.channels.push_back(std::move(bob));
    constexpr float tau = 6.2831853071795864769f;
    for (int i = 0; i < settings.samples; ++i) {
        const float phase = tau * float(i) / float(settings.samples - 1);
        const float stride = std::sin(phase);
        const float leftLift = std::max(0.0f, -stride);
        const float rightLift = std::max(0.0f, stride);
        const float angles[] = {
            settings.hipSwing * stride, settings.kneeLift * leftLift,
            -0.5f * settings.kneeLift * leftLift - 0.35f * settings.hipSwing * stride,
            -settings.hipSwing * stride, settings.kneeLift * rightLift,
            -0.5f * settings.kneeLift * rightLift + 0.35f * settings.hipSwing * stride,
            -settings.armSwing * stride, settings.armSwing * stride
        };
        const float time = settings.duration * float(i) / float(settings.samples - 1);
        for (size_t j = 0; j < 8; ++j) {
            auto& channel = clip.channels[j];
            const quat rotation = model.nodes[size_t(bones[j])].r * quat::axisAngle({1, 0, 0}, angles[j]);
            channel.times.push_back(time);
            channel.values.insert(channel.values.end(), {rotation.x, rotation.y, rotation.z, rotation.w});
        }
        auto& channel = clip.channels[8];
        const float3 base = model.nodes[size_t(rig.root)].t;
        channel.times.push_back(time);
        channel.values.insert(channel.values.end(),
                              {base.x, base.y + settings.rootBob * (1.0f - std::cos(2.0f * phase)) * 0.5f,
                               base.z + settings.rootSpeed * time});
    }
    return clip;
}

} // namespace aiverse::render
