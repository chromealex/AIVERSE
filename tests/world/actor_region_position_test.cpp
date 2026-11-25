// W1.7: actual world-space GameCore positions and opt-in ECS region state.
// No synthetic Transform is added to characters or vehicles.
#include "engine/actor/ActorModule.h"
#include "engine/actor/ActorDurableComponents.h"
#include "engine/world/UnifiedComponentSection.h"
#include "engine/world/UnifiedResourceSection.h"
#include "engine/world/RegionColdStore.h"
#include "engine/game/GameCore.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <iostream>
#include <memory>
#include <string>

using namespace aiverse;
namespace wd=aiverse::world;
namespace vs=aiverse::vehicles;

namespace fixture {
struct Work {
    int32_t progress=0;
    std::string intent;
};
inline void io(Archive& a,Work& value) {a(value.progress,value.intent);}
}


static std::unique_ptr<Simulation> make(bool enabled) {
    auto rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,
        "mode":"deathmatch","mechanics":{"enabled":true}}})");
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
    const wd::Cell originalCell{0,0,0};
    for(Entity e:{driver,passenger,car}) {
        AIVERSE_TEST_CHECK(world.alive(e));
        AIVERSE_TEST_CHECK(!world.has<Transform>(e));
        AIVERSE_TEST_CHECK(position(world,e).has_value());
        const auto* id=world.tryGetConst<wd::RegionStableIdentity>(e);
        AIVERSE_TEST_CHECK(id && id->id!=0);
        AIVERSE_TEST_CHECK(ledger->find(id->id)->owner==originalCell);
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

    // The same canonical reader is usable by the shared hot/cold controller:
    // park only a synthetic optional component, never ActorHost's VehicleDrive,
    // CharacterBody or combat data without their systems opting in.
    world.add<fixture::Work>(car,fixture::Work{42,"delivery"});
    wd::RegionColdStore cold;
    wd::RegionDormancyController policy;
    policy.setPositionReader(position);
    AIVERSE_TEST_CHECK(policy.registerComponent<fixture::Work>("test.region_work",1));
    std::array<Entity,1> parkedCar{car};
    AIVERSE_TEST_CHECK(policy.parkGroup(
        world,*ledger,cold,parkedCar,{1,0,0},&error));
    AIVERSE_TEST_CHECK(world.has<wd::RegionDormant>(car));
    AIVERSE_TEST_CHECK(!world.has<fixture::Work>(car));
    AIVERSE_TEST_CHECK(!host.transferVehicleRegion(
        *sim,car,{1,0,0},{2,0,0},&error));
    AIVERSE_TEST_CHECK(policy.wakeGroup(
        world,*ledger,cold,parkedCar,{1,0,0},&error));
    AIVERSE_TEST_CHECK(world.get<fixture::Work>(car).progress==42);
    AIVERSE_TEST_CHECK(world.get<fixture::Work>(car).intent=="delivery");
    world.remove<fixture::Work>(car);

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

static void portableActorProjection(){
    auto sim=make(true);
    auto& w=sim->world();
    auto& host=actor::host(*sim);
    const auto driver=host.characterEntity(*sim,1);
    const auto car=host.vehicleEntities(*sim).front();
    const auto driverId=w.tryGetConst<wd::RegionStableIdentity>(driver)->id;
    const auto carId=w.tryGetConst<wd::RegionStableIdentity>(car)->id;
    vs::VehicleDrive drive;
    drive.type=1;drive.seats.occupants={{10,1},{20,2}};
    w.add<vs::VehicleDrive>(car,drive);
    vs::seatDriver(w,car,w.get<vs::VehicleDrive>(car),driver,1,0);
    w.get<actor::CharacterBody>(driver).vehicle=w.get<actor::Vehicle>(car).id;
    w.get<actor::CharacterBody>(host.characterEntity(*sim,2)).vehicle=w.get<actor::Vehicle>(car).id;
    w.get<actor::CharacterHealth>(driver).health=73;
    w.resource<actor::State>()->round=3;
    // Projectile program targets use semantic (slot, life) keys rather
    // than ECS handles. Preserve history even if a target's life has ended.
    actor::Projectile shot;
    shot.id=w.resource<actor::State>()->nextObject++;
    shot.owner=1;shot.expires=120;shot.pos={36_fx,2_fx,0_fx};
    shot.program.enabled=true;shot.program.direction={1_fx,0_fx,0_fx};
    shot.program.distanceLeft=20_fx;shot.program.lifetimeLeft=2_fx;
    shot.program.gravity=10_fx;shot.program.verticalSpeed=3_fx;
    shot.program.ballistic=true;shot.programSpeed=15_fx;
    shot.program.hit={{2,1}};shot.programTarget={201u+w.get<actor::Vehicle>(car).id,0};
    auto shotEntity=w.create();w.add<actor::Projectile>(shotEntity,shot);
    host.synchronizeRegionOwnership(*sim);
    const auto shotId=w.tryGetConst<wd::RegionStableIdentity>(shotEntity)->id;
    AIVERSE_TEST_CHECK(w.resourceConst<wd::RegionEntityLedger>()->find(shotId)->owner==wd::Cell(1,0,0));

    namespace lm=aiverse::lib::mechanics;
    vs::RaceState race;race.started=true;race.racers={car};
    race.grid.push_back({{4_fx,1_fx,0_fx},Fixed{}});
    lm::RaceCourse course;course.grid=race.grid;
    lm::RaceCheckpoint first;first.gate={1,{64_fx,0_fx,0_fx},{0_fx,0_fx,1_fx},4_fx};
    first.respawn={60_fx,1_fx,0_fx};
    lm::RaceCheckpoint second;second.gate={2,{124_fx,0_fx,0_fx},{0_fx,0_fx,1_fx},4_fx};
    second.respawn={120_fx,1_fx,0_fx};course.checkpoints={first,second};
    const std::array<std::pair<int64_t,int64_t>,1> racers{{{1,1}}};
    const auto initialRace=lm::makeRace(course,racers);
    AIVERSE_TEST_CHECK(initialRace.has_value());
    race.session.race=*initialRace;
    race.session.race.route.racers.at(1).nextGate=1;
    w.setResource<vs::RaceState>(race);
    w.resource<wd::RegionMoveRequests>()->destinations[carId]={64_fx,2_fx,0_fx};
    wd::DurableComponentCodecs codecs;
    using Portable=actor::ActorDurableComponents;
    AIVERSE_TEST_CHECK(Portable::registerCodecs(codecs));
    PersistentEcsRegistry registry;Portable::registerResources(registry);registry.seal();
    const auto policy=Portable::resourcePolicy(registry);
    AIVERSE_TEST_CHECK(Portable::supportedWorld(w));
    constexpr size_t budget=1024*1024;
    wd::UnifiedCheckpointSections::Adapters adapters{
        wd::UnifiedEntitySection::adapter(Portable::spawnShell),
        wd::UnifiedComponentSection::adapter(codecs,budget),
        wd::UnifiedResourceSection::adapter(policy,budget)
    };
    wd::UnifiedWorldReplay::State state;
    state.ledger=*w.resourceConst<wd::RegionEntityLedger>();state.world=w;
    state.world.eraseResource<wd::RegionEntityLedger>();
    std::array<uint8_t,16> uuid{};uuid[0]=42;
    std::vector<uint8_t> bytes;
    AIVERSE_TEST_CHECK(wd::UnifiedCheckpointSections::capture(state,uuid,adapters,budget,bytes));
    std::unique_ptr<wd::UnifiedWorldReplay::State> restored;
    auto factory=[](){
        auto s=std::make_unique<wd::UnifiedWorldReplay::State>();
        // Shift the reconstructed allocator so persistent references must be remapped.
        for(int i=0;i<8;++i)s->world.create();
        return s;
    };
    AIVERSE_TEST_CHECK(wd::UnifiedCheckpointSections::restore(bytes,budget,uuid,adapters,factory,restored));
    wd::PersistentEntityReferences refs;
    AIVERSE_TEST_CHECK(refs.rebuild(restored->world,restored->ledger));
    Entity newCar,newDriver;
    AIVERSE_TEST_CHECK(refs.resolve(restored->world,carId,newCar)&&refs.resolve(restored->world,driverId,newDriver));
    AIVERSE_TEST_CHECK(newCar!=car&&newDriver!=driver);
    const auto& rw=restored->world;
    AIVERSE_TEST_CHECK(rw.resourceConst<actor::CharacterIndex>()->bySlot[0]==newDriver);
    AIVERSE_TEST_CHECK(rw.tryGetConst<vs::VehicleDrive>(newCar)->driver==newDriver);
    AIVERSE_TEST_CHECK(rw.tryGetConst<vs::VehicleOccupant>(newDriver)->vehicle==newCar);
    AIVERSE_TEST_CHECK(rw.tryGetConst<actor::CharacterHealth>(newDriver)->health==73);
    AIVERSE_TEST_CHECK(rw.resourceConst<actor::State>()->round==3);
    AIVERSE_TEST_CHECK(rw.resourceConst<wd::RegionMoveRequests>()->destinations.contains(carId));
    AIVERSE_TEST_CHECK(!rw.resourceConst<wd::RegionEntityLedger>());
    Entity newShot;
    AIVERSE_TEST_CHECK(refs.resolve(rw,shotId,newShot)&&newShot!=shotEntity);
    const auto* recoveredShot=rw.tryGetConst<actor::Projectile>(newShot);
    AIVERSE_TEST_CHECK(recoveredShot&&recoveredShot->program.hit==shot.program.hit);
    AIVERSE_TEST_CHECK(recoveredShot->programTarget==shot.programTarget);
    AIVERSE_TEST_CHECK(recoveredShot->program.gravity==10_fx&&recoveredShot->program.verticalSpeed==3_fx);
    AIVERSE_TEST_CHECK(recoveredShot->programSpeed==15_fx&&recoveredShot->program.ballistic);
    const auto* recoveredRace=rw.resourceConst<vs::RaceState>();
    AIVERSE_TEST_CHECK(recoveredRace&&recoveredRace->racers.size()==1&&recoveredRace->racers.front()==newCar);
    AIVERSE_TEST_CHECK(recoveredRace->session.race.route.racers.at(1).nextGate==1);
    std::vector<wd::DurableResourceTransaction::Record> unchanged;
    AIVERSE_TEST_CHECK(wd::DurableResourceTransaction::captureChanges(registry,policy.selected,
        state.world,rw,unchanged)&&unchanged.empty());
    // Retire the projectile and preserve its tombstone through the same ledger.
    w.destroy(shotEntity);w.flush();host.synchronizeRegionOwnership(*sim);
    AIVERSE_TEST_CHECK(!w.resourceConst<wd::RegionEntityLedger>()->find(shotId)->live);

    // A missing stable reference is a hard failure, not a stale-handle fallback.
    state.world.get<vs::VehicleDrive>(car).driver=state.world.create();
    AIVERSE_TEST_CHECK(!wd::UnifiedCheckpointSections::capture(state,uuid,adapters,budget,bytes));
}

int main() {
    portableActorProjection();
    canonicalOwnerAndRestore();
    noOptInDoesNotChangeLegacy();
    std::cout<<"PASS world-space canonical actor/vehicle position and ECS region state\n";
}
