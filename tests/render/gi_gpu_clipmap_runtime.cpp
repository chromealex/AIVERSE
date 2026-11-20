#include "engine/render/GiGpuClipmapRuntime.h"
#include "engine/render/GiDynamicShaders.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace aiverse::render;

struct ClipmapFakeDevice : FakeDevice {
    std::vector<BufferReadFn> completions;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t bytes,BufferReadFn callback) override {
        if(!inFrame || inCompute || !buffers.contains(h.id) || bytes!=4)return false;
        completions.push_back(std::move(callback));
        return true;
    }
    void completeFrame() {
        AIVERSE_TEST_CHECK(!completions.empty());
        auto callback=std::move(completions.front());
        completions.erase(completions.begin());
        const uint32_t marker=0x47494631u;
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(),&marker,4);
        callback(std::move(bytes));
    }
};
static std::shared_ptr<const GiChunkedGeometrySnapshot> buildScene() {
    GiChunkedGeometryBuilder builder;
    builder.replace(1,{
        {{-40,-40,2},{40,-40,2},{40,40,2},0},
        {{-40,-40,2},{40,40,2},{-40,40,2},0}
    },{{1,1,1}});
    return std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
}

int main() {
    ClipmapFakeDevice device;
    device.computeEnabled=true;
    device.info_.backend=gfx::Backend::Metal;
    GiGpuClipmapRuntime runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(device,{0,0,0},1,4,2,2,{0,0,0},16,6));
    AIVERSE_TEST_CHECK(runtime.ready());
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==16);
    const auto world=buildScene();
    AIVERSE_TEST_CHECK(runtime.setGeometry(world,1));
    AIVERSE_TEST_CHECK(runtime.setLights({
        GiMovingLight{3,{0,0,0},{1,0,0},5,10}
    }));

    device.beginFrame(640,480);
    auto step=runtime.updateFrame(16,{0,0,0});
    AIVERSE_TEST_CHECK(step.submitted.submitted && step.submitted.probes==16);
    AIVERSE_TEST_CHECK(step.inFlight==16 && !step.safeForPresentation);
    AIVERSE_TEST_CHECK(runtime.busy());
    // The user moves camera before the GPU finishes: ring reuse is forbidden.
    auto blocked=runtime.updateFrame(16,{1,0,0});
    AIVERSE_TEST_CHECK(blocked.scroll.blocked && !blocked.scroll.changed);
    AIVERSE_TEST_CHECK(runtime.volume().originCell().x==-2);
    AIVERSE_TEST_CHECK(!runtime.shutdown());
    device.endFrame();
    device.completeFrame();

    device.beginFrame(640,480);
    step=runtime.updateFrame(16,{1,0,0});
    AIVERSE_TEST_CHECK(step.completed.accepted==16);
    AIVERSE_TEST_CHECK(step.scroll.changed && step.scroll.recycled==4);
    AIVERSE_TEST_CHECK(step.scroll.reused==12);
    AIVERSE_TEST_CHECK(step.submitted.submitted && step.submitted.probes==4);
    AIVERSE_TEST_CHECK(runtime.volume().originCell().x==-1);
    AIVERSE_TEST_CHECK((runtime.volume().ringOffset()==std::array<int,3>{3,1,1}));
    // With a per-slot R8 validity mask, the 12 unchanged probes remain safe
    // for presentation while four entering slots are still being computed.
    AIVERSE_TEST_CHECK(step.safeForPresentation);
    AIVERSE_TEST_CHECK(runtime.validity().validCount()==12);
    device.endFrame();
    device.completeFrame();

    device.beginFrame(640,480);
    step=runtime.updateFrame(0,{1,0,0});
    AIVERSE_TEST_CHECK(step.completed.accepted==4);
    AIVERSE_TEST_CHECK(step.safeForPresentation);
    AIVERSE_TEST_CHECK(runtime.pendingProbes()==0);
    AIVERSE_TEST_CHECK(!runtime.busy());
    device.endFrame();

    // Verify the shader ABI uses the same modulo remapping as GiClipmapVolume.
    const std::string gl=AIVERSE_GLSL_DYNAMIC_GI;
    const std::string msl=AIVERSE_MSL_DYNAMIC_GI;
    AIVERSE_TEST_CHECK(gl.find("u_dynamicGiRing")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("physical=(c+")!=std::string::npos);
    AIVERSE_TEST_CHECK(msl.find("float4 ring")!=std::string::npos);
    AIVERSE_TEST_CHECK(msl.find("physical=(c+")!=std::string::npos);

    // 3D camera teleport recycles full atlas and must only become safe after a new fence.
    device.beginFrame(640,480);
    step=runtime.updateFrame(16,{100,10,100});
    AIVERSE_TEST_CHECK(step.scroll.recycled==16);
    AIVERSE_TEST_CHECK(step.scroll.reused==0);
    AIVERSE_TEST_CHECK(step.submitted.submitted && !step.safeForPresentation);
    device.endFrame();
    device.completeFrame();
    device.beginFrame(640,480);
    step=runtime.updateFrame(0,{100,10,100});
    AIVERSE_TEST_CHECK(step.safeForPresentation);
    device.endFrame();

    AIVERSE_TEST_CHECK(runtime.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
