#include "engine/render/GiTextureBudget.h"
#include "engine/render/GiShaderGraphTextureSlots.h"
#include "engine/render/GiCustomShaderPolicy.h"
#include "engine/render/GiPresentationPolicy.h"
#include "tests/AlwaysCheck.h"

using namespace aiverse::render;
static_assert(giFragmentTextureSlotsRequired(false,false,false)==14);
static_assert(giFragmentTextureSlotsRequired(true,false,false)==15);
static_assert(giFragmentTextureSlotsRequired(true,true,false)==16);
static_assert(giFragmentTextureSlotsRequired(true,true,true)==17);
static_assert(giFragmentTextureSlotsRequired(false,true,false)==0);
static_assert(giFragmentTextureSlotsRequired(true,false,true)==0);

int main() {
    // Terrain, foliage and arbitrary custom shader bindings must match
    // the exact GLSL/MSL permutation; no silent downgrade of a dual shader.
    AIVERSE_TEST_CHECK(giCustomModeTextureSlots(GiCustomShaderMode::Single)==14);
    AIVERSE_TEST_CHECK(giCustomModeTextureSlots(GiCustomShaderMode::Validity)==15);
    AIVERSE_TEST_CHECK(giCustomModeTextureSlots(GiCustomShaderMode::DualCascade)==16);
    AIVERSE_TEST_CHECK(giCustomModeTextureSlots(GiCustomShaderMode::TripleCascade)==17);
    AIVERSE_TEST_CHECK(giCustomModeFits(GiCustomShaderMode::Single,14,false));
    AIVERSE_TEST_CHECK(!giCustomModeFits(GiCustomShaderMode::DualCascade,15,false));
    AIVERSE_TEST_CHECK(giCustomModeFits(GiCustomShaderMode::DualCascade,16,false));
    AIVERSE_TEST_CHECK(!giCustomModeFits(GiCustomShaderMode::TripleCascade,16,false));
    AIVERSE_TEST_CHECK(!giCustomModeFits(GiCustomShaderMode::TripleCascade,32,false));
    AIVERSE_TEST_CHECK(giCustomModeFits(GiCustomShaderMode::TripleCascade,31,true));

    // Shader Graph's legacy tex13 base is retained without dynamic GI.
    AIVERSE_TEST_CHECK(giGraphTextureStart(false)==13);
    AIVERSE_TEST_CHECK(giGraphTextureStart(true)==14);
    AIVERSE_TEST_CHECK(giGraphTextureStart(true,true)==15);
    AIVERSE_TEST_CHECK(giGraphTextureStart(true,true,true)==16);
    AIVERSE_TEST_CHECK(giGraphTextureStart(true,true,true,true)==17);
    // GLES3 16-unit limit can run a single atlas plus two graph textures,
    // but not a dual cascade plus textured Shader Graph.
    AIVERSE_TEST_CHECK(giGraphTextureSlotsFit(16,2,true));
    AIVERSE_TEST_CHECK(!giGraphTextureSlotsFit(16,3,true));
    AIVERSE_TEST_CHECK(giGraphTextureSlotsFit(16,1,true,true));
    AIVERSE_TEST_CHECK(!giGraphTextureSlotsFit(16,1,true,true,true));
    AIVERSE_TEST_CHECK(!giGraphTextureSlotsFit(16,1,true,true,true,true));
    // High-end Vulkan and Metal may host a full four-texture Shader Graph
    // alongside the optional third cascade without any overlap with tex16.
    AIVERSE_TEST_CHECK(giGraphTextureSlotsFit(21,4,true,true,true,true));
    AIVERSE_TEST_CHECK(!giGraphTextureSlotsFit(20,4,true,true,true,true));
    AIVERSE_TEST_CHECK(!giGraphTextureSlotsFit(32,1,true,false,true));
    AIVERSE_TEST_CHECK(giGraphTextureSlotsFit(16,3,false));

    // GLES3/WebGL2 CPU probe atlas is a texture SAMPLE, not an HDR target.
    // RGBA8 framebuffer + sampled RGBA32F GI still supports PBR indirect.
    AIVERSE_TEST_CHECK(giDynamicColorPassAllowed(false,false));
    AIVERSE_TEST_CHECK(giDynamicColorPassAllowed(true,false));
    AIVERSE_TEST_CHECK(giDynamicColorPassAllowed(true,true));
    AIVERSE_TEST_CHECK(!giDynamicColorPassAllowed(false,true));

    // GLES3 minimum 16 fragment texture units: single/dual permitted,
    // Metal third atlas at texture(16) is NOT a GLES/WebGL2 permutation.
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,false,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,true,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,true,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(16,true,true,true));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(31,true,true,true));

    // Vulkan low descriptor caps must gracefully disable GI rather than
    // compiling a shader with unreachable texture units.
    for(int slots=0;slots<14;++slots)
        AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(slots,false,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(14,false,false,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(14,true,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(15,true,false,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(15,true,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(32,false,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(32,true,false,true));
}
