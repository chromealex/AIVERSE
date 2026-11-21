#include "engine/render/GiGpuResidencyPlanner.h"
#include "engine/render/GiMainGpuBridge.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <memory>
#include <vector>

using namespace aiverse::render;

static std::vector<StaticShadowGeometry::Triangle>
plane(float x,float y,unsigned copies) {
    std::vector<StaticShadowGeometry::Triangle> result;
    result.reserve(copies);
    for(unsigned i=0;i<copies;++i) {
        const float z=float(i%11)*.01f;
        result.push_back({{x,y,z},{x+1,y,z},{x,y,z+1},0});
    }
    return result;
}

struct GpuPlanFake : FakeDevice {
    GpuPlanFake(){info_.backend=gfx::Backend::Metal;computeEnabled=true;}
    bool supports(gfx::PixelFormat) const override {return true;}
    uint64_t gpuSubmissionFence() const override {return inFrame?1:0;}
    uint64_t gpuCompletedFence() const override {return 1;}
    bool gpuHealthy() const override {return true;}
    bool computeDispatchSucceeded() const override {return dispatchCount>0;}
};

int main() {
    GiChunkedGeometryBuilder chunks;
    chunks.replace(1,plane(0,0,2),{{1,1,1}});
    // The world has far more triangles than the bounded GPU backend permits,
    // but those at x=2000 cannot be hit by any probe/secondary light ray.
    chunks.replace(2,plane(2000,0,9000),{{.2f,.3f,.4f}});
    auto full=std::make_shared<const GiChunkedGeometrySnapshot>(chunks.publish());
    AIVERSE_TEST_CHECK(full->triangleCount()==9002);
    AIVERSE_TEST_CHECK(full->chunkCount()==2);
    const auto relevant=GiGpuResidencyPlanner::plan(
        *full,{-2,-2,-2},1,4,4,4,8,64);
    AIVERSE_TEST_CHECK(relevant.admitted && relevant.selected);
    AIVERSE_TEST_CHECK(!relevant.exceededBudget);
    AIVERSE_TEST_CHECK(relevant.sourceTriangles==9002);
    AIVERSE_TEST_CHECK(relevant.relevantTriangles==2);
    AIVERSE_TEST_CHECK(relevant.excludedRemoteTriangles==9000);
    AIVERSE_TEST_CHECK(relevant.selected->triangleCount()==2);
    AIVERSE_TEST_CHECK(relevant.selected->chunkCount()==1);
    // The selected scene reuses the EXACT immutable cached chunk BVH.
    AIVERSE_TEST_CHECK(relevant.selected->findChunk(1)==full->findChunk(1));
    AIVERSE_TEST_CHECK(relevant.selected->findChunk(2)==nullptr);

    const auto rejected=GiGpuResidencyPlanner::plan(
        *full,{-2,-2,-2},1,4,4,4,8,64,1);
    AIVERSE_TEST_CHECK(!rejected.admitted && rejected.exceededBudget);
    AIVERSE_TEST_CHECK(!rejected.selected);
    AIVERSE_TEST_CHECK(rejected.relevantTriangles==2);

    // With a light that reaches the far chunk, culling it would leak its
    // shadow: a sufficiently large light-reach policy must refuse the scene.
    const auto wide=GiGpuResidencyPlanner::plan(
        *full,{-2,-2,-2},1,4,4,4,8,2100);
    AIVERSE_TEST_CHECK(!wide.admitted && wide.exceededBudget);
    AIVERSE_TEST_CHECK(wide.relevantTriangles==9002);
    AIVERSE_TEST_CHECK(wide.excludedRemoteChunks==0);

    // Main GPU bridge: the same far-away chunk no longer forces the bounded
    // GPU path off; it simply is not resident.
    GpuPlanFake device;
    GiMainCpuConfig cfg;
    cfg.origin={-2,-2,-2};
    cfg.nx=cfg.ny=cfg.nz=4;
    cfg.samples=16;
    cfg.traceDistance=8;
    AIVERSE_TEST_CHECK(GiMainGpuBridge::planResidency(*full,cfg,64).admitted);
    AIVERSE_TEST_CHECK(!GiMainGpuBridge::planResidency(*full,cfg,2100).admitted);
    GiMainGpuBridge bridge;
    AIVERSE_TEST_CHECK(bridge.initialize(device,cfg,64));
    device.beginFrame(16,16);
    AIVERSE_TEST_CHECK(bridge.publishGeometry(full,1));
    AIVERSE_TEST_CHECK(bridge.residency().admitted);
    AIVERSE_TEST_CHECK(bridge.residency().relevantTriangles==2);
    AIVERSE_TEST_CHECK(bridge.residency().excludedRemoteChunks==1);
    AIVERSE_TEST_CHECK(bridge.pageStats().added==1);
    // A light reaching the excluded chunk would leak its shadow: refused
    // until geometry is republished with that reach (then the far chunk is
    // relevant and the 8192 budget refuses the scene instead).
    AIVERSE_TEST_CHECK(!bridge.publishLights({GiMovingLight{17,{0,0,0},{1,1,1},128,20}}));
    AIVERSE_TEST_CHECK(!bridge.canPresent());
    AIVERSE_TEST_CHECK(bridge.publishLights({GiMovingLight{17,{0,0,0},{1,1,1},16,20}}));
    AIVERSE_TEST_CHECK(bridge.publishEnvironment({}));
    // Editing the far chunk changes no resident page.
    chunks.replace(2,plane(2000,1,9000),{{.2f,.3f,.4f}});
    auto farEdit=std::make_shared<const GiChunkedGeometrySnapshot>(chunks.publish());
    AIVERSE_TEST_CHECK(bridge.publishGeometry(farEdit,2));
    AIVERSE_TEST_CHECK(bridge.pageStats().reused==1 && bridge.pageStats().rewritten==0);
    // Regional publication: converge, then an edit out of every probe's reach
    // dirties nothing, a near edit dirties only part of the volume.
    for(int i=0;i<64 && bridge.pendingProbes();++i)AIVERSE_TEST_CHECK(bridge.updateFrame({0,0,0}));
    bridge.poll();
    AIVERSE_TEST_CHECK(!bridge.pendingProbes() && bridge.canPresent());
    chunks.replace(2,plane(2000,2,9000),{{.2f,.3f,.4f}});
    AIVERSE_TEST_CHECK(bridge.publishGeometryRegion(
        std::make_shared<const GiChunkedGeometrySnapshot>(chunks.publish()),3,
        GiBounds{{2000,0,0},{2001,3,1}}));
    AIVERSE_TEST_CHECK(!bridge.pendingProbes() && bridge.canPresent());
    chunks.replace(1,plane(1,1,2),{{1,1,1}});
    AIVERSE_TEST_CHECK(bridge.publishGeometryRegion(
        std::make_shared<const GiChunkedGeometrySnapshot>(chunks.publish()),4,
        GiBounds{{0,0,0},{2,1,1}}));
    AIVERSE_TEST_CHECK(bridge.pendingProbes()>0 && !bridge.canPresent());
    // A relevant overflow is refused and keeps the previous upload.
    chunks.replace(3,plane(1,0,8192),{{1,0,0}});
    auto nearOverflow=std::make_shared<const GiChunkedGeometrySnapshot>(chunks.publish());
    AIVERSE_TEST_CHECK(!bridge.publishGeometry(nearOverflow,5));
    AIVERSE_TEST_CHECK(bridge.residency().exceededBudget);
    AIVERSE_TEST_CHECK(!bridge.canPresent());
    device.endFrame();
    AIVERSE_TEST_CHECK(bridge.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
