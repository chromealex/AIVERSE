// Integration test: consume real MotionWeave presentation ECS pose, keep its
// cached body pose immutable while VIVID composes fresh morph values.
#include "engine/character/vivid/MotionWeaveBridge.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

// Headless native consumer: the rendering runtime normally supplies this hook.
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string&) { return false; }
}

using namespace aiverse;
using namespace aiverse::vivid;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a,float b) { return std::fabs(a-b)<1e-5f; }
render::Model modelFixture() {
    render::Model model;
    model.nodes.resize(1);
    model.nodes[0].name = "head";
    model.nodes[0].mesh = 0;
    model.nodes[0].weights[0] = 0.2f;
    model.nodes[0].weights[2] = 0.13f;
    model.meshes.resize(1);
    model.meshes[0].primitives.resize(1);
    model.meshes[0].primitives[0].morphTargets = 3;
    model.order = {0};
    return model;
}
void verify() {
    auto model = modelFixture();
    auto graph=render::AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"}],
        "output":"bind"
    })"));
    World presentation;
    const Entity entity = presentation.create();
    auto& asset = presentation.add<motionweave::AnimationAssetComponent>(entity);
    asset.model = &model;
    asset.graph = std::move(graph);
    presentation.add<motionweave::AnimationPlayerComponent>(entity);
    presentation.add<motionweave::AnimationPoseComponent>(entity);

    motionweave::MotionWeave::update(presentation, 1.0f/60.0f);
    auto& motion = presentation.get<motionweave::AnimationPoseComponent>(entity);
    require(motion.valid && motion.local.size()==1 && motion.matrices.size()==1,
            "MotionWeave failed to evaluate the canonical pose");

    const std::array<MorphRoute, 2> routes{{
        {Channel::JawOpen, 0, 0, 1.0f},
        {Channel::EyeBlinkLeft, 0, 1, 1.0f}
    }};
    const auto binding = MorphBinding::compile(model, routes);
    FaceController face(0.0f);
    require(face.set(Layer::Tracking, Channel::JawOpen, 0.6f), "jaw input");
    require(face.set(Layer::Tracking, Channel::EyeBlinkLeft, 0.8f), "blink input");
    face.snap();

    render::Pose renderPose, scratch;
    require(composeFaceAfterMotionWeave(motion,face.frame(),binding,renderPose,scratch),
            "first facial composition failed");
    require(near(renderPose[0].w[0],0.6f) && near(renderPose[0].w[1],0.8f),
            "face morphs were not applied");
    require(near(renderPose[0].w[2],0.13f), "unmapped morph changed");
    require(near(motion.local[0].w[0],0.2f) && near(motion.local[0].w[1],0.0f),
            "MotionWeave cached source was mutated");

    require(face.set(Layer::Tracking, Channel::JawOpen,0.4f), "second jaw input");
    face.snap();
    for (int frame=0;frame<3;++frame) {
        require(composeFaceAfterMotionWeave(motion,face.frame(),binding,renderPose,scratch,
                                           1.0f,MorphApply::Additive),
                "additive composition failed");
        require(near(renderPose[0].w[0],0.6f),
                "additive face accumulated into MotionWeave cached pose");
        require(near(motion.local[0].w[0],0.2f),"MotionWeave source drift");
    }

    // Output is transactional: invalid input must not discard prior render pose.
    FaceFrame invalid=face.frame();
    invalid.values[static_cast<std::size_t>(Channel::JawOpen)] =
        std::numeric_limits<float>::quiet_NaN();
    const float last=renderPose[0].w[0];
    require(!composeFaceAfterMotionWeave(motion,invalid,binding,renderPose,scratch),
            "invalid facial frame accepted");
    require(near(renderPose[0].w[0],last),"invalid face altered output");
    require(!composeFaceAfterMotionWeave(motion,face.frame(),binding,renderPose,scratch,-1),
            "invalid weight accepted");
    require(!composeFaceAfterMotionWeave(motion,face.frame(),binding,renderPose,renderPose),
            "aliased output/scratch accepted");
    require(!composeFaceAfterMotionWeave(motion,face.frame(),binding,motion.local,scratch),
            "MotionWeave source alias accepted");
    require(near(renderPose[0].w[0],last),"invalid arguments altered output");
    motion.valid=false;
    require(!composeFaceAfterMotionWeave(motion,face.frame(),binding,renderPose,scratch),
            "invalid MotionWeave pose accepted");
    require(near(renderPose[0].w[0],last),"invalid MotionWeave frame altered output");
}
} // namespace
int main() {
    try {
        verify();
        std::cout << "VIVID MotionWeave bridge PASS (pose reuse, isolation, no additive drift)\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "VIVID MotionWeave bridge FAIL: " << e.what() << '\n';
        return 1;
    }
}
