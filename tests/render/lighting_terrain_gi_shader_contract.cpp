#include "modules/rts/view/RtsTerrainShaders.h"
#include "tests/AlwaysCheck.h"
#include <string>

using namespace aiverse::game::shaders;
int main() {
    // Textual shader ABI check; native GLSL/MSL compilation is a separate gate.
    const std::string glsl=kTerrainFS, metal=kTerrainMSL;
    AIVERSE_TEST_CHECK(glsl.find("uniform highp sampler2D tex21;")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("uniform highp sampler2D tex8;")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("DYNAMIC_GI_TERRAIN_COMPACT")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("dynamicGiAt(v_wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("layout(std140) uniform U6")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("struct GiDynamicU")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("constant GiDynamicU& dgi [[buffer(7)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("texture2d<float> tex21 [[texture(21)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("dynamicGiAt(tex21,dgi,in.wpos,n)")!=std::string::npos);
    // Every existing terrain tier still contains normal, splat, baked and
    // shadow code. Dynamic GI is gated by DYNAMIC_GI_TERRAIN only.
    AIVERSE_TEST_CHECK(glsl.find("#if defined(DYNAMIC_GI_TERRAIN)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("#if defined(DYNAMIC_GI_TERRAIN)")!=std::string::npos);
}
