// GiGeometryScene -> GiMainCpuBridge: the first publication resets every
// probe, a later local edit re-traces only probes that can see it.
#include "engine/render/GiGeometryScene.h"
#include "engine/render/GiMainCpuBridge.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace aiverse::render;

static std::vector<StaticShadowGeometry::Triangle> wall(float x) {
    return {
        {{x-5,-3,1},{x+5,-3,1},{x+5,3,1},0},
        {{x-5,-3,1},{x+5,3,1},{x-5,3,1},0}
    };
}
struct Refuse {
    bool publishGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot>,uint64_t){return false;}
    bool publishGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot>,uint64_t,GiBounds){return false;}
};

int main() {
    FakeDevice device;
    GiMainCpuBridge bridge;
    GiMainCpuConfig cfg;
    cfg.origin={0,0,0};cfg.spacing=1;cfg.nx=64;cfg.ny=1;cfg.nz=1;
    cfg.samples=32;cfg.traceDistance=2;cfg.cpuBudgetMs=1000;cfg.maxProbesPerFrame=16;
    AIVERSE_TEST_CHECK(bridge.initialize(device,cfg));
    GiGeometryScene scene;
    scene.replaceChunk(1,wall(5),{{1,0,0}});
    scene.replaceChunk(2,wall(48),{{0,0,1}});
    // A refusing target leaves the changes pending for a retry.
    Refuse refuse;
    AIVERSE_TEST_CHECK(!scene.publish(refuse).published && scene.hasPendingChanges());
    const auto initial=scene.publish(bridge);
    AIVERSE_TEST_CHECK(initial.published && initial.fullReset);
    AIVERSE_TEST_CHECK(initial.chunks==2 && initial.revision==1);
    AIVERSE_TEST_CHECK(bridge.publishLights({
        GiMovingLight{1,{5,.5f,0},{1,0,0},6,40},
        GiMovingLight{2,{48,.5f,0},{0,0,1},6,40}}));
    while(bridge.reference().scheduler().pending())
        AIVERSE_TEST_CHECK(bridge.updateFrame({5,.5f,.5f}).canPresent);
    const auto farBefore=bridge.reference().coefficient(48).evaluate({0,0,1});
    AIVERSE_TEST_CHECK(farBefore.z>0);
    AIVERSE_TEST_CHECK(!scene.publish(bridge).published);

    // Removing the near wall touches only probes within its reach.
    const auto* before=scene.current();
    const auto* keptChunk=before->findChunk(2);
    AIVERSE_TEST_CHECK(scene.removeChunk(1));
    const auto local=scene.publish(bridge);
    AIVERSE_TEST_CHECK(local.published && !local.fullReset && local.changedRegions==1);
    const size_t dirty=bridge.reference().scheduler().pending();
    AIVERSE_TEST_CHECK(dirty>0 && dirty<64);
    AIVERSE_TEST_CHECK(scene.current()->findChunk(1)==nullptr);
    AIVERSE_TEST_CHECK(scene.current()->findChunk(2)==keptChunk);
    AIVERSE_TEST_CHECK(bridge.reference().coefficient(48).evaluate({0,0,1}).z==farBefore.z);
    while(bridge.reference().scheduler().pending())
        AIVERSE_TEST_CHECK(bridge.updateFrame({5,.5f,.5f}).canPresent);
    AIVERSE_TEST_CHECK(bridge.reference().coefficient(5).evaluate({0,0,1}).x==0);
    AIVERSE_TEST_CHECK(bridge.reference().coefficient(48).evaluate({0,0,1}).z==farBefore.z);

    bool failed=false;
    try {
        auto malformed=wall(5);
        malformed[0].a.y=INFINITY;
        scene.replaceChunk(1,std::move(malformed),{{1,1,1}});
    }catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    AIVERSE_TEST_CHECK(!scene.hasPendingChanges());
    AIVERSE_TEST_CHECK(bridge.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(device.textures.empty());
    return 0;
}
