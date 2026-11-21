#include "engine/view/GeometryBounds.h"
#include "../AlwaysCheck.h"
#include <cmath>
#include <limits>

using namespace aiverse;

int main() {
    const render::float3 lo{-1.f,-2.f,-3.f},hi{1.f,2.f,3.f};
    const auto identity=view::transformAabb(lo,hi,render::mat4{});
    AIVERSE_TEST_CHECK(identity.valid);
    AIVERSE_TEST_CHECK(identity.min.x==-1.f && identity.max.z==3.f);

    const auto translation=render::mat4::translate({4.f,5.f,6.f});
    const auto shifted=view::transformAabb(lo,hi,translation);
    AIVERSE_TEST_CHECK(shifted.valid);
    AIVERSE_TEST_CHECK(shifted.min.x==3.f && shifted.max.x==5.f);
    AIVERSE_TEST_CHECK(shifted.min.y==3.f && shifted.max.z==9.f);

    const auto scaled=view::transformAabb(lo,hi,
        render::mat4::scale({2.f,.5f,-1.f}));
    AIVERSE_TEST_CHECK(scaled.valid);
    AIVERSE_TEST_CHECK(scaled.min.x==-2.f && scaled.max.x==2.f);
    AIVERSE_TEST_CHECK(scaled.min.y==-1.f && scaled.max.y==1.f);
    AIVERSE_TEST_CHECK(scaled.min.z==-3.f && scaled.max.z==3.f);

    const auto rotated=view::transformAabb({-1.f,0.f,-2.f},
        {1.f,0.f,2.f},render::mat4::rotateY(3.14159265f*.5f));
    AIVERSE_TEST_CHECK(rotated.valid);
    AIVERSE_TEST_CHECK(std::abs(rotated.min.x+2.f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(rotated.max.x-2.f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(rotated.min.z+1.f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(rotated.max.z-1.f)<.001f);

    const float nan=std::numeric_limits<float>::quiet_NaN();
    AIVERSE_TEST_CHECK(!view::transformAabb({nan,0,0},hi,render::mat4{}).valid);
    AIVERSE_TEST_CHECK(!view::transformAabb(hi,lo,render::mat4{}).valid);
    const float inf=std::numeric_limits<float>::infinity();
    AIVERSE_TEST_CHECK(!view::transformAabb(lo,hi,
        render::mat4::translate({inf,0,0})).valid);
}
