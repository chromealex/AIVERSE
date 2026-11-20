#include "engine/render/GiLightingController.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using namespace aiverse::render;

struct GiBackendDevice : FakeDevice {
    bool halfFloatSupported=true;
    bool supports(gfx::PixelFormat format) const override {
        if(format==gfx::PixelFormat::RGBA16F)return halfFloatSupported;
        return FakeDevice::supports(format);
    }
};

static std::shared_ptr<const GiChunkedGeometrySnapshot> wall() {
    GiChunkedGeometryBuilder builder;
    builder.replace(3, {
        {{-4,-4,2},{4,-4,2},{4,4,2},0},
        {{-4,-4,2},{4,4,2},{-4,4,2},0}
    }, {{1,0,0}});
    return std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
}

int main() {
    GiBackendDevice device;
    device.info_.backend=gfx::Backend::GLES3;
    device.computeEnabled=true; // GLES3 contract still has NO compute requirement.
    GiTransportConfig cfg;
    cfg.origin={-2,-2,-2};
    cfg.nx=4;cfg.ny=1;cfg.nz=1;
    cfg.raysPerProbe=16;
    cfg.cpuMaxProbes=2;

    GiLightingController controller;
    AIVERSE_TEST_CHECK(controller.initialize(device,cfg));
    AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::CpuProbes);
    AIVERSE_TEST_CHECK(controller.maxMovingLights()==64);
    AIVERSE_TEST_CHECK(controller.cpuRuntime()!=nullptr);
    AIVERSE_TEST_CHECK(controller.gpuRuntime()==nullptr);
    auto scene=wall();
    AIVERSE_TEST_CHECK(controller.setGeometry(scene,1));
    GiEnvironmentLight sunSky;
    sunSky.sunColor={1,0,0};sunSky.sunIntensity=2;
    sunSky.skyColor={0,0,1};sunSky.skyIntensity=.25f;
    AIVERSE_TEST_CHECK(controller.setEnvironment(sunSky));
    AIVERSE_TEST_CHECK(controller.cpuRuntime()->reference().environment()==sunSky);
    AIVERSE_TEST_CHECK(!controller.setGeometry(scene,1)); // monotonic revisions
    PointLight authored{{0,0,0},5,{1,0,0},10};
    authored.key=7;
    GiLightBridge::Stats authoredStats;
    // Indirect is explicitly disabled for ordinary realtime/baked lights.
    AIVERSE_TEST_CHECK(controller.setAuthoredLights({authored},3,{0,0,0},
        GiLightOverflowPolicy::Reject,&authoredStats));
    AIVERSE_TEST_CHECK(authoredStats.enabled==0);
    authored.indirectMultiplier=.5f;
    authored.makeSpot({0,0,1},60,.4f);
    AIVERSE_TEST_CHECK(controller.setAuthoredLights({authored},3,{0,0,0},
        GiLightOverflowPolicy::Reject,&authoredStats));
    AIVERSE_TEST_CHECK(authoredStats.enabled==1 && authoredStats.output==1);
    // The real Renderer3D includes an incomplete GPU cache owner and has
    // out-of-line implementation dependencies; this standalone/header-only
    // fixture verifies the read-only LightSystem API contract at compile time
    // without instantiating a complete renderer or linking the runtime.
    static_assert(std::is_same_v<
        decltype(std::declval<const LightSystem&>().submittedLights()),
        const std::vector<PointLight>&>);
    AIVERSE_TEST_CHECK(controller.setLights({GiMovingLight{7,{0,0,0},{1,0,0},5,10}}));
    const auto step=controller.updateFrame({0,0,0});
    AIVERSE_TEST_CHECK(step.mode==GiTransportMode::CpuProbes);
    AIVERSE_TEST_CHECK(step.cpu.trace.claimed<=2 && step.canPresent);
    AIVERSE_TEST_CHECK(controller.setLights({}));
    AIVERSE_TEST_CHECK(controller.updateFrame({0,0,0}).cpu.upload.probes>=1);
    GiWorldDirtyAccumulator changes;
    changes.submit({GiDirtyReason::Material,
                    {{-.5f,-1.5f,-1.5f},{-.5f,-1.5f,-1.5f}}});
    const auto dirtyStats=controller.applyWorldChanges(changes);
    AIVERSE_TEST_CHECK(dirtyStats.events==1 && dirtyStats.probeMarks==1);
    AIVERSE_TEST_CHECK(controller.updateFrame({0,0,0}).cpu.upload.probes>=1);
    AIVERSE_TEST_CHECK(controller.shutdown());
    AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::Off);
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());

    // Metal and Vulkan use compute only when the concrete device reports
    // the storage/image/queue support; no GPU execution is proved by this fake.
    for(const auto backend:{gfx::Backend::Metal,gfx::Backend::Vulkan}) {
        device.info_.backend=backend;
        device.computeEnabled=true;
        device.halfFloatSupported=true;
        AIVERSE_TEST_CHECK(controller.initialize(device,cfg));
        AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::GpuCompute);
        AIVERSE_TEST_CHECK(controller.maxMovingLights()==GiGpuSmallScene::kMaxLights);
        AIVERSE_TEST_CHECK(controller.gpuRuntime()!=nullptr);
        AIVERSE_TEST_CHECK(controller.setGeometry(scene,2));
        std::vector<GiMovingLight> tooMany(17);
        AIVERSE_TEST_CHECK(!controller.setLights(tooMany));
        AIVERSE_TEST_CHECK(controller.presentationBlocked());
        AIVERSE_TEST_CHECK(!controller.updateFrame({0,0,0},0).canPresent);
        AIVERSE_TEST_CHECK(controller.setLights({
            GiMovingLight{7,{0,0,0},{1,0,0},4,10}}));
        AIVERSE_TEST_CHECK(!controller.presentationBlocked());

        // A changed light beyond the current local GPU BVH reach cannot
        // reuse the old indirect result. A smaller accepted source restores
        // only the light gate, while normal dirty probes still gate pixels.
        AIVERSE_TEST_CHECK(!controller.setLights({
            GiMovingLight{7,{0,0,0},{1,0,0},128,10}}));
        AIVERSE_TEST_CHECK(controller.presentationBlocked());
        AIVERSE_TEST_CHECK(controller.setLights({
            GiMovingLight{7,{0,0,0},{1,0,0},4,10}}));
        AIVERSE_TEST_CHECK(!controller.presentationBlocked());

        GiEnvironmentLight invalidEnvironment;
        invalidEnvironment.skyIntensity=-1;
        AIVERSE_TEST_CHECK(!controller.setEnvironment(invalidEnvironment));
        AIVERSE_TEST_CHECK(controller.presentationBlocked());
        AIVERSE_TEST_CHECK(controller.setEnvironment(GiEnvironmentLight{}));
        AIVERSE_TEST_CHECK(!controller.presentationBlocked());
        AIVERSE_TEST_CHECK(controller.shutdown());
        AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());

        // No writable RGBA16F, or no compute: same CPU mode remains usable.
        device.halfFloatSupported=false;
        AIVERSE_TEST_CHECK(controller.initialize(device,cfg));
        AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::CpuProbes);
        AIVERSE_TEST_CHECK(controller.shutdown());
        device.halfFloatSupported=true;
        device.computeEnabled=false;
        AIVERSE_TEST_CHECK(controller.initialize(device,cfg));
        AIVERSE_TEST_CHECK(controller.mode()==GiTransportMode::CpuProbes);
        AIVERSE_TEST_CHECK(controller.shutdown());
    }

    cfg.cpuMaxProbes=0;
    AIVERSE_TEST_CHECK(!controller.initialize(device,cfg));
    AIVERSE_TEST_CHECK(!controller.ready());
    return 0;
}
