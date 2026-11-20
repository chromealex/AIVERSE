#include "engine/render/LumaWeaveLights.h"
#include "engine/render/LumaWeaveLegacyBridge.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    LumaWeaveRegistry registry;
    LumaLight alpha;
    alpha.key=17;
    alpha.position={0,1,0};
    alpha.radius=3;
    alpha.contributesToGi=true;
    LumaLight beta;
    beta.key=4;
    beta.position={12,0,0};
    beta.color={0,0,4};
    beta.intensity=20;
    beta.kind=LumaLightKind::Spot;
    beta.direction={0,-1,0};
    beta.cosOuter=.65f;
    beta.cosInner=.85f;
    auto first=registry.publish({alpha,beta});
    AIVERSE_TEST_CHECK(first.changed && first.inserted==2);
    AIVERSE_TEST_CHECK(first.removed==0 && first.updated==0);
    AIVERSE_TEST_CHECK(first.edits.size()==2);
    AIVERSE_TEST_CHECK(first.edits[0].key==4 && first.edits[1].key==17);
    AIVERSE_TEST_CHECK(registry.lights()[0].key==4);
    AIVERSE_TEST_CHECK(registry.revision()==1);
    AIVERSE_TEST_CHECK(registry.find(17) && registry.find(17)->contributesToGi);
    AIVERSE_TEST_CHECK(!registry.find(999));

    auto unchanged=registry.publish({beta,alpha});
    AIVERSE_TEST_CHECK(!unchanged.changed);
    AIVERSE_TEST_CHECK(unchanged.edits.empty() && registry.revision()==1);

    auto moved=alpha;
    moved.position={8,1,0};
    moved.color={.2f,1,0};
    auto second=registry.publish({moved});
    AIVERSE_TEST_CHECK(second.changed && second.updated==1 && second.removed==1);
    AIVERSE_TEST_CHECK(second.edits[0].key==4 && !second.edits[0].hasCurrent);
    AIVERSE_TEST_CHECK(second.edits[1].key==17 && second.edits[1].hadPrevious);
    AIVERSE_TEST_CHECK(second.edits[1].hasCurrent);
    AIVERSE_TEST_CHECK(second.edits[1].affectGi);
    AIVERSE_TEST_CHECK(second.edits[1].previous.min.x==-3);
    AIVERSE_TEST_CHECK(second.edits[1].current.min.x==5);
    AIVERSE_TEST_CHECK(second.edits[1].current.max.x==11);
    AIVERSE_TEST_CHECK(registry.revision()==2);

    bool rejected=false;
    try { (void)registry.publish({alpha,alpha}); }
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected && registry.revision()==2);
    rejected=false;
    alpha.key=0;
    try { (void)registry.publish({alpha}); }
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected && registry.revision()==2);
    rejected=false;
    alpha.key=13;
    alpha.radius=-2;
    try { (void)registry.publish({alpha}); }
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected && registry.find(17));

    rejected=false;
    auto invalidSpot=beta;
    invalidSpot.cosInner=.2f;invalidSpot.cosOuter=.8f;
    try { (void)registry.publish({invalidSpot}); }
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected && registry.revision()==2);
    rejected=false;
    invalidSpot.cosInner=.9f;invalidSpot.direction={100,0,0};
    try { (void)registry.publish({invalidSpot}); }
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected && registry.revision()==2);

    PointLight legacy{};
    legacy.pos={2,1,3};
    legacy.radius=6;
    legacy.color={.8f,.4f,1};
    legacy.intensity=8;
    legacy.key=42;
    const auto fromLegacy=lumaFromLegacy(legacy,uint64_t(10000042),true);
    AIVERSE_TEST_CHECK(fromLegacy.key==10000042 && fromLegacy.kind==LumaLightKind::Point);
    auto giRegistry=LumaWeaveRegistry{};
    (void)giRegistry.publish({fromLegacy});
    const auto gi=lumaGiPointSources(giRegistry);
    AIVERSE_TEST_CHECK(gi.size()==1 && gi[0].key==10000042);
    AIVERSE_TEST_CHECK(gi[0].color.x==.8f && gi[0].radius==6);
    legacy.makeSpot({0,-1,0},40);
    const auto legacySpot=lumaFromLegacy(legacy,uint64_t(10000042),true);
    AIVERSE_TEST_CHECK(legacySpot.kind==LumaLightKind::Spot);
    (void)giRegistry.publish({legacySpot});
    rejected=false;
    try {(void)lumaGiPointSources(giRegistry);}
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected); // no silent omnidirectional GI for a spot source

    auto cleared=registry.publish({});
    AIVERSE_TEST_CHECK(cleared.changed && cleared.removed==1);
    AIVERSE_TEST_CHECK(registry.lights().empty() && registry.revision()==3);
    AIVERSE_TEST_CHECK(!registry.publish({}).changed);
}
