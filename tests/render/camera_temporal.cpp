// Camera temporal signatures: per-camera history invalidation, no GPU.
#include "engine/render/CameraTemporalPolicy.h"
#include <stdexcept>
using namespace aiverse;
static void require(bool v){if(!v)throw std::runtime_error("camera temporal policy");}
int main() {
    render::EcsCameraView view;
    view.camera.setPose({0,0,0},{});
    auto first=render::cameraSignature(view,512,512);
    require(render::invalidateCameraHistory({},first));
    require(!render::invalidateCameraHistory(first,first));
    auto moved=first;moved.camera.position={.1f,.1f,.1f};
    require(!render::invalidateCameraHistory(first,moved));
    moved.camera.position={5,0,0};
    require(render::invalidateCameraHistory(first,moved));
    moved=first;moved.width=1024;
    require(render::invalidateCameraHistory(first,moved));
    moved=first;moved.hdr=true;
    require(render::invalidateCameraHistory(first,moved));
    moved=first;moved.target=42;
    require(render::invalidateCameraHistory(first,moved));
    moved=first;moved.camera.rotation=render::quat::axisAngle({0,1,0},1.5f);
    require(render::invalidateCameraHistory(first,moved));
}
