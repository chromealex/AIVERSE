// Vulkan DIRECT shadowed point/spot compute shader: GLSL450 -> SPIR-V contract.
// Shader compilation DOES NOT establish runtime Vulkan RHI, GPU pixel parity,
// real shadow correctness or GPU performance.
#include "engine/render/LumaWeaveVulkanDirectShader.h"
#include "engine/render/LumaWeaveVulkanScreenShader.h"
#include "engine/render/LumaWeaveVulkanDenoiseShader.h"
#include "engine/render/LumaWeaveVulkanNormalHistoryShader.h"
#include "engine/render/LumaWeaveGpuPacket.h"
#include "engine/render/GiGpuBvh.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace aiverse::render;

int main() {
    static_assert(sizeof(LumaGpuLight)==80);
    static_assert(sizeof(LumaGpuReceiver)==32);
    static_assert(sizeof(GiGpuBvhTri)==64);
    static_assert(sizeof(GiGpuBvhNode)==48);
    static_assert(sizeof(LumaGpuGridUniforms)==64);

    const std::string source=luma_vk::kReceiverDirectCompute;
    // Match descriptor set 0: SSBOs at 0..3, RGBA16F storage image at 4,
    // local size 64 and the 80-byte Vulkan GI compute push-constant ABI.
    for(const char* token:{"local_size_x = 64","binding=0","binding=1",
                           "binding=2","binding=3","binding=4",
                           "layout(push_constant)","layout(rgba16f",
                           "shadowed(","incident(","p.control.z"}) {
        AIVERSE_TEST_CHECK(source.find(token)!=std::string::npos);
    }
    std::vector<uint32_t> binary;
    std::string error;
    const bool ok=aiverse::gfx::vk::compileComputeProgram(source,binary,error);
    if(!ok)std::fprintf(stderr,"Luma Vulkan direct GLSL->SPIR-V failure: %s\n",
                        error.c_str());
    AIVERSE_TEST_CHECK(ok);
    AIVERSE_TEST_CHECK(binary.size()>=8);
    AIVERSE_TEST_CHECK(binary.front()==0x07230203u); // SPIR-V magic

    // The PBR fullscreen shader reuses the identical shared GPU BVH tracer
    // but receives actual opaque Renderer3D material/depth/normal textures.
    const std::string screen=aiverse::render::luma_vk::kScreenDirectCompute;
    for(const char* token:{"binding=0","binding=1","binding=2",
                           "binding=3","binding=4","binding=5",
                           "binding=6","binding=7","binding=8",
                           "binding=9","inverseViewProjection",
                           "pbrDirect","depth*2.0-1.0",
                           "shadowed(","sceneDepthTexture",
                           "materialDepthTexture","surfaceDepthTexture"}) {
        AIVERSE_TEST_CHECK(screen.find(token)!=std::string::npos);
    }
    binary.clear();error.clear();
    const bool compiled=aiverse::gfx::vk::compileComputeProgram(
        screen,binary,error);
    if(!compiled)std::fprintf(stderr,
        "Luma Vulkan screen PBR GLSL->SPIR-V failure: %s\n",error.c_str());
    AIVERSE_TEST_CHECK(compiled);
    AIVERSE_TEST_CHECK(binary.size()>=8 && binary.front()==0x07230203u);

    // Stage19 extends the denoiser to seven sampled textures at bindings
    // 5..11: current real motion and prior normal/depth history are never
    // substituted by the current frame's normal when history is valid.
    // Stage17 uses the same GPU descriptor ABI and creates one additional
    // bounded 3x3 spatial / history-reprojected direct-light resolve.
    const std::string denoise=aiverse::render::luma_vk::kDenoiseCompute;
    for(const char* token:{"binding=0","binding=1","binding=2",
                           "binding=3","binding=4","binding=5",
                           "binding=6","binding=7","binding=8",
                           "binding=9","binding=10","binding=11",
                           "previousVP","inverseCurrentVP","motionValid",
                           "objectMotion","previousNormals",
                           "filteredOutput","surfaceDepth",
                           "previousHistory"}) {
        AIVERSE_TEST_CHECK(denoise.find(token)!=std::string::npos);
    }
    binary.clear();error.clear();
    const bool filterCompiled=aiverse::gfx::vk::compileComputeProgram(
        denoise,binary,error);
    if(!filterCompiled)std::fprintf(stderr,
        "Luma Vulkan temporal/spatial GLSL->SPIR-V failure: %s\n",
        error.c_str());
    AIVERSE_TEST_CHECK(filterCompiled);
    AIVERSE_TEST_CHECK(binary.size()>=8 &&
                       binary.front()==0x07230203u);

    const std::string normalHistory=luma_vk::kNormalHistoryCompute;
    for(const char* token:{"binding=0","binding=1","binding=2",
                           "binding=3","binding=4","binding=5",
                           "binding=6","binding=7","surfaceNormals",
                           "surfaceDepth","outputNormals"}) {
        AIVERSE_TEST_CHECK(normalHistory.find(token)!=std::string::npos);
    }
    binary.clear();error.clear();
    const bool captured=aiverse::gfx::vk::compileComputeProgram(
        normalHistory,binary,error);
    if(!captured)std::fprintf(stderr,
        "Luma Vulkan normal/depth history GLSL->SPIR-V failure: %s\n",
        error.c_str());
    AIVERSE_TEST_CHECK(captured);
    AIVERSE_TEST_CHECK(binary.size()>=8 &&
                       binary.front()==0x07230203u);

    // The compiler must fail closed on an invalid compute module.
    AIVERSE_TEST_CHECK(!aiverse::gfx::vk::compileComputeProgram(
        "#version 450\nthis is deliberately invalid GLSL\n",binary,error));
    AIVERSE_TEST_CHECK(binary.empty() && !error.empty());
    return 0;
}
