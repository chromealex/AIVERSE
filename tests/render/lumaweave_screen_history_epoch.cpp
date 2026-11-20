// CPU-only FakeDevice regression for in-flight Metal temporal history
// invalidation. Confirms a delivered success fence cannot revive a history
// image after GPU route deactivation; NOT a real GPU pixel test.
#include "engine/render/LumaWeaveScreenMetal.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"

using namespace aiverse::render;

struct FakeMetalScreen : FakeDevice {
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F ||
               FakeDevice::supports(f);
    }
};

int main() {
    FakeMetalScreen dev;
    dev.info_.backend=gfx::Backend::Metal;
    dev.computeEnabled=true;
    LumaWeaveScreenMetal screen;
    LumaScreenSettings settings;
    settings.maxPixels=64;
    settings.maxSamples=1;
    AIVERSE_TEST_CHECK(screen.initialize(dev,settings));
    LumaWeaveRegistry registry;
    LumaLight l;
    l.key=42;
    l.position={0,2,3};
    l.radius=10;
    registry.publish({l});
    LumaWeaveClusters clusters;
    LumaClusterConfig cfg;
    cfg.nx=cfg.ny=cfg.nz=1;
    cfg.origin={-5,-5,-5};
    (void)clusters.rebuild(registry,cfg);
    AIVERSE_TEST_CHECK(screen.setSources(packLumaSources(registry,clusters)));
    auto bvh=GiGpuBvh::fromPackedTriangles({
        {{0,0,2,0},{2,0,2,0},{0,2,2,0},{1,1,1,0}}
    });
    AIVERSE_TEST_CHECK(screen.setGeometry(bvh,1));
    AIVERSE_TEST_CHECK(screen.resize(8,8));
    const gfx::TextureDesc depthDesc{
        .width=64,.height=64,.format=gfx::PixelFormat::Depth
    };
    const auto depth=dev.createTexture(depthDesc);

    dev.beginFrame(64,64);
    const auto frame1=screen.dispatch(depth,64,64,mat4{},mat4{},
                                       {0,0,0},1,7);
    AIVERSE_TEST_CHECK(frame1.encoded && !frame1.fenceQueued);
    AIVERSE_TEST_CHECK(screen.busy());
    screen.invalidateHistory(); // Metal -> GLES/Vulkan raster fallback
    AIVERSE_TEST_CHECK(!screen.confirmedOutput().valid());
    dev.endFrame();
    AIVERSE_TEST_CHECK(screen.confirmExternalCompletion(frame1.serial,true));
    const auto completed=screen.poll();
    AIVERSE_TEST_CHECK(completed.completed && completed.success);
    AIVERSE_TEST_CHECK(!screen.confirmedOutput().valid());

    // A fresh direct frame after switching back is accepted. History should
    // start from scratch instead of reprojecting the stale first frame.
    dev.beginFrame(64,64);
    const auto frame2=screen.dispatch(depth,64,64,mat4{},mat4{},
                                       {0,0,0},1,8);
    AIVERSE_TEST_CHECK(frame2.encoded && !frame2.fenceQueued);
    dev.endFrame();
    AIVERSE_TEST_CHECK(screen.confirmExternalCompletion(frame2.serial,true));
    const auto accepted=screen.poll();
    AIVERSE_TEST_CHECK(accepted.completed && accepted.success);
    AIVERSE_TEST_CHECK(screen.confirmedOutput().valid());
    screen.invalidateHistory();
    AIVERSE_TEST_CHECK(!screen.confirmedOutput().valid());
    AIVERSE_TEST_CHECK(screen.shutdown());
    dev.destroyTexture(depth);
    AIVERSE_TEST_CHECK(dev.buffers.empty() && dev.textures.empty());
    return 0;
}
