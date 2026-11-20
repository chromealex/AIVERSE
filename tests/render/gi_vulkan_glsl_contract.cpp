// Portable GI GLSL source -> Vulkan SPIR-V compilation gate.
// Uses the SAME GLSL ES 3.00 fragment bodies as the GLES3 renderer.
// Does not require a Vulkan device and does not prove GLES GPU rendering.
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/render/GiDynamicShaders.h"
#include "engine/render/GiCascadeShaders.h"
#include "engine/render/ModelShaders.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
#include <string>

int main() {
    const std::string vertex=R"(
layout(location=0) in vec3 a_pos;
out vec3 v_world;
void main() {
    v_world=a_pos;
    gl_Position=vec4(a_pos,1.0);
})";
    auto compile=[&](const char* defines,const char* evaluation) {
        const std::string fragment=
            std::string(defines) +
            AIVERSE_GLSL_DYNAMIC_GI +
            AIVERSE_GLSL_CASCADE_GI +
            "\nin vec3 v_world;\n"
            "layout(location=0) out vec4 o_color;\n"
            "void main() {\n"
            "  vec3 n=vec3(0.0,1.0,0.0);\n"
            "  o_color=vec4("+std::string(evaluation)+",1.0);\n"
            "}\n";
        aiverse::gfx::vk::SpirvProgram result;
        const bool ok=aiverse::gfx::vk::compileProgram(
            vertex,fragment,true,result);
        if(!ok)
            std::fprintf(stderr,"Vulkan GI shader compile failed: %s\n",
                         result.error.c_str());
        AIVERSE_TEST_CHECK(ok);
        AIVERSE_TEST_CHECK(!result.vertex.empty());
        AIVERSE_TEST_CHECK(!result.fragment.empty());
    };
    compile("#define DYNAMIC_GI 1\n",
            "dynamicGiAt(v_world,n)");
    compile("#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n",
            "dynamicGiAt(v_world,n)");
    compile("#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n"
            "#define DYNAMIC_GI_CASCADES 1\n",
            "dynamicGiCascadedAt(v_world,n)");
    // Full production ModelVS/ModelFS permutations, not just synthetic
    // fragments. This catches accidental GI exclusion from instanced PBR
    // objects and validates the real U6/tex13 Vulkan binding translation.
    const auto modelVariant=[&](const std::string& defines){
        const std::string vs=defines+aiverse::render::shaders::kModelVS;
        const std::string fs=defines+aiverse::render::shaders::kModelFS;
        aiverse::gfx::vk::SpirvProgram out;
        const bool ok=aiverse::gfx::vk::compileProgram(vs,fs,true,out);
        if(!ok)
            std::fprintf(stderr,"Vulkan instanced PBR GI compile failed: %s\n",
                         out.error.c_str());
        AIVERSE_TEST_CHECK(ok);
        AIVERSE_TEST_CHECK(!out.vertex.empty() && !out.fragment.empty());
    };
    modelVariant("#define AIVERSE_INST 2\n#define AIVERSE_Q 2\n"
                 "#define DYNAMIC_GI 1\n");
    modelVariant("#define AIVERSE_INST 2\n#define AIVERSE_Q 2\n"
                 "#define DYNAMIC_GI 1\n#define MATERIAL_PALETTE 1\n");
    modelVariant("#define AIVERSE_INST 2\n#define AIVERSE_Q 2\n"
                 "#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n"
                 "#define DYNAMIC_GI_CASCADES 1\n");
    return 0;
}
