#include "engine/animation/AnimationActionEdges.h"
#include "../AlwaysCheck.h"
using namespace aiverse::lib::animation;
int main(){
    AnimationActionEdges detector;
    AnimationActionFrame a;a.generation=1;a.revision=10;
    a.counters["build"]=2;a.flags["open"]=false;
    auto d=detector.observe(a);
    AIVERSE_TEST_CHECK(!d.generationChanged && d.increased.empty());
    a.revision=11;a.counters["build"]=3;a.flags["open"]=true;
    d=detector.observe(a);
    AIVERSE_TEST_CHECK(d.increasedCounter("build") && d.becameActive("open"));
    d=detector.observe(a);
    AIVERSE_TEST_CHECK(d.increased.empty() && d.activated.empty());
    AnimationActionFrame stale=a;stale.revision=5;stale.counters["build"]=20;
    AIVERSE_TEST_CHECK(detector.observe(stale).increased.empty());
    a.revision=12;a.flags["open"]=false;
    AIVERSE_TEST_CHECK(detector.observe(a).becameInactive("open"));
    a.generation=2;a.revision=1;a.counters["build"]=20;
    d=detector.observe(a);
    AIVERSE_TEST_CHECK(d.generationChanged && d.increased.empty());
    detector.reset();AIVERSE_TEST_CHECK(!detector.initialized());
}
