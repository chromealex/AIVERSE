#include "engine/bots/Persistence.h"
#include "engine/game/GameCore.h"
#include <aiverselib/bots/Waypoints.h>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::bots;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
Json rules(){return Json::parse(R"({"actor":{"mode":"deathmatch","roundTicks":10000,"weapon":{"magazine":3,"reserve":100,"reloadTicks":15}}})");}
Json map(){return Json::parse(R"({"players":[{"name":"A","team":1},{"name":"B","team":2},{"name":"C","team":1}],"spawns":[[0,0.01,0],[0,0.01,10],[20,0.01,0]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");}
std::unique_ptr<Simulation> make(Json r=rules(),Json m=map()){
    auto sim=std::make_unique<Simulation>(SimConfig{1,60,0},std::make_unique<game::GameCore>());sim->setup(r,m);return sim;
}
actor::ActorHost& module(Simulation& s){return actor::host(s);}
actor::Input input(const Command& cmd){actor::Input in;CHECK(actor::decodeInput(cmd,in));return in;}
void policies(){
    for(auto preset:{"easy","normal","hard","guard","rusher","survivor"})CHECK(valid(fpsConfig(Json::parse(std::string("{\"preset\":\"")+preset+"\"}"))));
    for(auto bad:{R"({"preset":"wrong"})",R"({"sensorTicks":0})",R"({"reactionTicks":1.5})",R"({"aimError":true})",R"({"unknown":1})"}){
        bool threw=false;try{fpsConfig(Json::parse(bad));}catch(const JsonError&){threw=true;}CHECK(threw);
    }
    auto sim=make();auto& m=module(*sim);ActorState s;auto c=fpsConfig(Json::parse(R"({"preset":"hard","reactionTicks":6,"strafe":0})"));
    auto first=fpsCommand(*sim,m,1,c,{},s);CHECK(first&&!(input(*first).buttons&actor::Fire));CHECK(s.target==2);
    CHECK(!fpsCommand(*sim,m,1,c,{},s));CHECK(!fpsCommand(*sim,m,0,c,{},s));
    sim->submit(*first);sim->step();
    // No wall hacks: preserve last known position while unseen target moves behind a wall.
    auto known=s.lastKnown;
    auto hidden=map();hidden.member("colliders").push(Json::parse(R"({"id":2,"min":[-4,0,3],"max":[4,4,5]})"));
    auto blocked=make(rules(),hidden);ActorState unseen;auto noTarget=fpsCommand(*blocked,module(*blocked),1,c,{},unseen);CHECK(noTarget&&unseen.target==0&&!(input(*noTarget).buttons&actor::Fire));
    m.character(*sim,2).pos={0_fx,Fixed::fromRatio(1,100),-10_fx};auto next=fpsCommand(*sim,m,1,c,{},s);CHECK(next&&s.lastKnown==known&&s.decision==ActorDecision::Investigate&&!(input(*next).buttons&actor::Fire));
    auto bytes=saveFps(s);ActorState restored;CHECK(loadFps(bytes,restored)&&saveFps(restored)==bytes);
    CHECK(!loadFps(bytes.substr(0,bytes.size()-1),restored)&&saveFps(restored)==bytes);CHECK(!loadFps(bytes+"x",restored));
    // Team mate in the actual firing ray suppresses fire.
    auto friendly=make();auto& fm=module(*friendly);fm.character(*friendly,3).pos={0_fx,Fixed::fromRatio(1,100),5_fx};
    ActorState fs;c.reactionTicks=0;c.turnPerTick=Fixed::fromRatio(1,2);auto safe=fpsCommand(*friendly,fm,1,c,{},fs);CHECK(safe&&!(input(*safe).buttons&actor::Fire));
    auto reload=make();module(*reload).character(*reload,1).ammo=0;ActorState rs;auto ri=fpsCommand(*reload,module(*reload),1,c,{},rs);CHECK(ri&&(input(*ri).buttons&actor::Reload));
    auto zoneRules=rules();zoneRules.member("actor").set("mode",Json::string("battleRoyale"));zoneRules.member("actor").set("zone",Json::parse(R"({"center":[30,0,0],"radius":5,"endRadius":1})"));
    auto zone=make(zoneRules);ActorState zs;CHECK(fpsCommand(*zone,module(*zone),1,c,{},zs)&&zs.decision==ActorDecision::Zone);
}
void expandedWeapons(){
    auto sim=make();auto& m=module(*sim);auto self=m.character(*sim,1);self.blindUntil=10;ActorState state;auto cfg=fpsConfig(Json::parse(R"({"preset":"hard","reactionTicks":0})"));
    auto blind=fpsCommand(*sim,m,1,cfg,{},state);CHECK(blind&&state.target==0&&!(input(*blind).buttons&actor::Fire));
    self.prone=true;CHECK(eye(self,m.config().motor).y==self.pos.y+actor::stanceHeight(self,m.config().motor)-Fixed::fromRatio(1,10));
    auto r=rules();r.member("actor").set("weapons",Json::parse(R"([{"range":100},{"range":2,"automatic":false,"intervalTicks":1}])"));
    auto ranged=make(r);auto& rm=module(*ranged);rm.character(*ranged,1).weaponIndex=1;ActorState rs;
    auto limited=fpsCommand(*ranged,rm,1,cfg,{},rs);CHECK(limited&&!(input(*limited).buttons&actor::Fire));
    r.member("actor").set("weapons",Json::parse(R"([{"range":100,"automatic":false,"intervalTicks":1}])"));
    auto semi=make(r);ActorState ss;uint32_t shots=0;
    for(int i=0;i<30;++i){auto cmd=fpsCommand(*semi,module(*semi),1,cfg,{},ss);CHECK(cmd);semi->submit(*cmd);semi->step();for(const auto& e:semi->events())shots+=e.type==1;}
    CHECK(shots>=2);
}
void navigation(){
    world::SpatialWorld w;CHECK(w.add({1,{{-2_fx,0_fx,3_fx},{2_fx,4_fx,7_fx}}}));
    auto nodes=readWaypoints(Json::parse(R"([{"point":[5,0,2],"links":[1]},{"point":[5,0,8],"links":[0],"cover":true}])"));
    auto step=waypointStep(w,nodes,{},{0_fx,0_fx,10_fx},Fixed::fromRatio(3,10),Fixed::fromRatio(18,10));CHECK(step&&step->x==5_fx);
    CHECK(!waypointStep(w,{}, {},{0_fx,0_fx,10_fx},Fixed::fromRatio(3,10),Fixed::fromRatio(18,10)));
    bool bad=false;try{readWaypoints(Json::parse(R"([{"point":[0,0,0],"links":[9]}])"));}catch(const JsonError&){bad=true;}CHECK(bad);
}
void combatRestore(){
    auto a=make(),b=make();auto c=fpsConfig(Json::parse(R"({"preset":"hard","halfFov":0.5,"strafe":0})"));ActorState states[2];
    for(int t=0;t<100;++t){for(uint8_t slot=1;slot<=2;++slot){auto cmd=fpsCommand(*a,module(*a),slot,c,{},states[slot-1]);CHECK(cmd);a->submit(*cmd);}a->step();}
    CHECK(module(*a).character(*a,1).ammo<3||module(*a).character(*a,1).kills>0);
    auto save=a->serialize();std::string error;CHECK(b->deserialize(save,&error));ActorState copy[2];
    for(size_t i=0;i<2;++i)CHECK(loadFps(saveFps(states[i]),copy[i]));
    for(int t=0;t<300;++t){for(uint8_t slot=1;slot<=2;++slot){
        auto ca=fpsCommand(*a,module(*a),slot,c,{},states[slot-1]),cb=fpsCommand(*b,module(*b),slot,c,{},copy[slot-1]);CHECK(ca&&cb);CHECK(input(*ca)==input(*cb));a->submit(*ca);b->submit(*cb);
    }a->step();b->step();CHECK(a->checksum()==b->checksum());}
    int kills=0;module(*a).forEachCharacter(*a,[&](actor::CharacterRef actor){kills+=actor.kills;});CHECK(kills>0);
}
int main(){try{policies();expandedWeapons();navigation();combatRestore();std::cout<<"FPS bot visibility, reaction, team safety, navigation, combat and 300-tick restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
