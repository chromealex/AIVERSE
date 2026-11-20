#include "engine/render/GiGpuDynamicRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace aiverse::render;
struct GpuRuntimeFake : FakeDevice {
    std::vector<BufferReadFn> callbacks;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t n,BufferReadFn f) override {
        if(!inFrame || inCompute || !buffers.contains(h.id) || n!=4)return false;
        callbacks.push_back(std::move(f));
        return true;
    }
    void finish() {
        AIVERSE_TEST_CHECK(!callbacks.empty());
        auto callback=std::move(callbacks.front());
        callbacks.erase(callbacks.begin());
        constexpr uint32_t marker=0x47494631u;
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(),&marker,4);
        callback(std::move(bytes));
    }
};
static std::shared_ptr<const GiChunkedGeometrySnapshot> scene(bool wall) {
    GiChunkedGeometryBuilder builder;
    if(wall) {
        builder.replace(1,{
            {{-4,-4,2},{4,-4,2},{4,4,2},0},
            {{-4,-4,2},{4,4,2},{-4,4,2},0}
        },{{1,0,0}});
    }
    return std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
}
int main() {
    GpuRuntimeFake device;
    device.info_.backend=gfx::Backend::Metal;
    device.computeEnabled=true;
    GiGpuDynamicRuntime runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{0,0,0},1,4,1,1,16,6));
    AIVERSE_TEST_CHECK(runtime.ready());
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==4);
    auto walled=scene(true);
    AIVERSE_TEST_CHECK(runtime.setGeometry(walled,1));
    AIVERSE_TEST_CHECK(!runtime.setGeometry(walled,1));
    AIVERSE_TEST_CHECK(!runtime.setGeometry(walled,0));
    AIVERSE_TEST_CHECK(runtime.gpuPages().pageCount()==1);
    AIVERSE_TEST_CHECK(runtime.lastGpuPageStats().added==1);
    AIVERSE_TEST_CHECK(runtime.backend().nodeCount()>=
                       GiGpuPagedBvhArena::kTlasCapacity+1);
    AIVERSE_TEST_CHECK(runtime.setLights({
        GiMovingLight{7,{0,0,0},{1,0,0},4,20}
    }));

    device.beginFrame(400,300);
    auto frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted);
    AIVERSE_TEST_CHECK(frame.submitted.fenceQueued);
    AIVERSE_TEST_CHECK(frame.submitted.probes==4);
    AIVERSE_TEST_CHECK(!frame.safeForPresentation);
    AIVERSE_TEST_CHECK(frame.inFlight==4);
    AIVERSE_TEST_CHECK(runtime.busy());
    // A changed scene/light set must wait for a real GPU fence. But a game
    // republishes the same immutable light snapshot every frame: this is
    // idempotent and MUST NOT fail or hide existing GI.
    AIVERSE_TEST_CHECK(!runtime.setGeometry(scene(false),2));
    AIVERSE_TEST_CHECK(runtime.setLights({
        GiMovingLight{7,{0,0,0},{1,0,0},4,20}}));
    AIVERSE_TEST_CHECK(!runtime.setLights({}));
    device.endFrame();
    device.finish();

    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.completed.accepted==4);
    AIVERSE_TEST_CHECK(frame.completed.stale==0);
    AIVERSE_TEST_CHECK(frame.safeForPresentation);
    AIVERSE_TEST_CHECK(!runtime.busy());
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==0);
    device.endFrame();

    // A lighting/world event can invalidate the completed GPU atlas without
    // changing geometry. Only the dirty probe is rescheduled; no old radiance
    // can be exposed before the matching completion callback is polled.
    GiWorldDirtyAccumulator worldChanges;
    worldChanges.submit({GiDirtyReason::Lighting,
                         {{.5f,.5f,.5f},{.5f,.5f,.5f}}});
    const auto changed=runtime.applyWorldChanges(worldChanges);
    AIVERSE_TEST_CHECK(changed.events==1 && changed.probeMarks==1);
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==1);
    device.beginFrame(400,300);
    frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted && frame.submitted.probes==1);
    AIVERSE_TEST_CHECK(!frame.safeForPresentation);
    device.endFrame();
    device.finish();
    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.completed.accepted==1 && frame.safeForPresentation);
    device.endFrame();

    // The sun/sky changes while Metal (or Vulkan) computes an old revision.
    // The completed old frame must be rejected, then the new environment
    // sampled only after the next genuine fence/callback is accepted.
    GiEnvironmentLight firstSky;
    firstSky.sunColor={1,0,0};
    firstSky.sunIntensity=2;
    AIVERSE_TEST_CHECK(runtime.setEnvironment(firstSky));
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==4);
    device.beginFrame(400,300);
    frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted && !frame.safeForPresentation);
    GiEnvironmentLight nextSky=firstSky;
    nextSky.sunIntensity=0;
    nextSky.skyColor={0,0,1};
    nextSky.skyIntensity=3;
    AIVERSE_TEST_CHECK(runtime.setEnvironment(nextSky));
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==4);
    device.endFrame();
    device.finish();
    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.completed.stale==4);
    AIVERSE_TEST_CHECK(!frame.safeForPresentation);
    frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted);
    device.endFrame();
    device.finish();
    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.completed.accepted==4 && frame.safeForPresentation);
    device.endFrame();

    auto empty=scene(false);
    const GiBounds edit{{0,-4,2},{4,4,2}};
    AIVERSE_TEST_CHECK(runtime.setGeometryRegion(empty,2,edit));
    AIVERSE_TEST_CHECK(!runtime.setGeometryRegion(empty,1,edit));
    AIVERSE_TEST_CHECK(!runtime.setGeometryRegion(empty,2,edit));
    AIVERSE_TEST_CHECK(runtime.gpuPages().pageCount()==0);
    AIVERSE_TEST_CHECK(runtime.lastGpuPageStats().removed==1);
    AIVERSE_TEST_CHECK(runtime.pendingProbes()>0);
    AIVERSE_TEST_CHECK(!runtime.setGeometryRegion(walled,3,{{3,0,0},{1,0,0}}));
    device.beginFrame(400,300);
    frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted);
    AIVERSE_TEST_CHECK(!frame.safeForPresentation);
    device.endFrame();
    device.finish();
    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.safeForPresentation);
    device.endFrame();

    // No-op input must not trigger arbitrary work.
    AIVERSE_TEST_CHECK(runtime.setLights({GiMovingLight{7,{0,0,0},{1,0,0},4,20}}));
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==0);
    // Same energy and position, DIFFERENT stable ECS source identity:
    // deletion/replacement cannot be treated as an idempotent light snapshot.
    AIVERSE_TEST_CHECK(runtime.setLights({GiMovingLight{8,{0,0,0},{1,0,0},4,20}}));
    AIVERSE_TEST_CHECK(runtime.pendingProbes()>0);
    AIVERSE_TEST_CHECK(runtime.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
    return 0;
}
