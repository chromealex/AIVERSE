#include "engine/render/GiGpuSmallScene.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <string>
#include <vector>

using namespace aiverse::render;

struct GiComputeFakeDevice : FakeDevice {
    bool supports(gfx::PixelFormat format) const override {
        return format==gfx::PixelFormat::RGBA16F ||
               FakeDevice::supports(format);
    }
};

static std::vector<StaticShadowGeometry::Triangle> plane(float z) {
    return {{{-4,-4,z},{4,-4,z},{4,4,z},0},
            {{-4,-4,z},{4,4,z},{-4,4,z},0}};
}

int main() {
    // No use of compute on GLES/GL/Vulkan without an explicitly compatible backend.
    GiComputeFakeDevice d;
    d.computeEnabled=true;
    GiGpuSmallScene gpu;
    AIVERSE_TEST_CHECK(!gpu.initialize(d,32));
    AIVERSE_TEST_CHECK(!gpu.ready());

    d.info_.backend=gfx::Backend::Metal;
    d.computeEnabled=false;
    AIVERSE_TEST_CHECK(!gpu.initialize(d,32));
    d.computeEnabled=true;
    AIVERSE_TEST_CHECK(gpu.initialize(d,32));
    AIVERSE_TEST_CHECK(gpu.ready());
    AIVERSE_TEST_CHECK(gpu.atlas().computeWritable());
    AIVERSE_TEST_CHECK(gpu.atlas().probeCount()==32);
    AIVERSE_TEST_CHECK(!d.textures.empty());

    GiChunkedGeometryBuilder chunks;
    chunks.replace(2,plane(2),{{1,0,0}});
    auto geometry=chunks.publish();
    AIVERSE_TEST_CHECK(gpu.setGeometry(geometry,1));
    AIVERSE_TEST_CHECK(gpu.geometryRevision()==1);
    AIVERSE_TEST_CHECK(gpu.triangleCount()==2);
    AIVERSE_TEST_CHECK(gpu.setLights({GiMovingLight{1,{0,0,0},{1,0,0},5,20}}));
    AIVERSE_TEST_CHECK(gpu.lightCount()==1);
    AIVERSE_TEST_CHECK(!gpu.setLights({GiMovingLight{2,{0,0,0},{1,0,0},5,20},
                                      GiMovingLight{2,{1,0,0},{1,0,0},5,20}}));
    AIVERSE_TEST_CHECK(gpu.lightCount()==1); // failed update retains previous lights

    std::vector<GiProbeTicket> tickets{
        {0,1,giDirtyBit(GiDirtyReason::Geometry),{.5f,.5f,.5f}},
        {1,1,giDirtyBit(GiDirtyReason::Geometry),{1.5f,.5f,.5f}}
    };
    const auto outsideFrame=gpu.dispatch(tickets,32,8);
    AIVERSE_TEST_CHECK(!outsideFrame.submitted);

    d.beginFrame(320,240);
    auto sent=gpu.dispatch(tickets,32,8);
    AIVERSE_TEST_CHECK(sent.submitted && sent.probes==2);
    AIVERSE_TEST_CHECK(sent.primaryRayBudget==64);
    AIVERSE_TEST_CHECK(sent.maximumTriangleTests==64*2*2);
    AIVERSE_TEST_CHECK(d.dispatchCount==1);
    AIVERSE_TEST_CHECK(!d.inCompute);
    AIVERSE_TEST_CHECK(!gpu.dispatch(tickets,0,8).submitted);
    AIVERSE_TEST_CHECK(!gpu.dispatch({tickets[0],tickets[0]},32,8).submitted);
    auto wrong=tickets;
    wrong[0].index=99;
    AIVERSE_TEST_CHECK(!gpu.dispatch(wrong,32,8).submitted);
    d.endFrame();

    // Shader existence/ABI checks, not an MSL compiler or GPU image parity test.
    std::string shader=gi_gpu::kMetalSmallScene;
    AIVERSE_TEST_CHECK(shader.find("kernel void cs_main")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("access::write")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("constant Params& p [[buffer(8)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("job.index*3u")!=std::string::npos);

    gpu.shutdown();
    AIVERSE_TEST_CHECK(d.textures.empty() && d.buffers.empty());
    return 0;
}
