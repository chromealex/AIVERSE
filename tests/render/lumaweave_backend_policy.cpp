// Cross-backend lighting feature-contract regression. No GPU is required.
// Real Vulkan/GLES shader compilation and visual comparisons are separate.
#include "engine/render/LumaWeaveBackendPolicy.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <type_traits>

using namespace aiverse::render;

static_assert(LumaWeaveBackendPolicy::cpuProbeLimit(gfx::Backend::GLES3)==2);
static_assert(LumaWeaveBackendPolicy::cpuProbeLimit(gfx::Backend::Vulkan)==16);
static_assert(LumaWeaveBackendPolicy::cpuProbeLimit(gfx::Backend::GL33)==8);
static_assert(LumaWeaveBackendPolicy::cpuProbeLimit(gfx::Backend::Metal)==8);

int main() {
    FakeDevice device;
    for(const gfx::Backend backend:{
        gfx::Backend::GLES3,gfx::Backend::GL33,
        gfx::Backend::Vulkan,gfx::Backend::Metal
    }) {
        device.info_.backend=backend;
        // Even if an unrelated backend advertises an abstract compute API,
        // the existing LumaWeave MSL producer is Metal-only.
        device.computeEnabled=true;
        const auto caps=LumaWeaveBackendPolicy::inspect(device);
        AIVERSE_TEST_CHECK(caps.backend==backend);
        AIVERSE_TEST_CHECK(caps.portableCpuProbeAtlas);
        AIVERSE_TEST_CHECK(caps.defaultCpuProbeBudget>0);
        AIVERSE_TEST_CHECK(caps.metalComputeCandidate==
                            (backend==gfx::Backend::Metal));
        AIVERSE_TEST_CHECK(LumaWeaveBackendPolicy::cpuBudget(backend,0)==0);
        AIVERSE_TEST_CHECK(LumaWeaveBackendPolicy::cpuBudget(backend,100000)
                            ==caps.defaultCpuProbeBudget);
        AIVERSE_TEST_CHECK(LumaWeaveBackendPolicy::cpuBudget(backend,1)==1);
    }
    // Portable CPU GI is only presentable with an accepted trace BVH.
    AIVERSE_TEST_CHECK(!LumaWeaveBackendPolicy::portableGiGeometryCurrent(false,4));
    AIVERSE_TEST_CHECK(!LumaWeaveBackendPolicy::portableGiGeometryCurrent(true,0));
    AIVERSE_TEST_CHECK(LumaWeaveBackendPolicy::portableGiGeometryCurrent(true,4));
    AIVERSE_TEST_CHECK(LumaWeaveBackendPolicy::portableGiGeometryCurrent(true,4,4));
    AIVERSE_TEST_CHECK(!LumaWeaveBackendPolicy::portableGiGeometryCurrent(true,4,5));
    AIVERSE_TEST_CHECK(!LumaWeaveBackendPolicy::portableGiGeometryCurrent(true,5,4));
    device.info_.backend=gfx::Backend::GLES3;
    device.computeEnabled=false;
    const auto mobile=LumaWeaveBackendPolicy::inspect(device);
    AIVERSE_TEST_CHECK(!mobile.metalComputeCandidate);
    AIVERSE_TEST_CHECK(mobile.portableCpuProbeAtlas);
    return 0;
}
