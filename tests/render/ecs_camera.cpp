// CPU-only AIVERSE LENS camera ECS contract, no GPU or genre modules.
#include "engine/render/EcsCameraRender.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace aiverse;

static void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
int main() {
    World world;
    auto main = world.create();
    auto capture = world.create();
    auto missingTransform = world.create();
    auto hidden = world.create();
    LocalTransform localMain{};
    localMain.position = {2_fx, 3_fx, 4_fx};
    world.add<WorldTransform>(main, hierarchy_detail::root(localMain));
    world.add<CameraComponent>(main, CameraComponent{});
    LocalTransform localCapture{};
    localCapture.position = {30_fx, 4_fx, 2_fx};
    world.add<WorldTransform>(capture, hierarchy_detail::root(localCapture));
    CameraComponent orthographic{};
    orthographic.projection = CameraProjection::Orthographic;
    orthographic.orthographicHalfHeight = 12_fx;
    orthographic.priority = 7;
    world.add<CameraComponent>(capture, orthographic);
    world.add<CameraOutputComponent>(capture,
        CameraOutputComponent{CameraOutputKind::Texture, 12345, 512, 256});
    world.add<CameraUpdateComponent>(capture,
        CameraUpdateComponent{CameraUpdateMode::EveryNFrames, 3, 30});
    world.add<CameraComponent>(missingTransform, CameraComponent{});
    auto inactive = CameraComponent{};
    inactive.enabled = false;
    world.add<CameraComponent>(hidden, inactive);
    world.add<WorldTransform>(hidden, hierarchy_detail::root({}));

    render::EcsCameraView first, second;
    size_t found=0;
    const World& source=world;
    const size_t visits = render::visitEcsCameras(source, [&](const auto& view) {
        if (view.entity==main) first=view;
        if (view.entity==capture) second=view;
        ++found;
    }, {1_fx, 0_fx, 0_fx});
    require(visits==2 && found==2, "only ready, enabled ECS camera entities");
    require(first.entity==main && second.entity==capture, "camera entity identities");
    require(first.camera.usePose && std::fabs(first.camera.eye().x-1.f)<0.0001f,
            "world-to-render-origin camera pose");
    require(std::fabs(first.camera.eye().y-3.f)<0.0001f,
            "camera y position");
    require(!first.camera.ortho && second.camera.ortho &&
            second.camera.orthoHalfHeight()==12.f, "projection modes");
    require(second.output.kind==CameraOutputKind::Texture &&
            second.output.targetAssetKey==12345 &&
            second.output.width==512 && second.output.height==256, "RT asset key");
    require(second.update.mode==CameraUpdateMode::EveryNFrames &&
            second.update.everyNFrames==3 && second.priority==7, "camera policy");
    auto vp = first.camera.view();
    require(std::fabs(vp.at(0,3)+1.f)<0.0001f &&
            std::fabs(vp.at(1,3)+3.f)<0.0001f, "world camera view");
    // Rotating a camera entity must support roll: orbit-only cameras could not.
    auto rolled=first.camera;
    rolled.setPose({0,0,0},render::quat::axisAngle({0,0,1},0.5f));
    require(std::fabs(rolled.view().at(0,1))>0.1f, "full 3D roll");
    world.destroy(main);
    require(!render::resolveEcsCamera(source,main,first), "stale entity generation");
    require(render::visitEcsCameras(source,[](const auto&){})==1,
            "destroyed camera must not render");
    std::cout << "AIVERSE LENS ECS camera contract passed\n";
}
