#include "aiverselib/world/WorldProcesses.h"
#include <cassert>
using namespace aiverse::lib::worldprocess;
int main(){
 auto s=supported(4,{true,false,false,false},{{0,1,10},{1,2,0},{2,3,4}});
 assert((s==std::vector<bool>{true,true,false,false}));
 std::vector<I> gas{10,0,0};assert(diffuse(gas,{{0,1,3},{1,2,10}}));
 assert((gas==std::vector<I>{7,3,0}));
 auto before=gas;assert(!diffuse(gas,{{0,5,3}})&&gas==before);
 I pos=2000;assert(relax(pos,1000,500)&&pos==1500);assert(!relax(pos,1000,1001));
 auto sound=audibility(3,0,100,{{0,1,30},{1,2,20},{0,2,90}});
 assert((sound==std::vector<I>{100,70,50}));
 auto slots=assignSlots({{0,1},{0,1},{0,1}},2);
 assert((slots==std::vector<I>{0,1,-1}));
 I imprint=9;assert(erode(imprint,3)&&imprint==6);assert(erode(imprint,100)&&imprint==0);
 auto power=energize(4,{true,false,false,false},{{0,1},{1,2},{2,1}});
 assert((power==std::vector<bool>{true,true,true,false}));
 std::vector<I> sand{4,0,0,0,0,0};assert(settle(sand,2,3,4));
 assert((sand==std::vector<I>{0,0,4,0,0,0}));
 I condition=20;assert(age(condition,3,5)&&condition==5);assert(age(condition,3,100)&&condition==0);
 auto news=shareKnowledge({true,false,false},{{0,1},{1,2}});
 assert((news==std::vector<bool>{true,true,false}));
}
