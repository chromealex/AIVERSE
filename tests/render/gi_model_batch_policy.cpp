#include "engine/render/GiModelBatchPolicy.h"
#include "engine/render/ModelShaders.h"
#include "tests/AlwaysCheck.h"
#include <string>

using namespace aiverse::render;

static_assert(giModelBatchMaySample(false,false));
static_assert(giModelBatchMaySample(false,true));
static_assert(!giModelBatchMaySample(true,false));
static_assert(!giModelBatchMaySample(true,true));

int main() {
    // TerraWeave terrain patch/grass and other foliage use ModelDraw. CPU
    // MDI per-material palettes use U3 factors, not GI's U6/tex13 atlas.
    AIVERSE_TEST_CHECK(giModelBatchMaySample(false,true));
    // Explicit GPU-authored Metal ICB is excluded until a compatible binding
    // ABI is developed; it is not the normal CPU ModelDraw / MDI route.
    AIVERSE_TEST_CHECK(!giModelBatchMaySample(true,true));
    const std::string gl=shaders::kModelFS;
    const std::string metal=shaders::kModelMSL;
    AIVERSE_TEST_CHECK(gl.find("MATERIAL_INSTANCE_PALETTE")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("MATERIAL_INSTANCE_PALETTE")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("amb += dynamicGiAt(v_wpos,n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("amb += dynamicGiAt(tex13,dgi,in.wpos,n)")!=std::string::npos);
}
