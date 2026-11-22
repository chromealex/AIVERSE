#include "HumanoidLocomotionFixture.h"
#include "engine/render/WalkCycleGenerator.h"
#include "../AlwaysCheck.h"
#include <cmath>
#include <fstream>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::render;

int main(int argc, char** argv) {
    motionweave_fixture::HumanoidBones bones;
    Model model = motionweave_fixture::makeHumanoidSkeleton(&bones);
    WalkRig rig{bones.root, bones.thigh[0], bones.shin[0], bones.foot[0],
                bones.thigh[1], bones.shin[1], bones.foot[1],
                bones.upperArm[0], bones.upperArm[1]};
    WalkCycleSettings settings;
    settings.rootSpeed = 1.2f;
    AnimClip clip = generateWalkCycle(model, rig, settings);
    AIVERSE_TEST_CHECK(clip.channels.size() == 9);
    AIVERSE_TEST_CHECK(clip.duration == 1.0f);
    for (const auto& channel : clip.channels) {
        AIVERSE_TEST_CHECK(channel.times.size() == 33);
        AIVERSE_TEST_CHECK(channel.times.front() == 0.0f);
        AIVERSE_TEST_CHECK(channel.times.back() == clip.duration);
        const size_t width = channel.comps;
        for (size_t k = 0; k < width; ++k) {
            const float difference = channel.values[channel.values.size() - width + k] - channel.values[k];
            const float expected = (&channel == &clip.channels.back() && k == 2) ? 1.2f : 0.0f;
            AIVERSE_TEST_CHECK(std::abs(difference - expected) < 1e-5f);
        }
    }
    Pose pose;
    bindPose(model, pose);
    sampleClip(clip, 0.25f, pose);
    AIVERSE_TEST_CHECK(std::abs(pose[size_t(bones.thigh[0])].r.x - pose[size_t(bones.thigh[1])].r.x) > 0.1f);
    bool rejected = false;
    try { WalkRig invalid = rig; invalid.leftKnee = invalid.rightKnee; (void)generateWalkCycle(model, invalid); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    if (argc > 1) {
        std::ofstream out(argv[1]);
        AIVERSE_TEST_CHECK(bool(out));
        out << "frame,time,segment,state,hostX,hostZ,hostYaw";
        for (const auto& node : model.nodes)
            out << ',' << node.name << "X," << node.name << "Y," << node.name << "Z";
        out << '\n';
        for (int frame = 0; frame <= 120; ++frame) {
            const float t = float(frame) / 60.0f;
            bindPose(model, pose);
            sampleClip(clip, std::fmod(t, clip.duration), pose);
            std::vector<mat4> world;
            worldMatrices(model, pose, world);
            out << frame << ',' << t << ",generated,walk,0,0,0";
            const float cycleTravel = std::floor(t / clip.duration) * settings.rootSpeed * clip.duration;
            for (const auto& joint : world)
                out << ',' << joint.at(0, 3) << ',' << joint.at(1, 3) << ',' << joint.at(2, 3) + cycleTravel;
            out << '\n';
        }
    }
    return 0;
}
