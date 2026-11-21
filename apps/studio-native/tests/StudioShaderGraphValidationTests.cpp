#include "apps/studio-native/StudioShaderGraphValidation.h"
#include "tests/TestCheck.h"
#include <iostream>
#include <string>
using namespace aiverse::studio;
int main() {
 AIVERSE_TEST_CHECK(validStudioShaderGraphName("mat_tint-1"));
 AIVERSE_TEST_CHECK(validStudioShaderGraphName("A"));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphName("../secret"));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphName("foo/bar"));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphName("foo.bar"));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphName(""));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphName(std::string(65,'z')));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphName("name space"));
 std::cout<<"Studio Shader Graph name validation PASS\n";
}
