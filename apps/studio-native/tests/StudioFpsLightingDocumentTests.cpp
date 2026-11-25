#include "apps/studio-native/StudioFpsLightingDocument.h"
#include "tests/TestCheck.h"
#include <iostream>

using namespace aiverse;
using namespace aiverse::studio;
int main() {
    Json doc=Json::parse(R"({"name":"FPS Arena","colliders":[],"spawns":[[1,2,3]]})");
    AIVERSE_TEST_CHECK(fpsLightingView(doc)["lights"].size()==0);
    doc=editFpsLighting(doc,"studio.lightAdd",Json::object());
    AIVERSE_TEST_CHECK(doc["lights"].size()==1);
    AIVERSE_TEST_CHECK(doc["spawns"][0][0].toDouble()==1);
    Json p=Json::parse(R"({"index":0,"key":"intensity","value":3.5})");
    doc=editFpsLighting(doc,"studio.lightUpdate",p);
    AIVERSE_TEST_CHECK(doc["lights"][0]["intensity"].toDouble()==3.5);
    for (const char* patch : {R"({"index":0,"key":"type","value":"spot"})",
                              R"({"index":0,"key":"pattern","value":"beacon"})",
                              R"({"index":0,"key":"rate","value":2.5})",
                              R"({"index":0,"key":"sweep","value":28})"})
        doc=editFpsLighting(doc,"studio.lightUpdate",Json::parse(patch));
    const auto& beacon=doc["lights"][0];
    AIVERSE_TEST_CHECK(beacon.getStr("pattern")=="beacon" && beacon["rate"].toDouble()==2.5);
    AIVERSE_TEST_CHECK(beacon["sweep"].toDouble()==28 && beacon["intensity"].toDouble()==3.5);
    bool invalidRate=false;
    try { (void)editFpsLighting(doc,"studio.lightUpdate",Json::parse(R"({"index":0,"key":"rate","value":-1})")); }
    catch (const std::exception&) { invalidRate=true; }
    AIVERSE_TEST_CHECK(invalidRate);
    doc=editFpsLighting(doc,"studio.lightUpdate",Json::parse(R"({"index":0,"key":"color","value":[0.2,0.4,1]})"));
    doc=editFpsLighting(doc,"studio.lightUpdate",Json::parse(R"({"index":0,"key":"halo","value":1.25})"));
    AIVERSE_TEST_CHECK(doc["lights"][0]["color"][1].toDouble()==.4 && doc["lights"][0]["halo"].toDouble()==1.25);
    for (const char* channels: {"[1,1]","[1,1,1,1]","[1,2,0]","[0,false,0]"})
        AIVERSE_TEST_CHECK(!validStudioLightProperty("color",Json::parse(channels)));
    Json scalar=Json::parse(R"({"key":"storm","value":0.45})");
    doc=editFpsLighting(doc,"studio.lightingSet",scalar);
    AIVERSE_TEST_CHECK(doc["lighting"]["storm"].toDouble()==0.45);
    AIVERSE_TEST_CHECK(fpsLightingView(doc)["lighting"]["storm"].toDouble()==0.45);
    doc=editFpsLighting(doc,"studio.lightingSet",Json::parse(R"({"key":"preset","value":"cave"})"));
    AIVERSE_TEST_CHECK(doc["lighting"].getStr("preset")=="cave" && doc["lighting"]["storm"].toDouble()==0.45);
    doc=editFpsLighting(doc,"studio.lightingSet",Json::parse(R"({"key":"preset","value":""})"));
    AIVERSE_TEST_CHECK(!doc["lighting"].has("preset") && doc["lighting"].has("storm"));
    auto inherited=editFpsLighting(doc,"studio.lightingSet",Json::parse(R"({"key":"remove","value":true})"));
    AIVERSE_TEST_CHECK(!inherited.has("lighting") && inherited["lights"]==doc["lights"]);
    const Json before=doc;
    bool rejected=false;
    try { (void)editFpsLighting(doc,"studio.lightingSet",Json::parse(R"({"key":"storm","value":2})")); }
    catch (const std::exception&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected && doc == before);
    rejected=false;
    try { (void)editFpsLighting(doc,"studio.lightDelete",Json::parse(R"({"index":0.5})")); }
    catch (const std::exception&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected && doc == before);
    doc=editFpsLighting(doc,"studio.lightDelete",Json::parse(R"({"index":0})"));
    AIVERSE_TEST_CHECK(doc["lights"].size()==0);
    std::cout << "Studio FPS Lighting serialization PASS\n";
}
