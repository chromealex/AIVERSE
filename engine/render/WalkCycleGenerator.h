#pragma once

#include "Animation.h"
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
    int torso = -1;
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
    float armSwing = 0.30f;       // radians, opposite the legs
    float rootBob = 0.018f;      // fraction of leg length; applied at pelvis
    float rootSpeed = 0.0f;      // model units/s along local +Z; 0 = in-place
    int samples = 33;            // includes duplicate closing pose
};

inline void appendWalkHeadMotion(const Model& model, int torso, int neck, int head,
                                 const WalkCycleSettings& settings, AnimClip& clip) {
    constexpr float tau = 6.2831853071795864769f;
    for (int bone : {torso, neck, head}) {
        if (bone < 0 || size_t(bone) >= model.nodes.size()) continue;
        AnimChannel channel;
        channel.node = bone;
        channel.path = AnimChannel::Rotation;
        channel.comps = 4;
        const float3 yawAxis = walkLocalAxis(model, bone, {0, 1, 0});
        const float3 pitchAxis = walkLocalAxis(model, bone, {1, 0, 0});
        const float3 rollAxis = walkLocalAxis(model, bone, {0, 0, 1});
        for (int i = 0; i < settings.samples; ++i) {
            const float phase = tau * float(i) / float(settings.samples - 1);
            const float torsoYaw = 0.055f * std::sin(phase);
            const float delayedYaw = 0.018f * (std::sin(phase - 0.35f) - std::sin(-0.35f));
            const float pitch = 0.012f * (std::cos(2.0f * phase - 0.6f) - std::cos(-0.6f));
            const float yaw = bone == torso ? torsoYaw :
                bone == neck ? -torsoYaw * 0.45f + delayedYaw * 0.6f :
                -torsoYaw * 0.35f + delayedYaw * 0.4f;
            const quat rotation = model.nodes[size_t(bone)].r *
                quat::axisAngle(yawAxis, yaw) *
                quat::axisAngle(pitchAxis, bone == head ? pitch : bone == torso ? -pitch * 0.6f : 0.0f) *
                quat::axisAngle(rollAxis, bone == torso ? 0.014f * std::sin(phase) : 0.0f);
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
        !std::isfinite(settings.rootBob) || settings.rootBob < 0 || settings.rootBob > 0.1f ||
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
    const int pelvis = model.nodes[size_t(rig.leftHip)].parent;
    if (pelvis < 0 || model.nodes[size_t(rig.rightHip)].parent != pelvis)
        throw std::invalid_argument("walk cycle: legs need a shared pelvis");
    const float leftLegLength = length(walkBindPoint(model, rig.leftHip) - walkBindPoint(model, rig.leftKnee)) +
                                length(walkBindPoint(model, rig.leftKnee) - walkBindPoint(model, rig.leftAnkle));
    const float rightLegLength = length(walkBindPoint(model, rig.rightHip) - walkBindPoint(model, rig.rightKnee)) +
                                 length(walkBindPoint(model, rig.rightKnee) - walkBindPoint(model, rig.rightAnkle));
    const float legScale = std::min(leftLegLength, rightLegLength);
    const int pelvisParent = model.nodes[size_t(pelvis)].parent;
    mat4 pelvisParentWorld;
    if (pelvisParent >= 0) {
        Pose bind;
        bindPose(model, bind);
        std::vector<mat4> world;
        worldMatrices(model, bind, world);
        pelvisParentWorld = world[size_t(pelvisParent)];
    }
    const float xScale = length(pelvisParentWorld.transformDir({1, 0, 0}));
    const float yScale = length(pelvisParentWorld.transformDir({0, 1, 0}));
    if (legScale < 1e-4f || xScale < 1e-5f || yScale < 1e-5f)
        throw std::invalid_argument("walk cycle: invalid leg or pelvis scale");
    clip.channels.reserve(16);
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
    AnimChannel pelvisTranslation;
    pelvisTranslation.node = pelvis;
    pelvisTranslation.path = AnimChannel::Translation;
    pelvisTranslation.comps = 3;
    clip.channels.push_back(std::move(pelvisTranslation));
    AnimChannel pelvisRotation;
    pelvisRotation.node = pelvis;
    pelvisRotation.path = AnimChannel::Rotation;
    pelvisRotation.comps = 4;
    clip.channels.push_back(std::move(pelvisRotation));
    const float3 pelvisYawAxis = walkLocalAxis(model, pelvis, {0, 1, 0});
    const float3 pelvisRollAxis = walkLocalAxis(model, pelvis, {0, 0, 1});
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
            -settings.armSwing * std::sin(phase - 0.18f),
             settings.armSwing * std::sin(phase - 0.18f)
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
                              {base.x, base.y, base.z + settings.rootSpeed * time});
        auto& hipMove = clip.channels[9];
        const float3 hipBase = model.nodes[size_t(pelvis)].t;
        hipMove.times.push_back(time);
        hipMove.values.insert(hipMove.values.end(),
                              {hipBase.x - legScale * 0.024f * std::sin(phase) / xScale,
                               hipBase.y + legScale * settings.rootBob *
                                   (1.0f - std::cos(2.0f * phase)) * 0.5f / yScale,
                               hipBase.z});
        auto& hipTurn = clip.channels[10];
        const quat hipRotation = model.nodes[size_t(pelvis)].r *
            quat::axisAngle(pelvisYawAxis, 0.045f * std::sin(phase)) *
            quat::axisAngle(pelvisRollAxis, -0.025f * std::sin(phase));
        hipTurn.times.push_back(time);
        hipTurn.values.insert(hipTurn.values.end(),
                              {hipRotation.x, hipRotation.y, hipRotation.z, hipRotation.w});
    }
    appendWalkHeadMotion(model, rig.torso, rig.neck, rig.head, settings, clip);
    for (int side = 0; side < 2; ++side) {
        const int elbow = side == 0 ? rig.leftElbow : rig.rightElbow;
        if (!valid(elbow)) continue;
        AnimChannel channel;
        channel.node = elbow;
        channel.path = AnimChannel::Rotation;
        channel.comps = 4;
        const float3 axis = walkLocalAxis(model, elbow, {1, 0, 0});
        for (int i = 0; i < settings.samples; ++i) {
            const float phase = tau * float(i) / float(settings.samples - 1) + float(side) * 3.14159265358979323846f;
            const float bend = 0.10f * (1.0f - std::cos(phase));
            const quat q = model.nodes[size_t(elbow)].r * quat::axisAngle(axis, bend);
            channel.times.push_back(settings.duration * float(i) / float(settings.samples - 1));
            channel.values.insert(channel.values.end(), {q.x, q.y, q.z, q.w});
        }
        clip.channels.push_back(std::move(channel));
    }

    // Bake stance targets through the shared two-bone solver. During stance,
    // the foot moves backward relative to the advancing root by the same
    // distance, so its model-world target remains fixed on the ground.
    Pose bind;
    bindPose(model, bind);
    std::vector<mat4> bindWorld;
    worldMatrices(model, bind, bindWorld);
    Pose end = bind;
    end[size_t(rig.root)].t.z += settings.rootSpeed * settings.duration;
    std::vector<mat4> endWorld;
    worldMatrices(model, end, endWorld);
    const float3 rootStart = bindWorld[size_t(rig.root)].transformPoint({});
    const float3 rootTravel = endWorld[size_t(rig.root)].transformPoint({}) - rootStart;
    const float3 rootForward = bindWorld[size_t(rig.root)].transformDir({0, 0, 1});
    const float3 forward = normalize(length(rootTravel) > 1e-5f ? rootTravel : rootForward);
    const int hips[2] = {rig.leftHip, rig.rightHip};
    const int knees[2] = {rig.leftKnee, rig.rightKnee};
    const int ankles[2] = {rig.leftAnkle, rig.rightAnkle};
    float legLength[2];
    for (int side = 0; side < 2; ++side) {
        legLength[side] = length(bindWorld[size_t(hips[side])].transformPoint({}) -
                                 bindWorld[size_t(knees[side])].transformPoint({})) +
                          length(bindWorld[size_t(knees[side])].transformPoint({}) -
                                 bindWorld[size_t(ankles[side])].transformPoint({}));
        if (legLength[side] < 1e-4f) throw std::invalid_argument("walk cycle: zero-length leg");
    }
    constexpr float duty = 0.6f;
    constexpr float pi = 3.14159265358979323846f;
    const float stride = length(rootTravel) > 1e-5f ? length(rootTravel) * duty :
                         0.4f * std::min(legLength[0], legLength[1]);
    for (int i = 0; i < settings.samples; ++i) {
        const float time = settings.duration * float(i) / float(settings.samples - 1);
        Pose pose;
        bindPose(model, pose);
        sampleClip(clip, time, pose);
        std::vector<mat4> world;
        worldMatrices(model, pose, world);
        const float3 rootOffset = world[size_t(rig.root)].transformPoint({}) - rootStart;
        float3 targets[2];
        bool stance[2];
        for (int side = 0; side < 2; ++side) {
            float phase = std::fmod(float(i) / float(settings.samples - 1) + float(side) * 0.5f, 1.0f);
            stance[side] = phase < duty;
            float offset, lift = 0;
            if (stance[side]) offset = stride * (0.5f - phase / duty);
            else {
                const float swing = (phase - duty) / (1.0f - duty);
                offset = stride * (-0.5f + 0.5f - 0.5f * std::cos(pi * swing));
                lift = legLength[side] * 0.075f * std::pow(std::sin(pi * swing), 1.25f);
            }
            targets[side] = bindWorld[size_t(ankles[side])].transformPoint({}) +
                            rootOffset + forward * offset + float3{0, lift, 0};
        }
        float lowerPelvis = 0;
        for (int side = 0; side < 2; ++side) if (stance[side]) {
            const float3 hip = world[size_t(hips[side])].transformPoint({});
            const float3 flat{targets[side].x - hip.x, 0, targets[side].z - hip.z};
            const float reach = std::max(0.0f, legLength[side] - 0.01f);
            const float reachableHeight = std::sqrt(std::max(0.0f, reach * reach - dot(flat, flat)));
            lowerPelvis = std::max(lowerPelvis, hip.y - targets[side].y - reachableHeight);
        }
        const int pelvisParent = model.nodes[size_t(pelvis)].parent;
        const float upScale = pelvisParent >= 0 ?
            world[size_t(pelvisParent)].transformDir({0, 1, 0}).y : 1.0f;
        if (lowerPelvis > 0 && upScale > 1e-5f) {
            pose[size_t(pelvis)].t.y -= lowerPelvis / upScale;
            worldMatrices(model, pose, world);
        }
        for (int side = 0; side < 2; ++side) {
            const float3 pole = targets[side] + forward * legLength[side] + float3{0, legLength[side] * 0.2f, 0};
            solveTwoBoneIK(model, pose, world, hips[side], knees[side], ankles[side],
                           targets[side], pole, 1.0f);
            const quat kneeWorld = rotationOf(world[size_t(knees[side])]);
            const quat inverse{-kneeWorld.x, -kneeWorld.y, -kneeWorld.z, kneeWorld.w};
            pose[size_t(ankles[side])].r = inverse * rotationOf(bindWorld[size_t(ankles[side])]);
            worldMatrices(model, pose, world);
        }
        for (int j = 0; j < 6; ++j) {
            const quat q = pose[size_t(bones[j])].r;
            auto& values = clip.channels[size_t(j)].values;
            values[size_t(i) * 4 + 0] = q.x;
            values[size_t(i) * 4 + 1] = q.y;
            values[size_t(i) * 4 + 2] = q.z;
            values[size_t(i) * 4 + 3] = q.w;
        }
        auto& pelvisValues = clip.channels[9].values;
        const auto t = pose[size_t(pelvis)].t;
        pelvisValues[size_t(i) * 3 + 0] = t.x;
        pelvisValues[size_t(i) * 3 + 1] = t.y;
        pelvisValues[size_t(i) * 3 + 2] = t.z;
    }
    return clip;
}

} // namespace aiverse::render
