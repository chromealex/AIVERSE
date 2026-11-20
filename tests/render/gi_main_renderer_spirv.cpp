// Stage23 Vulkan GLSL450 real-compiler gate for the MAIN Renderer3D PBR
// shader permutations with opt-in L1 and fine-mask/far clipmap bindings.
// Not evidence of a GPU frame, Metal MSL or GLES hardware execution.
#include "engine/render/ModelShaders.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

using namespace aiverse;

int main(){
    struct Variant{const char* name;const char* defines;};
    constexpr Variant variants[]{
        {"unchanged baseline","#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n"},
        {"CPU L1 single","#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define DYNAMIC_GI 1\n"},
        {"CPU L1 validity","#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n"},
        {"fine+far clipmap shader contract",
         "#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n#define DYNAMIC_GI_CASCADES 1\n"},
        {"CPU L1 + baked probes",
         "#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define PROBES 1\n#define AIVERSE_GIQ 2\n#define DYNAMIC_GI 1\n"}
    };
    for(const auto& variant:variants){
        gfx::vk::SpirvProgram program;
        const std::string vertex=std::string(variant.defines)+render::shaders::kModelVS;
        const std::string fragment=std::string(variant.defines)+render::shaders::kModelFS;
        const bool compiled=gfx::vk::compileProgram(vertex,fragment,true,program);
        if(!compiled)std::fprintf(stderr,"MAIN GI shader variant '%s': %s\n",
                                   variant.name,program.error.c_str());
        AIVERSE_TEST_CHECK(compiled);
        AIVERSE_TEST_CHECK(!program.vertex.empty() && !program.fragment.empty());
        AIVERSE_TEST_CHECK(program.vertex.front()==0x07230203u &&
                           program.fragment.front()==0x07230203u);
    }
    return 0;
}
