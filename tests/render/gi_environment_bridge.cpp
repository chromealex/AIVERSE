#include "engine/render/GiEnvironmentBridge.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <stdexcept>

using namespace aiverse::render;

int main() {
    Lighting direct;
    const auto off=giEnvironmentFromLighting(direct);
    AIVERSE_TEST_CHECK(!off.enabled() && off.valid());

    const auto bounced=giEnvironmentFromLighting(direct,.5f,.25f);
    AIVERSE_TEST_CHECK(bounced.enabled());
    AIVERSE_TEST_CHECK(std::abs(bounced.sunIntensity-direct.sunIntensity*.5f)<1e-6f);
    AIVERSE_TEST_CHECK(std::abs(bounced.skyIntensity-.25f)<1e-6f);
    AIVERSE_TEST_CHECK(std::abs(dot(bounced.sunDirection,bounced.sunDirection)-1.0f)<1e-4f);
    AIVERSE_TEST_CHECK(bounced.sunColor.x==direct.sunColor.x);
    AIVERSE_TEST_CHECK(bounced.skyColor.z==direct.skyColor.z);

    direct.sunDir={0,0,0};
    // A disabled sun does not require an authored direction.
    AIVERSE_TEST_CHECK(giEnvironmentFromLighting(direct,0,.2f).valid());
    bool invalid=false;
    try { (void)giEnvironmentFromLighting(direct,1,0); }
    catch(const std::invalid_argument&) { invalid=true; }
    AIVERSE_TEST_CHECK(invalid);

    invalid=false;
    try { (void)giEnvironmentFromLighting(direct,-1,0); }
    catch(const std::invalid_argument&) { invalid=true; }
    AIVERSE_TEST_CHECK(invalid);
    invalid=false;
    direct.skyColor={0,-1,1};
    try { (void)giEnvironmentFromLighting(direct,0,1); }
    catch(const std::invalid_argument&) { invalid=true; }
    AIVERSE_TEST_CHECK(invalid);
}
