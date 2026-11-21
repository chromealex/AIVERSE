#include "apps/studio-native/StudioFpsLightingContract.h"
#include "tests/TestCheck.h"
#include <iostream>
using namespace aiverse::studio;
int main() {
    AIVERSE_TEST_CHECK(validFpsLightingChange("studio.lightingSet","time",.5,true,false,{},false));
    AIVERSE_TEST_CHECK(!validFpsLightingChange("studio.lightingSet","time",1.5,true,false,{},false));
    AIVERSE_TEST_CHECK(!validFpsLightingChange("studio.lightingSet","name",.5,true,false,{},false));
    AIVERSE_TEST_CHECK(validFpsLightingChange("studio.lightUpdate","name",0,false,true,"Sun",false));
    AIVERSE_TEST_CHECK(validFpsLightingChange("studio.lightUpdate","nightOnly",0,false,false,{},true));
    AIVERSE_TEST_CHECK(!validFpsLightingChange("studio.lightUpdate","deleteAll",0,false,false,{},true));
    AIVERSE_TEST_CHECK(!validFpsLightingChange("someArbitraryOp","x",0,true,false,{},false));
    std::cout << "Studio FPS Lighting authorization PASS\n";
}
