#include "engine/render/GiLightBridge.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    PointLight p{{0,0,0},5,{1,0,0},20};
    p.key=21;
    p.indirectMultiplier=.5f;
    PointLight inactive=p;
    inactive.key=22;
    inactive.indirectMultiplier=0;
    PointLight spot=p;
    spot.key=23;
    spot.makeSpot({0,0,1},60,.25f);
    spot.indirectMultiplier=1;

    GiLightBridge::Stats stats;
    auto out=GiLightBridge::collect({inactive,spot,p},7,16,{0,0,0},
                                   GiLightOverflowPolicy::Reject,&stats);
    AIVERSE_TEST_CHECK(stats.input==3 && stats.enabled==2 &&
                       stats.output==2 && stats.dropped==0);
    AIVERSE_TEST_CHECK(out.size()==2);
    AIVERSE_TEST_CHECK(out[0].key==(uint64_t(7)<<32 | 21));
    AIVERSE_TEST_CHECK(out[0].intensity==10 && !out[0].spot());
    AIVERSE_TEST_CHECK(out[1].spot() &&
                       out[1].cosInner>=out[1].cosOuter);
    AIVERSE_TEST_CHECK(std::abs(dot(out[1].direction,out[1].direction)-1.f)<.001f);

    bool rejected=false;
    try { (void)GiLightBridge::collect({p,spot},0,16); }
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try { (void)GiLightBridge::collect({p,p},7,16); }
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try { (void)GiLightBridge::collect({p,spot},7,1); }
    catch(const std::length_error&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);

    // Deliberate bounded selection MUST report omitted indirect sources.
    out=GiLightBridge::collect({p,spot},7,1,{0,0,0},
                               GiLightOverflowPolicy::TopInfluence,&stats);
    AIVERSE_TEST_CHECK(out.size()==1 && stats.dropped==1);
    // Stronger 100% indirect spotlight wins over 50% point source at same distance.
    AIVERSE_TEST_CHECK(out[0].key==(uint64_t(7)<<32 | 23));

    PointLight invalid=p;
    invalid.key=0;
    rejected=false;
    try { (void)GiLightBridge::collect({invalid},7,16); }
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    invalid=spot;
    invalid.dir={0,0,0};
    rejected=false;
    try { (void)GiLightBridge::collect({invalid},7,16); }
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);

    // CPU-reference GI sees the spot CONE, not a phantom omni point light.
    StaticShadowGeometry wall;
    wall.triangles={{{-4,-4,2},{4,-4,2},{4,4,2},0},
                    {{-4,-4,2},{4,4,2},{-4,4,2},0}};
    wall.albedos={{1,1,1}};
    wall.build();
    GiDynamicProbeReference gi;
    gi.configure({-.5f,-.5f,-1.5f},1,1,1,1,64,6);
    gi.setGeometry(&wall,1);
    spot.indirectMultiplier=1;
    spot.makeSpot({0,0,1},80,.5f);
    auto towardWall=GiLightBridge::collect({spot},7,16);
    gi.setLights(towardWall);
    AIVERSE_TEST_CHECK(gi.update(1,{0,0,-1}).published==1);
    const auto illuminated=gi.coefficient(0).evaluate({0,0,-1});
    AIVERSE_TEST_CHECK(illuminated.x>0);

    spot.makeSpot({0,0,-1},80,.5f);
    gi.setLights(GiLightBridge::collect({spot},7,16));
    AIVERSE_TEST_CHECK(gi.update(1,{0,0,-1}).published==1);
    const auto dark=gi.coefficient(0).evaluate({0,0,-1});
    AIVERSE_TEST_CHECK(dark.x<.0001f);
}
