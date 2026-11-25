// AIVERSE LENS capture-view construction, CPU contract only.
#include "engine/render/CameraCaptureViews.h"
#include <cmath>
#include <stdexcept>
using namespace aiverse;
static void check(bool ok){if(!ok)throw std::runtime_error("capture views");}
int main() {
    render::Camera main;
    main.designAspect=4.f/3.f;
    main.setPose({0,0,0},{});
    const std::array<uint64_t,6> keys{1,2,3,4,5,6};
    auto cube=render::makeCubemapCameraViews(main,keys,256);
    check(cube[0].targetAssetKey==1&&cube[5].targetAssetKey==6);
    for(const auto& face:cube)
        check(face.width==256&&face.height==256&&
              std::abs(face.camera.fovY-render::cameraPi*.5f)<.0001f);
    const auto cubeProjection=cube[0].camera.proj(1.f,false);
    check(std::abs(cubeProjection.at(0,0)-cubeProjection.at(1,1))<.0001f);
    check(cube[0].camera.rotation.rotate({0,0,-1}).x>.9f);
    // All faces share one light frustum around the capture point.
    for(const auto& face:cube)
        check(face.shadowBounds&&face.shadowMin.x==cube[0].shadowMin.x&&
              face.shadowMax.y==cube[0].shadowMax.y&&face.shadowMax.z-face.shadowMin.z==80.f);
    auto noShadow=render::makeCubemapCameraViews(main,keys,256,false,0.f);
    check(!noShadow[3].shadowBounds);
    auto stereo=render::makeStereoCameraViews(main,.064f,2.f,{7,8},1024,1024);
    check(stereo[0].camera.eye().x<0 && stereo[1].camera.eye().x>0);
    check(stereo[0].camera.lensShift.x>0 && stereo[1].camera.lensShift.x<0);
    auto mirror=render::makeMirrorCamera(main,{1,0,0},-2.f);
    check(std::abs(mirror.eye().x-4.f)<.0001f);
    auto portal=render::makePortalCamera(main,render::mat4::identity(),
        render::mat4::translate({0,0,10}));
    check(std::abs(portal.eye().z-10.f)<.0001f);
}
