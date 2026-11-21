#include "engine/render/ModelShaders.h"
#include "engine/render/GiDynamicShaders.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/GiTextureBudget.h"
#include "tests/AlwaysCheck.h"
#include <string>
#include <type_traits>
using namespace aiverse::render;
static_assert(std::is_same_v<decltype(&Renderer3D::bindDynamicGiForCustom),
                             bool (Renderer3D::*)()>);
static_assert(std::is_same_v<decltype(&Renderer3D::bindDynamicGiForCustomAt),
                             bool (Renderer3D::*)(int)>);
int main() {
    const std::string glsl=shaders::kModelFS;
    const std::string metal=shaders::kModelMSL;
    // Test source assembly, not actual shader compilation or driver parity.
    AIVERSE_TEST_CHECK(glsl.find("layout(std140) uniform U6")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("uniform highp sampler2D tex13")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("dynamicGiAt(v_wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("struct GiDynamicU")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("constant GiDynamicU& dgi [[buffer(DYNAMIC_GI_BUFFER)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("texture2d<float> DYNAMIC_GI_TEXTURE [[texture(DYNAMIC_GI_SLOT)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("dynamicGiAt(DYNAMIC_GI_TEXTURE,dgi,in.wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,false,false,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(13,false,false,false));
    // No unconditional light transport in a non-opted-in material.
    AIVERSE_TEST_CHECK(glsl.find("#if defined(DYNAMIC_GI)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("#if defined(DYNAMIC_GI)")!=std::string::npos);
}
