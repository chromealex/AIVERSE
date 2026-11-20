#include "engine/render/GiGeometryScene.h"
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

int main() {
    FakeDevice device;
    GiDynamicRuntime runtime;
    GiGeometryScene scene;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{0,0,0},1,64,1,1,32,2));
    runtime.setLights({
        GiMovingLight{1,{5,.5f,0},{1,0,0},6,40},
        GiMovingLight{2,{48,.5f,0},{0,0,1},6,40}
    });
    scene.replaceChunk(1,wall(5),{{1,0,0}});
    scene.replaceChunk(2,wall(48),{{0,0,1}});
    const auto initial=scene.publish(runtime);
    AIVERSE_TEST_CHECK(initial.published && initial.fullReset);
    AIVERSE_TEST_CHECK(initial.chunks==2 && initial.revision>0);
    while(runtime.reference().scheduler().pending()) {
        const auto step=runtime.update(16,{5,.5f,.5f});
        AIVERSE_TEST_CHECK(step.trace.published>0);
    }
    const auto current=scene.current();
    AIVERSE_TEST_CHECK(current && current->chunkCount()==2);
    const auto farBefore=runtime.reference().coefficient(48).evaluate({0,0,1});
    AIVERSE_TEST_CHECK(farBefore.z>0);
    const auto same=scene.publish(runtime);
    AIVERSE_TEST_CHECK(!same.published && same.revision==initial.revision);

    AIVERSE_TEST_CHECK(scene.removeChunk(1));
    const auto local=scene.publish(runtime);
    AIVERSE_TEST_CHECK(local.published && !local.fullReset);
    AIVERSE_TEST_CHECK(local.invalidatedProbes>0 && local.invalidatedProbes<64);
    AIVERSE_TEST_CHECK(scene.retiredCount()==1);
    AIVERSE_TEST_CHECK(current->findChunk(1)!=nullptr);
    AIVERSE_TEST_CHECK(scene.current()->findChunk(1)==nullptr);
    AIVERSE_TEST_CHECK(scene.current()->findChunk(2)==current->findChunk(2));
    const auto zero=runtime.update(0,{5,.5f,.5f});
    AIVERSE_TEST_CHECK(zero.upload.probes==local.invalidatedProbes);
    AIVERSE_TEST_CHECK(zero.trace.primaryRays==0);
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(48).evaluate({0,0,1}).z==farBefore.z);
    while(runtime.reference().scheduler().pending()) {
        const auto step=runtime.update(8,{5,.5f,.5f});
        AIVERSE_TEST_CHECK(step.trace.published>0 && step.trace.published<=8);
    }
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(5).evaluate({0,0,1}).x==0);
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(48).evaluate({0,0,1}).z==farBefore.z);

    // Once reference and future GPU fences are done, the owner may reclaim old views.
    scene.releaseRetiredThrough(initial.revision);
    AIVERSE_TEST_CHECK(scene.retiredCount()==0);
    bool failed=false;
    try {
        auto malformed=wall(5);
        malformed[0].a.y=INFINITY;
        scene.replaceChunk(1,std::move(malformed),{{1,1,1}});
    }catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    AIVERSE_TEST_CHECK(!scene.hasPendingChanges());

    runtime.shutdown(); // owner remains alive until the renderer has unbound the atlas
    AIVERSE_TEST_CHECK(device.textures.empty());
    return 0;
}
