#include "Definitions.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsScript.h"
#include "modules/rts/RtsTiles.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::strategy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
bool nativeAdmissionProbe();
int main(int argc,char** argv){try{
    CHECK(nativeAdmissionProbe());
    CHECK(argc==2);std::filesystem::path root=argv[1];auto manifest=Json::parseFile((root/"game.json").string());auto rules=Json::parseFile((root/"rules.json").string());auto map=Json::parseFile((root/manifest["maps"][size_t(0)].getStr("file")).string());std::string error;
    auto read=[&](const std::string& p,std::string& bytes){std::ifstream f(root/p,std::ios::binary);if(!f)return false;bytes.assign(std::istreambuf_iterator<char>(f),{});return true;};
    CHECK(game::applyTilesToMap(map,read,&error));CHECK(game::embedScripts(rules,map,read,&error));
    Simulation a({3,20,0},std::make_unique<game::RtsModule>()),b({3,20,0},std::make_unique<game::RtsModule>());a.setup(rules,map);b.setup(rules,map);
    auto send=[](Simulation& sim,uint8_t player,int64_t action){Command cmd;cmd.type=32768;cmd.player=player;cmd.param=action;sim.submit(cmd);sim.step();};
    send(a,2,1);
    PushHistory rejected;auto* initialStore=a.world().resource<game::NativeStore>();CHECK(initialStore&&initialStore->modules.size()==1);
    CHECK(restoreState(initialStore->modules[0],"puzzle",rejected)&&rejected.current.player==6&&rejected.past.empty());
    send(a,1,1);send(a,1,4);send(a,1,2);send(a,2,2);send(a,1,6);send(a,1,9);send(a,1,10);send(a,1,13);
    auto* store=a.world().resource<game::NativeStore>();CHECK(store&&store->modules.size()==1);auto& state=store->modules[0];PushHistory puzzle;CardState cards;TurnState turns;
    CHECK(restoreState(state,"puzzle",puzzle)&&pushSolved(strategy_demo::board(),puzzle.current));
    CHECK(restoreState(state,"cards",cards)&&cards.stack.size()==2);CHECK(restoreState(state,"turns",turns)&&turns.pending.size()==1);
    ZoneState zones;RhythmState rhythm;CHECK(restoreState(state,"zones",zones)&&zones.zone==1&&zones.abilities==1);
    CHECK(restoreState(state,"rhythm",rhythm)&&rhythm.results[0]==Judgement::Holding);
    RunState run;CHECK(restoreState(state,"run",run)&&run.phase==RunPhase::Reward);
    MachineState machine;CHECK(restoreState(state,"machine",machine)&&machine.pc==1&&machine.registers[0]==3);
    CHECK(b.deserialize(a.serialize(),&error));CHECK(a.checksum()==b.checksum());
    for(uint8_t player:{uint8_t(1),uint8_t(2),uint8_t(1),uint8_t(2)}){send(a,player,3);send(b,player,3);CHECK(a.checksum()==b.checksum());}
    send(a,1,5);send(b,1,5);CHECK(a.checksum()==b.checksum());
    for(auto action:{7,8}){send(a,1,action);send(b,1,action);CHECK(a.checksum()==b.checksum());}
    for(auto action:{11,11,12,10,11,12,10,11}){send(a,1,action);send(b,1,action);CHECK(a.checksum()==b.checksum());}
    for(int i=0;i<200;++i){a.step();b.step();CHECK(a.checksum()==b.checksum());}
    store=a.world().resource<game::NativeStore>();CHECK(restoreState(store->modules[0],"cards",cards)&&cards.stack.empty()&&cards.seats[1].health==28);
    OfflineState offline;FactoryState factory;BigNumber number;CHECK(restoreState(store->modules[0],"offline",offline)&&offline.tick==a.tick());CHECK(restoreState(store->modules[0],"factory",factory)&&factory.tick==a.tick());CHECK(restoreState(store->modules[0],"number",number)&&number.decimal()==std::to_string(a.tick()+1));
    CHECK(restoreState(store->modules[0],"zones",zones)&&zones.zone==2);
    CHECK(restoreState(store->modules[0],"rhythm",rhythm)&&rhythm.score==1000&&rhythm.results[0]==Judgement::Perfect);
    CHECK(restoreState(store->modules[0],"run",run)&&run.phase==RunPhase::Completed&&run.claims.size()==3);
    CHECK(restoreState(store->modules[0],"machine",machine)&&machine.output==std::vector<int32_t>({6,8}));
    std::cout<<"External native strategy module: ten simulation libraries and host admission, routed commands, pending reactions and 200-tick restore OK\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
