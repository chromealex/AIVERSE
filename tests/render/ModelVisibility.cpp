#include "engine/render/ModelVisibility.h"
#include <cassert>
#include <cmath>
using namespace rts::render;
int main(){
    mat4 clip=mat4::identity();
    assert(modelBoxVisible(clip,{-.5f,-.5f,-.5f},{.5f,.5f,.5f}));
    assert(!modelBoxVisible(clip,{2,0,0},{3,1,1}));
    assert(!modelBoxVisible(clip,{0,0,-3},{1,1,-2}));
    assert(modelBoxVisible(clip,{-2,-2,-2},{2,2,2}));
    assert(std::abs(modelPixelDiameter(clip,{-.5f,-.5f,0},{.5f,.5f,0},800,600)-400)<.01f);
    clip.m[15]=0;
    assert(std::isinf(modelPixelDiameter(clip,{0,0,0},{1,1,1},800,600)));
}
