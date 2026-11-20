#include "engine/render/GiGpuResidencyPlanner.h"
#include "engine/render/GiLightingController.h"
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
    bool supports(gfx::PixelFormat format) const override {
        return format==gfx::PixelFormat::RGBA16F || FakeDevice::supports(format);
    }
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

    GpuPlanFake device;
    device.info_.backend=gfx::Backend::Vulkan;
    device.computeEnabled=true;
    GiTransportConfig settings;
    settings.origin={-2,-2,-2};
    settings.nx=settings.ny=settings.nz=4;
    settings.raysPerProbe=16;
    settings.traceDistance=8;
    settings.maxGpuMovingLightRadius=64;
    GiLightingController controller;
    AIVERSE_TEST_CHECK(controller.initialize(device,settings));
    AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::GpuCompute);
    AIVERSE_TEST_CHECK(controller.setGeometry(full,1));
    AIVERSE_TEST_CHECK(controller.gpuResidency().selected->triangleCount()==2);
    // Source light radius must be within the acknowledged reach of the local
    // resident BVH; otherwise the controller rejects it instead of leaking.
    AIVERSE_TEST_CHECK(!controller.setLights({
        GiMovingLight{17,{0,0,0},{1,1,1},128,20}}));
    AIVERSE_TEST_CHECK(controller.setLights({
        GiMovingLight{17,{0,0,0},{1,1,1},16,20}}));

    // Relevant-budget refusal leaves previous published geometry intact.
    chunks.replace(3,plane(1,0,8192),{{1,0,0}});
    auto nearOverflow=std::make_shared<const GiChunkedGeometrySnapshot>(
        chunks.publish());
    AIVERSE_TEST_CHECK(!controller.setGeometry(nearOverflow,2));
    AIVERSE_TEST_CHECK(controller.gpuResidency().exceededBudget);
    AIVERSE_TEST_CHECK(!controller.gpuResidency().selected);
    AIVERSE_TEST_CHECK(controller.gpuRuntime()->backend().triangleCount()==2);
    AIVERSE_TEST_CHECK(controller.presentationBlocked());
    // Even after old probes completed, an authoritatively newer geometry
    // revision has not yet been accepted. No stale indirect frame is valid.
    AIVERSE_TEST_CHECK(!controller.updateFrame({0,0,0},0).canPresent);
    AIVERSE_TEST_CHECK(controller.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());

    // Explicit opt-in CPU fallback admits the SAME full authoritative scene
    // without dropping nearby geometry just to satisfy the 8192 GPU cap.
    settings.cpuFallbackOnLocalGpuOverflow=true;
    AIVERSE_TEST_CHECK(controller.initialize(device,settings));
    AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::GpuCompute);
    AIVERSE_TEST_CHECK(controller.setGeometry(full,1));
    AIVERSE_TEST_CHECK(controller.setLights({
        GiMovingLight{17,{0,0,0},{1,1,1},16,20}}));
    GiEnvironmentLight sky;
    sky.skyColor={0,0,1};sky.skyIntensity=1;
    AIVERSE_TEST_CHECK(controller.setEnvironment(sky));
    AIVERSE_TEST_CHECK(controller.setGeometry(nearOverflow,2));
    AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::CpuProbes);
    AIVERSE_TEST_CHECK(controller.cpuRuntime()!=nullptr);
    AIVERSE_TEST_CHECK(controller.gpuRuntime()==nullptr);
    AIVERSE_TEST_CHECK(controller.gpuResidency().exceededBudget);
    AIVERSE_TEST_CHECK(!controller.presentationBlocked());
    AIVERSE_TEST_CHECK(controller.cpuRuntime()->reference().environment()==sky);
    AIVERSE_TEST_CHECK(controller.cpuRuntime()->reference().scheduler().pending()>0);
    // The CPU path traces the COMPLETE snapshot, including nearOverflow,
    // under the ordinary per-frame bounded CPU probe budget.
    const auto cpuFrame=controller.updateFrame({0,0,0});
    AIVERSE_TEST_CHECK(cpuFrame.mode==GiTransportMode::CpuProbes);
    AIVERSE_TEST_CHECK(cpuFrame.cpu.trace.claimed<=settings.cpuMaxProbes);
    AIVERSE_TEST_CHECK(controller.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
