#include <aiverselib/gameplay/Diagnostics.h>
#include <aiverselib/gameplay/Input.h>
#include <aiverselib/gameplay/Camera.h>
#include <aiverselib/gameplay/Configured.h>
#include <aiverselib/gameplay/Squads.h>
#include <aiverselib/gameplay/Feedback.h>
#include <aiverselib/gameplay/Tutorial.h>
#include <aiverselib/gameplay/Saves.h>
#include <aiverselib/gameplay/NativeCommands.h>
#include "engine/data/Json.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <limits>
#define CHECK(...) do { if(!(__VA_ARGS__)){std::cerr<<"Failed "<<__LINE__<<": "<<#__VA_ARGS__<<'\n';std::abort();} }while(false)
int main(){
    using namespace aiverse::lib::gameplay;
    DecisionTrace trace(2);for(int i=0;i<4;++i)trace.record({i,0,0,{Reason::Blocked}});CHECK(trace.events().size()==2&&trace.events().front().tick==2);CHECK(reasonKey(Reason::Blocked)=="library.reason.blocked");
    const std::array bindings{Binding{2,20,4,1,2,5},Binding{1,10,4,1,2,5}};
    CHECK(resolveInput(bindings,{4,1,2})==10);CHECK(!resolveInput(bindings,{4,0,2}));CHECK(deadzone(-1000,100)==-1000&&deadzone(99,100)==0);
    std::vector<std::int64_t> selection{3,1};const std::array<std::int64_t,3> picked{3,2,2};updateSelection(selection,picked,SelectionMode::Toggle);CHECK(selection==std::vector<std::int64_t>({1,2}));
    camera::Pose pose{0,0,20,179,45},target{100,20,100,-179,45};camera::Limits limits;CHECK(camera::advance(pose,target,limits,1,8));CHECK(pose.x>90&&pose.yaw<0);CHECK(!camera::advance(pose,target,limits,-1,8));
    auto invalidLimits=limits;invalidLimits.minZoom=300;CHECK(!camera::constrain(pose,invalidLimits));
    Placement place{0,0,2,3,1,1,5,true};int cells=0;
    CHECK(checkPlacement(place,[&](Cell c){++cells;CHECK(c.x<3&&c.z<2);return CellInfo{true,1,3,0};}));CHECK(cells==6);
    CHECK(checkPlacement(place,[](Cell){return CellInfo{false,1,0,42};}).subject==42);
    CHECK(checkPlacement(place,[](Cell){return CellInfo{true,2,0,0};}).reason==Reason::Territory);
    const std::array costs{Cost{1,3},Cost{1,3}};CHECK(checkCosts(costs,[](auto){return 5;}).reason==Reason::Resources);
    CHECK(refund(std::numeric_limits<std::int64_t>::max(),1000)==std::numeric_limits<std::int64_t>::max());
    AbilityConfig ability{TargetKind::Unit,2,10,5,3,7,true};AbilityState state;AbilityContext context{TargetKind::Unit,5,10,true,true};
    CHECK(beginAbility(ability,state,context,10));CHECK(state.finishTick==13&&state.readyTick==20);CHECK(!completeAbility(state,12));CHECK(completeAbility(state,13));CHECK(checkAbility(ability,state,context,19).reason==Reason::Cooldown);
    CHECK(beginAbility(ability,state,context,20));CHECK(cancelAbility(ability,state));CHECK(!cancelAbility(ability,state));
    CHECK(checkAbility(ability,{},context,std::numeric_limits<std::int64_t>::max()).reason==Reason::Invalid);
    const ItemType potion{1,5,2,1};auto lookup=[&](std::int64_t id)->const ItemType*{return id==1?&potion:nullptr;};InventoryConfig bag{2,20};std::vector<ItemStack> a,b;
    CHECK(addItem(a,bag,{1,7},lookup));CHECK(a.size()==2&&a[1].count==2);CHECK(!addItem(a,bag,{1,4},lookup));CHECK(a[1].count==2);
    CHECK(transferItem(a,b,bag,{1,3},lookup));CHECK(a[0].count==2&&b[0].count==3);CHECK(!transferItem(a,b,{0,20},{1,1},lookup));CHECK(a[0].count==2);CHECK(canEquip(potion,0)&&!canEquip(potion,64));
    std::vector<Equipment> equipment;CHECK(equipItem(b,equipment,1,0,lookup));CHECK(equipment.size()==1);CHECK(!equipItem(b,equipment,1,0,lookup));CHECK(unequipItem(b,equipment,bag,0,lookup));CHECK(equipment.empty());
    const std::array ingredients{ItemStack{1,1}},products{ItemStack{1,2}};CHECK(craftItems(b,bag,ingredients,products,lookup));
    ControlGroups groups(2);CHECK(groups.assign(0,picked));CHECK(groups.recall(0,[](auto id){return id!=3;})==std::vector<std::int64_t>({2}));CHECK(!groups.assign(3,picked));
    DragGesture gesture;gesture.begin(0,0);CHECK(!gesture.isDrag(2,2,5)&&gesture.isDrag(6,0,5));gesture.cancel();CHECK(!gesture.isDrag(10,10,5));
    const std::array members{Member{3,0,1},Member{1,0,1},Member{2,1,9,false}};CHECK(selectLeader(members)==1);auto slots=formationSlots(members,Formation::Box,10,1);CHECK(slots&&slots->size()==2&&(*slots)[0].entity==1&&(*slots)[1].offset.z==10);
    CHECK(!formationSlots(members,Formation::Box,0));
    FeedbackRule rule{1,3,5,true,{{FeedbackKind::Sound,7,1000},{FeedbackKind::Particle,8,1000}}};std::array rules{rule};FeedbackState feedback;int emitted=0;auto emit=[&](auto,auto){++emitted;};
    dispatchFeedback(rules,feedback,{3,4,10,true},emit);dispatchFeedback(rules,feedback,{3,4,11,true},emit);CHECK(emitted==2);dispatchFeedback(rules,feedback,{3,4,15,true},emit);CHECK(emitted==4);
    const std::array steps{TutorialStep{0,1,2,1,100,false,{7}},TutorialStep{1,2,1,-1,200,true,{}}};TutorialState tutorial;CHECK(validateTutorial(steps));CHECK(!tutorialAllows(steps,tutorial,7,true));CHECK(tutorialAllows(steps,tutorial,7,false));CHECK(!tutorialEvent(steps,tutorial,1,true));
    CHECK(tutorialEvent(steps,tutorial,1));CHECK(tutorial.step==0&&tutorial.progress==1);CHECK(tutorialEvent(steps,tutorial,1));CHECK(tutorial.step==1);CHECK(tutorialEvent(steps,tutorial,0,true));CHECK(tutorial.complete);
    using Store=std::map<int,int>;Store store{{1,3}};std::int64_t version=1;
    const std::array<Migration<Store>,2> migrations{{{1,2,[](auto& s){s[1]+=2;return true;}},{2,3,[](auto& s){s[1]=0;return false;}}}};
    CHECK(!migrate<Store>(store,version,3,migrations));CHECK(version==1&&store[1]==3);CHECK(migrate<Store>(store,version,2,migrations));CHECK(version==2&&store[1]==5);
    auto parsed=readGameplaySettings(aiverse::Json::parse(R"({"schemaVersion":1,"inventory":{"slots":4},"construction":{"requireTerritory":true}})"));CHECK(parsed.value&&parsed.value->inventory.slots==4&&parsed.value->construction.requireTerritory);
    CHECK(!readGameplaySettings(aiverse::Json::parse(R"({"schemaVersion":1,"inventory":{"slots":1.5}})")).value);
    CHECK(!readGameplaySettings(aiverse::Json::parse(R"({"schemaVersion":1,"camera":{"minZoom":200,"maxZoom":1}})")).value);
    CHECK(!readGameplaySettings(aiverse::Json::parse(R"({"schemaVersion":1,"unknown":{}})")).value);
    CHECK(!readGameplaySettings(aiverse::Json::parse(R"({"schemaVersion":1,"input":{"allowRebind":1}})")).value);
    const std::array entities{aiverse::Entity{3,1},aiverse::Entity{1,1},aiverse::Entity{3,1}};
    auto cmd=buildCommand(20,1,7,entities,4,{},true);CHECK(cmd.type==aiverse::game::kCmdBuild&&cmd.units.size()==2&&cmd.units[0].index==1&&cmd.flags==aiverse::kCmdQueue);
    CHECK(abilityCommand(20,1,8,entities,2,{}).type==aiverse::game::kCmdAbility);CHECK(formationCommand(20,1,9,entities,1).type==aiverse::game::kCmdFormation);
    std::cout<<"Gameplay configuration, commands and subsystem scenarios passed\n";
}
