#include "engine/render/GiTextureBudget.h"
#include "engine/render/GiPresentationPolicy.h"
#include "engine/render/LumaWeaveLights.h"
#include "tests/AlwaysCheck.h"
using namespace aiverse::render;
int main() {
    AIVERSE_TEST_CHECK(giDynamicColorPassAllowed(false,false));
    AIVERSE_TEST_CHECK(giDynamicColorPassAllowed(true,false));
    AIVERSE_TEST_CHECK(giDynamicColorPassAllowed(true,true));
    AIVERSE_TEST_CHECK(!giDynamicColorPassAllowed(false,true));
    AIVERSE_TEST_CHECK(giFragmentTextureSlotsRequired(false,false,false)==14);
    AIVERSE_TEST_CHECK(giFragmentTextureSlotsRequired(true,false,false)==15);
    AIVERSE_TEST_CHECK(giFragmentTextureSlotsRequired(true,true,false)==16);
    AIVERSE_TEST_CHECK(giFragmentTextureSlotsRequired(true,true,true)==17);
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(16,true,true,true));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(17,true,true,true));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(16,false,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(16,true,false,true));
    LumaLight direct;
    direct.key=1234;
    AIVERSE_TEST_CHECK(lumaValidLight(direct));
    AIVERSE_TEST_CHECK(!direct.contributesToGi);
    direct.contributesToGi=true;
    AIVERSE_TEST_CHECK(direct.contributesToGi);
}
