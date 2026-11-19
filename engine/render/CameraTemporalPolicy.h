// AIVERSE LENS temporal history invalidation policy. GPU-independent.
// Per-camera view variant must have isolated state for TAA, SSR, exposure.
#pragma once
#include "EcsCameraRender.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace aiverse::render {
struct CameraTemporalSignature {
    Camera camera{};
    int width=0,height=0;
    uint64_t target=0;
    bool hdr=false, valid=false;
};
inline bool invalidateCameraHistory(const CameraTemporalSignature& previous,
                                    const CameraTemporalSignature& current) {
    if(!previous.valid || !current.valid) return true;
    if(previous.width!=current.width || previous.height!=current.height ||
       previous.target!=current.target || previous.hdr!=current.hdr ||
       previous.camera.ortho!=current.camera.ortho) return true;
    if(length(previous.camera.eye()-current.camera.eye())>2.f) return true;
    const float fov=std::abs(previous.camera.fovY-current.camera.fovY);
    const float ortho=std::abs(previous.camera.orthoHalfHeight()-current.camera.orthoHalfHeight());
    if(fov>.08f || (current.camera.ortho && ortho>.5f))return true;
    const auto forward=[](const Camera& c)->float3 {
        if(c.usePose)return normalize(c.rotation.rotate({0,0,-1}));
        return normalize(c.target-c.eye());
    };
    if(dot(forward(previous.camera),forward(current.camera))<.94f)return true;
    return false;
}
inline CameraTemporalSignature cameraSignature(const EcsCameraView& view,int w,int h) {
    CameraTemporalSignature sig;
    sig.camera=view.camera;sig.width=w;sig.height=h;
    sig.target=view.output.kind==CameraOutputKind::Texture?view.output.targetAssetKey:0;
    sig.hdr=view.output.hdr;sig.valid=(w>0&&h>0);
    return sig;
}
} // namespace aiverse::render
