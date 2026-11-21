// AIVERSE LENS HDR tonemap CPU reference and configuration contract.
#include "engine/render/CameraHdrResolve.h"
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace aiverse;
static void check(bool x){if(!x)throw std::runtime_error("camera hdr resolve");}
static bool near(float a,float b,float epsilon=.0001f){return std::fabs(a-b)<epsilon;}
int main(){
    using namespace render;
    check(validCameraHdrResolveSettings({0.f}));
    check(!validCameraHdrResolveSettings({17.f}));
    check(!validCameraHdrResolveSettings({-17.f}));
    check(!validCameraHdrResolveSettings({std::numeric_limits<float>::quiet_NaN()}));
    check(near(cameraHdrToSrgb(0.f,0.f),0.f));
    check(near(cameraHdrToSrgb(-10.f,0.f),0.f));
    check(near(cameraHdrToSrgb(1.f,0.f),.7353569f));
    check(near(cameraHdrToSrgb(1.f,1.f),.8360069f));
    check(near(cameraHdrToSrgb(1.f,-1.f),.612501f));
    check(cameraHdrToSrgb(200.f,0.f)>cameraHdrToSrgb(1.f,0.f));
    check(cameraHdrToSrgb(200.f,0.f)<=1.f);
}
