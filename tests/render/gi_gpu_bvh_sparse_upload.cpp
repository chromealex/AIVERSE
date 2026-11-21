#include "engine/render/GiGpuChunkBvhCache.h"
#include "engine/render/GiGpuSmallScene.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace aiverse::render;

struct UploadDevice : FakeDevice {
    struct Write {uint32_t buffer=0;size_t offset=0,bytes=0;};
    std::vector<Write> writes;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    void updateBuffer(gfx::BufferHandle h,const void* data,size_t size,size_t offset=0) override {
        FakeDevice::updateBuffer(h,data,size,offset);
        writes.push_back({h.id,offset,size});
    }
};
static std::vector<StaticShadowGeometry::Triangle> wall(float x,float height) {
    return {{{x-1,-1,height},{x+1,-1,height},{x+1,1,height},0},
            {{x-1,-1,height},{x+1,1,height},{x-1,1,height},0}};
}

int main() {
    GiChunkedGeometryBuilder source;
    for(uint64_t i=0;i<64;++i)
        source.replace(i,wall(float(i)*12.f,2),{{.5f,.5f,.5f}});
    GiGpuChunkBvhCache cache;
    auto first=source.publish();
    const auto cold=cache.refresh(first);
    AIVERSE_TEST_CHECK(cold.rebuilt==64);
    auto packet=cache.pack();
    UploadDevice device;
    device.info_.backend=gfx::Backend::Metal;
    device.computeEnabled=true;
    GiGpuSmallScene gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(device,32));
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(packet,1));
    const auto baseline=gpu.lastGeometryUpload();
    AIVERSE_TEST_CHECK(baseline.full && baseline.bytes>0);
    const size_t originalHandles=device.buffers.size();
    // triangles, nodes, lights, jobs + the two grown-out dummy buffers, which
    // main retires through GPU fences instead of freeing them immediately.
    AIVERSE_TEST_CHECK(originalHandles==6);

    source.replace(7,wall(84,4),{{1,0,0}});
    auto second=source.publish();
    const auto stats=cache.refresh(second);
    AIVERSE_TEST_CHECK(stats.rebuilt==1 && stats.reused==63);
    auto changed=cache.pack();
    device.writes.clear();
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(changed,2));
    const auto sparse=gpu.lastGeometryUpload();
    AIVERSE_TEST_CHECK(!sparse.full);
    AIVERSE_TEST_CHECK(sparse.regions>=1);
    AIVERSE_TEST_CHECK(sparse.bytes>0 && sparse.bytes<baseline.bytes/2);
    AIVERSE_TEST_CHECK(device.buffers.size()==originalHandles);
    size_t sum=0;
    for(const auto& write:device.writes) {
        AIVERSE_TEST_CHECK(write.bytes>0);
        AIVERSE_TEST_CHECK(write.offset+write.bytes<=device.buffers.at(write.buffer).size());
        sum+=write.bytes;
    }
    AIVERSE_TEST_CHECK(sum==sparse.bytes);

    // Same packet with a new scene revision must incur zero GPU geometry writes.
    device.writes.clear();
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(changed,3));
    AIVERSE_TEST_CHECK(gpu.lastGeometryUpload().bytes==0);
    AIVERSE_TEST_CHECK(gpu.lastGeometryUpload().regions==0);
    AIVERSE_TEST_CHECK(device.writes.empty());

    // Removing one chunk changes packet capacity: safe full-buffer fallback.
    source.remove(7);
    auto third=source.publish();
    cache.refresh(third);
    auto smaller=cache.pack();
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(smaller,4));
    AIVERSE_TEST_CHECK(gpu.lastGeometryUpload().full);
    AIVERSE_TEST_CHECK(gpu.triangleCount()==126);
    AIVERSE_TEST_CHECK(gpu.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
