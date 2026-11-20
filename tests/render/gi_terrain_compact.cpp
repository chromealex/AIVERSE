#include "modules/rts/view/RtsTerrainShaderBudget.h"
#include "engine/render/GiCustomShaderSource.h"
#include "tests/AlwaysCheck.h"
#include <string>

using namespace aiverse::game;
using aiverse::render::GiCustomShaderMode;
using aiverse::render::giCustomModeFitsAt;

int main() {
    // Legacy rich terrain reserves tex0..20; strictly 16-unit GLES3 drivers
    // MUST use compact no-GI terrain instead of a shader with 21 samplers.
    AIVERSE_TEST_CHECK(terrainNeedsCompactSamplers(16));
    AIVERSE_TEST_CHECK(terrainNeedsCompactSamplers(20));
    AIVERSE_TEST_CHECK(!terrainNeedsCompactSamplers(21));
    AIVERSE_TEST_CHECK(!terrainCompactSupported(15));
    AIVERSE_TEST_CHECK(terrainCompactSupported(16));
    AIVERSE_TEST_CHECK(!giCustomModeFitsAt(GiCustomShaderMode::Single,21,16,false));
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::Single,7,16,false));
    AIVERSE_TEST_CHECK(giCustomModeFitsAt(GiCustomShaderMode::Single,7,16,true));
    const std::string compactGi=giCustomGlslFunctions(
        GiCustomShaderMode::Single,7);
    AIVERSE_TEST_CHECK(compactGi.find("uniform highp sampler2D tex7;")!=std::string::npos);
    AIVERSE_TEST_CHECK(compactGi.find("texelFetch(tex7")!=std::string::npos);
    AIVERSE_TEST_CHECK(compactGi.find("uniform highp sampler2D tex13;")==std::string::npos);

    const std::string rich=aiverse::game::shaders::kTerrainFS;
    const std::string low=terrainCompactLowFragment();
    AIVERSE_TEST_CHECK(rich.find("uniform sampler2D tex19")!=std::string::npos ||
                       rich.find("tex19")!=std::string::npos);
    AIVERSE_TEST_CHECK(low.find("tex18")==std::string::npos);
    AIVERSE_TEST_CHECK(low.find("tex19")==std::string::npos);
    AIVERSE_TEST_CHECK(low.find("tex20")==std::string::npos);
    AIVERSE_TEST_CHECK(low.find("tex15")!=std::string::npos); // vision/FOW
    AIVERSE_TEST_CHECK(low.find("bakedTerrain(")!=std::string::npos);
    AIVERSE_TEST_CHECK(low.find("terrainShadow(")!=std::string::npos);
    AIVERSE_TEST_CHECK(low.find("dynamicLight(")!=std::string::npos);
    // Material texture declarations are guarded out on low-tier, while
    // the baked/trace declarations are remapped to 4/5/6.
    AIVERSE_TEST_CHECK(low.find("#if TERRAIN_TIER >= 1\nuniform sampler2D tex4;")!=std::string::npos);
    AIVERSE_TEST_CHECK(low.find("uniform sampler2D tex4;")!=std::string::npos);
    AIVERSE_TEST_CHECK(low.find("uniform sampler2D tex5;")!=std::string::npos);
    AIVERSE_TEST_CHECK(low.find("uniform sampler2D tex6;")!=std::string::npos);
    // Vertex-stage cell colour and dynamic overlays remain on original
    // tex2/3 regardless of the low-tier fragment remapping.
    const std::string vertex=shaders::kTerrainVS;
    AIVERSE_TEST_CHECK(vertex.find("texelFetch(tex2")!=std::string::npos);
    AIVERSE_TEST_CHECK(vertex.find("texelFetch(tex3")!=std::string::npos);

    // Crucially the rich medium/high GLSL still has the original tex18,
    // tex19, tex20 bindings and can be used unchanged on a capable GPU.
    AIVERSE_TEST_CHECK(std::string(shaders::kTerrainFS).find("tex18")!=std::string::npos);
    AIVERSE_TEST_CHECK(std::string(shaders::kTerrainFS).find("tex19")!=std::string::npos);
    AIVERSE_TEST_CHECK(std::string(shaders::kTerrainFS).find("tex20")!=std::string::npos);
    // These are source-contract checks. Compiler/GPU render is still required.
}
