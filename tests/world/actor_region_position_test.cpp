// W1.7: actual world-space GameCore positions and opt-in ECS region state.
// No synthetic Transform is added to characters or vehicles.
#include "engine/actor/ActorModule.h"
#include "engine/game/GameCore.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <iostream>
#include <memory>
#include <string>

using namespace aiverse;
namespace wd=aiverse::world;
namespace vs=aiverse::vehicles;

static std::unique_ptr<Simulation> make(bool enabled) {
    auto rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,
        "mode":"deathmatch"}})");
    if(enabled)rules.set("regionOwnership",Json::object());
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],
        "spawns":[[4,2,0],[8,2,0]],"colliders":[],
        "vehicles":[{"pos":[28,2,0],"speed":15}]})");
    auto sim=std::make_unique<Simulation>(
        SimConfig{17,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    return sim;
}
static void canonicalOwnerAndRestore() {
    auto sim=make(true);
    auto& world=sim->world();
    auto& host=actor::host(*sim);
    auto* ledger=world.resource<wd::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(ledger);
    const Entity driver=host.characterEntity(*sim,1);
    const Entity passenger=host.characterEntity(*sim,2);
    const auto vehicles=host.vehicleEntities(*sim);
    AIVERSE_TEST_CHECK(vehicles.size()==1);
    const Entity car=vehicles[0];
    const auto position=actor::ActorHost::regionPositionReader();
    for(Entity e:{driver,passenger,car}) {
        AIVERSE_TEST_CHECK(world.alive(e));
        AIVERSE_TEST_CHECK(!world.has<Transform>(e));
        AIVERSE_TEST_CHECK(position(world,e).has_value());
        const auto* id=world.tryGetConst<wd::RegionStableIdentity>(e);
        AIVERSE_TEST_CHECK(id && id->id==e.packed()+1);
        AIVERSE_TEST_CHECK(ledger->find(id->id)->owner==wd::Cell{0,0,0});
    }
    std::map<wd::RegionEntityId,Entity> index;
    std::string error;
    AIVERSE_TEST_CHECK(wd::RegionEcsBridge::rebuild(
        world,*ledger,index,position,&error));
    AIVERSE_TEST_CHECK(index.size()==3);
    wd::SpatialWorld spatial(32);
    std::vector<wd::RegionEcsBridge::Crossing> crossings;
    AIVERSE_TEST_CHECK(wd::RegionEcsBridge::crossings(
        world,*ledger,spatial,crossings,position,&error));
    AIVERSE_TEST_CHECK(crossings.empty());

    // Real ActorHost car/character components own the positions. A test
    // VehicleDrive declares existing seat relationships, never new physics.
    vs::VehicleDrive drive;
    drive.driver=driver;
    drive.seats.occupants={{10,1},{20,2}};
    world.add<vs::VehicleDrive>(car,drive);
    vs::VehicleOccupant backlink;
    backlink.vehicle=car;
    world.add<vs::VehicleOccupant>(driver,backlink);
    world.get<actor::Vehicle>(car).pos={34_fx,2_fx,0_fx};
    world.get<actor::CharacterBody>(driver).pos={34_fx,2_fx,0_fx};
    world.get<actor::CharacterBody>(passenger).pos={35_fx,2_fx,0_fx};
    AIVERSE_TEST_CHECK(wd::RegionEcsBridge::crossings(
        world,*ledger,spatial,crossings,position,&error));
    AIVERSE_TEST_CHECK(crossings.size()==3);
    const auto oldRevision=ledger->lastRevision();
    AIVERSE_TEST_CHECK(host.transferVehicleRegion(
        *sim,car,{0,0,0},{1,0,0},&error));
    AIVERSE_TEST_CHECK(ledger->lastRevision()==oldRevision+3);
    AIVERSE_TEST_CHECK(ledger->activeIn({1,0,0}).size()==3);
    AIVERSE_TEST_CHECK(!host.transferVehicleRegion(
        *sim,car,{0,0,0},{2,0,0},&error));
    AIVERSE_TEST_CHECK(ledger->lastRevision()==oldRevision+3);
    AIVERSE_TEST_CHECK(wd::RegionEcsBridge::crossings(
        world,*ledger,spatial,crossings,position,&error));
    AIVERSE_TEST_CHECK(crossings.empty());

    // The host's old untyped map vehicle is still an untyped vehicle:
    // remove only the synthetic seat test fixture before actual disk IO.
    world.remove<vs::VehicleDrive>(car);
    world.remove<vs::VehicleOccupant>(driver);
    const auto digest=sim->checksum();
    const auto bytes=sim->serialize();
    AIVERSE_TEST_CHECK(!bytes.empty() && uint8_t(bytes[4])==10);
    auto loaded=make(true);
    AIVERSE_TEST_CHECK(loaded->deserialize(bytes,&error));
    AIVERSE_TEST_CHECK(loaded->checksum()==digest);
    const auto* restored=loaded->world().resourceConst<wd::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(restored && restored->activeIn({1,0,0}).size()==3);
    AIVERSE_TEST_CHECK(!loaded->world().has<Transform>(driver));
    AIVERSE_TEST_CHECK(loaded->world().get<actor::CharacterBody>(passenger).pos.x==35_fx);
    auto* latest=loaded->world().resource<wd::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(wd::RegionEcsBridge::rebuild(
        loaded->world(),*latest,index,position,&error));
    AIVERSE_TEST_CHECK(index.size()==3);
    loaded->world().enableCowSnapshots();
    SimSnapshot snapshot;
    loaded->saveState(snapshot);
    const auto baseline=loaded->checksum();
    latest=loaded->world().resource<wd::RegionEntityLedger>();
    std::array<Entity,3> group{car,driver,passenger};
    AIVERSE_TEST_CHECK(wd::RegionEcsBridge::transferGroup(
        loaded->world(),*latest,group,{1,0,0},{2,0,0},position));
    AIVERSE_TEST_CHECK(loaded->checksum()!=baseline);
    loaded->loadState(snapshot);
    AIVERSE_TEST_CHECK(loaded->checksum()==baseline);
}
static void noOptInDoesNotChangeLegacy() {
    auto regular=make(false);
    AIVERSE_TEST_CHECK(!regular->world().resourceConst<wd::RegionEntityLedger>());
    const auto bytes=regular->serialize();
    AIVERSE_TEST_CHECK(!bytes.empty() && uint8_t(bytes[4])==9);
}
int main() {
    canonicalOwnerAndRestore();
    noOptInDoesNotChangeLegacy();
    std::cout<<"PASS world-space canonical actor/vehicle position and ECS region state\n";
}
