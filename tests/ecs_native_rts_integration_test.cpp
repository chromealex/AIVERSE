// Real RTS NativeModule (not a mock GameModule): persistent ECS regression.
#include "engine/data/JsonCompose.h"
#include "engine/game/GameCore.h"
#include "engine/game/Native.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;
namespace g=aiverse::game;
namespace fixture {
struct Counter { int32_t ticks=3; Fixed bonus=Fixed::fromRatio(1,2); };
struct Ledger { int64_t processed=11; };
struct Rogue { int value=7; };
void io(Archive& a,Counter& v){a(v.ticks,v.bonus);}
void io(Archive& a,Ledger& v){a(v.processed);}
class Native final:public g::NativeModule {
public:
    void setup(g::GameCore&,Simulation& s,g::NativeState& state,const Json&)const override {
        s.registerPersistentComponent<Counter>("test.native.counter",1);
        s.registerPersistentResource<Ledger>("test.native.ledger",1);
        const Entity entity=s.world().create();
        s.world().add<Counter>(entity);
        s.world().setResource<Ledger>();
        state.values["entity"]=int64_t(entity.packed());
    }
    void tick(g::GameCore&,Simulation& s,g::NativeState& state)const override {
        Entity e=Entity::fromPacked(uint64_t(state.values.at("entity")));
        auto* component=s.world().tryGet<Counter>(e);
        auto* resource=s.world().resource<Ledger>();
        if(!component || !resource)throw std::logic_error("native ECS state missing");
        ++component->ticks;
        component->bonus+=Fixed::fromRatio(1,4);
        resource->processed+=component->ticks;
    }
};
}
static std::unique_ptr<g::NativeModule> factory(){
    return std::make_unique<fixture::Native>();
}
static void check(bool yes,const std::string& error){
    if(!yes)throw std::runtime_error(error);
}
static std::unique_ptr<Simulation> make(bool native){
    const std::string base=std::string(AIVERSE_TEMPLATES_DIR)+"/mushrooms/";
    Json rules=readComposedJson(base+"rules.json",fileReader());
    const Json map=Json::parseFile(base+"maps/duel.json");
    std::vector<g::NativeDefinition> definitions;
    if(native){
        rules.set("nativeModules",Json::parse(R"([{"id":"ecs_persist_test"}])"));
        definitions.push_back({"ecs_persist_test",1,&factory});
    }
    auto sim=std::make_unique<Simulation>(
        SimConfig{},std::make_unique<g::GameCore>(std::move(definitions)));
    sim->setup(rules,map);
    return sim;
}
static Entity marker(const Simulation& sim){
    auto* store=sim.world().resourceConst<g::NativeStore>();
    check(store && store->modules.size()==1,"native store absent");
    auto it=store->modules[0].values.find("entity");
    check(it!=store->modules[0].values.end(),"native entity identity absent");
    return Entity::fromPacked(uint64_t(it->second));
}
int main(){
    auto sim=make(true),other=make(true);
    Entity e=marker(*sim);
    check(sim->world().has<fixture::Counter>(e),"registered component absent");
    check(sim->world().resourceConst<fixture::Ledger>()!=nullptr,"resource absent");
    check(sim->checksum()==other->checksum(),"initial RTS native checksum mismatch");
    for(int i=0;i<8;++i){
        sim->step();other->step();
        check(sim->checksum()==other->checksum(),"real RTS peer checksum diverged");
    }
    check(sim->world().get<fixture::Counter>(e).ticks==11,"native tick missed");
    const uint64_t expected=sim->checksum();
    const std::string bytes=sim->serialize();
    check(bytes.size()>8 && uint8_t(bytes[4])==10,"native RTS save not v10");
    auto loaded=make(true);std::string error;
    check(loaded->deserialize(bytes,&error),"RTS/native reload: "+error);
    check(loaded->checksum()==expected,"reloaded checksum mismatch");
    check(loaded->world().get<fixture::Counter>(e).ticks==11,"reloaded counter mismatch");
    check(loaded->world().resourceConst<fixture::Ledger>()->processed==
          sim->world().resourceConst<fixture::Ledger>()->processed,
          "reloaded native resource mismatch");
    sim->world().enableCowSnapshots();
    SimSnapshot snap;sim->saveState(snap);
    sim->step();check(sim->checksum()!=expected,"tick failed to mutate ECS state");
    sim->loadState(snap);
    check(sim->checksum()==expected,"COW rollback did not restore native state");
    for(int i=0;i<4;++i){
        sim->step();loaded->step();
        check(sim->checksum()==loaded->checksum(),"post-load RTS replay diverged");
    }
    auto rogue=make(true);
    rogue->world().add<fixture::Rogue>(rogue->world().create());
    check(rogue->serialize().empty(),"unregistered native pool was saved");
    auto legacy=make(false);
    const std::string old=legacy->serialize();
    check(old.size()>8 && uint8_t(old[4])==9,"RTS without native changed v9");
    check(!loaded->deserialize(old,&error),"native simulation accepted missing v10 state");
    const std::string before=loaded->serialize();
    const uint64_t hash=loaded->checksum();
    auto corrupt=bytes;corrupt.back()=char(uint8_t(corrupt.back())^0x7f);
    check(!loaded->deserialize(corrupt,&error),"accepted corrupted native save");
    check(loaded->checksum()==hash && loaded->serialize()==before,
          "failed disk load altered running RTS state");
    std::cout<<"PASS real RTS NativeModule, ECS v9/v10, COW, replay, rogue pool, corruption\n";
}
