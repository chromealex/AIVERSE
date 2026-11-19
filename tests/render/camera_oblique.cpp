// Oblique clip projection must preserve finite entries for both depth ranges.
#include "engine/render/CameraCaptureViews.h"
#include <cmath>
#include <stdexcept>
using namespace aiverse::render;
static void check(bool b){if(!b)throw std::runtime_error("camera oblique clip");}
int main(){
 Camera camera;
 camera.setPose({0,0,0},{});
 camera.zNear=.1f;camera.zFar=100.f;
 camera.obliqueClip=true;
 camera.clipWorldPlane={0,0,-1,-2.f};
 for(bool zeroToOne:{false,true}) {
     auto projection=camera.proj(1.f,zeroToOne);
     for(float f:projection.m)check(std::isfinite(f));
 }
 camera.obliqueClip=false;
 auto raw=camera.proj(1.f,false);
 camera.obliqueClip=true;
 auto clipped=camera.proj(1.f,false);
 check(std::abs(raw.at(2,2)-clipped.at(2,2))>.01f);
}
