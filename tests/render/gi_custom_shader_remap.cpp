#include "engine/render/GiCustomShaderSource.h"
#include "tests/AlwaysCheck.h"
#include <stdexcept>
#include <string>

using namespace aiverse::render;

int main() {
    // Existing terrain reserves tex0..20. It can only add a GI atlas at 21+
    // on a GPU reporting enough fragment sampled bindings. GLES3 min 16
    // falls back to terrain-only shading (no corrupted tex13 cliff texture).
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::Single,21,22,true));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::Single,21,16,false));
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::Validity,21,23,true));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::DualCascade,21,23,true));
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::DualCascade,21,24,false));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::TripleCascade,21,32,false));
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::TripleCascade,21,25,true));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::Single,-1,32,true));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::Single,2147483647,32,true));

    const std::string single=giCustomGlslFunctions(GiCustomShaderMode::Single,21);
    AIVERSE_TEST_CHECK(single.find("uniform highp sampler2D tex21;")!=std::string::npos);
    AIVERSE_TEST_CHECK(single.find("texelFetch(tex21")!=std::string::npos);
    AIVERSE_TEST_CHECK(single.find("uniform highp sampler2D tex13;")==std::string::npos);
    const std::string validity=giCustomGlslFunctions(GiCustomShaderMode::Validity,14);
    AIVERSE_TEST_CHECK(validity.find("uniform highp sampler2D tex14;")!=std::string::npos);
    AIVERSE_TEST_CHECK(validity.find("uniform highp sampler2D tex15;")!=std::string::npos);
    AIVERSE_TEST_CHECK(validity.find("texelFetch(tex14")!=std::string::npos);
    AIVERSE_TEST_CHECK(validity.find("texelFetch(tex15")!=std::string::npos);
    const std::string dual=giCustomGlslFunctions(GiCustomShaderMode::DualCascade,21);
    AIVERSE_TEST_CHECK(dual.find("uniform highp sampler2D tex23;")!=std::string::npos);
    AIVERSE_TEST_CHECK(dual.find("u_dynamicGiFarOrigin")!=std::string::npos);
    AIVERSE_TEST_CHECK(giCustomGlslExpression(GiCustomShaderMode::DualCascade,
                      "worldPos","normal")=="dynamicGiCascadedAt(worldPos,normal)");

    const std::string msl=giCustomMslFunctions(GiCustomShaderMode::TripleCascade);
    AIVERSE_TEST_CHECK(msl.find("dynamicGiTripleAt")!=std::string::npos);
    const std::string args=giCustomMslFragmentArguments(
        GiCustomShaderMode::TripleCascade,21);
    AIVERSE_TEST_CHECK(args.find("giFineAtlas [[texture(21)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(args.find("giFineMask [[texture(22)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(args.find("giFarAtlas [[texture(23)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(args.find("giThirdAtlas [[texture(24)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(args.find("giThird [[buffer(9)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(giCustomMslExpression(GiCustomShaderMode::Single,
                      "p","n")=="dynamicGiAt(giFineAtlas,giFine,p,n)");
    AIVERSE_TEST_CHECK(giCustomMslExpression(GiCustomShaderMode::DualCascade,
                      "p","n").find("dynamicGiCascadedAt(")==0);

    bool rejected=false;
    try {(void)giCustomGlslFunctions(GiCustomShaderMode::TripleCascade,21);}
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(giCustomGlslFunctions(GiCustomShaderMode::Off,21).empty());
}
