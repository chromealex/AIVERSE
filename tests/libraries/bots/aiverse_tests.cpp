#include <aiverselib/bots/Rts.h>
#include "modules/rts/RtsScript.h"
#include "modules/rts/RtsTiles.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace aiverse;using namespace aiverse::lib::bots;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==2);std::filesystem::path root=argv[1];auto manifest=Json::parseFile((root/"game.json").string());
    auto rules=Json::parseFile((root/"rules.json").string());auto map=Json::parseFile((root/manifest["maps"][size_t(0)].getStr("file")).string());std::string error;
    auto read=[&](const std::string& p,std::string& bytes){std::ifstream f(root/p,std::ios::binary);if(!f)return false;bytes.assign(std::istreambuf_iterator<char>(f),{});return true;};
    CHECK(game::applyTilesToMap(map,read,&error));CHECK(game::embedScripts(rules,map,read,&error));
    for(auto preset:{"easy","normal","hard","rush","defender","economist"})CHECK(rtsConfig(Json::parse(std::string("{\"preset\":\"")+preset+"\"}")).thinkTicks>0);
    auto settings=Json::parse(R"({"preset":"rush"})");
    Simulation a({3,20,0},std::make_unique<game::RtsModule>()),b({3,20,0},std::make_unique<game::RtsModule>());a.setup(rules,map);b.setup(rules,map);
    RtsBot one(1,3,settings),two(2,3,settings),restoreOne(1,3,settings),restoreTwo(2,3,settings);
    for(int i=0;i<300;++i){one.update(a);two.update(a);a.step();}
    CHECK(!a.executedCommands().empty());CHECK(b.deserialize(a.serialize(),&error));auto saved=one.save();CHECK(restoreOne.load(saved));CHECK(restoreTwo.load(two.save()));
    CHECK(!restoreOne.load(saved.substr(0,saved.size()-1)));CHECK(!restoreOne.load(saved+"x"));CHECK(!restoreTwo.load(saved));CHECK(restoreOne.save()==saved);
    for(int i=0;i<300;++i){one.update(a);two.update(a);restoreOne.update(b);restoreTwo.update(b);a.step();b.step();CHECK(a.checksum()==b.checksum());CHECK(one.save()==restoreOne.save());CHECK(two.save()==restoreTwo.save());}
    std::cout<<"RTS bot presets, command production and 300-tick fresh restore: OK\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
