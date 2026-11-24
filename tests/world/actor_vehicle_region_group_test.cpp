// W1.6: real world-space GameCore -> ActorHost seat-to-Entity adapter.
// No new gameplay mode and no vehicle-controller implementation.
#include "engine/actor/ActorModule.h"
#include "engine/game/GameCore.h"
#include "tests/AlwaysCheck.h"
#include <iostream>
#include <memory>
#include <string>

using namespace aiverse;
namespace v=vehicles;
namespace w=world;

int main() {
    auto rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,
        "mode":"deathmatch"}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],
        "spawns":[[0,2,0],[5,2,0]],"colliders":[]})");
    auto sim=std::make_unique<Simulation>(
        SimConfig{9,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    auto& host=actor::host(*sim);
    auto& ecs=sim->world();
    const Entity driver=host.characterEntity(*sim,1);
    const Entity passenger=host.characterEntity(*sim,2);
    AIVERSE_TEST_CHECK(ecs.alive(driver) && ecs.alive(passenger));
    const Entity car=ecs.create();
    v::VehicleDrive drive;
    drive.driver=driver;
    drive.seats.occupants={{10,1},{20,2}};
    ecs.add<v::VehicleDrive>(car,drive);
    ecs.add<Transform>(car,Transform{});
    v::VehicleOccupant reverse;
    reverse.vehicle=car;
    ecs.add<v::VehicleOccupant>(driver,reverse);

    w::VehicleRegionGroup group;
    std::string error;
    AIVERSE_TEST_CHECK(host.vehicleRegionGroup(*sim,car,group,&error));
    AIVERSE_TEST_CHECK(group.members.size()==3);
    AIVERSE_TEST_CHECK(group.vehicle==car);
    AIVERSE_TEST_CHECK(group.members[0]==driver);
    AIVERSE_TEST_CHECK(group.members[1]==passenger);
    AIVERSE_TEST_CHECK(group.members[2]==car);

    // Real ActorHost characters currently own position in CharacterBody,
    // not Transform. The read-only group query works, but a W1 ledger
    // ownership transfer MUST refuse until the host maps those positions
    // and registers stable IDs for every actor.
    w::RegionEntityLedger ledger;
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        ecs,ledger,car,
        [&host,&sim](int64_t slot){
            return slot>=1 && slot<=2?
                host.characterEntity(*sim,uint8_t(slot)):kNullEntity;
        },{0,0,0},{1,0,0},&error));

    ecs.get<v::VehicleDrive>(car).seats.occupants[20]=99;
    AIVERSE_TEST_CHECK(!host.vehicleRegionGroup(*sim,car,group,&error));
    AIVERSE_TEST_CHECK(group.members.size()==3);
    AIVERSE_TEST_CHECK(!error.empty());

    std::cout<<"PASS ActorHost real seat slot resolution and fail-closed W1 transfer\n";
}
