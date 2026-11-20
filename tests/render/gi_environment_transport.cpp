#include "engine/render/GiDynamicProbeReference.h"
#include "engine/render/GiGpuSmallSceneShaders.h"
#include "engine/render/GiGpuVulkanShaders.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <limits>
#include <string>

using namespace aiverse::render;

static StaticShadowGeometry floorScene(bool roof) {
    StaticShadowGeometry geometry;
    // Lower plane (a diffuse white ground), ray samples start above it.
    geometry.triangles={{{-20,-1,-20},{20,-1,-20},{20,-1,20},0},
                        {{-20,-1,-20},{20,-1,20},{-20,-1,20},0}};
    if (roof) {
        // Roof between the ground and +Y sky visibility ray.
        geometry.triangles.push_back({{-20,.5f,-20},{20,.5f,-20},{20,.5f,20},0});
        geometry.triangles.push_back({{-20,.5f,-20},{20,.5f,20},{-20,.5f,20},0});
    }
    geometry.albedos={{1,1,1}};
    geometry.build();
    return geometry;
}

int main() {
    GiEnvironmentLight invalid;
    invalid.sunIntensity=-1;
    AIVERSE_TEST_CHECK(!invalid.valid());
    invalid={};
    invalid.sunDirection={0,0,0};
    AIVERSE_TEST_CHECK(!invalid.valid());
    invalid={};
    invalid.skyIntensity=std::numeric_limits<float>::infinity();
    AIVERSE_TEST_CHECK(!invalid.valid());

    auto ground=floorScene(false);
    GiDynamicProbeReference gi;
    gi.configure({-.5f,-.5f,-.5f},1,1,1,1,64,6);
    gi.setGeometry(&ground,1);
    AIVERSE_TEST_CHECK(!gi.environment().enabled());
    GiEnvironmentLight env;
    env.sunDirection={0,1,0};
    env.sunColor={1,0,0};
    env.sunIntensity=20;
    AIVERSE_TEST_CHECK(gi.setEnvironment(env));
    AIVERSE_TEST_CHECK(gi.update(1,{0,0,0}).published==1);
    const auto sun=gi.coefficient(0);
    AIVERSE_TEST_CHECK(sun.irradiance[0].x>0);
    AIVERSE_TEST_CHECK(sun.irradiance[1].x==0 && sun.irradiance[2].x==0);
    AIVERSE_TEST_CHECK(!gi.setEnvironment(env));
    AIVERSE_TEST_CHECK(gi.scheduler().pending()==0);

    env.sunIntensity=0;
    env.skyColor={0,0,1};
    env.skyIntensity=8;
    AIVERSE_TEST_CHECK(gi.setEnvironment(env));
    AIVERSE_TEST_CHECK(gi.scheduler().pending()==1);
    AIVERSE_TEST_CHECK(gi.coefficient(0).irradiance[0].x==0);
    AIVERSE_TEST_CHECK(gi.update(1,{0,0,0}).published==1);
    const auto sky=gi.coefficient(0);
    AIVERSE_TEST_CHECK(sky.irradiance[2].x>0);
    AIVERSE_TEST_CHECK(sky.irradiance[0].x==0);

    auto covered=floorScene(true);
    gi.setGeometry(&covered,2);
    AIVERSE_TEST_CHECK(gi.update(1,{0,0,0}).published==1);
    // Roof blocks the overhead sky ray; underside roof cannot see +Y sky.
    AIVERSE_TEST_CHECK(gi.coefficient(0).irradiance[2].x==0);

    env.skyIntensity=0;
    AIVERSE_TEST_CHECK(gi.setEnvironment(env));
    AIVERSE_TEST_CHECK(gi.update(1,{0,0,0}).published==1);
    for(const auto& row:gi.coefficient(0).irradiance)
        AIVERSE_TEST_CHECK(row.x==0 && row.y==0 && row.z==0 && row.w==0);

    const std::string msl=gi_gpu::kMetalSmallScene;
    const std::string glsl=gi_gpu::kVulkanSmallScene;
    // Source/ABI tests: NEVER call these a shader compile or hardware pass.
    AIVERSE_TEST_CHECK(msl.find("float4 sunDirectionIntensity")!=std::string::npos);
    AIVERSE_TEST_CHECK(msl.find("p.skyColor.xyz*first.albedo")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("vec4 sunDirectionIntensity")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("p.skyColor.xyz*first.albedo")!=std::string::npos);
}
