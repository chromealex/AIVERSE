#include "aiverselib/world/WorldProcesses.h"
#include "tests/AlwaysCheck.h"
using namespace aiverse::lib::worldprocess;
int main(){
 auto s=supported(4,{true,false,false,false},{{0,1,10},{1,2,0},{2,3,4}});
 AIVERSE_TEST_CHECK((s==std::vector<bool>{true,true,false,false}));
 std::vector<I> gas{10,0,0};AIVERSE_TEST_CHECK(diffuse(gas,{{0,1,3},{1,2,10}}));
 AIVERSE_TEST_CHECK((gas==std::vector<I>{7,3,0}));
 auto before=gas;AIVERSE_TEST_CHECK(!diffuse(gas,{{0,5,3}})&&gas==before);
 I pos=2000;AIVERSE_TEST_CHECK(relax(pos,1000,500)&&pos==1500);AIVERSE_TEST_CHECK(!relax(pos,1000,1001));
 auto sound=audibility(3,0,100,{{0,1,30},{1,2,20},{0,2,90}});
 AIVERSE_TEST_CHECK((sound==std::vector<I>{100,70,50}));
 auto slots=assignSlots({{0,1},{0,1},{0,1}},2);
 AIVERSE_TEST_CHECK((slots==std::vector<I>{0,1,-1}));
 I imprint=9;AIVERSE_TEST_CHECK(erode(imprint,3)&&imprint==6);AIVERSE_TEST_CHECK(erode(imprint,100)&&imprint==0);
 auto power=energize(4,{true,false,false,false},{{0,1},{1,2},{2,1}});
 AIVERSE_TEST_CHECK((power==std::vector<bool>{true,true,true,false}));
 std::vector<I> sand{4,0,0,0,0,0};AIVERSE_TEST_CHECK(settle(sand,2,3,4));
 AIVERSE_TEST_CHECK((sand==std::vector<I>{0,0,4,0,0,0}));
 I condition=20;AIVERSE_TEST_CHECK(age(condition,3,5)&&condition==5);AIVERSE_TEST_CHECK(age(condition,3,100)&&condition==0);
 auto news=shareKnowledge({true,false,false},{{0,1},{1,2}});
 AIVERSE_TEST_CHECK((news==std::vector<bool>{true,true,false}));
}
