#include "engine/render/GiCascadeBlend.h"
#include "engine/render/ModelShaders.h"
#include "engine/render/GiCascadeShaders.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cmath>
#include <string>

using namespace aiverse::render;

int main() {
    // Reference: near is partially valid, far is completely valid. Their
    // combined linear INDIRECT energy must never be the naive sum of both.
    std::array<GiCascadeBlendLevel,3> levels{};
    levels[0]={{-4,-4,-4},1,{8,8,8},{10,0,0},.5f,1.5f};
    levels[1]={{-8,-8,-8},2,{8,8,8},{0,20,0},1.f,1.5f};
    const auto center=composeCascadeGi(levels,2,{0,0,0});
    AIVERSE_TEST_CHECK(std::abs(center.indirect.x-5.f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(center.indirect.y-10.f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(center.uncovered)<.001f);

    // At the right edge of the fine volume, coarse coverage takes over.
    levels[0].validFraction=1.f;
    const auto edge=composeCascadeGi(levels,2,{3.5f,0,0});
    AIVERSE_TEST_CHECK(edge.levelWeights[0]>0 && edge.levelWeights[0]<1);
    AIVERSE_TEST_CHECK(edge.levelWeights[1]>0 && edge.levelWeights[1]<1);
    AIVERSE_TEST_CHECK(std::abs(edge.levelWeights[0]+edge.levelWeights[1]-1.f)<.001f);

    const std::string gl=shaders::kModelFS;
    const std::string metal=shaders::kModelMSL;
    AIVERSE_TEST_CHECK(gl.find("dynamicGiCascadedAt(v_wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("u_dynamicGiFarOrigin")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("uniform highp sampler2D tex15")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("smoothstep(0.0,1.5,border)")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("fine.rgb+(1.0-fine.a)*coarse.rgb")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("dynamicGiCascadedAt(tex13,tex14,tex15,dgi,farGi")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("constant GiDynamicU& farGi [[buffer(8)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("texture2d<float> tex15 [[texture(15)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("fine.rgb+(1.0f-fine.w)*coarse.rgb")!=std::string::npos);
    // Test only verifies authored shader contracts, NOT a real GPU compiler,
    // backend texture limits, GPU output or exact numerical shader parity.
}
