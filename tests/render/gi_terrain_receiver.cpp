#include "engine/render/GiCustomShaderSource.h"
#include "modules/rts/view/RtsTerrainShaders.h"
#include "tests/AlwaysCheck.h"
#include <string>

int main() {
    using namespace aiverse::render;
    const std::string fragment=aiverse::game::shaders::kTerrainFS;
    const std::string metal=aiverse::game::shaders::kTerrainMSL;

    // The existing live terrain pass owns tex13 cliff, tex14 normals,
    // tex15 visibility and tex18..20 trace/baked inputs. The added GI
    // permutation must not rewrite any of those original shader samples.
    AIVERSE_TEST_CHECK(fragment.find("uniform sampler2D tex13;")!=std::string::npos);
    AIVERSE_TEST_CHECK(fragment.find("uniform sampler2D tex14;")!=std::string::npos);
    AIVERSE_TEST_CHECK(fragment.find("texture(tex15")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("texture2d<float> tex20 [[texture(20)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(fragment.find("col += base * dynamicGiAt(v_wpos, n);")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("col += base * dynamicGiAt(giFineAtlas, giFine, in.wpos, n);")!=std::string::npos);

    // Runtime GI GLSL helper uses a disjoint atlas at tex21 and no other
    // injected declaration or function aliases legacy terrain tex13..20.
    const auto gi=giCustomGlslFunctions(GiCustomShaderMode::Single,21);
    AIVERSE_TEST_CHECK(gi.find("uniform highp sampler2D tex21;")!=std::string::npos);
    AIVERSE_TEST_CHECK(gi.find("uniform highp sampler2D tex13;")==std::string::npos);
    const std::string mslArgs=giCustomMslFragmentArguments(
        GiCustomShaderMode::Single,21);
    AIVERSE_TEST_CHECK(mslArgs.find("giFineAtlas [[texture(21)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(mslArgs.find("giFine [[buffer(7)]]")!=std::string::npos);

    // No-GI mode is always safe on devices with fewer than 22 fragment slots;
    // if a device reports 22, only the one-atlas path fits at index 21.
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::Single,21,16,false));
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::Single,21,22,false));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::Validity,21,22,false));
    // These are source/admission tests, NOT GLSL/MSL compilation or GPU parity.
}
