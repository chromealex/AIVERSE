#include "engine/world/RegionEcsBridge.h"
#include "engine/sim/Simulation.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::world;

// Independent synthetic host needs its own explicit Transform codec.
// Production projects keep using their existing registered position schema.
namespace aiverse {
inline void io(Archive& ar,Transform& transform) {
    ar(transform.pos,transform.prevPos,transform.yaw);
}
}
namespace {
constexpr Cell west{0,0,0},east{1,0,0};
const auto at=[](int x){return Vec3{Fixed::fromInt(x),{},Fixed{}};};

struct Host final: GameModule {
    std::array<Entity,2> initial{};
    std::string name()const override{return "region-ecs-bridge-test";}
    void setup(Simulation& simulation,const Json&,const Json&)override {
        registerRegionEntityLedger(simulation);
        registerRegionStableIdentity(simulation);
        simulation.registerPersistentComponent<Transform>("test.region_transform",1);
        auto& w=simulation.world();
        w.setResource<RegionEntityLedger>(RegionEntityLedger{});
        for(size_t i=0;i<initial.size();++i) {
            const Entity e=w.create();
            initial[i]=e;
            w.add<Transform>(e,Transform{at(6+int(i)),at(6+int(i)),Fixed{}});
        }
    }
    void applyCommand(Simulation&,const Command&)override {}
    void hashState(Simulation&,Hasher&)override {}
    bool serializeState(Simulation&,Archive&)override{return true;}
    int result(Simulation&)override{return 0;}
};
std::unique_ptr<Simulation> createSim() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    sim->setup(Json::object(),Json::object());
    return sim;
}
std::array<Entity,2> initial(Simulation& sim) {
    return static_cast<Host&>(sim.module()).initial;
}
void testLiveBindings() {
    auto sim=createSim();
    auto& w=sim->world();
    auto* ledger=w.resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(ledger);
    const auto [car,passenger]=initial(*sim);
    std::string reason;
    AIVERSE_TEST_CHECK(RegionEcsBridge::bindNew(w,*ledger,car,101,west,&reason));
    AIVERSE_TEST_CHECK(RegionEcsBridge::bindNew(w,*ledger,passenger,102,west,&reason));
    AIVERSE_TEST_CHECK(!RegionEcsBridge::bindNew(w,*ledger,car,103,west,&reason));
    AIVERSE_TEST_CHECK(!RegionEcsBridge::bindNew(w,*ledger,passenger,101,west,&reason));
    std::map<RegionEntityId,Entity> live;
    AIVERSE_TEST_CHECK(RegionEcsBridge::rebuild(w,*ledger,live,&reason));
    AIVERSE_TEST_CHECK(live.size()==2 && live.at(101)==car && live.at(102)==passenger);
    SpatialWorld spatial(32);
    std::vector<RegionEcsBridge::Crossing> pending;
    AIVERSE_TEST_CHECK(RegionEcsBridge::crossings(w,*ledger,spatial,pending,&reason));
    AIVERSE_TEST_CHECK(pending.empty());

    // Simulated actual vehicle and passenger movement on a gameplay tick.
    w.get<Transform>(car).pos=at(34);
    w.get<Transform>(passenger).pos=at(35);
    AIVERSE_TEST_CHECK(RegionEcsBridge::crossings(w,*ledger,spatial,pending,&reason));
    AIVERSE_TEST_CHECK(pending.size()==2 && pending[0].id==101 && pending[1].id==102);
    for(const auto& move:pending)
        AIVERSE_TEST_CHECK(move.from==west && move.to==east);
    std::array<Entity,2> group{passenger,car};
    AIVERSE_TEST_CHECK(RegionEcsBridge::transferGroup(w,*ledger,group,west,east,&reason));
    const auto rev=ledger->lastRevision();
    AIVERSE_TEST_CHECK(!RegionEcsBridge::transferGroup(w,*ledger,group,west,east,&reason));
    AIVERSE_TEST_CHECK(rev==ledger->lastRevision()); // atomic refusal
    AIVERSE_TEST_CHECK(ledger->activeIn(east)==std::vector<uint64_t>({101,102}));
    AIVERSE_TEST_CHECK(RegionEcsBridge::crossings(w,*ledger,spatial,pending,&reason));
    AIVERSE_TEST_CHECK(pending.empty());

    // Reify from an external (host-owned) parked Transform.
    // No component archiving is implied by this test.
    const Transform saved=w.get<Transform>(car);
    w.destroy(car);
    auto substitute=w.create();
    w.add<Transform>(substitute,saved);
    AIVERSE_TEST_CHECK(RegionEcsBridge::bindExisting(w,*ledger,substitute,101,east,&reason));
    AIVERSE_TEST_CHECK(RegionEcsBridge::rebuild(w,*ledger,live,&reason));
    AIVERSE_TEST_CHECK(live.at(101)==substitute && !w.alive(car));
    AIVERSE_TEST_CHECK(RegionEcsBridge::retire(w,*ledger,substitute,east,&reason));
    AIVERSE_TEST_CHECK(!w.alive(substitute) && ledger->find(101) && !ledger->find(101)->live);
    auto another=w.create();
    w.add<Transform>(another,saved);
    AIVERSE_TEST_CHECK(!RegionEcsBridge::bindExisting(w,*ledger,another,101,east,&reason));
    AIVERSE_TEST_CHECK(!RegionEcsBridge::bindNew(w,*ledger,another,101,east,&reason));
    AIVERSE_TEST_CHECK(RegionEcsBridge::rebuild(w,*ledger,live,&reason));
    AIVERSE_TEST_CHECK(live.size()==1 && live.at(102)==passenger);
}

