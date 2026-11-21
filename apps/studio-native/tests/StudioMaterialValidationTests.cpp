#include "apps/studio-native/StudioMaterialValidation.h"
#include "tests/AlwaysCheck.h"
#include <iostream>
#include <limits>
using namespace aiverse::studio;
int main(){
  AIVERSE_TEST_CHECK(validStudioMaterialPreset("matte"));
  AIVERSE_TEST_CHECK(validStudioMaterialPreset("metal"));
  AIVERSE_TEST_CHECK(validStudioMaterialPreset("glass"));
  AIVERSE_TEST_CHECK(!validStudioMaterialPreset("../../../shader"));
  AIVERSE_TEST_CHECK(!validStudioMaterialPreset("custom"));
  AIVERSE_TEST_CHECK(validStudioMaterialName("steel_1"));
  AIVERSE_TEST_CHECK(!validStudioMaterialName("../bad")); AIVERSE_TEST_CHECK(!validStudioMaterialName(""));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("metallic",0));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("roughness",1));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("shadowDiffusion",0));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("shadowDiffusion",.025));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("shadowDiffusion",1));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("shadowDiffusion",-1));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("shadowDiffusion",1.001));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("shadowDiffusion",std::numeric_limits<double>::quiet_NaN()));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("thinTransmission",.35));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("thinTransmission",1.1));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("thinTransmission",std::numeric_limits<double>::quiet_NaN()));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("sssStrength",.6));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("sssThickness",.06));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("sssThickness",-1));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("sssScale",2));
  AIVERSE_TEST_CHECK(validStudioMaterialText("sssColor","1, 0.4, 0.2"));
  AIVERSE_TEST_CHECK(!validStudioMaterialText("sssColor","1, 0.4, 2"));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("metallic",1.001));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("metallic",std::numeric_limits<double>::infinity()));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("texture",.5));
  AIVERSE_TEST_CHECK(validStudioMaterialNumber("normalScale",12));
  AIVERSE_TEST_CHECK(!validStudioMaterialNumber("emissiveStrength",101));
  AIVERSE_TEST_CHECK(validStudioMaterialBoolean("doubleSided")); AIVERSE_TEST_CHECK(!validStudioMaterialBoolean("delete"));
  AIVERSE_TEST_CHECK(validStudioMaterialText("alpha","blend"));
  AIVERSE_TEST_CHECK(!validStudioMaterialText("alpha","transparent"));
  AIVERSE_TEST_CHECK(validStudioMaterialText("team","tint"));
  AIVERSE_TEST_CHECK(!validStudioMaterialText("base","../x"));
  std::cout<<"Studio material validation PASS\n";
}
