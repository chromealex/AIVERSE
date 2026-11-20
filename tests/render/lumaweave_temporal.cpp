#include "engine/render/LumaWeaveTemporal.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <stdexcept>

using namespace aiverse::render;

static LumaTemporalReceiver receiver(uint64_t id,float x,float value) {
    LumaTemporalReceiver r;
    r.key=id;
    r.worldPosition={x,0,0};
    r.normal={0,1,0};
    r.depth=3;
    r.direct={value,value,value};
    return r;
}
int main() {
    LumaTemporalConfig config;
    config.maxHistoryBlend=.90f;
    config.maxWorldMotion=.3f;
    config.maxDepthDelta=.15f;
    config.maxEntries=2;
    LumaWeaveTemporalHistory history(config);
    LumaWeaveRegistry lightSources;
    LumaLight nearLight,farLight;
    nearLight.key=1;nearLight.position={0,0,0};nearLight.radius=2;
    farLight.key=2;farLight.position={40,0,0};farLight.radius=2;
    const auto first=lightSources.publish({nearLight,farLight});
    AIVERSE_TEST_CHECK(first.revision==1);

    history.beginFrame(1,1);
    auto a=history.resolve(receiver(11,0,5));
    auto b=history.resolve(receiver(12,40,5));
    AIVERSE_TEST_CHECK(!a.reusedHistory && a.accumulatedFrames==1);
    AIVERSE_TEST_CHECK(history.historySize()==2);

    history.beginFrame(1,1);
    a=history.resolve(receiver(11,0,3));
    b=history.resolve(receiver(12,40,3));
    AIVERSE_TEST_CHECK(a.reusedHistory && b.reusedHistory);
    AIVERSE_TEST_CHECK(a.accumulatedFrames==2 && b.accumulatedFrames==2);
    AIVERSE_TEST_CHECK(std::abs(a.direct.x-4)<.0001f);

    nearLight.position={5,0,0};
    const auto moved=lightSources.publish({nearLight,farLight});
    AIVERSE_TEST_CHECK(moved.changed && moved.updated==1);
    history.beginFrame(moved.revision,1,moved.edits);
    a=history.resolve(receiver(11,0,3));
    b=history.resolve(receiver(12,40,3));
    AIVERSE_TEST_CHECK(!a.reusedHistory);  // old radius still invalidates old location
    AIVERSE_TEST_CHECK(b.reusedHistory);   // unaffected distant source keeps history

    // Geometry revision must reject all history conservatively.
    history.beginFrame(moved.revision,2);
    a=history.resolve(receiver(11,0,3));
    b=history.resolve(receiver(12,40,3));
    AIVERSE_TEST_CHECK(!a.reusedHistory && !b.reusedHistory);

    history.beginFrame(moved.revision,2);
    auto shifted=receiver(11,1,3);
    a=history.resolve(shifted);
    AIVERSE_TEST_CHECK(!a.reusedHistory); // camera/reprojection moved too far

    history.beginFrame(moved.revision,2);
    auto tilted=receiver(11,1,3);
    tilted.normal={0,0,1};
    a=history.resolve(tilted);
    AIVERSE_TEST_CHECK(!a.reusedHistory);
    history.beginFrame(moved.revision,2);
    tilted.depth=3.5f;
    a=history.resolve(tilted);
    AIVERSE_TEST_CHECK(!a.reusedHistory);

    history.beginFrame(moved.revision,2);
    tilted.valid=false;
    a=history.resolve(tilted);
    AIVERSE_TEST_CHECK(!a.reusedHistory && history.historySize()==1);

    // Unknown light revision without complete old+new bound records clears
    // history rather than reprojecting obsolete direct lighting.
    history.beginFrame(moved.revision+3,2);
    AIVERSE_TEST_CHECK(history.historySize()==0);
    auto one=receiver(1,0,1),two=receiver(2,1,1),three=receiver(3,2,1);
    (void)history.resolve(one);
    (void)history.resolve(two);
    (void)history.resolve(three);
    AIVERSE_TEST_CHECK(history.historySize()==2);

    bool rejected=false;
    try {history.beginFrame(moved.revision+3,2,moved.edits);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try {(void)history.resolve(receiver(0,0,1));}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    history.reset();
    AIVERSE_TEST_CHECK(history.historySize()==0 && history.frame()==0);
}
