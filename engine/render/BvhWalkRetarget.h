#pragma once

#include "Animation.h"
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::render {

// BVH source data stays outside the engine package. The caller supplies its text
// and an explicit map from source joint names to target skeleton node IDs.
struct BvhMotion {
    struct Joint {
        std::string name;
        int parent = -1;
        float3 offset{};
        std::vector<std::string> channels;
        size_t firstChannel = 0;
    };
    std::vector<Joint> joints;
    std::vector<std::vector<float>> frames;
    float frameTime = 0;
    float duration() const { return frames.size() > 1 ? float(frames.size() - 1) * frameTime : 0; }
    int find(const std::string& name) const {
        for (size_t i = 0; i < joints.size(); ++i) if (joints[i].name == name) return int(i);
        return -1;
    }
};

inline BvhMotion parseBvhMotion(const std::string& text) {
    std::istringstream in(text);
    std::string token;
    if (!(in >> token) || token != "HIERARCHY") throw std::invalid_argument("BVH: missing hierarchy");
    BvhMotion motion;
    size_t channels = 0;
    const auto parseJoint = [&](auto&& self, int parent, const std::string& kind) -> void {
        std::string name, brace;
        if (!(in >> name >> brace) || brace != "{") throw std::invalid_argument("BVH: malformed joint");
        const int index = int(motion.joints.size());
        motion.joints.push_back({name, parent, {}, {}, 0});
        while (in >> token) {
            if (token == "}") return;
            if (token == "OFFSET") {
                auto& offset = motion.joints[size_t(index)].offset;
                if (!(in >> offset.x >> offset.y >> offset.z)) throw std::invalid_argument("BVH: malformed offset");
            } else if (token == "CHANNELS") {
                int count = 0;
                if (!(in >> count) || count < 0 || count > 6) throw std::invalid_argument("BVH: invalid channels");
                motion.joints[size_t(index)].firstChannel = channels;
                for (int i = 0; i < count; ++i) {
                    std::string channel;
                    if (!(in >> channel)) throw std::invalid_argument("BVH: missing channel");
                    motion.joints[size_t(index)].channels.push_back(channel);
                    ++channels;
                }
            } else if (token == "JOINT") self(self, index, token);
            else if (token == "End") {
                std::string site;
                if (!(in >> site >> brace) || site != "Site" || brace != "{")
                    throw std::invalid_argument("BVH: malformed end site");
                int depth = 1;
                while (depth && in >> token) { if (token == "{") ++depth; else if (token == "}") --depth; }
                if (depth) throw std::invalid_argument("BVH: unterminated end site");
            } else throw std::invalid_argument("BVH: unknown joint token " + token);
        }
        throw std::invalid_argument("BVH: unterminated joint " + kind);
    };
    if (!(in >> token) || token != "ROOT") throw std::invalid_argument("BVH: missing root");
    parseJoint(parseJoint, -1, token);
    std::string framesLabel, frameLabel, timeLabel;
    int count = 0;
    if (!(in >> token >> framesLabel >> count >> frameLabel >> timeLabel >> motion.frameTime) ||
        token != "MOTION" || framesLabel != "Frames:" || frameLabel != "Frame" || timeLabel != "Time:" ||
        count < 2 || count > 100000 || !(motion.frameTime > 0) || !std::isfinite(motion.frameTime))
        throw std::invalid_argument("BVH: malformed motion header");
    motion.frames.resize(size_t(count), std::vector<float>(channels));
    for (auto& frame : motion.frames)
        for (float& value : frame)
            if (!(in >> value) || !std::isfinite(value)) throw std::invalid_argument("BVH: truncated frame");
    return motion;
}

inline quat bvhInverse(quat q) { return {-q.x, -q.y, -q.z, q.w}; }

inline AnimClip retargetBvhMotion(const BvhMotion& motion, const Model& model,
                                  int targetRoot, const std::map<std::string, int>& boneMap) {
    if (motion.frames.size() < 2 || targetRoot < 0 || size_t(targetRoot) >= model.nodes.size())
        throw std::invalid_argument("BVH retarget: missing motion or root");
    const int sourceHip = motion.find("Hips");
    const int sourceThigh = motion.find("LeftUpLeg"), sourceKnee = motion.find("LeftLeg");
    const int sourceAnkle = motion.find("LeftFoot");
    const auto hip = boneMap.find("Hips"), thigh = boneMap.find("LeftUpLeg");
    const auto knee = boneMap.find("LeftLeg"), ankle = boneMap.find("LeftFoot");
    if (sourceHip < 0 || sourceThigh < 0 || sourceKnee < 0 || sourceAnkle < 0 ||
        hip == boneMap.end() || thigh == boneMap.end() || knee == boneMap.end() || ankle == boneMap.end())
        throw std::invalid_argument("BVH retarget: missing leg mapping");
    Pose bind;
    bindPose(model, bind);
    std::vector<mat4> bindWorld;
    worldMatrices(model, bind, bindWorld);
    const float sourceLeg = length(motion.joints[size_t(sourceKnee)].offset) +
                            length(motion.joints[size_t(sourceAnkle)].offset);
    const float targetLeg = length(bindWorld[size_t(thigh->second)].transformPoint({}) -
                                   bindWorld[size_t(knee->second)].transformPoint({})) +
                            length(bindWorld[size_t(knee->second)].transformPoint({}) -
                                   bindWorld[size_t(ankle->second)].transformPoint({}));
    if (sourceLeg < 1e-5f || targetLeg < 1e-5f) throw std::invalid_argument("BVH retarget: zero-length leg");
    const float scale = targetLeg / sourceLeg;
    const int rootParent = model.nodes[size_t(targetRoot)].parent;
    const int pelvisParent = model.nodes[size_t(hip->second)].parent;
    const mat4 rootParentWorld = rootParent < 0 ? mat4{} : bindWorld[size_t(rootParent)];
    const mat4 pelvisParentWorld = pelvisParent < 0 ? mat4{} : bindWorld[size_t(pelvisParent)];
    const float sx = length(rootParentWorld.transformDir({1, 0, 0}));
    const float sz = length(rootParentWorld.transformDir({0, 0, 1}));
    const float sy = length(pelvisParentWorld.transformDir({0, 1, 0}));
    if (sx < 1e-5f || sy < 1e-5f || sz < 1e-5f) throw std::invalid_argument("BVH retarget: invalid parent scale");
    std::map<int, int> targetToSource;
    for (const auto& [name, target] : boneMap) {
        const int source = motion.find(name);
        if (source >= 0 && target >= 0 && size_t(target) < model.nodes.size())
            targetToSource.emplace(target, source);
    }
    AnimClip clip;
    clip.name = "CMU_02_01_RetargetedWalk";
    clip.duration = motion.duration();
    for (const auto& [target, source] : targetToSource) {
        (void)source;
        AnimChannel channel;
        channel.node = target;
        channel.path = AnimChannel::Rotation;
        channel.comps = 4;
        clip.channels.push_back(std::move(channel));
    }
    AnimChannel rootMove;
    rootMove.node = targetRoot; rootMove.path = AnimChannel::Translation; rootMove.comps = 3;
    clip.channels.push_back(std::move(rootMove));
    AnimChannel pelvisMove;
    pelvisMove.node = hip->second; pelvisMove.path = AnimChannel::Translation; pelvisMove.comps = 3;
    clip.channels.push_back(std::move(pelvisMove));
    const auto sourceRootPosition = [&](const std::vector<float>& frame) {
        float3 p{};
        const auto& joint = motion.joints[size_t(sourceHip)];
        for (size_t i = 0; i < joint.channels.size(); ++i) {
            const float v = frame[joint.firstChannel + i];
            if (joint.channels[i] == "Xposition") p.x = v;
            else if (joint.channels[i] == "Yposition") p.y = v;
            else if (joint.channels[i] == "Zposition") p.z = v;
        }
        return p;
    };
    const float3 firstPosition = sourceRootPosition(motion.frames.front());
    constexpr float deg = 0.01745329251994329577f;
    for (size_t frameIndex = 0; frameIndex < motion.frames.size(); ++frameIndex) {
        const auto& frame = motion.frames[frameIndex];
        std::vector<quat> sourceWorld(motion.joints.size());
        std::vector<quat> sourceLocal(motion.joints.size());
        for (size_t j = 0; j < motion.joints.size(); ++j) {
            const auto& joint = motion.joints[j];
            quat local;
            for (size_t c = 0; c < joint.channels.size(); ++c) {
                const std::string& name = joint.channels[c];
                const float angle = frame[joint.firstChannel + c] * deg;
                if (name == "Xrotation") local = local * quat::axisAngle({1, 0, 0}, angle);
                else if (name == "Yrotation") local = local * quat::axisAngle({0, 1, 0}, angle);
                else if (name == "Zrotation") local = local * quat::axisAngle({0, 0, 1}, angle);
            }
            sourceLocal[j] = qnormalize(local);
            sourceWorld[j] = joint.parent < 0 ? sourceLocal[j] :
                qnormalize(sourceWorld[size_t(joint.parent)] * sourceLocal[j]);
        }
        for (size_t n = 0; n < model.nodes.size(); ++n) {
            quat local = model.nodes[n].r;
            const auto mapped = targetToSource.find(int(n));
            if (mapped != targetToSource.end()) {
                const int source = mapped->second;
                const int sourceParent = motion.joints[size_t(source)].parent;
                const quat sourceParentWorld = sourceParent < 0 ? quat{} :
                    sourceWorld[size_t(sourceParent)];
                const quat deltaWorld = sourceParentWorld * sourceLocal[size_t(source)] *
                                        bvhInverse(sourceParentWorld);
                const quat targetBindWorld = rotationOf(bindWorld[n]);
                local = qnormalize(model.nodes[n].r * bvhInverse(targetBindWorld) *
                                   deltaWorld * targetBindWorld);
                auto& channel = clip.channels[size_t(std::distance(targetToSource.begin(), mapped))];
                channel.times.push_back(float(frameIndex) * motion.frameTime);
                channel.values.insert(channel.values.end(), {local.x, local.y, local.z, local.w});
            }
        }
        const float3 travel = (sourceRootPosition(frame) - firstPosition) * scale;
        const float3 rootBase = model.nodes[size_t(targetRoot)].t;
        auto& root = clip.channels[clip.channels.size() - 2];
        root.times.push_back(float(frameIndex) * motion.frameTime);
        root.values.insert(root.values.end(), {rootBase.x + travel.x / sx, rootBase.y, rootBase.z + travel.z / sz});
        const float3 hipBase = model.nodes[size_t(hip->second)].t;
        auto& pelvis = clip.channels.back();
        pelvis.times.push_back(float(frameIndex) * motion.frameTime);
        pelvis.values.insert(pelvis.values.end(), {hipBase.x, hipBase.y + travel.y / sy, hipBase.z});
    }
    const auto rightAnkle = boneMap.find("RightFoot");
    const float ground = rightAnkle == boneMap.end() ?
        bindWorld[size_t(ankle->second)].transformPoint({}).y :
        std::min(bindWorld[size_t(ankle->second)].transformPoint({}).y,
                 bindWorld[size_t(rightAnkle->second)].transformPoint({}).y);
    auto& pelvis = clip.channels.back();
    for (size_t i = 0; i < motion.frames.size(); ++i) {
        Pose pose;
        bindPose(model, pose);
        const float time = std::min(float(i) * motion.frameTime,
                                    clip.duration - motion.frameTime * 0.001f);
        sampleClip(clip, time, pose);
        std::vector<mat4> world;
        worldMatrices(model, pose, world);
        float footY = world[size_t(ankle->second)].transformPoint({}).y;
        int stanceAnkle = ankle->second;
        if (rightAnkle != boneMap.end()) {
            const float rightY = world[size_t(rightAnkle->second)].transformPoint({}).y;
            if (rightY < footY) { footY = rightY; stanceAnkle = rightAnkle->second; }
        }
        pelvis.values[i * 3 + 1] += (ground - footY) / sy;
        const int stanceParent = model.nodes[size_t(stanceAnkle)].parent;
        if (stanceParent >= 0) {
            const quat parentWorld = rotationOf(world[size_t(stanceParent)]);
            const quat targetWorld = rotationOf(bindWorld[size_t(stanceAnkle)]);
            const quat local = qnormalize(bvhInverse(parentWorld) * targetWorld);
            for (auto& channel : clip.channels)
                if (channel.node == stanceAnkle && channel.path == AnimChannel::Rotation) {
                    const size_t offset = i * 4;
                    channel.values[offset] = local.x;
                    channel.values[offset + 1] = local.y;
                    channel.values[offset + 2] = local.z;
                    channel.values[offset + 3] = local.w;
                    break;
                }
        }
    }
    return clip;
}

} // namespace aiverse::render
