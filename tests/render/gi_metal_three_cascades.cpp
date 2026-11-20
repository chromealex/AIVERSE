#include "engine/render/GiCascadeBlend.h"
#include "engine/render/GiCascadeShaders.h"
#include "engine/render/ModelShaders.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cmath>
#include <string>

using namespace aiverse::render;

static bool close(float a,float b,float eps=.0001f) {
    return std::abs(a-b)<eps;
}

int main() {
    std::array<GiCascadeBlendLevel,3> levels{};
    levels[0]={{-4,-4,-4},1,{8,8,8},{10,0,0},.5f,1.5f};
    levels[1]={{-8,-8,-8},2,{8,8,8},{0,20,0},1.f,1.5f};
    levels[2]={{-16,-16,-16},4,{8,8,8},{0,0,30},1.f,1.5f};

    // Only the fine level has a partial validity factor. Medium and
    // distant atlas bindings require fully completed GPU probe tickets.
    const auto center=composeCascadeGi(levels,3,{0,0,0});
    AIVERSE_TEST_CHECK(close(center.indirect.x,5.f));
    AIVERSE_TEST_CHECK(close(center.indirect.y,10.f));
    AIVERSE_TEST_CHECK(close(center.indirect.z,0.f));
    AIVERSE_TEST_CHECK(close(center.uncovered,0.f));

    // At a medium-level boundary, distant GI fills precisely the remaining
    // weight instead of being directly added to a complete medium estimate.
    levels[0].validFraction=0;
    const auto edge=composeCascadeGi(levels,3,{7,0,0});
    AIVERSE_TEST_CHECK(close(edge.levelWeights[0],0));
    AIVERSE_TEST_CHECK(edge.levelWeights[1]>0 && edge.levelWeights[1]<1);
    AIVERSE_TEST_CHECK(edge.levelWeights[2]>0 && edge.levelWeights[2]<1);
    AIVERSE_TEST_CHECK(close(edge.levelWeights[0]+edge.levelWeights[1]+
                              edge.levelWeights[2],1.f));

    const std::string metal=shaders::kModelMSL;
    const std::string gl=shaders::kModelFS;
    AIVERSE_TEST_CHECK(metal.find("dynamicGiTripleAt(tex13,tex14,tex15,tex16,dgi,farGi,thirdGi")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("float4 distant=giCascadeLayer")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("remaining*(1.0f-middle.w)*distant.rgb")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("constant GiDynamicU& thirdGi [[buffer(9)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("texture2d<float> tex16 [[texture(16)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("#if defined(DYNAMIC_GI_TRIPLE)")!=std::string::npos);
    // GLES3 must retain the 16-slot two-cascade route; no tex16 binding
    // is referenced by its shader source even when MSL has triple support.
    AIVERSE_TEST_CHECK(gl.find("dynamicGiCascadedAt(v_wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("uniform highp sampler2D tex16")==std::string::npos);
}
