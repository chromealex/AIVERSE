// AIVERSE LENS: CPU contract for exact asymmetric frusta and XR eye conversion.
#include "engine/render/CameraXrViews.h"
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace aiverse;
static void check(bool condition) { if(!condition) throw std::runtime_error("camera frustum"); }
static bool close(float a,float b) {return std::abs(a-b)<.0002f;}
int main() {
    render::Camera camera;
    camera.setPose({0,0,0},{});
    camera.zNear=.1f;camera.zFar=100.f;
    check(camera.setFrustumTangents(-.7f,1.2f,-.5f,1.0f));
    for(bool zeroToOne:{false,true}) {
        const auto p=camera.proj(1.f,zeroToOne);
        const auto ndc=[&](float x,float y) {
            const auto c=p*render::float4{x,y,-camera.zNear,1.f};
            return render::float2{c.x/c.w,c.y/c.w};
        };
        auto bottomLeft=ndc(-.7f*camera.zNear,-.5f*camera.zNear);
        auto topRight=ndc(1.2f*camera.zNear,1.f*camera.zNear);
        check(close(bottomLeft.x,-1)&&close(bottomLeft.y,-1));
        check(close(topRight.x,1)&&close(topRight.y,1));
        const auto a=p*render::float4{0,0,-camera.zNear,1};
        const auto b=p*render::float4{0,0,-camera.zFar,1};
        check(close(a.z/a.w,zeroToOne?0.f:-1.f));
        check(close(b.z/b.w,1.f));
    }
    const auto original=camera.frustumLeftTan;
    check(!camera.setFrustumTangents(std::numeric_limits<float>::quiet_NaN(),1,-1,1));
    check(camera.useFrustumTangents&&camera.frustumLeftTan==original);
    camera.clearFrustumTangents();
    check(!camera.useFrustumTangents);
    render::CameraXrFrame frame;
    frame.eyes[0].targetAssetKey=17;frame.eyes[1].targetAssetKey=18;
    frame.eyes[0].width=1200;frame.eyes[0].height=800;
    frame.eyes[1].width=800;frame.eyes[1].height=1200;
    frame.eyes[0].leftTan=-.6f;frame.eyes[0].rightTan=1.1f;
    frame.eyes[0].downTan=-.9f;frame.eyes[0].upTan=.8f;
    frame.eyes[1].leftTan=-1.2f;frame.eyes[1].rightTan=.7f;
    frame.eyes[1].downTan=-.6f;frame.eyes[1].upTan=1.1f;
    std::array<render::CameraCaptureView,2> views{};
    check(render::makeXrCameraViews(frame,camera,views));
    check(views[0].camera.useFrustumTangents&&views[1].camera.useFrustumTangents);
    check(close(views[0].camera.frustumRightTan,1.1f));
    check(close(views[1].camera.frustumLeftTan,-1.2f));
    const auto saved=views;
    frame.eyes[1].targetAssetKey=17;
    check(!render::makeXrCameraViews(frame,camera,views));
    check(views[0].targetAssetKey==saved[0].targetAssetKey &&
          views[1].targetAssetKey==saved[1].targetAssetKey);
}
