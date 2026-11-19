// ECS camera -> immutable render-view snapshot. CPU-only, read-only bridge.
// Call after SceneHierarchy::update(); no GPU work, no ECS mutations.
// Output keys refer to host-managed render textures, never RHI handles in ECS.
#pragma once
#include "../core/ECS.h"
#include "../sim/CameraComponents.h"
#include "../sim/CameraViewportComponents.h"
#include "../sim/SceneHierarchy.h"
#include "Math3D.h"
#include <cmath>
#include <cstdint>

namespace aiverse::render {

struct EcsCameraView {
    aiverse::Entity entity{};
    render::Camera camera{};
    CameraOutputComponent output{};
    CameraUpdateComponent update{};
    CameraDependenciesComponent dependencies{};
    CameraViewportComponent viewport{};
    CameraStackComponent stack{};
    uint32_t visibilityMask = UINT32_MAX;
    int32_t priority = 0;
};

inline bool validCameraComponent(const CameraComponent& c) {
    const double fov = c.verticalFov.toDouble();
    const double size = c.orthographicHalfHeight.toDouble();
    const double nearPlane = c.nearPlane.toDouble();
    const double farPlane = c.farPlane.toDouble();
    return std::isfinite(fov) && fov > 0.01 && fov < 3.13 &&
           std::isfinite(size) && size > 0.0 &&
           std::isfinite(nearPlane) && nearPlane > 0.0 &&
           std::isfinite(farPlane) && farPlane > nearPlane;
}

// Conversion from authoritative fixed-point ECS state to transient rendering
// state. Neither CameraComponent nor WorldTransform contains GPU resources.
inline bool resolveEcsCamera(const aiverse::World& world, aiverse::Entity entity,
                             EcsCameraView& out, aiverse::Vec3 renderOrigin = {}) {
    const auto* settings = world.tryGetConst<CameraComponent>(entity);
    const auto* transform = world.tryGetConst<WorldTransform>(entity);
    if (!settings || !transform || !settings->enabled || !validCameraComponent(*settings))
        return false;
    const auto toFloat=[](Fixed v) { return float(v.toDouble()); };
    Camera camera;
    camera.setPose(
        {toFloat(transform->position.x-renderOrigin.x),
         toFloat(transform->position.y-renderOrigin.y),
         toFloat(transform->position.z-renderOrigin.z)},
        {toFloat(transform->rotation.x),toFloat(transform->rotation.y),
         toFloat(transform->rotation.z),toFloat(transform->rotation.w)});
    camera.ortho = settings->projection == CameraProjection::Orthographic;
    camera.fovY = toFloat(settings->verticalFov);
    camera.orthoHalfHeightOverride = toFloat(settings->orthographicHalfHeight);
    camera.zNear = toFloat(settings->nearPlane);
    camera.zFar = toFloat(settings->farPlane);
    out = {};
    out.entity = entity;
    out.camera = camera;
    if (const auto* output=world.tryGetConst<CameraOutputComponent>(entity)) out.output=*output;
    if (const auto* update=world.tryGetConst<CameraUpdateComponent>(entity)) out.update=*update;
    if (const auto* inputs=world.tryGetConst<CameraDependenciesComponent>(entity)) out.dependencies=*inputs;
    if (const auto* layout=world.tryGetConst<CameraViewportComponent>(entity)) out.viewport=*layout;
    if (const auto* stack=world.tryGetConst<CameraStackComponent>(entity)) out.stack=*stack;
    out.visibilityMask = settings->visibilityMask;
    out.priority = settings->priority;
    return true;
}

template<class Submit>
size_t visitEcsCameras(const aiverse::World& world, Submit&& submit,
                       aiverse::Vec3 renderOrigin = {}) {
    size_t count = 0;
    world.eachExistingConst<CameraComponent>(
        [&](aiverse::Entity entity, const CameraComponent&) {
            EcsCameraView view;
            if (!resolveEcsCamera(world, entity, view, renderOrigin)) return;
            submit(view);
            ++count;
        });
    return count;
}

} // namespace aiverse::render
