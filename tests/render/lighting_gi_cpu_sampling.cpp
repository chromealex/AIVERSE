#include "engine/render/GiDynamicSampling.h"
#include "tests/AlwaysCheck.h"
#include <cmath>

using namespace aiverse::render;
int main() {
    GiDynamicProbeReference gi;
    const float3 origin{-1,-1,-1};
    gi.configure(origin,1,3,3,2,16,6);
    StaticShadowGeometry wall;
    wall.triangles={{{-3,-3,1},{3,-3,1},{3,3,1},0},
                    {{-3,-3,1},{3,3,1},{-3,3,1},0}};
    wall.albedos={{1,1,1}};
    wall.build();
    gi.setGeometry(&wall,1);
    gi.setLights({GiMovingLight{1,{0,0,0},{1,0,0},4,20}});
    while(gi.scheduler().pending())
        (void)gi.update(8,{0,0,0});
    const float3 normal{0,0,1};
    const auto expected=gi.coefficient(0).evaluate(normal);
    const auto actual=sampleDynamicGi(gi,origin,1,3,3,2,{-0.5f,-0.5f,-0.5f},normal);
    AIVERSE_TEST_CHECK(std::abs(expected.x-actual.x)<1e-4f);
    AIVERSE_TEST_CHECK(std::abs(expected.y-actual.y)<1e-4f);
    AIVERSE_TEST_CHECK(std::abs(expected.z-actual.z)<1e-4f);
    // Never inject dynamic GI where the input geometry or light has not
    // been published: an untouched reference must remain black.
    GiDynamicProbeReference fresh;
    fresh.configure(origin,1,3,3,2,16,6);
    const auto black=sampleDynamicGi(fresh,origin,1,3,3,2,
                                     {-0.5f,-0.5f,-0.5f},normal);
    AIVERSE_TEST_CHECK(black.x==0 && black.y==0 && black.z==0);
}
