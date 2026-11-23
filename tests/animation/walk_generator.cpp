#include "HumanoidLocomotionFixture.h"
#include "engine/render/WalkCycleGenerator.h"
#include "engine/render/BvhWalkRetarget.h"
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
                bones.upperArm[0], bones.upperArm[1],
                bones.foreArm[0], bones.foreArm[1], bones.neck, bones.head, bones.upperChest};
    WalkCycleSettings settings;
    settings.rootSpeed = 1.2f;
    AnimClip clip = generateWalkCycle(model, rig, settings);
    AIVERSE_TEST_CHECK(clip.channels.size() == 16);
    AIVERSE_TEST_CHECK(clip.duration == 1.0f);
    for (const auto& channel : clip.channels) {
        AIVERSE_TEST_CHECK(channel.times.size() == 33);
        AIVERSE_TEST_CHECK(channel.times.front() == 0.0f);
        AIVERSE_TEST_CHECK(channel.times.back() == clip.duration);
        const size_t width = channel.comps;
        for (size_t k = 0; k < width; ++k) {
            const float difference = channel.values[channel.values.size() - width + k] - channel.values[k];
            const float expected = (channel.node == rig.root && channel.path == AnimChannel::Translation && k == 2) ? 1.2f : 0.0f;
            AIVERSE_TEST_CHECK(std::abs(difference - expected) < 1e-5f);
        }
    }
    Pose pose;
    bindPose(model, pose);
    sampleClip(clip, 0.25f, pose);
    std::vector<mat4> stanceA, stanceB;
    worldMatrices(model, pose, stanceB);
    bindPose(model, pose);
    sampleClip(clip, 0.0625f, pose);
    worldMatrices(model, pose, stanceA);
    const float3 footA = stanceA[size_t(bones.foot[0])].transformPoint({});
    const float3 footB = stanceB[size_t(bones.foot[0])].transformPoint({});
    AIVERSE_TEST_CHECK(length(footA - footB) < 0.05f);
    AIVERSE_TEST_CHECK(classifyWalkArmRestPose(model, bones.upperArm[0], bones.foreArm[0]) == WalkArmRestPose::Relaxed);
    Model aPose = model;
    aPose.nodes[size_t(bones.upperArm[0])].r = quat::axisAngle({0, 0, 1}, 0.8f);
    aPose.nodes[size_t(bones.upperArm[1])].r = quat::axisAngle({0, 0, 1}, -0.8f);
    AIVERSE_TEST_CHECK(classifyWalkArmRestPose(aPose, bones.upperArm[0], bones.foreArm[0]) == WalkArmRestPose::A);
    AnimClip lowered = generateWalkCycle(aPose, rig, settings);
    Pose loweredPose;
    bindPose(aPose, loweredPose);
    sampleClip(lowered, 0.0f, loweredPose);
    std::vector<mat4> loweredWorld;
    worldMatrices(aPose, loweredPose, loweredWorld);
    const auto shoulder = loweredWorld[size_t(bones.upperArm[0])].transformPoint({});
    const auto elbow = loweredWorld[size_t(bones.foreArm[0])].transformPoint({});
    AIVERSE_TEST_CHECK(dot(normalize(elbow - shoulder), float3{0, -1, 0}) > 0.90f);
    aPose.nodes[size_t(bones.upperArm[0])].r = quat::axisAngle({0, 0, 1}, 1.57f);
    AIVERSE_TEST_CHECK(classifyWalkArmRestPose(aPose, bones.upperArm[0], bones.foreArm[0]) == WalkArmRestPose::T);
    lowered = generateWalkCycle(aPose, rig, settings);
    bindPose(aPose, loweredPose);
    sampleClip(lowered, 0.0f, loweredPose);
    worldMatrices(aPose, loweredPose, loweredWorld);
    AIVERSE_TEST_CHECK(dot(normalize(loweredWorld[size_t(bones.foreArm[0])].transformPoint({}) -
                                   loweredWorld[size_t(bones.upperArm[0])].transformPoint({})),
                           float3{0, -1, 0}) > 0.90f);
    AIVERSE_TEST_CHECK(clip.channels[9].node == bones.hips);
    AIVERSE_TEST_CHECK(clip.channels[11].node == rig.torso);
    AIVERSE_TEST_CHECK(clip.channels[12].node == rig.neck && clip.channels[13].node == rig.head);
    AIVERSE_TEST_CHECK(clip.channels[14].node == rig.leftElbow && clip.channels[15].node == rig.rightElbow);
    AIVERSE_TEST_CHECK(std::abs(clip.channels[9].values[3 * 8] - clip.channels[9].values[0]) > 0.005f);
    AIVERSE_TEST_CHECK(std::abs(clip.channels[13].values[4 * 8 + 1] - clip.channels[13].values[1]) > 0.0001f);
    bool rejected = false;
    try { WalkRig invalid = rig; invalid.leftKnee = invalid.rightKnee; (void)generateWalkCycle(model, invalid); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    const std::string tinyBvh = R"(HIERARCHY
ROOT Hips
{
 OFFSET 0 0 0
 CHANNELS 6 Xposition Yposition Zposition Zrotation Yrotation Xrotation
 JOINT LeftUpLeg
 {
  OFFSET 0 -1 0
  CHANNELS 3 Zrotation Yrotation Xrotation
  JOINT LeftLeg
  {
   OFFSET 0 -1 0
   CHANNELS 3 Zrotation Yrotation Xrotation
   JOINT LeftFoot
   {
    OFFSET 0 -1 0
    CHANNELS 3 Zrotation Yrotation Xrotation
    End Site
    {
     OFFSET 0 0 1
    }
   }
  }
 }
}
MOTION
Frames: 2
Frame Time: 0.5
0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
0 0 1 0 0 0 0 0 10 0 0 0 0 0 0
)";
    const BvhMotion bvh = parseBvhMotion(tinyBvh);
    AIVERSE_TEST_CHECK(bvh.frames.size() == 2 && bvh.find("LeftLeg") >= 0);
    const AnimClip captured = retargetBvhMotion(bvh, model, bones.root,
        {{"Hips", bones.hips}, {"LeftUpLeg", bones.thigh[0]},
         {"LeftLeg", bones.shin[0]}, {"LeftFoot", bones.foot[0]}});
    AIVERSE_TEST_CHECK(captured.channels.size() == 6);
    AIVERSE_TEST_CHECK(captured.duration == 0.5f);
    AIVERSE_TEST_CHECK(captured.channels[4].values.back() > captured.channels[4].values[2]);
    Pose capturePose;
    bindPose(model, capturePose);
    sampleClip(captured, 0.499f, capturePose);
    std::vector<mat4> captureWorld, captureBind;
    worldMatrices(model, capturePose, captureWorld);
    bindPose(model, capturePose);
    worldMatrices(model, capturePose, captureBind);
    AIVERSE_TEST_CHECK(std::abs(captureWorld[size_t(bones.foot[0])].transformPoint({}).y -
                                captureBind[size_t(bones.foot[0])].transformPoint({}).y) < 0.03f);
    rejected = false;
    try { (void)parseBvhMotion(tinyBvh.substr(0, tinyBvh.size() - 20)); }
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
