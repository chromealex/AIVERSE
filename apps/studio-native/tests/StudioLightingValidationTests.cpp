#include "apps/studio-native/StudioLightingValidation.h"
#include "tests/TestCheck.h"
#include <limits>
#include <iostream>
using namespace aiverse::studio;
int main() {
    AIVERSE_TEST_CHECK(validLightingScalar("time",0)); AIVERSE_TEST_CHECK(validLightingScalar("time",1));
    AIVERSE_TEST_CHECK(!validLightingScalar("time",1.000001)); AIVERSE_TEST_CHECK(!validLightingScalar("storm",-0.01));
    AIVERSE_TEST_CHECK(!validLightingScalar("arbitrary",0.5));
    AIVERSE_TEST_CHECK(validLightingScalar("dayLength",86400));
    AIVERSE_TEST_CHECK(!validLightingScalar("dayLength",100000001));
    AIVERSE_TEST_CHECK(!validLightingScalar("clouds",std::numeric_limits<double>::quiet_NaN()));
    AIVERSE_TEST_CHECK(validLightScalar("x",-1000000)); AIVERSE_TEST_CHECK(!validLightScalar("z",1000001));
    AIVERSE_TEST_CHECK(validLightScalar("radius",0.1)); AIVERSE_TEST_CHECK(!validLightScalar("radius",0));
    AIVERSE_TEST_CHECK(!validLightScalar("intensity",std::numeric_limits<double>::infinity()));
    AIVERSE_TEST_CHECK(validLightText("name","My lamp"));
    AIVERSE_TEST_CHECK(validLightText("bake","mixed")); AIVERSE_TEST_CHECK(!validLightText("bake","unknown"));
    AIVERSE_TEST_CHECK(validLightText("type","point")); AIVERSE_TEST_CHECK(!validLightText("type","directional"));
    AIVERSE_TEST_CHECK(validLightBoolean("nightOnly")); AIVERSE_TEST_CHECK(!validLightBoolean("remove"));
    std::cout << "Studio lighting validation PASS\n";
}
