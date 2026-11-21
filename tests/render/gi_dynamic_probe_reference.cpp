#include "engine/render/GiDynamicProbeReference.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    StaticShadowGeometry wall;
    wall.triangles = {
        {{-4,-4,1},{4,-4,1},{4,4,1},0},
        {{-4,-4,1},{4,4,1},{-4,4,1},0}
    };
    wall.albedos = {{1,1,1}};
    wall.build();

    GiDynamicProbeReference gi;
    gi.configure({-1,-1,-1},1,3,3,2,64,6);
    gi.setGeometry(&wall,1);
    const GiMovingLight red{42,{0,0,0},{1,0,0},4,50};
    gi.setLights({red});
    AIVERSE_TEST_CHECK(gi.scheduler().pending() == 18);

    const auto partial = gi.update(1,{0,0,0});
    AIVERSE_TEST_CHECK(partial.claimed == 1 && partial.published == 1);
    AIVERSE_TEST_CHECK(partial.primaryRays == 64 && partial.visibilityRays <= 64);
    AIVERSE_TEST_CHECK(gi.scheduler().pending() == 17);
    while (gi.scheduler().pending()) {
        const auto step = gi.update(4,{0,0,0});
        AIVERSE_TEST_CHECK(step.published > 0 && step.published <= 4);
    }
    AIVERSE_TEST_CHECK(gi.scheduler().inFlight() == 0);

    float strongestRed = 0;
    for (const auto& probe : gi.coefficients()) {
        const auto c = probe.evaluate({0,0,1});
        strongestRed = std::max(strongestRed, c.x);
        AIVERSE_TEST_CHECK(c.y == 0 && c.z == 0);
    }
    AIVERSE_TEST_CHECK(strongestRed > 0);

    // Reapplying the same stable light set does not trigger unnecessary work.
    gi.setLights({red});
    AIVERSE_TEST_CHECK(gi.scheduler().pending() == 0);

    // Turning off a moving light erases the independent dynamic indirect overlay.
    auto off = red; off.intensity = 0;
    gi.setLights({off});
    AIVERSE_TEST_CHECK(gi.scheduler().pending() > 0);
    while (gi.scheduler().pending()) gi.update(16,{0,0,0});
    for (const auto& probe : gi.coefficients())
        for (const auto& sh : probe.irradiance)
            AIVERSE_TEST_CHECK(sh.x == 0 && sh.y == 0 && sh.z == 0 && sh.w == 0);

    // Restoring the light and changing opaque surface albedo must cause a rebake.
    gi.setLights({red});
    while (gi.scheduler().pending()) gi.update(16,{0,0,0});
    wall.albedos[0] = {0,0,0};
    gi.setGeometry(&wall,2); // caller publishes a new immutable BVH snapshot/revision
    AIVERSE_TEST_CHECK(gi.scheduler().pending() == 18);
    while (gi.scheduler().pending()) gi.update(16,{0,0,0});
    for (const auto& probe : gi.coefficients())
        for (const auto& sh : probe.irradiance)
            AIVERSE_TEST_CHECK(sh.x == 0 && sh.y == 0 && sh.z == 0 && sh.w == 0);

    // No geometry => no stale bounce, no CPU per-frame tracing.
    gi.setGeometry(nullptr,3);
    AIVERSE_TEST_CHECK(gi.scheduler().pending() == 0);
    AIVERSE_TEST_CHECK(gi.update(100,{0,0,0}).primaryRays == 0);

    bool rejected = false;
    try { gi.setLights({red,red}); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);

    rejected = false;
    try { gi.configure({0,0,0},1,1,1,1,4,6); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    return 0;
}
