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
    // GLES3/WebGL2 must retain the CPU GI transport fallback: compute is
    // optional on these systems, even when a fake device reports a capability.
    GiComputeFakeDevice d;
    d.computeEnabled=true;
    GiGpuSmallScene gpu;
    AIVERSE_TEST_CHECK(!gpu.initialize(d,32));
    AIVERSE_TEST_CHECK(!gpu.ready());

    // Vulkan is allowed to use the same bounded GI jobs with the distinct
    // SPIR-V shader. These are FakeDevice API tests, not Vulkan execution.
    d.info_.backend=gfx::Backend::Vulkan;
    AIVERSE_TEST_CHECK(gpu.initialize(d,32));
    AIVERSE_TEST_CHECK(gpu.ready() && gpu.atlas().computeWritable());
    d.beginFrame(320,240);
    const auto vkDispatch=gpu.dispatch(
        {{0,1,giDirtyBit(GiDirtyReason::Geometry),{.5f,.5f,.5f}}},16,6);
    AIVERSE_TEST_CHECK(vkDispatch.submitted && vkDispatch.probes==1);
    d.endFrame();
    gpu.shutdown();
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());

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
    GiEnvironmentLight env;
    env.sunDirection={0,1,0};
    env.sunColor={1,0,0};
    env.sunIntensity=3;
    env.skyColor={0,0,1};
    env.skyIntensity=2;
    AIVERSE_TEST_CHECK(gpu.setEnvironment(env));
    AIVERSE_TEST_CHECK(gpu.environment()==env);
    AIVERSE_TEST_CHECK(sizeof(GiGpuSmallScene::Uniforms)==80);
    AIVERSE_TEST_CHECK(!gpu.setLights({GiMovingLight{2,{0,0,0},{1,0,0},5,20},
                                      GiMovingLight{2,{1,0,0},{1,0,0},5,20}}));
    AIVERSE_TEST_CHECK(gpu.lightCount()==1); // failed update retains previous lights
    GiMovingLight cone{3,{0,0,0},{0,1,0},5,20};
    cone.direction={0,0,1};cone.cosOuter=.6f;cone.cosInner=.9f;
    AIVERSE_TEST_CHECK(gpu.setLights({cone}));
    AIVERSE_TEST_CHECK(gpu.lightCount()==1);
    cone.direction={0,0,0};
    AIVERSE_TEST_CHECK(!gpu.setLights({cone})); // unnormalised cone rejected
    cone.direction={0,0,1};
    AIVERSE_TEST_CHECK(gpu.setLights({cone}));

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
    const std::string vulkanShader=gi_gpu::kVulkanSmallScene;
    AIVERSE_TEST_CHECK(vulkanShader.find("#version 450")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("local_size_x=64")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("binding=4,rgba16f")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("layout(push_constant)")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("imageStore(outputImage")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("job.index*3u")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("spotDirectionOuter")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("spotInner.x")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("sunDirectionIntensity")!=std::string::npos);
    AIVERSE_TEST_CHECK(vulkanShader.find("skyColor")!=std::string::npos);
    AIVERSE_TEST_CHECK(sizeof(GiGpuSmallScene::GpuLamp)==64);

    std::string shader=gi_gpu::kMetalSmallScene;
    AIVERSE_TEST_CHECK(shader.find("kernel void cs_main")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("access::write")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("constant Params& p [[buffer(8)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("job.index*3u")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("spotDirectionOuter")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("spotInner.x")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("sunDirectionIntensity")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("skyColor")!=std::string::npos);

    gpu.shutdown();
    AIVERSE_TEST_CHECK(d.textures.empty() && d.buffers.empty());
    return 0;
}
