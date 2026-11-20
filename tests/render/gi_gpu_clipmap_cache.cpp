#include "engine/render/GiGpuClipmapRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace aiverse::render;

struct CachedClipDevice : FakeDevice {
    std::vector<BufferReadFn> completions;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t n,BufferReadFn callback) override {
        if(!inFrame || inCompute || n!=sizeof(uint32_t) || !buffers.contains(h.id))
            return false;
        completions.push_back(std::move(callback));
        return true;
    }
    void finish() {
        AIVERSE_TEST_CHECK(!completions.empty());
        auto callback=std::move(completions.front());
        completions.erase(completions.begin());
        const uint32_t marker=0x47494631u;
        std::vector<uint8_t> bytes(sizeof(marker));
        std::memcpy(bytes.data(),&marker,sizeof(marker));
        callback(std::move(bytes));
    }
};

static std::vector<StaticShadowGeometry::Triangle> wall(float x,float z) {
    return {{{x-2,-2,z},{x+2,-2,z},{x+2,2,z},0},
            {{x-2,-2,z},{x+2,2,z},{x-2,2,z},0}};
}
static std::shared_ptr<const GiChunkedGeometrySnapshot> snapshot(
        GiChunkedGeometryBuilder& scene) {
    return std::make_shared<GiChunkedGeometrySnapshot>(scene.publish());
}

int main() {
    CachedClipDevice device;
    device.computeEnabled=true;
    device.info_.backend=gfx::Backend::Metal;
    GiGpuClipmapRuntime runtime;
    GiGpuChunkBvhCache cache;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{0,0,0},1,64,1,1,
                                           {0,0,0},16,2));

    GiChunkedGeometryBuilder builder;
    builder.replace(1,wall(0,2),{{1,0,0}});
    builder.replace(2,wall(25,2),{{0,0,1}});
    auto first=snapshot(builder);
    GiGpuChunkBvhCache::Stats stats;
    AIVERSE_TEST_CHECK(runtime.setGeometryCached(first,1,cache,&stats));
    AIVERSE_TEST_CHECK(stats.rebuilt==2 && stats.reused==0);
    const auto baseline=runtime.backend().lastGeometryUpload();
    AIVERSE_TEST_CHECK(baseline.full && baseline.bytes>0);
    AIVERSE_TEST_CHECK(runtime.setLights({GiMovingLight{10,{0,0,0},{1,0,0},4,10}}));

    // Complete two batches of 32 physical probes with explicit callbacks.
    for(int pass=0;pass<2;++pass) {
        device.beginFrame(300,200);
        auto step=runtime.updateFrame(32,{0,0,0});
        AIVERSE_TEST_CHECK(step.submitted.submitted && step.submitted.probes==32);
        // After the first completed GPU fence, the R8 validity mask permits
        // the already-computed 32 probes to be presented even while the
        // remaining 32 are in flight. A fresh atlas has none valid yet.
        AIVERSE_TEST_CHECK(runtime.busy());
        AIVERSE_TEST_CHECK(step.safeForPresentation == (pass > 0));
        AIVERSE_TEST_CHECK(runtime.validity().validCount() == size_t(pass)*32u);
        device.endFrame();
        device.finish();
        device.beginFrame(300,200);
        step=runtime.updateFrame(0,{0,0,0});
        AIVERSE_TEST_CHECK(step.completed.accepted==32);
        device.endFrame();
    }
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==0);
    AIVERSE_TEST_CHECK(runtime.validity().validCount()==64);
    AIVERSE_TEST_CHECK(runtime.fullyValid());

    // One changed chunk retains old BLAS for chunk 2 and only uploads changed
    // GPU spans (assuming unchanged node/triangle packet capacities).
    auto saved=cache.chunkBvh(2);
    builder.replace(1,wall(0,4),{{.5f,1,0}});
    auto second=snapshot(builder);
    const GiBounds changed{{-2,-2,2},{2,2,4}};
    AIVERSE_TEST_CHECK(runtime.setGeometryRegionCached(second,2,changed,cache,0,&stats));
    AIVERSE_TEST_CHECK(stats.rebuilt==1 && stats.reused==1);
    AIVERSE_TEST_CHECK(cache.chunkBvh(2)==saved);
    auto partial=runtime.backend().lastGeometryUpload();
    AIVERSE_TEST_CHECK(!partial.full && partial.bytes>0);
    AIVERSE_TEST_CHECK(partial.bytes<baseline.bytes);
    AIVERSE_TEST_CHECK(runtime.pendingProbes()>0 && runtime.pendingProbes()<64);
    const auto dirtyProbes=runtime.pendingProbes();
    // Publication dirties the relevant CPU scheduler probes immediately;
    // the validity mask is resynchronized at the next updateFrame().
    AIVERSE_TEST_CHECK(!runtime.hasRenderableProbes());

    device.beginFrame(300,200);
    auto pending=runtime.updateFrame(16,{0,0,0});
    AIVERSE_TEST_CHECK(pending.submitted.submitted && runtime.busy());
    AIVERSE_TEST_CHECK(pending.safeForPresentation);
    AIVERSE_TEST_CHECK(runtime.validity().validCount()==64-dirtyProbes);
    AIVERSE_TEST_CHECK(!runtime.fullyValid());
    AIVERSE_TEST_CHECK(!runtime.setGeometryRegionCached(first,3,changed,cache));
    device.endFrame();
    device.finish();
    device.beginFrame(300,200);
    pending=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(pending.completed.accepted>0);
    device.endFrame();
    // Complete remaining dirty probes; the previously valid portion stays
    // renderable through the R8 mask while the new batch runs.
    while(runtime.pendingProbes()) {
        device.beginFrame(300,200);
        auto job=runtime.updateFrame(16,{0,0,0});
        AIVERSE_TEST_CHECK(job.submitted.submitted);
        device.endFrame();
        device.finish();
        device.beginFrame(300,200);
        job=runtime.updateFrame(0,{0,0,0});
        AIVERSE_TEST_CHECK(job.completed.accepted>0);
        device.endFrame();
    }
    AIVERSE_TEST_CHECK(runtime.fullyValid());
    AIVERSE_TEST_CHECK(runtime.validity().validCount()==64);
    AIVERSE_TEST_CHECK(runtime.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
