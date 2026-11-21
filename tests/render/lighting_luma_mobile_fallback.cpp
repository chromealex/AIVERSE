#include "engine/render/LumaWeaveMobileFallback.h"
#include "engine/render/LumaWeaveLegacyBridge.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace aiverse::render;
int main() {
    LumaWeaveRegistry registry;
    LumaLight a; a.key=1;a.position={0,0,0};a.radius=8;
    a.color={1,0,0};a.intensity=16;a.contributesToGi=true;
    LumaLight b=a;b.key=2;b.position={2,0,0};
    b.color={0,1,0};b.intensity=4;b.kind=LumaLightKind::Spot;
    b.direction={0,-1,0};b.cosInner=.8f;b.cosOuter=.6f;
    LumaLight far=a;far.key=3;far.position={1000,0,0};
    (void)registry.publish({far,b,a});
    const auto plan=LumaWeaveMobileFallback::plan(registry,{0,0,0},16,1);
    AIVERSE_TEST_CHECK(plan.totalRegistered==3);
    AIVERSE_TEST_CHECK(plan.candidates==2);
    AIVERSE_TEST_CHECK(plan.selected.size()==1);
    AIVERSE_TEST_CHECK(plan.dropped==1 && plan.lossy);
    AIVERSE_TEST_CHECK(plan.retainedImportanceFraction>0.f);
    AIVERSE_TEST_CHECK(plan.retainedImportanceFraction<1.f);
    AIVERSE_TEST_CHECK(plan.selected[0].key==1);
    const auto all=LumaWeaveMobileFallback::plan(registry,{0,0,0},16,16);
    AIVERSE_TEST_CHECK(all.selected.size()==2);
    AIVERSE_TEST_CHECK(all.dropped==0 && !all.lossy);
    AIVERSE_TEST_CHECK(all.selected[1].spot());
    const auto gi=lumaGiMovingSources(registry);
    AIVERSE_TEST_CHECK(gi.size()==3); // eligible remote point is still an authored GI source
    AIVERSE_TEST_CHECK(gi[0].key==1);
    AIVERSE_TEST_CHECK(gi[1].key==2 && gi[1].spot());
    AIVERSE_TEST_CHECK(std::abs(gi[1].cosOuter-.6f)<1e-6f);
    AIVERSE_TEST_CHECK(lumaGiPointSources(registry).size()==gi.size());
    PointLight original;
    original.pos={1,2,3};original.radius=4;original.color={.5f,.5f,.5f};
    original.intensity=2;original.key=99;
    const auto imported=lumaFromLegacy(original,1234,true);
    AIVERSE_TEST_CHECK(imported.key==1234 && imported.contributesToGi);
    AIVERSE_TEST_CHECK(imported.position.y==2);
    bool rejected=false;
    try { (void)LumaWeaveMobileFallback::plan(registry,{0,0,0},0,1); }
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}
