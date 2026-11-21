// AIVERSE LENS: immutable frame contract for camera-independent geometry.
// A native view/animation system prepares one logical frame exactly once,
// then supplies a pure submitter for every camera. This interface does not
// depend on genre, MotionWeave internals or GPU textures.
#pragma once
#include "EcsCameraRender.h"
#include "Renderer3D.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <type_traits>
namespace aiverse::render {
struct CameraVisualFrameId {
    uint64_t simulationTick=0;
    uint64_t presentationFrame=0;
    double seconds=0;
    bool valid()const{return presentationFrame!=0;}
};
struct CameraVisualFrameStats {
    size_t cameraSubmissions=0;
    size_t geometryPackets=0;
};
// A renderer callback MUST be presentation-only: no animator::update,
// event consumption, ECS mutation or modifications to the prepared frame.
// The frame and all referenced model/material assets must remain resident
// until GPU submissions have finished.
template<class PreparedFrame,class SubmitFn>
class CameraVisualFrame {
    const PreparedFrame* frame_=nullptr;
    CameraVisualFrameId id_{};
    SubmitFn submit_;
    size_t submissions_=0;
public:
    CameraVisualFrame(const PreparedFrame& frame,CameraVisualFrameId id,SubmitFn fn)
        :frame_(&frame),id_(id),submit_(std::move(fn)){}
    bool valid()const{return frame_ && id_.valid();}
    const CameraVisualFrameId& id()const{return id_;}
    size_t submissions()const{return submissions_;}
    bool submit(const EcsCameraView& view,Renderer3D& renderer) {
        if(!valid() || !view.entity.valid())return false;
        // Read-only prepared-frame boundary. An adapter must guarantee
        // logical purity; this C++ interface cannot prevent mutable globals
        // inside a user-defined submitter.
        submit_(*frame_,view,renderer);
        ++submissions_;
        return true;
    }
};
template<class PreparedFrame,class SubmitFn>
auto makeCameraVisualFrame(const PreparedFrame& frame,CameraVisualFrameId id,SubmitFn&& fn) {
    return CameraVisualFrame<PreparedFrame,std::decay_t<SubmitFn>>(
        frame,id,std::forward<SubmitFn>(fn));
}
} // namespace aiverse::render
