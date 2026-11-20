#include "engine/render/GiDynamicRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <stdexcept>

using namespace aiverse::render;

int main() {
    FakeDevice device;
    GiDynamicRuntime runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{-1,-1,-1},1,3,3,2,16,6));
    AIVERSE_TEST_CHECK(runtime.ready());
    AIVERSE_TEST_CHECK(runtime.atlas().probeCount()==18);
    StaticShadowGeometry wall;
    wall.triangles={{{-4,-4,1},{4,-4,1},{4,4,1},0},
                    {{-4,-4,1},{4,4,1},{-4,4,1},0}};
    wall.albedos={{1,1,1}};
    wall.build();
    runtime.setGeometry(&wall,1);
    AIVERSE_TEST_CHECK(runtime.hasTraceGeometry() && runtime.geometryRevision()==1);
    runtime.setGeometry(&wall,1); // identical snapshot is idempotent
    bool staleRejected=false;
    try {runtime.setGeometry(nullptr,1);} // old generation cannot clear a wall
    catch(const std::invalid_argument&) {staleRejected=true;}
    AIVERSE_TEST_CHECK(staleRejected);
    staleRejected=false;
    try {runtime.setGeometry(&wall,0);} // older generation is invalid
    catch(const std::invalid_argument&) {staleRejected=true;}
    AIVERSE_TEST_CHECK(staleRejected);
    staleRejected=false;
    try {runtime.setGeometryRegion(&wall,0,{{-1,-1,-1},{1,1,1}});}
    catch(const std::invalid_argument&) {staleRejected=true;}
    AIVERSE_TEST_CHECK(staleRejected);
    AIVERSE_TEST_CHECK(runtime.geometryRevision()==1 && runtime.hasTraceGeometry());
    runtime.setLights({GiMovingLight{42,{0,0,0},{1,0,0},4,20}});

    auto first=runtime.update(2,{0,0,0});
    AIVERSE_TEST_CHECK(first.trace.published==2);
    AIVERSE_TEST_CHECK(first.upload.probes==20); // full zero/reset + two updated
    AIVERSE_TEST_CHECK(first.dirty==16 && first.inFlight==0);
    const auto next=runtime.update(2,{0,0,0});
    AIVERSE_TEST_CHECK(next.trace.published==2);
    AIVERSE_TEST_CHECK(next.upload.probes==2 && next.upload.bytes==96);
    AIVERSE_TEST_CHECK(next.dirty==14);

    // Even with zero new tracing budget the portable GLES/Vulkan CPU GI
    // fallback must upload BLACK into all stale cells after a source move.
    runtime.setLights({GiMovingLight{42,{100,0,0},{1,0,0},4,20}});
    const auto clearedLight=runtime.update(0,{0,0,0});
    AIVERSE_TEST_CHECK(clearedLight.trace.primaryRays==0);
    AIVERSE_TEST_CHECK(clearedLight.upload.probes>0);
    AIVERSE_TEST_CHECK(clearedLight.upload.bytes>0);
    const auto afterLightMove=runtime.atlas().texture();
    for(const auto& value:device.textures.at(afterLightMove.id).data)
        AIVERSE_TEST_CHECK(value.x==0 && value.y==0 &&
                           value.z==0 && value.w==0);

    // Geometry revision resets stale light transport on CPU and GPU even with zero trace budget.
    runtime.setGeometry(nullptr,2);
    AIVERSE_TEST_CHECK(!runtime.hasTraceGeometry() && runtime.geometryRevision()==2);
    auto cleared=runtime.update(0,{0,0,0});
    AIVERSE_TEST_CHECK(cleared.trace.primaryRays==0);
    AIVERSE_TEST_CHECK(cleared.upload.probes==18);
    AIVERSE_TEST_CHECK(cleared.dirty==0);
    const auto texture=runtime.atlas().texture();
    for(const auto& value:device.textures.at(texture.id).data)
        AIVERSE_TEST_CHECK(value.x==0 && value.y==0 && value.z==0 && value.w==0);

    runtime.setGeometry(&wall,3);
    AIVERSE_TEST_CHECK(runtime.hasTraceGeometry() && runtime.geometryRevision()==3);
    while(runtime.reference().scheduler().pending()) {
        const auto step=runtime.update(4,{0,0,0});
        AIVERSE_TEST_CHECK(step.trace.published>=1 && step.trace.published<=4);
        AIVERSE_TEST_CHECK(step.inFlight==0);
    }
    AIVERSE_TEST_CHECK(runtime.update(4,{0,0,0}).upload.bytes==0);
    runtime.shutdown();
    AIVERSE_TEST_CHECK(!runtime.ready() && device.textures.empty());

    bool rejected=false;
    try { (void)runtime.update(1,{0,0,0}); }
    catch(const std::logic_error&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
}
