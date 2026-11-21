#include "engine/render/GiGpuChunkBvhCache.h"
#include "engine/render/GiGpuSmallScene.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static std::vector<StaticShadowGeometry::Triangle> wall(float x,float z) {
    return {{{x-1,-1,z},{x+1,-1,z},{x+1,1,z},0},
            {{x-1,-1,z},{x+1,1,z},{x-1,1,z},0}};
}
int main() {
    GiChunkedGeometryBuilder builder;
    for(uint64_t i=0;i<60;++i) {
        builder.replace(i,wall(float(i)*12.f,2),{{float(i%4)/4.f,.4f,.6f}});
    }
    auto first=builder.publish();
    GiGpuChunkBvhCache cache;
    const auto cold=cache.refresh(first);
    AIVERSE_TEST_CHECK(cold.rebuilt==60 && cold.reused==0);
    AIVERSE_TEST_CHECK(cold.triangles==120 && cache.chunkCount()==60);
    const GiGpuBvh* saved=cache.chunkBvh(40);
    AIVERSE_TEST_CHECK(saved && saved->nodes.size()>0);
    auto packet=cache.pack();
    AIVERSE_TEST_CHECK(packet.triangles.size()==120);
    AIVERSE_TEST_CHECK(packet.nodes.size()>cache.chunkCount());
    AIVERSE_TEST_CHECK(packet.nodes[0].meta[3]==0);

    // One ray per chunk must traverse its own BLAS and prune distant chunks.
    for(uint64_t i=0;i<60;++i) {
        const float3 o{float(i)*12.f,0,0};
        float3 n{},c{};
        const auto reference=first.distance(o,{0,0,1},10,&n,&c);
        const auto bvh=packet.trace(o,{0,0,1},10);
        AIVERSE_TEST_CHECK(std::abs(reference-bvh.distance)<1e-5f);
        AIVERSE_TEST_CHECK(std::abs(bvh.albedo.x-c.x)<1e-5f);
        AIVERSE_TEST_CHECK(std::abs(bvh.albedo.y-c.y)<1e-5f);
        AIVERSE_TEST_CHECK(bvh.trianglesVisited<12);
    }

    // A no-op publication shares all cached BLAS allocations.
    const auto warm=cache.refresh(first);
    AIVERSE_TEST_CHECK(warm.rebuilt==0 && warm.reused==60);
    AIVERSE_TEST_CHECK(cache.chunkBvh(40)==saved);

    // Rebuilding exactly one source chunk must not rebuild the other 59 BLAS.
    builder.replace(7,wall(84,4),{{1,0,0}});
    const auto second=builder.publish();
    const auto changed=cache.refresh(second);
    AIVERSE_TEST_CHECK(changed.rebuilt==1 && changed.reused==59);
    AIVERSE_TEST_CHECK(cache.chunkBvh(40)==saved);
    packet=cache.pack();
    AIVERSE_TEST_CHECK(std::abs(packet.trace({84,0,0},{0,0,1},10).distance-4)<1e-5f);
    AIVERSE_TEST_CHECK(std::abs(first.distance({84,0,0},{0,0,1},10)-2)<1e-5f);

    builder.remove(7);
    const auto third=builder.publish();
    const auto removed=cache.refresh(third);
    AIVERSE_TEST_CHECK(removed.removed==1 && removed.rebuilt==0 && removed.reused==59);
    packet=cache.pack();
    AIVERSE_TEST_CHECK(packet.trace({84,0,0},{0,0,1},10).distance==10);

    // Even though cache builds per chunk, GPU upload uses a verified combined
    // TLAS/BLAS packet (not an unimplemented sparse GPU-write claim).
    struct MetalFake : FakeDevice {
        bool supports(gfx::PixelFormat f) const override {
            return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
        }
    } fake;
    fake.info_.backend=gfx::Backend::Metal;
    fake.computeEnabled=true;
    GiGpuSmallScene gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(fake,8));
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(packet,3));
    AIVERSE_TEST_CHECK(gpu.triangleCount()==118);
    AIVERSE_TEST_CHECK(gpu.nodeCount()==packet.nodes.size());
    AIVERSE_TEST_CHECK(!fake.buffers.empty());
    const uint32_t afterColdUpload=fake.next;
    // Adding one chunk while staying inside a grow-only storage capacity
    // must preserve GPU buffer handles. Per-chunk cached BVHs are shared;
    // only TLAS/BLAS spans with differing bytes are updated.
    const auto priorUpload=gpu.lastGeometryUpload();
    AIVERSE_TEST_CHECK(priorUpload.bytes>0);
    auto packetAgain=cache.pack();
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(packetAgain,4));
    AIVERSE_TEST_CHECK(fake.next==afterColdUpload);
    AIVERSE_TEST_CHECK(gpu.lastGeometryUpload().bytes==0);
    AIVERSE_TEST_CHECK(gpu.lastGeometryUpload().regions==0);

    // Re-admit the original chunk after removal (118->120 triangles),
    // still within the 128-element resident capacity. The older backend
    // reallocated on ANY changed triangle count; the new one must not.
    cache.refresh(first);
    packetAgain=cache.pack();
    AIVERSE_TEST_CHECK(packetAgain.triangles.size()==120);
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(packetAgain,5));
    AIVERSE_TEST_CHECK(gpu.triangleCount()==120);
    AIVERSE_TEST_CHECK(fake.next==afterColdUpload);
    AIVERSE_TEST_CHECK(!fake.buffers.empty());
    AIVERSE_TEST_CHECK(gpu.lastGeometryUpload().bytes<=
        packetAgain.triangles.size()*sizeof(GiGpuBvhTri)+
        packetAgain.nodes.size()*sizeof(GiGpuBvhNode));

    // One changed chunk reuses all unchanged CPU per-chunk BLAS sources,
    // and all GPU allocation handles while the resident capacities fit.
    cache.refresh(second);
    packetAgain=cache.pack();
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(packetAgain,6));
    AIVERSE_TEST_CHECK(fake.next==afterColdUpload);
    AIVERSE_TEST_CHECK(gpu.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(fake.buffers.empty() && fake.textures.empty());
}
