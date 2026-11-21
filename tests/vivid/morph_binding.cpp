#include "engine/character/vivid/MorphBinding.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

namespace {
void check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a-b) < eps; }
render::Model sample() {
    render::Model model;
    model.nodes.resize(1);
    model.nodes[0].name = "face";
    model.nodes[0].mesh = 0;
    model.meshes.resize(1);
    model.meshes[0].primitives.resize(1);
    model.meshes[0].primitives[0].morphTargets = 3;
    return model;
}
void reject(const render::Model& model, const std::vector<MorphRoute>& routes) {
    try { (void)MorphBinding::compile(model, routes); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid morph binding was accepted");
}
void checkCompile() {
    const auto model = sample();
    reject(model, {{Channel::JawOpen, 1, 0, 1}});
    reject(model, {{Channel::JawOpen, 0, 8, 1}});
    reject(model, {{Channel::JawOpen, 0, 3, 1}});
    reject(model, {{Channel::Count, 0, 0, 1}});
    reject(model, {{Channel::JawOpen, 0, 0, -1}});
    reject(model, {{Channel::JawOpen, 0, 0, std::numeric_limits<float>::quiet_NaN()}});
    reject(model, {{Channel::JawOpen, 0, 0, 1}, {Channel::EyeBlinkLeft, 0, 0, 1}});
    render::Model noMesh = sample();
    noMesh.nodes[0].mesh = -1;
    reject(noMesh, {{Channel::JawOpen, 0, 0, 1}});
    check(MorphBinding::compile(model, {}).routes() == 0, "empty route table failed");
}
void checkApply() {
    const auto model = sample();
    const std::vector<MorphRoute> routes{{Channel::JawOpen, 0, 1, 0.8f},
                                         {Channel::EyeBlinkLeft, 0, 2, 1.0f}};
    const MorphBinding binding = MorphBinding::compile(model, routes);
    check(binding.routes() == 2 && binding.nodes() == 1, "compiled route metadata");
    render::Pose pose;
    render::bindPose(model, pose);
    check(pose.size() == 1, "bind pose setup failed");
    pose[0].w[0] = 0.72f;
    pose[0].w[1] = 0.2f;
    FaceController face(0);
    check(face.set(Layer::Tracking, Channel::JawOpen, 0.75f), "jaw input");
    check(face.set(Layer::Tracking, Channel::EyeBlinkLeft, 1.0f), "blink input");
    face.snap();
    check(binding.apply(face.frame(), pose, 0.5f), "half influence failed");
    check(near(pose[0].w[0], 0.72f), "unmapped animation channel changed");
    check(near(pose[0].w[1], 0.4f), "jaw override blend incorrect");
    check(near(pose[0].w[2], 0.5f), "blink override blend incorrect");
    check(binding.apply(face.frame(), pose, 1.0f, MorphApply::Additive), "additive failed");
    check(near(pose[0].w[1], 1.0f), "additive saturation incorrect");
    check(near(pose[0].w[2], 1.0f), "additive blink saturation incorrect");
    render::Pose invalidPose;
    check(!binding.apply(face.frame(), invalidPose), "mismatched pose accepted");
    const auto before = pose[0].w[1];
    check(!binding.apply(face.frame(), pose, -0.1f), "negative influence accepted");
    check(!binding.apply(face.frame(), pose, std::numeric_limits<float>::infinity()), "Inf influence accepted");
    check(near(pose[0].w[1], before), "failed validation modified pose");
    FaceFrame corrupt = face.frame();
    corrupt.values[static_cast<std::size_t>(Channel::EyeBlinkLeft)] =
        std::numeric_limits<float>::quiet_NaN();
    check(!binding.apply(corrupt, pose), "nonfinite semantic channel accepted");
    check(near(pose[0].w[1], before), "partially wrote pose on invalid channel");
}
void checkFullPalette() {
    auto model=sample();
    model.meshes[0].primitives[0].morphTargets=render::kMaxMorphTargets;
    std::vector<MorphRoute> routes;
    FaceFrame frame{};
    for(std::size_t i=0;i<kChannelCount;++i) {
        routes.push_back({static_cast<Channel>(i),0,int(i),1.f});
        frame.values[i]=float(i+1)/float(kChannelCount);
    }
    render::Pose pose;
    render::bindPose(model,pose);
    const auto binding=MorphBinding::compile(model,routes);
    check(binding.apply(frame,pose),"52 semantic channels rejected");
    for(std::size_t i=0;i<kChannelCount;++i)
        check(near(pose[0].w[i],frame.values[i]),"upper semantic channel truncated");
    routes={{Channel::JawOpen,0,render::kMaxMorphTargets-1,1.f}};
    check(MorphBinding::compile(model,routes).apply(frame,pose),"last Model slot rejected");
    check(near(pose[0].w[render::kMaxMorphTargets-1],frame.value(Channel::JawOpen)),
          "last Model slot not written");
    reject(model,{{Channel::JawOpen,0,render::kMaxMorphTargets,1.f}});
}
} // namespace

int main() {
    try {
        checkCompile();
        checkApply();
        checkFullPalette();
        std::cout << "VIVID morph binding PASS (compile validation, pose blending, atomic failure)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "VIVID morph binding FAIL: " << ex.what() << '\n';
        return 1;
    }
}
