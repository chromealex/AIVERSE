#include "engine/render/GiDynamicSampling.h"
#include "engine/render/ModelShaders.h"
#include "tests/AlwaysCheck.h"
#include <algorithm>
#include <cmath>
#include <string>
using namespace aiverse::render;

static bool near(float x,float y,float eps=.00001f) { return std::abs(x-y)<eps; }

int main() {
    StaticShadowGeometry wall;
    wall.triangles={{{-4,-4,1},{4,-4,1},{4,4,1},0},
                    {{-4,-4,1},{4,4,1},{-4,4,1},0}};
    wall.albedos={{1,1,1}};
    wall.build();
    GiDynamicProbeReference gi;
    const float3 origin{-1,-1,-1};
    gi.configure(origin,1,3,3,2,64,6);
    gi.setGeometry(&wall,1);
    gi.setLights({GiMovingLight{1,{0,0,0},{1,0,0},4,30}});
    while(gi.scheduler().pending()) gi.update(10,{0,0,0});
    const float3 normal{0,0,1};

    for(int z=0;z<2;++z)for(int y=0;y<3;++y)for(int x=0;x<3;++x) {
        const auto p=origin+float3{float(x)+.5f,float(y)+.5f,float(z)+.5f};
        const auto a=sampleDynamicGi(gi,origin,1,3,3,2,p,normal);
        const auto b=gi.coefficient(uint32_t(x+3*(y+3*z))).evaluate(normal);
        AIVERSE_TEST_CHECK(near(a.x,b.x) && near(a.y,b.y) && near(a.z,b.z));
    }
    AIVERSE_TEST_CHECK(sampleDynamicGi(gi,origin,1,3,3,2,{-100,0,0},normal).x==0);
    AIVERSE_TEST_CHECK(sampleDynamicGi(gi,origin,1,3,3,2,{-1,0,0},normal).x==0);
    // Linear interpolation across the x axis when y/z sit at probe centres.
    const auto left=gi.coefficient(0).evaluate(normal);
    const auto right=gi.coefficient(1).evaluate(normal);
    const auto mid=sampleDynamicGi(gi,origin,1,3,3,2,{0, -.5f, -.5f},normal);
    AIVERSE_TEST_CHECK(near(mid.x,(left.x+right.x)*.5f));
    AIVERSE_TEST_CHECK(near(mid.y,(left.y+right.y)*.5f));

    // Shader sources include opt-in fragment interpolation in both backends.
    const std::string gl=shaders::kModelFS;
    const std::string metal=shaders::kModelMSL;
    AIVERSE_TEST_CHECK(gl.find("uniform U6")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("dynamicGiAt(v_wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("struct GiDynamicU")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("dynamicGiAt(tex13,dgi,in.wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("texture2d<float> tex13 [[texture(13)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("#if defined(DYNAMIC_GI)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("#if defined(DYNAMIC_GI)")!=std::string::npos);
    return 0;
}
