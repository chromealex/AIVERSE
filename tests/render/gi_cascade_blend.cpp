#include "engine/render/GiCascadeBlend.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cmath>
#include <stdexcept>

using namespace aiverse::render;
static bool near(float a,float b,float eps=.0001f) {
    return std::abs(a-b)<eps;
}

int main() {
    std::array<GiCascadeBlendLevel,3> levels{};
    levels[0]={{-4,-4,-4},1,{8,8,8},{10,0,0},1,1.5f};
    levels[1]={{-8,-8,-8},2,{8,8,8},{0,20,0},1,1.5f};
    levels[2]={{-16,-16,-16},4,{8,8,8},{0,0,30},1,1.5f};

    const auto center=composeCascadeGi(levels,3,{0,0,0});
    AIVERSE_TEST_CHECK(near(center.levelWeights[0],1));
    AIVERSE_TEST_CHECK(near(center.levelWeights[1],0));
    AIVERSE_TEST_CHECK(near(center.indirect.x,10));
    AIVERSE_TEST_CHECK(near(center.indirect.y,0));
    AIVERSE_TEST_CHECK(near(center.uncovered,0));

    // Fine clipmap fades smoothly into medium near its right border.
    const auto edge=composeCascadeGi(levels,3,{3.5f,0,0});
    AIVERSE_TEST_CHECK(edge.levelWeights[0]>0 && edge.levelWeights[0]<1);
    AIVERSE_TEST_CHECK(near(edge.levelWeights[0]+edge.levelWeights[1],1));
    AIVERSE_TEST_CHECK(near(edge.levelWeights[2],0));
    AIVERSE_TEST_CHECK(near(edge.indirect.x,10*edge.levelWeights[0]));
    AIVERSE_TEST_CHECK(near(edge.indirect.y,20*edge.levelWeights[1]));

    // Incomplete fine GPU probes should be replaced by coarser illumination
    // without adding both sources or creating a sudden saturated double bounce.
    levels[0].validFraction=0;
    const auto fallback=composeCascadeGi(levels,3,{0,0,0});
    AIVERSE_TEST_CHECK(near(fallback.levelWeights[0],0));
    AIVERSE_TEST_CHECK(near(fallback.levelWeights[1],1));
    AIVERSE_TEST_CHECK(near(fallback.indirect.y,20));
    levels[0].validFraction=.5f;
    const auto partial=composeCascadeGi(levels,3,{0,0,0});
    AIVERSE_TEST_CHECK(near(partial.levelWeights[0],.5f));
    AIVERSE_TEST_CHECK(near(partial.levelWeights[1],.5f));
    AIVERSE_TEST_CHECK(near(partial.indirect.x,5));
    AIVERSE_TEST_CHECK(near(partial.indirect.y,10));

    // Outer cascade fades to zero at the world edge. No false GI outside it.
    levels[0].validFraction=1;
    const auto far=composeCascadeGi(levels,3,{7,0,0});
    AIVERSE_TEST_CHECK(far.levelWeights[0]==0);
    AIVERSE_TEST_CHECK(far.levelWeights[1]>0 && far.levelWeights[1]<1);
    AIVERSE_TEST_CHECK(far.levelWeights[2]>0);
    AIVERSE_TEST_CHECK(near(far.levelWeights[1]+far.levelWeights[2],1));
    const auto outside=composeCascadeGi(levels,3,{50,0,0});
    AIVERSE_TEST_CHECK(outside.indirect.x==0 && outside.indirect.y==0 &&
                       outside.indirect.z==0 && outside.uncovered==1);

    bool rejected=false;
    try {(void)composeCascadeGi(levels,0,{0,0,0});}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    levels[0].validFraction=1.1f;
    try {(void)composeCascadeGi(levels,3,{0,0,0});}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    levels[0].validFraction=1;
    levels[1].spacing=0;
    try {(void)composeCascadeGi(levels,3,{0,0,0});}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}
