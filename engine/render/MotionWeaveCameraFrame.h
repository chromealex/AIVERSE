// AIVERSE LENS + MotionWeave: borrowed visual frame that reuses a single
// animation evaluation across multiple ECS cameras. No RTS/FPS dependency.
// The host calls View::update() ONCE, then submits this frame to N cameras.
// Borrowed model/pose/material arrays must remain alive until submission ends.
#pragma once
#include "CameraVisualFrame.h"
#include "engine/view/View.h"
#include <span>
#include <utility>

namespace aiverse::render {
struct MotionWeaveCameraFrame {
    const ::aiverse::view::View* animatedView=nullptr;
    std::span<const ::aiverse::view::Object> objects;
    CameraVisualFrameId id{};

    bool valid()const noexcept {
        return animatedView!=nullptr && id.valid();
    }
    // Renderer's beginScene(camera,width,height) is managed by the LENS
    // pipeline. This only emits prepared model packets and attachments.
    // Camera-scoped LOD hysteresis is independent for each ECS camera.
    void submit(const EcsCameraView& camera,Renderer3D& renderer) const {
        if(!valid())return;
        animatedView->submit(renderer,objects,camera.entity.packed());
    }
};
inline auto makeMotionWeaveCameraVisualFrame(
        const MotionWeaveCameraFrame& prepared) {
    return makeCameraVisualFrame(prepared,prepared.id,
        [](const MotionWeaveCameraFrame& data,
           const EcsCameraView& camera,Renderer3D& renderer) {
            data.submit(camera,renderer);
        });
}
} // namespace aiverse::render
