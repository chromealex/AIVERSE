// Actual GameCore map fixture proving native module ECS hydration via mapPlaced().
#include "aiverselib/world/WorldProcessesNativeModule.h"
#include "engine/data/JsonCompose.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace aiverse;
namespace g=aiverse::game;
namespace wp=aiverse::lib::worldprocess;
static Json baseMap(){
    return Json::parseFile(std::string(AIVERSE_TEMPLATES_DIR)+"/mushrooms/maps/duel.json");
}
static Json mapWithProcesses(){
    Json m=baseMap();
    m.member("entities").at(1).set("components",Json::parse(
        R"({"EchoZone":{"enabled":true},"FractureMember":{"anchored":true}})"));
    m.member("entities").at(2).set("components",Json::parse(
        R"({"AetherGasCell":{"smoke":250,"toxin":50,"oxygen":500,"enabled":true}})"));
    return m;
}
static std::unique_ptr<Simulation> make(const Json& map,bool enable){
    Json rules=readComposedJson(std::string(AIVERSE_TEMPLATES_DIR)+
                                 "/mushrooms/rules.json",fileReader());
    std::vector<g::NativeDefinition> modules;
    if(enable){
        rules.set("nativeModules",Json::parse(R"([{"id":"world_processes"}])"));
        modules.push_back(wp::worldProcessesNativeDefinition());
    }
    auto sim=std::make_unique<Simulation>(SimConfig{1,20,0,true},
                                         std::make_unique<g::GameCore>(std::move(modules)));
    sim->setup(rules,map);
    return sim;
}
static Entity placed(const Simulation& sim,std::size_t i){
    auto& game=static_cast<const g::GameCore&>(sim.module());
    AIVERSE_TEST_CHECK(game.mapPlaced().size()>i);
    return game.mapPlaced()[i];
}
int main(){
    const Json map=mapWithProcesses();
    auto sim=make(map,true),peer=make(map,true);
    auto zone=placed(*sim,1),gas=placed(*sim,2);
    AIVERSE_TEST_CHECK(sim->world().alive(zone)&&sim->world().alive(gas));
    AIVERSE_TEST_CHECK(sim->world().get<echo::AcousticZone>(zone).enabled);
    AIVERSE_TEST_CHECK(sim->world().get<fracture::Member>(zone).anchored);
    AIVERSE_TEST_CHECK(sim->world().get<aether::GasCell>(gas).oxygen==500);
    AIVERSE_TEST_CHECK(sim->world().get<aether::GasCell>(gas).smoke==250);
    AIVERSE_TEST_CHECK(uint8_t(sim->serialize()[4])==10);
    AIVERSE_TEST_CHECK(sim->checksum()==peer->checksum());
    for(int i=0;i<5;++i){
        sim->step();peer->step();
        AIVERSE_TEST_CHECK(sim->checksum()==peer->checksum());
    }
    const std::string bytes=sim->serialize();
    auto restored=make(map,true);
    std::string err;
    AIVERSE_TEST_CHECK(restored->deserialize(bytes,&err));
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
    sim->world().enableCowSnapshots();
    SimSnapshot snap;sim->saveState(snap);
    sim->world().get<aether::GasCell>(gas).oxygen=1;
    sim->loadState(snap);
    AIVERSE_TEST_CHECK(sim->checksum()==restored->checksum());
    // Without the explicit project native module these authored configs do
    // not instantiate ECS component pools or alter the legacy game save.
    const auto original=make(map,false);
    AIVERSE_TEST_CHECK(!original->world().has<echo::AcousticZone>(placed(*original,1)));
    AIVERSE_TEST_CHECK(!original->world().has<aether::GasCell>(placed(*original,2)));
    AIVERSE_TEST_CHECK(uint8_t(original->serialize()[4])==9);
    Json invalid=map;
    invalid.member("entities").at(2).member("components")
        .member("AetherGasCell").set("oxygen",Json::number(int64_t(-5)));
    bool rejected=false;
    try{(void)make(invalid,true);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    std::cout<<"PASS native GameCore map World Processes hydration, opt-in, persistence, replay and rollback\n";
}
