#include "apps/studio-native/StudioShaderGraphDraft.h"
#include "tests/TestCheck.h"
#include <iostream>
using namespace aiverse;
using namespace aiverse::studio;
int main(){
 auto graph=Json::parse(R"({"version":1,"domain":"material","nodes":[
   {"id":"source","op":"input","position":[12,25]},
   {"id":"tint","op":"constant","position":[230,120],"value":[1,0,0,1]}],
   "outputs":{"color":"tint"}})");
 AIVERSE_TEST_CHECK(validStudioShaderGraphDraft(graph));
 graph.member("nodes").at(0).set("position",Json::parse("[10,99]"));
 AIVERSE_TEST_CHECK(validStudioShaderGraphDraft(graph));
 graph.member("nodes").at(1).set("id",Json::string("source"));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphDraft(graph));
 graph.member("nodes").at(1).set("id",Json::string("tint"));
 graph.member("nodes").at(0).set("position",Json::parse("[1000001,20]"));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphDraft(graph));
 AIVERSE_TEST_CHECK(!validStudioShaderGraphDraft(Json::parse(R"({"domain":"unknown","nodes":[]})")));
 std::cout<<"Studio Shader Graph draft contract PASS\n";
}
