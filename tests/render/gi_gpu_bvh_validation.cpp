#include "engine/render/GiGpuBvhValidator.h"
#include "engine/render/GiGpuPagedBvhArena.h"
#include "engine/render/GiGpuSmallScene.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace aiverse::render;

static GiChunkedGeometrySnapshot scene(uint32_t count) {
    GiChunkedGeometryBuilder builder;
    std::vector<StaticShadowGeometry::Triangle> tris;
    for(uint32_t i=0;i<count;++i) {
        const float x=float(i)*.5f;
        tris.push_back({{x,-1,2},{x+1,-1,2},{x,1,2},0});
    }
    builder.replace(7,std::move(tris),{{1,.3f,.2f}});
    return builder.publish();
}
struct ComputeFake:FakeDevice {
    bool supports(gfx::PixelFormat format) const override {
        return format==gfx::PixelFormat::RGBA16F ||
               FakeDevice::supports(format);
    }
};

int main() {
    GiGpuBvh empty;
    AIVERSE_TEST_CHECK(giValidateGpuBvh(empty).valid);
    empty.nodes.push_back({});
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(empty).valid);

    const auto snapshot=scene(8);
    const auto good=GiGpuBvh::build(snapshot);
    const auto checked=giValidateGpuBvh(good);
    AIVERSE_TEST_CHECK(checked.valid);
    AIVERSE_TEST_CHECK(checked.reachableNodes>1);
    AIVERSE_TEST_CHECK(checked.referencedTriangles==8);
    AIVERSE_TEST_CHECK(checked.maxDepth<=GiGpuBvh::kGpuStackCapacity);

    auto corrupt=good;
    corrupt.nodes[0].meta[0]=uint32_t(corrupt.nodes.size()+1);
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);
    corrupt=good;
    corrupt.nodes[0].meta[0]=0;  // immediate parent-to-self cycle
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);
    corrupt=good;
    // Child indices can be legal yet the parent box may no longer contain
    // its ray-visible descendants: reject this to prevent false occlusion.
    corrupt.nodes[0].hi.x=corrupt.nodes[0].lo.x;
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);
    corrupt=good;
    corrupt.nodes[0].lo.x=std::numeric_limits<float>::quiet_NaN();
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);
    corrupt=good;
    for(auto& node:corrupt.nodes)if(node.meta[3]) {
        node.meta[2]=uint32_t(corrupt.triangles.size());
        break;
    }
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);
    corrupt=good;
    corrupt.triangles[0].a.x=100000; // leaf's AABB no longer covers its vertex
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);
    corrupt=good;
    corrupt.triangles[0].albedo.x=-1;
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(corrupt).valid);

    // A chain 65 levels deep would overflow the shared Metal/Vulkan shader
    // BVH traversal stack of 64 entries; reject it before GPU upload.
    GiGpuBvh deep;
    deep.triangles.push_back(good.triangles[0]);
    deep.nodes.resize(GiGpuBvh::kGpuStackCapacity+1);
    for(size_t i=0;i<deep.nodes.size();++i) {
        auto& node=deep.nodes[i];
        node.lo={-1,-1,1,0};node.hi={8,1,3,0};
        node.meta=i+1<deep.nodes.size()
            ? std::array<uint32_t,4>{uint32_t(i+1),UINT32_MAX,0,0}
            : std::array<uint32_t,4>{0,0,0,1};
    }
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(deep).valid);

    // An immutable paged TLAS has reserved/unreachable node and triangle
    // holes. Validate from node[0] only; those intentionally unused storage
    // elements must NOT cause a false rejection.
    GiGpuChunkBvhCache cache;
    AIVERSE_TEST_CHECK(cache.refresh(snapshot).rebuilt==1);
    GiGpuPagedBvhArena pages;
    auto page=pages.plan(snapshot,cache);
    AIVERSE_TEST_CHECK(page.accepted);
    AIVERSE_TEST_CHECK(giValidateGpuBvh(page.packet).valid);
    pages.commit(std::move(page));

    // Invalid packets cannot mutate the underlying RHI storage, previously
    // accepted layout, GPU geometry revision, or scene triangle counts.
    ComputeFake device;
    device.computeEnabled=true;
    device.info_.backend=gfx::Backend::Metal;
    GiGpuSmallScene gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(device,16));
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(good,1));
    const uint32_t handleSeed=device.next;
    const auto triangleCount=gpu.triangleCount();
    corrupt=good;
    corrupt.nodes[0].meta[0]=0;
    AIVERSE_TEST_CHECK(!gpu.setPackedGeometry(corrupt,2));
    AIVERSE_TEST_CHECK(device.next==handleSeed);
    AIVERSE_TEST_CHECK(gpu.geometryRevision()==1);
    AIVERSE_TEST_CHECK(gpu.triangleCount()==triangleCount);
    AIVERSE_TEST_CHECK(!gpu.setPackedGeometry(good,1)); // duplicated revision
    AIVERSE_TEST_CHECK(!gpu.setPackedGeometry(good,0)); // invalid revision
    AIVERSE_TEST_CHECK(gpu.setPackedGeometry(good,2));  // normal recovery
    gpu.shutdown();
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
