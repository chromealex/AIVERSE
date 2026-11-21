#include "apps/studio-native/StudioAuthoredLights.h"
#include "tests/TestCheck.h"
#include <iostream>
using namespace aiverse;
int main() {
    editor::MapDoc doc;
    doc.data.extra=Json::parse(R"({"lighting":{"time":0.5},"lights":[
      {"x":5,"z":3,"height":2,"radius":7,"intensity":3,"color":[0.1,0.5,0.9]},
      {"x":1,"z":1,"nightOnly":true},
      {"x":2,"z":2,"type":"spot","angle":50,"pitch":30,"yaw":45}
    ]})");
    auto lights=studio::studioAuthoredLights(doc,false);
    AIVERSE_TEST_CHECK(lights.size()==2); // night-only hidden at noon
    AIVERSE_TEST_CHECK(lights[0].pos.x==5 && lights[0].pos.y==2);
    AIVERSE_TEST_CHECK(lights[0].radius==7 && lights[0].intensity==3);
    AIVERSE_TEST_CHECK(lights[0].color.z>.8f);
    AIVERSE_TEST_CHECK(lights[1].spot());
    doc.data.extra.member("lighting").set("time",Json::number(.9,3));
    lights=studio::studioAuthoredLights(doc,false);
    AIVERSE_TEST_CHECK(lights.size()==3);
    // FPS authoring reads FPS JSON rather than discarded MapDoc.extra.
    doc.data.fps=Json::parse(R"({"lights":[{"x":9,"z":8,"height":3,"radius":4}]})");
    const auto fps=studio::studioAuthoredLights(doc,true);
    AIVERSE_TEST_CHECK(fps.size()==1 && fps[0].pos.x==9 && fps[0].pos.y==3);
    auto many=Json::array();for(size_t i=0;i<1500;++i)many.push(Json::parse(R"({"radius":4,"intensity":1})"));
    doc.data.fps.set("lights",many);
    AIVERSE_TEST_CHECK(studio::studioAuthoredLights(doc,true).size()==render::LightSystem::kMaxLights);
    AIVERSE_TEST_CHECK(studio::studioAuthoredLights(doc,true,1.5f,4096).size()==1500);
    std::cout<<"Studio authored lights PASS\n";
}
