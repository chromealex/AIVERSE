// Vulkan DIRECT shadowed point/spot compute shader: GLSL450 -> SPIR-V contract.
// Shader compilation DOES NOT establish runtime Vulkan RHI, GPU pixel parity,
// real shadow correctness or GPU performance.
#include "engine/render/LumaWeaveVulkanDirectShader.h"
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

    // The compiler must fail closed on an invalid compute module.
    AIVERSE_TEST_CHECK(!aiverse::gfx::vk::compileComputeProgram(
        "#version 450\nthis is deliberately invalid GLSL\n",binary,error));
    AIVERSE_TEST_CHECK(binary.empty() && !error.empty());
    return 0;
}
