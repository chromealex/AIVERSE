#include "engine/render/GiWorldPresentationSync.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace aiverse::render;

struct SyncDevice:FakeDevice {
    std::vector<BufferReadFn> callbacks;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t bytes,BufferReadFn fn) override {
        if(!inFrame || inCompute || bytes!=sizeof(uint32_t) || !buffers.contains(h.id))
            return false;
        callbacks.push_back(std::move(fn));
        return true;
    }
    void finish() {
        AIVERSE_TEST_CHECK(!callbacks.empty());
        auto fn=std::move(callbacks.front());
        callbacks.erase(callbacks.begin());
        constexpr uint32_t marker=0x47494631u;
        std::vector<uint8_t> value(sizeof(marker));
        std::memcpy(value.data(),&marker,sizeof(marker));
        fn(std::move(value));
    }
};

static std::vector<StaticShadowGeometry::Triangle> surface(float y) {
    return {{{-3,y,-3},{3,y,-3},{3,y,3},0},
            {{-3,y,-3},{3,y,3},{-3,y,3},0}};
}

int main() {
    GiTransportConfig cfg;
    cfg.origin={-2,-2,-2};
    cfg.spacing=1;
    cfg.nx=4;cfg.ny=1;cfg.nz=1;
    cfg.raysPerProbe=16;

    // GLES3 CPU: geometry and ECS/world events are committed atomically;
    // CPU material/bounce updates are synchronous with bounded trace budget.
    FakeDevice cpuDevice;
    cpuDevice.info_.backend=gfx::Backend::GLES3;
    GiLightingController cpu;
    AIVERSE_TEST_CHECK(cpu.initialize(cpuDevice,cfg));
    AIVERSE_TEST_CHECK(cpu.mode()==GiTransportMode::CpuProbes);
    GiWorldPresentationSync cpuWorld;
    cpuWorld.geometry().replaceChunk(17,surface(-1),{{1,1,1}});
    cpuWorld.submit({GiDirtyReason::Geometry,{{-3,-1,-3},{3,-1,3}}});
    auto first=cpuWorld.publish(cpu);
    AIVERSE_TEST_CHECK(first.geometry.published && first.events.events==1);
    AIVERSE_TEST_CHECK(!first.deferred && first.canTrace);
    AIVERSE_TEST_CHECK(!cpuWorld.pending());
    AIVERSE_TEST_CHECK(cpuWorld.geometry().revision()==1);
    AIVERSE_TEST_CHECK(cpu.updateFrame({0,0,0}).mode==GiTransportMode::CpuProbes);
    cpuWorld.geometry().replaceChunk(17,surface(-2),{{0,1,0}});
    auto revised=cpuWorld.publish(cpu);
    AIVERSE_TEST_CHECK(revised.geometry.published && revised.geometry.revision==2);
    AIVERSE_TEST_CHECK(revised.retainedSnapshots==0);
    AIVERSE_TEST_CHECK(cpu.shutdown());
    AIVERSE_TEST_CHECK(cpuDevice.textures.empty());

    // Metal fake: while an old GPU dispatch is in-flight, do not publish the
    // replacement collider snapshot and do not drain its new dirty events.
    SyncDevice device;
    device.info_.backend=gfx::Backend::Metal;
    device.computeEnabled=true;
    GiLightingController gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(device,cfg));
    AIVERSE_TEST_CHECK(gpu.mode()==GiTransportMode::GpuCompute);
    GiWorldPresentationSync scene;
    scene.geometry().replaceChunk(17,surface(-1),{{1,1,1}});
    auto initial=scene.publish(gpu);
    AIVERSE_TEST_CHECK(initial.geometry.published && initial.geometry.revision==1);
    device.beginFrame(320,200);
    auto work=gpu.updateFrame({0,0,0},4);
    AIVERSE_TEST_CHECK(work.gpu.submitted.submitted && work.gpu.submitted.probes==4);
    scene.geometry().replaceChunk(17,surface(-2),{{0,1,0}});
    scene.submit({GiDirtyReason::Material,{{-3,-2,-3},{3,-2,3}},
                  {{-3,-1,-3},{3,-1,3}},true});
    const auto blocked=scene.publish(gpu);
    AIVERSE_TEST_CHECK(blocked.deferred && !blocked.geometry.published);
    AIVERSE_TEST_CHECK(scene.geometry().hasPendingChanges());
    AIVERSE_TEST_CHECK(scene.pendingRegions()==2); // old and new Y bounds do not overlap
    AIVERSE_TEST_CHECK(scene.geometry().revision()==1); // original still published
    AIVERSE_TEST_CHECK(gpu.presentationBlocked());
    device.endFrame();
    device.finish();

    device.beginFrame(320,200);
    work=gpu.updateFrame({0,0,0},0); // observes actual completed fake callback
    AIVERSE_TEST_CHECK(work.gpu.completed.accepted==4);
    const auto completed=scene.publish(gpu);
    AIVERSE_TEST_CHECK(completed.geometry.published &&
                       completed.geometry.revision==2);
    AIVERSE_TEST_CHECK(completed.events.events==1 && !completed.deferred);
    AIVERSE_TEST_CHECK(!gpu.presentationBlocked());
    AIVERSE_TEST_CHECK(completed.retainedSnapshots==0);
    AIVERSE_TEST_CHECK(!scene.pending());
    work=gpu.updateFrame({0,0,0},4);
    AIVERSE_TEST_CHECK(work.gpu.submitted.submitted && !work.canPresent);
    device.endFrame();
    device.finish();
    device.beginFrame(320,200);
    work=gpu.updateFrame({0,0,0},0);
    AIVERSE_TEST_CHECK(work.gpu.completed.accepted==4 && work.canPresent);
    device.endFrame();
    AIVERSE_TEST_CHECK(gpu.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
