#include "engine/render/GiChunkedGeometry.h"
#include "engine/render/GiDynamicRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
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
    GiChunkedGeometryBuilder builder;
    builder.replace(1,wall(5),{{1,0,0}});
    builder.replace(2,wall(48),{{0,0,1}});
    auto beforeGeometry=builder.publish();
    AIVERSE_TEST_CHECK(beforeGeometry.chunkCount()==2);
    AIVERSE_TEST_CHECK(beforeGeometry.traceView().valid());

    FakeDevice device;
    GiDynamicRuntime runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{0,0,0},1,64,1,1,64,2));
    runtime.setTraceGeometry(beforeGeometry.traceView(),1);
    runtime.setLights({
        GiMovingLight{1,{5,.5f,0},{1,0,0},6,40},
        GiMovingLight{2,{48,.5f,0},{0,0,1},6,40}
    });

    while(runtime.reference().scheduler().pending()) {
        const auto stats=runtime.update(16,{5,.5f,.5f});
        AIVERSE_TEST_CHECK(stats.trace.published>0);
    }
    const auto nearBefore=runtime.reference().coefficient(5).evaluate({0,0,1});
    const auto farBefore=runtime.reference().coefficient(48).evaluate({0,0,1});
    AIVERSE_TEST_CHECK(nearBefore.x>0);
    AIVERSE_TEST_CHECK(farBefore.z>0);

    // Open the wall: only one chunk BVH is discarded, another is shared.
    AIVERSE_TEST_CHECK(builder.remove(1));
    auto afterGeometry=builder.publish();
    AIVERSE_TEST_CHECK(afterGeometry.chunkCount()==1);
    AIVERSE_TEST_CHECK(afterGeometry.findChunk(2)==beforeGeometry.findChunk(2));
    const GiBounds changed{{0,-3,1},{10,3,1}};
    const size_t affected=runtime.setTraceGeometryRegion(afterGeometry.traceView(),2,changed);
    AIVERSE_TEST_CHECK(affected>0 && affected<64);
    const auto zero=runtime.update(0,{5,.5f,.5f});
    AIVERSE_TEST_CHECK(zero.upload.probes==affected && zero.trace.primaryRays==0);
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(5).evaluate({0,0,1}).x==0);
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(48).evaluate({0,0,1}).z==farBefore.z);

    while(runtime.reference().scheduler().pending()) {
        const auto stats=runtime.update(8,{5,.5f,.5f});
        AIVERSE_TEST_CHECK(stats.trace.published>0 && stats.trace.published<=8);
    }
    const auto nearAfter=runtime.reference().coefficient(5).evaluate({0,0,1});
    const auto farAfter=runtime.reference().coefficient(48).evaluate({0,0,1});
    AIVERSE_TEST_CHECK(nearAfter.x==0);
    AIVERSE_TEST_CHECK(farAfter.z==farBefore.z);
    // Old snapshot must stay queryable until in-flight consumers release it.
    AIVERSE_TEST_CHECK(beforeGeometry.traceView().distance({5,.5f,0},{0,0,1},10)<10);
    AIVERSE_TEST_CHECK(afterGeometry.traceView().distance({5,.5f,0},{0,0,1},10)==10);

    runtime.setTraceGeometry({},3);
    AIVERSE_TEST_CHECK(runtime.reference().scheduler().pending()==0);
    runtime.shutdown();
    AIVERSE_TEST_CHECK(device.textures.empty());
    return 0;
}
