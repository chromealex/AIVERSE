#include "engine/render/GiGpuDynamicRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace aiverse::render;
struct GpuRuntimeFake : FakeDevice {
    std::vector<BufferReadFn> callbacks;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t n,BufferReadFn f) override {
        if(!inFrame || inCompute || !buffers.contains(h.id) || n!=4)return false;
        callbacks.push_back(std::move(f));
        return true;
    }
    void finish() {
        AIVERSE_TEST_CHECK(!callbacks.empty());
        auto callback=std::move(callbacks.front());
        callbacks.erase(callbacks.begin());
        constexpr uint32_t marker=0x47494631u;
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(),&marker,4);
        callback(std::move(bytes));
    }
};
static std::shared_ptr<const GiChunkedGeometrySnapshot> scene(bool wall) {
    GiChunkedGeometryBuilder builder;
    if(wall) {
        builder.replace(1,{
            {{-4,-4,2},{4,-4,2},{4,4,2},0},
            {{-4,-4,2},{4,4,2},{-4,4,2},0}
        },{{1,0,0}});
    }
    return std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
}
int main() {
    GpuRuntimeFake device;
    device.info_.backend=gfx::Backend::Metal;
    device.computeEnabled=true;
    GiGpuDynamicRuntime runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{0,0,0},1,4,1,1,16,6));
    AIVERSE_TEST_CHECK(runtime.ready());
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==4);
    auto walled=scene(true);
    AIVERSE_TEST_CHECK(runtime.setGeometry(walled,1));
    AIVERSE_TEST_CHECK(runtime.setLights({
        GiMovingLight{7,{0,0,0},{1,0,0},4,20}
    }));

    device.beginFrame(400,300);
    auto frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted);
    AIVERSE_TEST_CHECK(frame.submitted.fenceQueued);
    AIVERSE_TEST_CHECK(frame.submitted.probes==4);
    AIVERSE_TEST_CHECK(!frame.safeForPresentation);
    AIVERSE_TEST_CHECK(frame.inFlight==4);
    AIVERSE_TEST_CHECK(runtime.busy());
    // The scene and lamps cannot be replaced while the GPU reads them.
    AIVERSE_TEST_CHECK(!runtime.setGeometry(scene(false),2));
    AIVERSE_TEST_CHECK(!runtime.setLights({}));
    device.endFrame();
    device.finish();

    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.completed.accepted==4);
    AIVERSE_TEST_CHECK(frame.completed.stale==0);
    AIVERSE_TEST_CHECK(frame.safeForPresentation);
    AIVERSE_TEST_CHECK(!runtime.busy());
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==0);
    device.endFrame();

    auto empty=scene(false);
    const GiBounds edit{{0,-4,2},{4,4,2}};
    AIVERSE_TEST_CHECK(runtime.setGeometryRegion(empty,2,edit));
    AIVERSE_TEST_CHECK(runtime.pendingProbes()>0);
    AIVERSE_TEST_CHECK(!runtime.setGeometryRegion(walled,3,{{3,0,0},{1,0,0}}));
    device.beginFrame(400,300);
    frame=runtime.updateFrame(4,{0,0,0});
    AIVERSE_TEST_CHECK(frame.submitted.submitted);
    AIVERSE_TEST_CHECK(!frame.safeForPresentation);
    device.endFrame();
    device.finish();
    device.beginFrame(400,300);
    frame=runtime.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(frame.safeForPresentation);
    device.endFrame();

    // No-op input must not trigger arbitrary work.
    AIVERSE_TEST_CHECK(runtime.setLights({GiMovingLight{7,{0,0,0},{1,0,0},4,20}}));
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==0);
    AIVERSE_TEST_CHECK(runtime.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
    return 0;
}