void testPersistenceAndRollback() {
    auto sim=createSim();
    auto& w=sim->world();
    auto entities=initial(*sim);
    auto* ledger=w.resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(RegionEcsBridge::bindNew(w,*ledger,entities[0],500,west));
    AIVERSE_TEST_CHECK(RegionEcsBridge::bindNew(w,*ledger,entities[1],501,west));

    // Real ECS Transform components cross one hundred region boundaries.
    const SpatialWorld spatial(32);
    std::vector<RegionEcsBridge::Crossing> pending;
    for(int i=0;i<100;++i) {
        const Cell from{i,0,0},to{i+1,0,0};
        for(size_t j=0;j<2;++j)w.get<Transform>(entities[j]).pos=at((i+1)*32+int(j));
        AIVERSE_TEST_CHECK(RegionEcsBridge::crossings(w,*ledger,spatial,pending));
        AIVERSE_TEST_CHECK(pending.size()==2);
        AIVERSE_TEST_CHECK(RegionEcsBridge::transferGroup(w,*ledger,entities,from,to));
    }
    AIVERSE_TEST_CHECK(ledger->activeIn({100,0,0})==std::vector<uint64_t>({500,501}));
    const auto baseline=sim->checksum();
    const auto bytes=sim->serialize();
    sim->world().enableCowSnapshots();
    SimSnapshot snapshot;
    sim->saveState(snapshot);

    // COW IMPORTANT: reacquire mutable resource after saveState()
    // so snapshots don't observe later mutations through a stale pointer.
    ledger=w.resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(RegionEcsBridge::transferGroup(w,*ledger,entities,{100,0,0},{101,0,0}));
    AIVERSE_TEST_CHECK(sim->checksum()!=baseline);
    sim->loadState(snapshot);
    AIVERSE_TEST_CHECK(sim->checksum()==baseline);
    ledger=w.resource<RegionEntityLedger>();
    std::map<RegionEntityId,Entity> live;
    AIVERSE_TEST_CHECK(RegionEcsBridge::rebuild(w,*ledger,live));
    AIVERSE_TEST_CHECK(live.size()==2 && live.at(500)==entities[0]);
    auto restored=createSim();
    std::string reason;
    AIVERSE_TEST_CHECK(restored->deserialize(bytes,&reason));
    AIVERSE_TEST_CHECK(restored->checksum()==baseline);
    auto* remote=restored->world().resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(RegionEcsBridge::rebuild(restored->world(),*remote,live));
    AIVERSE_TEST_CHECK(live.size()==2 && live.at(501)==entities[1]);
    for(size_t j=0;j<2;++j) {
        const auto entity=entities[j];
        AIVERSE_TEST_CHECK(restored->world().get<Transform>(entity).pos.x==
                          w.get<Transform>(entity).pos.x);
    }
    // Two peers advance with the same region transitions and fixed ticks.
    AIVERSE_TEST_CHECK(RegionEcsBridge::transferGroup(w,*ledger,entities,{100,0,0},{101,0,0}));
    AIVERSE_TEST_CHECK(RegionEcsBridge::transferGroup(restored->world(),*remote,entities,
                                                    {100,0,0},{101,0,0}));
    sim->step();
    restored->step();
    AIVERSE_TEST_CHECK(sim->checksum()==restored->checksum());
}
} // namespace

int main() {
    testLiveBindings();
    testPersistenceAndRollback();
    std::cout<<"PASS region_ecs_bridge (live bindings, 100 crossings, restore, COW, reify)\n";
}
