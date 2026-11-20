// Header/API compile gate for the optional native GI+direct frame integration,
// plus Release-safe regression tests for fence-aware bounced source sync.
// Does not pretend to exercise actual Metal GPU submissions.
#include "engine/render/LumaWeaveNativeDriver.h"
#include "engine/render/LumaWeaveGiSync.h"
#include "tests/AlwaysCheck.h"
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace aiverse::render;

static_assert(requires(LumaWeaveNativeCoordinator& coordinator,
                       const GiGpuBvh* optionalGeometry) {
    { coordinator.prepareFrame(optionalGeometry,0,float3{},100.f,640u,480u) }
        -> std::same_as<LumaNativeFrameStats>;
});
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.indirectGi),
                              GiGpuCascadeRuntime*>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.indirectCpuGi),
                              GiDynamicRuntime*>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.cpuGiFrame),
                              GiRuntimeStats>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.expectedCpuGiGeometryRevision),
                              uint64_t>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.giGeometryMismatch),
                              bool>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.backendCapabilities),
                              LumaLightingBackendCaps>);
static_assert(LumaWeaveBackendPolicy::cpuProbeLimit(gfx::Backend::GLES3)==2);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.giFrame),
                              GiCascadeFrame>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.giPollFrame),
                              GiCascadeFrame>);
static_assert(requires(LumaWeaveNativeDriver& driver,
                       LumaWeaveNativeCoordinator& coordinator) {
    driver.resetGiSourceSync();
    coordinator.resetGiSourceSync();
});
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.priorGpuCompletion),
                              LumaWeaveScreenMetal::Completion>);

struct FakeBouncedGi {
    bool available=true;
    bool inflight=false;
    bool reject=false;
    int publications=0;
    std::vector<GiMovingLight> lights;

    bool ready() const {return available;}
    bool busy() const {return inflight;}
    bool setLights(std::vector<GiMovingLight> next) {
        if(reject)return false;
        lights=std::move(next);
        ++publications;
        return true;
    }
};

int main() {
    LumaWeaveRegistry registry;
    LumaLight moving;
    moving.key=4;
    moving.position={1,2,3};
    moving.contributesToGi=true;
    (void)registry.publish({moving});

    FakeBouncedGi gi;
    LumaWeaveGiSync sync;
    gi.available=false;
    auto pending=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(pending.deferred && !pending.accepted);
    AIVERSE_TEST_CHECK(gi.publications==0);

    gi.available=true;
    auto accepted=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(accepted.accepted && !accepted.deferred);
    AIVERSE_TEST_CHECK(accepted.bouncedSources==1 && gi.publications==1);
    AIVERSE_TEST_CHECK(gi.lights.size()==1 && gi.lights[0].key==4);
    AIVERSE_TEST_CHECK(sync.acceptedSourceRevision()==registry.revision());

    gi.inflight=true;
    auto same=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(same.accepted && same.unchanged);
    AIVERSE_TEST_CHECK(gi.publications==1);
    auto moved=moving;
    moved.position={100,2,3};
    (void)registry.publish({moved});
    pending=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(pending.deferred && !pending.accepted);
    AIVERSE_TEST_CHECK(gi.lights[0].position.x==1);
    AIVERSE_TEST_CHECK(gi.publications==1);

    gi.inflight=false;
    gi.reject=true;
    pending=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(pending.deferred && gi.publications==1);
    gi.reject=false;
    accepted=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(accepted.accepted && !accepted.unchanged);
    AIVERSE_TEST_CHECK(gi.lights[0].position.x==100);
    AIVERSE_TEST_CHECK(gi.publications==2);

    auto spot=moved;
    spot.key=5;
    spot.kind=LumaLightKind::Spot;
    spot.direction={0,-1,0};
    spot.cosOuter=.6f;
    spot.cosInner=.9f;
    (void)registry.publish({moved,spot});
    bool threw=false;
    try {(void)sync.publish(registry,gi);}
    catch(const std::invalid_argument&) {threw=true;}
    AIVERSE_TEST_CHECK(threw && gi.publications==2);
    AIVERSE_TEST_CHECK(sync.acceptedSourceRevision()!=registry.revision());

    std::vector<LumaLight> many;
    for(uint64_t i=1;i<=17;++i) {
        auto l=moving;
        l.key=i;
        many.push_back(l);
    }
    (void)registry.publish(many);
    threw=false;
    try {(void)sync.publish(registry,gi);}
    catch(const std::length_error&) {threw=true;}
    AIVERSE_TEST_CHECK(threw && gi.publications==2);

    (void)registry.publish({});
    accepted=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(accepted.accepted && accepted.bouncedSources==0);
    AIVERSE_TEST_CHECK(gi.publications==3 && gi.lights.empty());
    sync.reset();
    accepted=sync.publish(registry,gi);
    AIVERSE_TEST_CHECK(accepted.accepted && !accepted.unchanged);
    AIVERSE_TEST_CHECK(gi.publications==4);
    return 0;
}
