#include "engine/view/GeometryBounds.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace aiverse;

int main() {
    const render::float3 lo{-1.f,-2.f,-3.f},hi{1.f,2.f,3.f};
    const auto identity=view::transformAabb(lo,hi,render::mat4{});
    assert(identity.valid);
    assert(identity.min.x==-1.f && identity.max.z==3.f);

    const auto translation=render::mat4::translate({4.f,5.f,6.f});
    const auto shifted=view::transformAabb(lo,hi,translation);
    assert(shifted.valid);
    assert(shifted.min.x==3.f && shifted.max.x==5.f);
    assert(shifted.min.y==3.f && shifted.max.z==9.f);

    const auto scaled=view::transformAabb(lo,hi,
        render::mat4::scale({2.f,.5f,-1.f}));
    assert(scaled.valid);
    assert(scaled.min.x==-2.f && scaled.max.x==2.f);
    assert(scaled.min.y==-1.f && scaled.max.y==1.f);
    assert(scaled.min.z==-3.f && scaled.max.z==3.f);

    const auto rotated=view::transformAabb({-1.f,0.f,-2.f},
        {1.f,0.f,2.f},render::mat4::rotateY(3.14159265f*.5f));
    assert(rotated.valid);
    assert(std::abs(rotated.min.x+2.f)<.001f);
    assert(std::abs(rotated.max.x-2.f)<.001f);
    assert(std::abs(rotated.min.z+1.f)<.001f);
    assert(std::abs(rotated.max.z-1.f)<.001f);

    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!view::transformAabb({nan,0,0},hi,render::mat4{}).valid);
    assert(!view::transformAabb(hi,lo,render::mat4{}).valid);
    const float inf=std::numeric_limits<float>::infinity();
    assert(!view::transformAabb(lo,hi,
        render::mat4::translate({inf,0,0})).valid);
}
