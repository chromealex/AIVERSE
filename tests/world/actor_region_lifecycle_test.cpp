// W1.8: real ActorHost, no-genre, post-tick region ownership reconciliation.
#include "engine/actor/ActorModule.h"
#include "engine/game/GameCore.h"
#include "engine/world/RegionEntityLedger.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace aiverse;
namespace wd=aiverse::world;
namespace vs=aiverse::vehicles;

namespace {
std::unique_ptr<Simulation> make(bool tracking) {
    Json rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,
        "mode":"deathmatch","mechanics":{"enabled":true}}})");
    if(tracking)rules.set("regionOwnership",Json::object());
    Json map=Json::parse(R"({"players":[{"team":1},{"team":2}],
        "spawns":[[4,2,4],[8,2,4]],"colliders":[],
        "vehicles":[{"pos":[12,2,4],"speed":15}]})");
    auto sim=std::make_unique<Simulation>(
        SimConfig{43,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    return sim;
}
struct Actors {
    Entity car{},driver{},passenger{};
};
Actors actors(Simulation& sim) {
    auto& host=actor::host(sim);
    auto cars=host.vehicleEntities(sim);
    AIVERSE_TEST_CHECK(cars.size()==1);
    return {cars.front(),host.characterEntity(sim,1),
            host.characterEntity(sim,2)};
}
wd::RegionEntityId stable(Entity e) {
    AIVERSE_TEST_CHECK(e.packed()!=UINT64_MAX);
    return e.packed()+1;
}
wd::Cell owner(const Simulation& sim,Entity e) {
    const auto* ledger=sim.world().resourceConst<wd::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(ledger);
    const auto* rec=ledger->find(stable(e));
    AIVERSE_TEST_CHECK(rec && rec->live);
    return rec->owner;
}
void testAutomaticTickAndLegacy() {
    auto a=make(true),b=make(true);
    auto one=actors(*a),two=actors(*b);
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(owner(*a,one.car)==wd::Cell(0,0,0));
    // The ActorHost system automatically follows movement at the end of the
    // fixed gameplay tick; no manual ledger transfer is called here.
    a->world().get<actor::Vehicle>(one.car).pos={36_fx,2_fx,4_fx};
    b->world().get<actor::Vehicle>(two.car).pos={36_fx,2_fx,4_fx};
    a->step();
    b->step();
    AIVERSE_TEST_CHECK(a->tick()==1 && b->tick()==1);
    AIVERSE_TEST_CHECK(owner(*a,one.car)==wd::Cell(1,0,0));
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    auto legacy=make(false);
    AIVERSE_TEST_CHECK(!legacy->world().resourceConst<wd::RegionEntityLedger>());
    auto bytes=legacy->serialize();
    AIVERSE_TEST_CHECK(bytes.size()>8 && uint8_t(bytes[4])==9);
}
void testVehicleGroupAndCrossings() {
    auto a=make(true),b=make(true);
    auto left=actors(*a),right=actors(*b);
    auto install=[&](Simulation& sim,Actors group) {
        auto& world=sim.world();
        vs::VehicleDrive vehicle;
        vehicle.driver=group.driver;
        vehicle.seats.occupants={{10,1},{20,2}};
        world.add<vs::VehicleDrive>(group.car,vehicle);
        vs::VehicleOccupant backlink;
        backlink.vehicle=group.car;
        world.add<vs::VehicleOccupant>(group.driver,backlink);
    };
    install(*a,left);install(*b,right);
    // One passenger starts in another region before boarding. The vehicle
    // group must converge atomically to the vehicle owner without inventing
    // a duplicate Transform or losing the first passenger.
    auto move=[&](Simulation& sim,Actors group,int x) {
        auto& world=sim.world();
        world.get<actor::Vehicle>(group.car).pos={Fixed::fromInt(x),2_fx,4_fx};
        world.get<actor::CharacterBody>(group.driver).pos={Fixed::fromInt(x),2_fx,4_fx};
        world.get<actor::CharacterBody>(group.passenger).pos={Fixed::fromInt(x+1),2_fx,4_fx};
        actor::host(sim).synchronizeRegionOwnership(sim);
    };
    a->world().get<actor::CharacterBody>(left.passenger).pos={35_fx,2_fx,4_fx};
    b->world().get<actor::CharacterBody>(right.passenger).pos={35_fx,2_fx,4_fx};
    // On-foot crossing first; seats will then bind the passenger to the car.
    // Avoid independently migrating the seat occupant while seated.
    a->world().remove<vs::VehicleDrive>(left.car);
    b->world().remove<vs::VehicleDrive>(right.car);
    a->world().remove<vs::VehicleOccupant>(left.driver);
    b->world().remove<vs::VehicleOccupant>(right.driver);
    actor::host(*a).synchronizeRegionOwnership(*a);
    actor::host(*b).synchronizeRegionOwnership(*b);
    AIVERSE_TEST_CHECK(owner(*a,left.passenger)==wd::Cell(1,0,0));
    install(*a,left);install(*b,right);

    for(int i=1;i<=100;++i) {
        const int x=32*i+4;
        move(*a,left,x);move(*b,right,x);
        const wd::Cell expected{i,0,0};
        AIVERSE_TEST_CHECK(owner(*a,left.car)==expected);
        AIVERSE_TEST_CHECK(owner(*a,left.driver)==expected);
        AIVERSE_TEST_CHECK(owner(*a,left.passenger)==expected);
        AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    }
    // The real host's built-in serializer accepts only its own configured
    // vehicle type. Remove the test-only synthetic seat fixture before
    // testing disk state; the ledger and actor positions remain.
    a->world().remove<vs::VehicleDrive>(left.car);
    b->world().remove<vs::VehicleDrive>(right.car);
    a->world().remove<vs::VehicleOccupant>(left.driver);
    b->world().remove<vs::VehicleOccupant>(right.driver);
    const auto saved=a->serialize();
    AIVERSE_TEST_CHECK(!saved.empty());
    const auto checksum=a->checksum();
    auto restored=make(true);
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(restored->checksum()==checksum);
    AIVERSE_TEST_CHECK(owner(*restored,left.car)==wd::Cell(100,0,0));
    restored->world().enableCowSnapshots();
    SimSnapshot snap;
    restored->saveState(snap);
    const auto before=restored->checksum();
    restored->world().get<actor::Vehicle>(left.car).pos={3270_fx,2_fx,4_fx};
    actor::host(*restored).synchronizeRegionOwnership(*restored);
    AIVERSE_TEST_CHECK(restored->checksum()!=before);
    restored->loadState(snap);
    AIVERSE_TEST_CHECK(restored->checksum()==before);
}
void testOrphanedVehicleReference() {
    auto sim=make(true);
    const auto subjects=actors(*sim);
    auto& ecs=sim->world();
    const auto before=sim->checksum();
    ecs.get<actor::CharacterBody>(subjects.driver).vehicle=9999;
    const auto damaged=sim->checksum();
    AIVERSE_TEST_CHECK(damaged!=before);
    bool rejected=false;
    try {
        actor::host(*sim).synchronizeRegionOwnership(*sim);
    } catch(const std::logic_error&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(sim->checksum()==damaged);
    ecs.get<actor::CharacterBody>(subjects.driver).vehicle=0;
    actor::host(*sim).synchronizeRegionOwnership(*sim);
    AIVERSE_TEST_CHECK(sim->checksum()==before);
}

void testDynamicSpawnRetireAndRecycledIds() {
    auto sim=make(true);
    auto& ecs=sim->world();
    auto& host=actor::host(*sim);
    auto* state=ecs.resource<actor::State>();
    const Entity item=ecs.create();
    const uint32_t lootId=state->nextObject++;
    ecs.add<actor::Loot>(item,actor::Loot{lootId,{6_fx,2_fx,4_fx},1,1});
    AIVERSE_TEST_CHECK(host.reportRegionChange(
        *sim,item,wd::RegionDirtyKind::Spawn));
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->entries.size()==1);
    host.synchronizeRegionOwnership(*sim);
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->entries.empty());
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->acknowledged==1);
    const auto first=stable(item);
    const auto* ledger=ecs.resourceConst<wd::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(ledger->find(first) && ledger->find(first)->live);
    ecs.get<actor::Loot>(item).pos={67_fx,2_fx,4_fx};
    // Legacy direct component writes remain detected by the fallback scan
    // even when no explicit Move event was reported.
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->entries.empty());
    host.synchronizeRegionOwnership(*sim);
    AIVERSE_TEST_CHECK(owner(*sim,item)==wd::Cell(2,0,0));
    AIVERSE_TEST_CHECK(host.reportRegionChange(
        *sim,item,wd::RegionDirtyKind::Retire));
    ecs.destroy(item);
    host.synchronizeRegionOwnership(*sim);
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->entries.empty());
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->acknowledged==2);
    AIVERSE_TEST_CHECK(ledger->find(first) && !ledger->find(first)->live);
    const Entity replacement=ecs.create();
    const uint32_t secondLootId=state->nextObject++;
    ecs.add<actor::Loot>(replacement,actor::Loot{secondLootId,{3_fx,2_fx,4_fx},1,1});
    AIVERSE_TEST_CHECK(host.reportRegionChange(
        *sim,replacement,wd::RegionDirtyKind::Spawn));
    host.synchronizeRegionOwnership(*sim);
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionDirtyQueue>()
                        ->acknowledged==3);
    AIVERSE_TEST_CHECK(stable(replacement)!=first);
    AIVERSE_TEST_CHECK(owner(*sim,replacement)==wd::Cell(0,0,0));
    AIVERSE_TEST_CHECK(!ledger->find(first)->live);
    const auto bytes=sim->serialize();
    AIVERSE_TEST_CHECK(!bytes.empty());
    auto loaded=make(true);
    std::string error;
    AIVERSE_TEST_CHECK(loaded->deserialize(bytes,&error));
    const auto* loadedDirty=loaded->world().resourceConst<wd::RegionDirtyQueue>();
    AIVERSE_TEST_CHECK(loadedDirty && loadedDirty->entries.empty() &&
                       loadedDirty->acknowledged==3);
    const auto* loadedLedger=loaded->world().resourceConst<wd::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(loadedLedger && !loadedLedger->find(first)->live);
    AIVERSE_TEST_CHECK(loadedLedger->find(stable(replacement))->live);
}
}
int main() {
    testAutomaticTickAndLegacy();
    testVehicleGroupAndCrossings();
    testOrphanedVehicleReference();
    testDynamicSpawnRetireAndRecycledIds();
    std::cout<<"PASS world-space authoritative region reconciliation and tombstones\n";
}
