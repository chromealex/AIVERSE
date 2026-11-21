#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/render/GiGpuVulkanShaders.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
int main() {
    std::vector<uint32_t> words;std::string error;
    const bool ok=aiverse::gfx::vk::compileComputeProgram(aiverse::render::gi_gpu::kVulkanSmallScene,words,error);
    if(!ok)std::fprintf(stderr,"%s\n",error.c_str());
    AIVERSE_TEST_CHECK(ok && words.size()>5 && words[0]==0x07230203);
    AIVERSE_TEST_CHECK(!aiverse::gfx::vk::compileComputeProgram("invalid shader",words,error));
    AIVERSE_TEST_CHECK(words.empty() && !error.empty());
}
