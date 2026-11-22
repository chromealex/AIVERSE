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
    int leftElbow = -1, rightElbow = -1;
    int neck = -1, head = -1;
};

enum class WalkArmRestPose { Unknown, Relaxed, A, T };

inline quat walkBindRotation(const Model& model, int bone) {
    quat world;
    std::vector<int> chain;
    for (int node = bone; node >= 0; node = model.nodes[size_t(node)].parent) {
        if (chain.size() >= model.nodes.size())
            throw std::invalid_argument("walk cycle: cyclic rig hierarchy");
        chain.push_back(node);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        world = world * model.nodes[size_t(*it)].r;
    return world;
}

inline float3 walkLocalAxis(const Model& model, int bone, float3 worldAxis) {
    const quat world = walkBindRotation(model, bone);
    return quat{-world.x, -world.y, -world.z, world.w}.rotate(worldAxis);
}

inline float3 walkBindPoint(const Model& model, int bone) {
    mat4 world;
    std::vector<int> chain;
    for (int node = bone; node >= 0; node = model.nodes[size_t(node)].parent) {
        if (chain.size() >= model.nodes.size())
            throw std::invalid_argument("walk cycle: cyclic rig hierarchy");
        chain.push_back(node);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto& n = model.nodes[size_t(*it)];
        world = world * mat4::trs(n.t, n.r, n.s);
    }
    return world.transformPoint({});
}

// A/T/relaxed recognition uses the bind shoulder-to-elbow direction in model
// space, so it does not depend on authored bone names or local joint axes.
inline WalkArmRestPose classifyWalkArmRestPose(const Model& model, int shoulder, int elbow) {
    if (shoulder < 0 || elbow < 0 || size_t(shoulder) >= model.nodes.size() ||
        size_t(elbow) >= model.nodes.size()) return WalkArmRestPose::Unknown;
    const float3 direction = walkBindPoint(model, elbow) - walkBindPoint(model, shoulder);
    if (length(direction) < 1e-6f) return WalkArmRestPose::Unknown;
    const float down = dot(normalize(direction), float3{0, -1, 0});
    if (down >= 0.8660254f) return WalkArmRestPose::Relaxed; // within 30 degrees of down
    if (down >= 0.3420201f) return WalkArmRestPose::A;       // 30–70 degrees
    return WalkArmRestPose::T;                               // near horizontal or higher
}

inline quat walkArmRestCorrection(const Model& model, int shoulder, int elbow) {
    const auto pose = classifyWalkArmRestPose(model, shoulder, elbow);
    if (pose == WalkArmRestPose::Unknown || pose == WalkArmRestPose::Relaxed) return quat{};
    const float3 from = normalize(walkBindPoint(model, elbow) - walkBindPoint(model, shoulder));
    const float sign = from.x >= 0 ? 1.0f : -1.0f;
    const float3 to = normalize(float3{0.18f * sign, -1.0f, 0});
    const float3 crossAxis = cross(from, to);
    if (length(crossAxis) < 1e-6f) return quat{};
    const float angle = std::acos(std::clamp(dot(from, to), -1.0f, 1.0f));
    return quat::axisAngle(walkLocalAxis(model, shoulder, normalize(crossAxis)), angle);
}

struct WalkCycleSettings {
    float duration = 1.0f;
    float hipSwing = 0.35f;       // radians
    float kneeLift = 0.55f;       // radians during swing
    float armSwing = 0.22f;       // radians, opposite the legs
    float rootBob = 0.025f;      // model units
    float rootSpeed = 0.0f;      // model units/s along local +Z; 0 = in-place
    int samples = 33;            // includes duplicate closing pose
};

inline void appendWalkHeadMotion(const Model& model, int neck, int head,
                                 const WalkCycleSettings& settings, AnimClip& clip) {
    constexpr float tau = 6.2831853071795864769f;
    for (int bone : {neck, head}) {
        if (bone < 0 || size_t(bone) >= model.nodes.size()) continue;
        AnimChannel channel;
        channel.node = bone;
        channel.path = AnimChannel::Rotation;
        channel.comps = 4;
        const float3 yawAxis = walkLocalAxis(model, bone, {0, 1, 0});
        const float3 pitchAxis = walkLocalAxis(model, bone, {1, 0, 0});
        for (int i = 0; i < settings.samples; ++i) {
            const float phase = tau * float(i) / float(settings.samples - 1);
            const float yaw = 0.10f * (std::sin(phase - 0.4f) - std::sin(-0.4f));
            const float pitch = 0.05f * (std::cos(2.0f * phase - 0.6f) - std::cos(-0.6f));
            const quat rotation = model.nodes[size_t(bone)].r *
                quat::axisAngle(yawAxis, bone == neck ? yaw * 0.6f : yaw * 0.4f) *
                quat::axisAngle(pitchAxis, bone == head ? pitch : -pitch * 0.3f);
            channel.times.push_back(settings.duration * float(i) / float(settings.samples - 1));
            channel.values.insert(channel.values.end(), {rotation.x, rotation.y, rotation.z, rotation.w});
        }
        clip.channels.push_back(std::move(channel));
    }
}

// Generates a looping AnimClip with optional forward root motion. The rig must
// face model +Z; each joint's local pitch axis is derived from its bind rotation.
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
        !std::isfinite(settings.rootBob) || settings.rootBob < 0 || settings.rootBob > 100.0f ||
        !std::isfinite(settings.rootSpeed) || settings.rootSpeed < 0 || settings.rootSpeed > 1000.0f ||
        settings.samples < 5 || settings.samples > 257)
        throw std::invalid_argument("walk cycle: invalid settings");

    AnimClip clip;
    clip.name = "GeneratedWalk";
    clip.duration = settings.duration;
    const int bones[] = {rig.leftHip, rig.leftKnee, rig.leftAnkle,
                         rig.rightHip, rig.rightKnee, rig.rightAnkle,
                         rig.leftShoulder, rig.rightShoulder};
    float3 pitchAxes[8];
    for (size_t j = 0; j < 8; ++j) {
        pitchAxes[j] = walkLocalAxis(model, bones[j], {1, 0, 0});
    }
    const quat armRest[2] = {walkArmRestCorrection(model, rig.leftShoulder, rig.leftElbow),
                             walkArmRestCorrection(model, rig.rightShoulder, rig.rightElbow)};
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
            const quat rest = j >= 6 ? armRest[j - 6] : quat{};
            const quat rotation = model.nodes[size_t(bones[j])].r * rest *
                                  quat::axisAngle(pitchAxes[j], angles[j]);
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
    appendWalkHeadMotion(model, rig.neck, rig.head, settings, clip);
    return clip;
}

} // namespace aiverse::render
