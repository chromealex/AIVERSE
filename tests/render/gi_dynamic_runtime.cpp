#include "engine/render/GiDynamicRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <initializer_list>
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
    runtime.setLights({GiMovingLight{42,{0,0,0},{1,0,0},4,20}});

    auto first=runtime.update(2,{0,0,0});
    AIVERSE_TEST_CHECK(first.trace.published==2);
    AIVERSE_TEST_CHECK(first.upload.probes==20); // full zero/reset + two updated
    AIVERSE_TEST_CHECK(first.dirty==16 && first.inFlight==0);
    const auto next=runtime.update(2,{0,0,0});
    AIVERSE_TEST_CHECK(next.trace.published==2);
    AIVERSE_TEST_CHECK(next.upload.probes==2 && next.upload.bytes==96);
    AIVERSE_TEST_CHECK(next.dirty==14);

    // Geometry revision resets stale light transport on CPU and GPU even with zero trace budget.
    runtime.setGeometry(nullptr,2);
    auto cleared=runtime.update(0,{0,0,0});
    AIVERSE_TEST_CHECK(cleared.trace.primaryRays==0);
    AIVERSE_TEST_CHECK(cleared.upload.probes==18);
    AIVERSE_TEST_CHECK(cleared.dirty==0);
    const auto texture=runtime.atlas().texture();
    for(const auto& value:device.textures.at(texture.id).data)
        AIVERSE_TEST_CHECK(value.x==0 && value.y==0 && value.z==0 && value.w==0);

    runtime.setGeometry(&wall,3);
    while(runtime.reference().scheduler().pending()) {
        const auto step=runtime.update(4,{0,0,0});
        AIVERSE_TEST_CHECK(step.trace.published>=1 && step.trace.published<=4);
        AIVERSE_TEST_CHECK(step.inFlight==0);
    }
    AIVERSE_TEST_CHECK(runtime.update(4,{0,0,0}).upload.bytes==0);

    // Moving a light must synchronously invalidate the CPU coefficients, then
    // flush the old GPU bounce even with ZERO tracing budget. All 18 probes
    // are within the old source's conservative radius plus trace distance.
    runtime.setLights({GiMovingLight{42,{1,0,0},{0,0,1},4,20}});
    AIVERSE_TEST_CHECK(runtime.reference().scheduler().pending()==18);
    for (const auto& probe:runtime.reference().coefficients())
        for (const auto& c:probe.irradiance)
            AIVERSE_TEST_CHECK(c.x==0 && c.y==0 && c.z==0 && c.w==0);
    const auto oldLightClear=runtime.update(0,{0,0,0});
    AIVERSE_TEST_CHECK(oldLightClear.trace.claimed==0);
    AIVERSE_TEST_CHECK(oldLightClear.upload.probes==18 &&
                       oldLightClear.upload.bytes==18*3*sizeof(float4));
    for (const auto& value:device.textures.at(texture.id).data)
        AIVERSE_TEST_CHECK(value.x==0 && value.y==0 && value.z==0 && value.w==0);

    // Source removal is the same invalidation case; it cannot keep stale GI.
    AIVERSE_TEST_CHECK(runtime.update(2,{0,0,0}).trace.published==2);
    runtime.setLights({});
    const auto removedLightClear=runtime.update(0,{0,0,0});
    AIVERSE_TEST_CHECK(removedLightClear.trace.claimed==0);
    AIVERSE_TEST_CHECK(removedLightClear.upload.probes==18);
    for (const auto& value:device.textures.at(texture.id).data)
        AIVERSE_TEST_CHECK(value.x==0 && value.y==0 && value.z==0 && value.w==0);

    // An explicit world invalidate must clear even an already-dirty probe in
    // the GPU atlas when tracing is paused (for example, simulation pause).
    runtime.invalidate({{-.5f,-.5f,-.5f},{-.5f,-.5f,-.5f}},
                       GiDirtyReason::Environment);
    AIVERSE_TEST_CHECK(runtime.update(0,{0,0,0}).upload.probes==1);

    GiWorldDirtyAccumulator worldEvents;
    worldEvents.submit({GiDirtyReason::Material,
                        {{.5f,-.5f,-.5f},{.5f,-.5f,-.5f}}});
    const auto dirty=runtime.applyWorldChanges(worldEvents);
    AIVERSE_TEST_CHECK(dirty.events==1 && dirty.probeMarks==1 && !dirty.fullVolume);
    AIVERSE_TEST_CHECK(runtime.update(0,{0,0,0}).upload.probes==1);

    // More than 128 independently authored foliage changes saturate the batch
    // into a full-volume reset, with no stale transport left for presentation.
    for (int i=0;i<129;++i) {
        const float x=1000.f+float(3*i);
        worldEvents.submit({GiDirtyReason::Geometry,{{x,0,0},{x,0,0}}});
    }
    AIVERSE_TEST_CHECK(worldEvents.saturated());
    const auto fullDirty=runtime.applyWorldChanges(worldEvents);
    AIVERSE_TEST_CHECK(fullDirty.fullVolume && fullDirty.probeMarks==18);
    const auto worldReset=runtime.update(0,{0,0,0});
    AIVERSE_TEST_CHECK(worldReset.trace.claimed==0 && worldReset.upload.probes==18);

    // Exercise the generic RHI CPU-probe path using each backend's identity.
    // FakeDevice is NOT real Vulkan/GLES/Metal GPU execution or shader validation.
    for (const auto backend:{gfx::Backend::GLES3,gfx::Backend::Vulkan,gfx::Backend::Metal}) {
        FakeDevice portable;
        portable.info_.backend=backend;
        GiDynamicRuntime cross;
        AIVERSE_TEST_CHECK(cross.initialize(portable,{-1,-1,-1},1,3,3,2,16,6));
        cross.setGeometry(&wall,1);
        cross.setLights({GiMovingLight{7,{0,0,0},{1,1,1},4,20}});
        AIVERSE_TEST_CHECK(cross.update(3,{0,0,0}).trace.published==3);
        cross.setLights({});
        const auto clearedOnBackend=cross.update(0,{0,0,0});
        AIVERSE_TEST_CHECK(clearedOnBackend.upload.probes==18);
        cross.cpuTraceBudget().configure(.5,2);
        const auto bounded=cross.updateAdaptive({0,0,0});
        AIVERSE_TEST_CHECK(bounded.trace.claimed<=2);
        AIVERSE_TEST_CHECK(cross.cpuTraceBudget().maxProbes()==2);
        cross.shutdown();
        AIVERSE_TEST_CHECK(portable.textures.empty());
    }
    runtime.shutdown();
    AIVERSE_TEST_CHECK(!runtime.ready() && device.textures.empty());

    bool rejected=false;
    try { (void)runtime.update(1,{0,0,0}); }
    catch(const std::logic_error&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
}
