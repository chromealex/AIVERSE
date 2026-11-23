// Linked map-authoring -> authoritative ECS references across all 3 systems.
#include "aiverselib/world/WorldProcessesMapHydration.h"
#include "tests/AlwaysCheck.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;
namespace wa=aiverse::lib::worldprocess::authoring;
namespace wl=aiverse::lib::worldprocess::linked;

static Json linkedMap(){
    return Json::parse(R"({"entities":[
      {"id":"zone1","components":{"EchoZone":{"enabled":true}}},
      {"id":"zone2","components":{"EchoZone":{"enabled":true}}},
      {"id":"portal","components":{"EchoPortal":{
        "from":"zone1","to":"zone2","loss":25,"bidirectional":true,"enabled":true}}},
      {"id":"source","components":{"EchoEmitter":{
        "zone":"zone1","intensity":100,"enabled":true}}},
      {"id":"listener","components":{"EchoReceiver":{
        "zone":"zone2","threshold":30,"enabled":true}}},
      {"id":"base","components":{"FractureMember":{"anchored":true}}},
      {"id":"beam","components":{"FractureMember":{"anchored":false}}},
      {"id":"bond","components":{"FractureBond":{
        "first":"base","second":"beam","strength":100,"damage":0,"enabled":true}}},
      {"id":"air1","components":{"AetherGasCell":{
        "smoke":100,"toxin":0,"oxygen":0,"enabled":true}}},
      {"id":"air2","components":{"AetherGasCell":{
        "smoke":0,"toxin":0,"oxygen":0,"enabled":true}}},
      {"id":"duct","components":{"AetherDuct":{
        "from":"air1","to":"air2","maximumPerTick":10,
        "oneWay":false,"enabled":true}}},
      {"id":"flux","components":{"AetherFlux":{
        "cell":"air1","smoke":5,"toxin":0,"oxygen":0,"enabled":true}}}
    ]})");
}
template<int N>struct Host final:GameModule {
    std::vector<Entity> created;
    wa::MapHydration counts;
    std::string name()const override{return "world-processes-linked-host";}
    void setup(Simulation& sim,const Json&,const Json& map)override{
        for(const auto& entry:map["entities"].items()){
            (void)entry;created.push_back(sim.world().create());
        }
        counts=wa::hydrateMap(sim,map,created);
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<int N>std::unique_ptr<Simulation> make(const Json& map,bool cow=false){
    auto sim=std::make_unique<Simulation>(SimConfig{17,20,0,true},
                                         std::make_unique<Host<N>>());
    sim->setup(Json::object(),map);
    if(cow)sim->world().enableCowSnapshots();
    return sim;
}
int main(){
    auto portal=wl::defaults("EchoPortal");
    AIVERSE_TEST_CHECK(wl::validate("EchoPortal",portal));
    AIVERSE_TEST_CHECK(!wl::edit("EchoPortal","enabled",Json::fromBool(true),portal));
    AIVERSE_TEST_CHECK(wl::edit("EchoPortal","from",Json::string("zone1"),portal));
    AIVERSE_TEST_CHECK(wl::edit("EchoPortal","to",Json::string("zone2"),portal));
    AIVERSE_TEST_CHECK(wl::edit("EchoPortal","enabled",Json::fromBool(true),portal));
    AIVERSE_TEST_CHECK(!wl::edit("EchoPortal","loss",Json::number(int64_t(-1)),portal));
    auto bond=wl::defaults("FractureBond");
    AIVERSE_TEST_CHECK(!wl::edit("FractureBond","damage",Json::number(int64_t(2)),bond));
    auto flux=wl::defaults("AetherFlux");
    AIVERSE_TEST_CHECK(wl::edit("AetherFlux","smoke",Json::number(int64_t(-12)),flux));

    const Json map=linkedMap();
    auto a=make<1>(map),b=make<2>(map,true);
    auto& h=static_cast<Host<1>&>(a->module());
    auto& k=static_cast<Host<2>&>(b->module());
    AIVERSE_TEST_CHECK(h.counts.zones==2&&h.counts.portals==1&&
                      h.counts.emitters==1&&h.counts.receivers==1);
    AIVERSE_TEST_CHECK(h.counts.members==2&&h.counts.bonds==1);
    AIVERSE_TEST_CHECK(h.counts.gasCells==2&&h.counts.ducts==1&&h.counts.fluxes==1);
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticPortal>(h.created[2]).from==h.created[0]);
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticPortal>(h.created[2]).to==h.created[1]);
    AIVERSE_TEST_CHECK(a->world().has<echo::AcousticHearing>(h.created[4]));
    AIVERSE_TEST_CHECK(a->world().get<fracture::Bond>(h.created[7]).second==h.created[6]);
    AIVERSE_TEST_CHECK(a->world().get<aether::GasDuct>(h.created[10]).to==h.created[9]);

    a->step();b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticHearing>(h.created[4]).audible);
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticHearing>(h.created[4]).intensity==75);
    AIVERSE_TEST_CHECK(!a->world().get<fracture::Member>(h.created[6]).detached);
    AIVERSE_TEST_CHECK(a->world().get<aether::GasCell>(h.created[8]).smoke==95);
    AIVERSE_TEST_CHECK(a->world().get<aether::GasCell>(h.created[9]).smoke==10);
    const auto saved=a->serialize();
    auto restored=make<1>(map);
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(restored->checksum()==a->checksum());
    SimSnapshot snap;
    b->saveState(snap);
    b->world().get<fracture::Bond>(k.created[7]).damage=100;
    a->world().get<fracture::Bond>(h.created[7]).damage=100;
    b->step();a->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(a->world().get<fracture::Member>(h.created[6]).detached);
    b->loadState(snap);
    AIVERSE_TEST_CHECK(!b->world().get<fracture::Member>(k.created[6]).detached);

    auto bad=map;
    bad.member("entities").at(2).member("components")
       .member("EchoPortal").set("to",Json::string("never-existed"));
    bool failed=false;
    try {(void)make<3>(bad);}catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    bad=map;
    bad.member("entities").at(1).set("id",Json::string("zone1"));
    failed=false;
    try {(void)make<3>(bad);}catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    bad=map;
    bad.member("entities").at(1).member("components").erase("EchoZone");
    failed=false;
    try {(void)make<3>(bad);}catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    std::cout<<"PASS World Processes stable-id links, ECHO+FRACTURE+AETHER, save, replay and invalid refs\n";
}
