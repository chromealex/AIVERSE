// Shared map->ECS installer: two generic native hosts, save/rollback & v9 opt-out.
#include "aiverselib/world/WorldProcessesMapHydration.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace wp=aiverse::lib::worldprocess::authoring;
template<int HostId>struct MapHost final:GameModule {
    Entity e0=kNullEntity,e1=kNullEntity;
    wp::MapHydration counts;
    std::string name()const override{return "world-processes-map-host";}
    void setup(Simulation& sim,const Json&,const Json& map)override {
        std::vector<Entity> entities;
        for(const auto& x:map["entities"].items()) {
            (void)x;
            entities.push_back(sim.world().create());
        }
        if(!entities.empty())e0=entities[0];
        if(entities.size()>1)e1=entities[1];
        counts=wp::hydrateMap(sim,map,entities);
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<int N>std::unique_ptr<Simulation> make(const Json& map,bool cow=false){
    auto s=std::make_unique<Simulation>(SimConfig{12,20,0,true},
         std::make_unique<MapHost<N>>());
    s->setup(Json::object(),map);
    if(cow)s->world().enableCowSnapshots();
    return s;
}
static Json mapData(){
    return Json::parse(R"({"entities":[
      {"id":"anchor","components":{"FractureMember":{"anchored":true},
                                    "EchoZone":{"enabled":true}}},
      {"id":"gas","components":{"AetherGasCell":{
           "smoke":120,"toxin":0,"oxygen":900,"enabled":true}}}
    ]})");
}
int main(){
    const Json map=mapData();
    auto s=make<1>(map),peer=make<2>(map,true);
    auto& host=static_cast<MapHost<1>&>(s->module());
    AIVERSE_TEST_CHECK(host.counts.zones==1&&host.counts.members==1&&host.counts.gasCells==1);
    AIVERSE_TEST_CHECK(s->world().get<fracture::Member>(host.e0).anchored);
    AIVERSE_TEST_CHECK(s->world().get<aether::GasCell>(host.e1).smoke==120);
    AIVERSE_TEST_CHECK(s->serialize().size()>5 && uint8_t(s->serialize()[4])==10);
    for(int i=0;i<3;++i){s->step();peer->step();AIVERSE_TEST_CHECK(s->checksum()==peer->checksum());}
    const std::string saved=s->serialize();
    auto loaded=make<1>(map);
    std::string error;
    AIVERSE_TEST_CHECK(loaded->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(loaded->checksum()==s->checksum());
    SimSnapshot snap;
    s->saveState(snap);
    s->world().get<aether::GasCell>(host.e1).smoke=10;
    s->loadState(snap);
    AIVERSE_TEST_CHECK(s->checksum()==loaded->checksum());
    // Absent package = untouched v9 manifest. Opt-in is per map.
    auto plain=make<3>(Json::parse(R"({"entities":[{},{}]})"));
    AIVERSE_TEST_CHECK(uint8_t(plain->serialize()[4])==9);
    AIVERSE_TEST_CHECK(plain->module().name()=="world-processes-map-host");
    // Validate all authored configurations before any new component is installed.
    Json malformed=map;
    malformed.member("entities").at(1).member("components")
        .member("AetherGasCell").set("oxygen",Json::number(int64_t(-1)));
    bool failed=false;
    try { (void)make<4>(malformed); }catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    // Count must be a one-to-one map ordering; do not guess by world position.
    World world;
    auto first=world.create();
    Simulation spare(SimConfig{},std::make_unique<MapHost<5>>());
    (void)first;
    failed=false;
    try { (void)wp::hydrateMap(spare,map,{spare.world().create()}); }
    catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    // A collider or destroyed entity may not own an authored component.
    const Entity dead=spare.world().create();spare.world().destroy(dead);
    failed=false;
    try { (void)wp::hydrateMap(spare,map,{spare.world().create(),dead}); }
    catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    std::cout<<"PASS World Processes map hydration, cross-host checksum, v9 opt-out and v10 rollback\n";
}
