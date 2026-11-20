#include "engine/render/GiGpuBvh.h"
#include "engine/render/GiGpuSmallScene.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static std::vector<StaticShadowGeometry::Triangle> makeWall(float x,float z) {
    return {{{x-1,-1,z},{x+1,-1,z},{x+1,1,z},0},
            {{x-1,-1,z},{x+1,1,z},{x-1,1,z},0}};
}

int main() {
    GiChunkedGeometryBuilder scene;
    // 128 independent chunks, 256 triangles, all at distant X offsets.
    for(uint64_t i=0;i<128;++i) {
        const float x=float(i)*20.0f;
        scene.replace(i,makeWall(x,2),{{float(i%5)/4.f,.6f,.3f}});
    }
    const auto snapshot=scene.publish();
    const GiGpuBvh bvh=GiGpuBvh::build(snapshot);
    AIVERSE_TEST_CHECK(bvh.triangles.size()==256);
    AIVERSE_TEST_CHECK(bvh.nodes.size()>1 && bvh.nodes.size()<256);
    AIVERSE_TEST_CHECK(bvh.nodes.front().meta[3]==0);
    AIVERSE_TEST_CHECK(bvh.nodes.front().meta[0]<bvh.nodes.size());
    AIVERSE_TEST_CHECK(bvh.nodes.front().meta[1]<bvh.nodes.size());
    AIVERSE_TEST_CHECK(sizeof(GiGpuBvhTri)==64 && sizeof(GiGpuBvhNode)==48);

    for(uint64_t i=0;i<128;++i) {
        const float x=float(i)*20.0f;
        const float3 origin{x,0,0}, dir{0,0,1};
        float3 expectNormal{},expectAlbedo{};
        const float expected=snapshot.distance(origin,dir,10,&expectNormal,&expectAlbedo);
        const auto hit=bvh.trace(origin,dir,10);
        AIVERSE_TEST_CHECK(std::abs(hit.distance-expected)<1e-5f);
        AIVERSE_TEST_CHECK(std::abs(hit.albedo.x-expectAlbedo.x)<1e-5f);
        AIVERSE_TEST_CHECK(std::abs(hit.albedo.y-expectAlbedo.y)<1e-5f);
        AIVERSE_TEST_CHECK(hit.trianglesVisited<20);
        AIVERSE_TEST_CHECK(hit.nodesVisited<30);
    }

    const float3 miss{0,0,-1};
    const auto missed=bvh.trace({0,0,0},miss,10);
    AIVERSE_TEST_CHECK(missed.distance==10);

    // BVH construction and ordering must be identical for identical chunks.
    const auto b=GiGpuBvh::build(snapshot);
    AIVERSE_TEST_CHECK(b.nodes.size()==bvh.nodes.size());
    for(size_t i=0;i<b.nodes.size();++i) {
        AIVERSE_TEST_CHECK(b.nodes[i].meta==bvh.nodes[i].meta);
        AIVERSE_TEST_CHECK(b.nodes[i].lo.x==bvh.nodes[i].lo.x);
    }

    // A live GPU dispatch must use its node buffer and report a nonzero node count.
    struct BvhFake : FakeDevice {
        bool supports(gfx::PixelFormat f) const override {
            return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
        }
    } gpu;
    gpu.info_.backend=gfx::Backend::Metal;
    gpu.computeEnabled=true;
    GiGpuSmallScene runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(gpu,16));
    AIVERSE_TEST_CHECK(runtime.setGeometry(snapshot,12));
    AIVERSE_TEST_CHECK(runtime.nodeCount()>0);
    AIVERSE_TEST_CHECK(runtime.triangleCount()==256);
    AIVERSE_TEST_CHECK(runtime.setLights({GiMovingLight{1,{0,0,0},{1,0,0},4,1}}));
    gpu.beginFrame(200,200);
    const auto submit=runtime.dispatch({GiProbeTicket{0,1,1,{0,0,0}}},16,10);
    AIVERSE_TEST_CHECK(submit.submitted && submit.nodes>1);
    AIVERSE_TEST_CHECK(submit.triangles==256);
    AIVERSE_TEST_CHECK(submit.primaryRayBudget==16);
    AIVERSE_TEST_CHECK(gpu.dispatchCount==1);
    gpu.endFrame();
    runtime.shutdown();
    AIVERSE_TEST_CHECK(gpu.buffers.empty() && gpu.textures.empty());

    // Empty BVH must be valid and produce a no-hit query.
    GiChunkedGeometryBuilder emptyBuilder;
    const auto empty=GiGpuBvh::build(emptyBuilder.publish());
    AIVERSE_TEST_CHECK(empty.nodes.empty() && empty.triangles.empty());
    AIVERSE_TEST_CHECK(empty.trace({0,0,0},{0,1,0},9).distance==9);
}
