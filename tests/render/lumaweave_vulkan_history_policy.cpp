// Device-free temporal reuse policy regression. This test is Release-safe;
// GLSL/SPIR-V and real Vulkan filtering require separate on-device acceptance.
#include "engine/render/LumaWeaveVulkanHistoryPolicy.h"
#include "tests/AlwaysCheck.h"
#include <limits>

using namespace aiverse::render;

int main() {
    const LumaVulkanDenoiseSettings settings;
    AIVERSE_TEST_CHECK(LumaVulkanDenoiseSettings::valid(settings));
    auto prev=LumaVulkanHistoryStamp{};
    prev.viewKey=42;
    prev.viewEpoch=8;
    prev.sourceRevision=10;
    prev.geometryRevision=27;
    prev.width=64;prev.height=64;
    prev.eye={0,2,5};
    auto current=prev;
    const auto check=[&](LumaVulkanHistoryReason reason) {
        return LumaVulkanHistoryPolicy::compare(prev,current,settings,true)==reason;
    };
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::Accepted));
    AIVERSE_TEST_CHECK(
        LumaVulkanHistoryPolicy::compare(prev,current,settings,false)==
        LumaVulkanHistoryReason::Disabled);

    current.viewKey=43;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::ViewChanged));
    current=prev;current.viewEpoch=9;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::ViewCut));
    current=prev;current.sourceRevision=11;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::SourceChanged));
    current=prev;current.geometryRevision=28;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::GeometryChanged));
    current=prev;current.width=32;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::ResolutionChanged));
    current=prev;current.eye.x=3.0f;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::CameraCut));
    current=prev;current.eye.x=.1f;
    current.viewProjection.m[0]+=0.6f;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::CameraCut));
    current=prev;current.eye.x=.1f;
    current.viewProjection.m[0]+=.02f;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::Accepted));
    current=prev;current.viewKey=0;
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::InvalidIdentity));
    current=prev;current.viewProjection.m[0]=
        std::numeric_limits<float>::quiet_NaN();
    AIVERSE_TEST_CHECK(check(LumaVulkanHistoryReason::CameraCut));

    auto invalid=settings;invalid.historyWeight=1.5f;
    AIVERSE_TEST_CHECK(!LumaVulkanDenoiseSettings::valid(invalid));
    invalid=settings;invalid.normalAgreement=-1.f;
    AIVERSE_TEST_CHECK(!LumaVulkanDenoiseSettings::valid(invalid));
    invalid=settings;invalid.spatialDepthTolerance=
        std::numeric_limits<float>::infinity();
    AIVERSE_TEST_CHECK(!LumaVulkanDenoiseSettings::valid(invalid));
    return 0;
}
