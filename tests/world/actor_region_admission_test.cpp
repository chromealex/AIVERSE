// W1.9: real GameCore pre-tick destination admission with async collision.
#include "engine/actor/ActorModule.h"
#include "engine/game/GameCore.h"
#include "engine/pack/Sha256.h"
#include "tests/AlwaysCheck.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace aiverse;
namespace wd=aiverse::world;

namespace {
const std::map<std::string,std::string> kFiles{
    {"near.json",R"({"terrain":{"resolution":2,"heights":[0,0,0,0]}})"},
    {"far.json",R"({"terrain":{"resolution":2,"heights":[0,0,0,0]}})"}
};
std::string sha(const std::string& content) {
    auto h=pack::sha256(content.data(),content.size());
    return pack::toHex(h.data(),h.size());
}
Json worldManifest() {
    auto j=Json::parse(R"({"async":true,"version":1,"cellSize":16,
        "maxResidentBytes":4194304,"maxResidentChunks":2,
        "maxStagingBytes":4194304,"maxJobs":2,"retryUpdates":1,
        "assets":{},"chunks":[]})");
    for(const auto& [name,content]:kFiles)
        j.member("assets").set(name,Json::string(sha(content)));
    auto add=[&](const char* file,int cell) {
        auto chunk=Json::object();
        chunk.set("file",Json::string(file));
        chunk.set("cell",Json::parse("["+std::to_string(cell)+",0,0]"));
        chunk.set("reservedBytes",Json::number(int64_t(1048576)));
        j.member("chunks").push(std::move(chunk));
    };
    add("near.json",0);
    add("far.json",20); // x in [320,336), beyond regular player interest
    return j;
}
std::unique_ptr<Simulation> create(bool streamed=true) {
    Json rules=Json::parse(R"({"actor":{
        "tickRate":60,"maxPlayers":2,"mode":"deathmatch",
        "weapon":{"range":20}},"regionOwnership":{}})");
    Json map=Json::parse(R"({"players":[{"team":1},{"team":2}],
        "spawns":[[4,2,4],[8,2,4]],"colliders":[]})");
    if(streamed)map.set("world",worldManifest());
    auto sim=std::make_unique<Simulation>(
        SimConfig{73,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    return sim;
}
std::unique_ptr<Simulation> createTyped(bool streamed=true) {
    // Actual ActorHost vehicleTypes -> shared VehicleDrive controller and
    // two defined seat slots. No mocked GameModule or duplicate car physics.
    Json rules=Json::parse(R"({"actor":{
        "tickRate":60,"maxPlayers":2,"mode":"deathmatch",
        "mechanics":{"enabled":true},
        "vehicleTypes":{"vehicles":[
            {"id":1,"kind":"wheeled","seats":[
                {"id":1,"role":"driver","entryMilli":[-400,0,200]},
                {"id":2,"role":"passenger","entryMilli":[400,0,200]}
            ]}
        ]},"weapon":{"range":20}},"regionOwnership":{}})");
    Json map=Json::parse(R"({"players":[{"team":1},{"team":2}],
        "spawns":[[4,2,4],[8,2,4]],"colliders":[],
        "vehicles":[{"pos":[12,2,4],"type":1}]})");
    if(streamed)map.set("world",worldManifest());
    auto sim=std::make_unique<Simulation>(
        SimConfig{73,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    return sim;
}
struct TypedActors {
    Entity car=kNullEntity,driver=kNullEntity,passenger=kNullEntity;
};
TypedActors seatTyped(Simulation& sim) {
    auto& host=actor::host(sim);
    auto& ecs=sim.world();
    const auto cars=host.vehicleEntities(sim);
    AIVERSE_TEST_CHECK(cars.size()==1);
    const Entity car=cars.front();
    const Entity driver=host.characterEntity(sim,1);
    const Entity passenger=host.characterEntity(sim,2);
    auto* drive=ecs.tryGet<vehicles::VehicleDrive>(car);
    auto* value=ecs.tryGet<actor::Vehicle>(car);
    AIVERSE_TEST_CHECK(drive && value && value->id!=0);
    drive->seats.occupants={{1,1},{2,2}};
    vehicles::seatDriver(ecs,car,*drive,driver,1,0);
    value->driver=1;
    ecs.get<actor::CharacterBody>(driver).vehicle=value->id;
    ecs.get<actor::CharacterBody>(passenger).vehicle=value->id;
    // The passenger also has a real reverse link, not just a seat manifest.
    vehicles::VehicleOccupant reverse;
    reverse.vehicle=car;
    ecs.add<vehicles::VehicleOccupant>(passenger,reverse);
    return {car,driver,passenger};
}
wd::AssetReader reader(std::atomic<bool>& remote) {
    return [&remote](const std::string& path,std::string& out){
        auto it=kFiles.find(path);
        if(it==kFiles.end())return false;
        if(path=="far.json" && !remote.load())return false;
        out=it->second;
        return true;
    };
}
void progressOne(Simulation& sim) {
    const auto start=std::chrono::steady_clock::now();
    const auto before=sim.tick();
    while(sim.tick()==before) {
        sim.step();
        AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()-start<
                           std::chrono::seconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void testFailedDestinationPreservesAuthoritativeState() {
    std::atomic<bool> remote{false};
    auto a=create(),b=create();
    actor::host(*a).setContentReader(reader(remote));
    actor::host(*b).setContentReader(reader(remote));
    const Entity ea=actor::host(*a).characterEntity(*a,1);
    const Entity eb=actor::host(*b).characterEntity(*b,1);
    const Vec3 destination{324_fx,2_fx,4_fx};
    std::string error;
    AIVERSE_TEST_CHECK(actor::host(*a).requestRegionMove(*a,ea,destination,&error));
    AIVERSE_TEST_CHECK(actor::host(*b).requestRegionMove(*b,eb,destination,&error));
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    // A redundant request is idempotent and retains the same saved payload.
    const auto before=a->checksum();
    AIVERSE_TEST_CHECK(actor::host(*a).requestRegionMove(*a,ea,destination,&error));
    AIVERSE_TEST_CHECK(a->checksum()==before);
    const uint64_t rngBefore=a->rng().state();
    const uint32_t tickBefore=a->tick();
    const auto posBefore=a->world().get<actor::CharacterBody>(ea).pos;
    actor::Input input;
    input.sequence=1;
    input.forward=1000;
    auto cmd=actor::command(1,input);
    cmd.tick=tickBefore;
    a->inject(cmd);b->inject(cmd);
    for(int attempts=0;attempts<8;++attempts) {
        a->step();
        AIVERSE_TEST_CHECK(a->tick()==tickBefore);
        AIVERSE_TEST_CHECK(a->rng().state()==rngBefore);
        AIVERSE_TEST_CHECK(a->checksum()==before);
        AIVERSE_TEST_CHECK(a->executedCommands().empty());
        AIVERSE_TEST_CHECK(a->world().get<actor::CharacterBody>(ea).pos.x==posBefore.x);
        AIVERSE_TEST_CHECK(a->world().resourceConst<wd::RegionMoveRequests>()
                            ->destinations.size()==1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Remote chunk succeeds, and both peers process the *same* pending
    // command and staged destination on their next actual tick.
    remote=true;
    progressOne(*a);
    progressOne(*b);
    AIVERSE_TEST_CHECK(a->tick()==tickBefore+1);
    AIVERSE_TEST_CHECK(a->executedCommands().size()==1);
    AIVERSE_TEST_CHECK(a->world().resourceConst<wd::RegionMoveRequests>()
                        ->destinations.empty());
    AIVERSE_TEST_CHECK(a->world().resourceConst<wd::RegionMoveRequests>()->applied==1);
    AIVERSE_TEST_CHECK(a->world().get<actor::CharacterBody>(ea).pos.x>320_fx);
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    const auto payload=a->serialize();
    auto restored=create();
    actor::host(*restored).setContentReader(reader(remote));
    AIVERSE_TEST_CHECK(restored->deserialize(payload,&error));
    AIVERSE_TEST_CHECK(restored->checksum()==a->checksum());
    for(int i=0;i<5;++i) {
        progressOne(*a);
        progressOne(*restored);
        AIVERSE_TEST_CHECK(a->checksum()==restored->checksum());
    }
}
// A request created *inside* the tick cannot use a readyToStep check from
// earlier in that tick. It waits for the next admission decision instead.
void testLateGameplayCommandWaitsForNextAdmission() {
    auto sim=create(false);
    auto& host=actor::host(*sim);
    const Entity actor=host.characterEntity(*sim,1);
    const Vec3 target{68_fx,2_fx,4_fx};
    int commands=0;
    sim->insertSystem(0,"test.stageLater",[&](Simulation& s){
        if(commands++==0) {
            std::string error;
            AIVERSE_TEST_CHECK(host.requestRegionMove(s,actor,target,&error));
        }
    });
    const auto original=sim->world().get<actor::CharacterBody>(actor).pos;
    sim->step();
    AIVERSE_TEST_CHECK(sim->tick()==1);
    AIVERSE_TEST_CHECK(sim->world().get<actor::CharacterBody>(actor).pos.x==original.x);
    AIVERSE_TEST_CHECK(sim->world().resourceConst<wd::RegionMoveRequests>()
                        ->destinations.size()==1);
    AIVERSE_TEST_CHECK(sim->world().resourceConst<wd::RegionMoveRequests>()->applied==0);
    sim->step();
    AIVERSE_TEST_CHECK(sim->tick()==2);
    AIVERSE_TEST_CHECK(sim->world().get<actor::CharacterBody>(actor).pos.x>=64_fx);
    AIVERSE_TEST_CHECK(sim->world().resourceConst<wd::RegionMoveRequests>()
                        ->destinations.empty());
    AIVERSE_TEST_CHECK(sim->world().resourceConst<wd::RegionMoveRequests>()->applied==1);
}

void testQueuedDestinationSurvivesSave() {
    auto original=create(false);
    auto& host=actor::host(*original);
    const Entity actor=host.characterEntity(*original,1);
    const Vec3 destination{100_fx,2_fx,4_fx};
    std::string error;
    AIVERSE_TEST_CHECK(host.requestRegionMove(
        *original,actor,destination,&error));
    const auto checksum=original->checksum();
    const auto bytes=original->serialize();
    AIVERSE_TEST_CHECK(!bytes.empty() && uint8_t(bytes[4])==10);
    auto recovered=create(false);
    AIVERSE_TEST_CHECK(recovered->deserialize(bytes,&error));
    AIVERSE_TEST_CHECK(recovered->checksum()==checksum);
    AIVERSE_TEST_CHECK(recovered->world().resourceConst<wd::RegionMoveRequests>()
                        ->destinations.size()==1);
    original->step();
    recovered->step();
    AIVERSE_TEST_CHECK(original->checksum()==recovered->checksum());
    AIVERSE_TEST_CHECK(recovered->world().resourceConst<wd::RegionMoveRequests>()
                        ->destinations.empty());
    AIVERSE_TEST_CHECK(recovered->world().get<actor::CharacterBody>(actor).pos.x>=96_fx);
}

void testTypedSeatSafety() {
    auto sim=createTyped(false);
    auto group=seatTyped(*sim);
    auto& ecs=sim->world();
    auto& host=actor::host(*sim);
    const auto hash=sim->checksum();
    const auto queueBefore=ecs.resourceConst<wd::RegionMoveRequests>()
                               ->destinations.size();
    std::string error;

    // Inconsistent occupied passenger is refused before any request is
    // stored or any controller/character position is changed.
    ecs.get<actor::CharacterBody>(group.passenger).vehicle=0;
    const auto broken=sim->checksum();
    AIVERSE_TEST_CHECK(!host.requestRegionMove(
        *sim,group.car,{64_fx,2_fx,4_fx},&error));
    AIVERSE_TEST_CHECK(sim->checksum()==broken);
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionMoveRequests>()
                        ->destinations.size()==queueBefore);
    ecs.get<actor::CharacterBody>(group.passenger).vehicle=
        ecs.get<actor::Vehicle>(group.car).id;
    AIVERSE_TEST_CHECK(sim->checksum()==hash);

    // An extra reverse-only occupant must not be left behind.
    const Entity extra=ecs.create();
    vehicles::VehicleOccupant orphan;
    orphan.vehicle=group.car;
    ecs.add<vehicles::VehicleOccupant>(extra,orphan);
    AIVERSE_TEST_CHECK(!host.requestRegionMove(
        *sim,group.car,{64_fx,2_fx,4_fx},&error));
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionMoveRequests>()
                        ->destinations.empty());
    ecs.destroy(extra);
    AIVERSE_TEST_CHECK(host.requestRegionMove(
        *sim,group.car,{64_fx,2_fx,4_fx},&error));
    AIVERSE_TEST_CHECK(ecs.resourceConst<wd::RegionMoveRequests>()
                        ->destinations.size()==1);
}

void testTypedVehicleAdmissionAndRestore() {
    std::atomic<bool> remote{false};
    auto a=createTyped(),b=createTyped();
    auto actorsA=seatTyped(*a),actorsB=seatTyped(*b);
    actor::host(*a).setContentReader(reader(remote));
    actor::host(*b).setContentReader(reader(remote));
    const Vec3 destination{324_fx,2_fx,4_fx};
    std::string error;
    AIVERSE_TEST_CHECK(actor::host(*a).requestRegionMove(
        *a,actorsA.car,destination,&error));
    AIVERSE_TEST_CHECK(actor::host(*b).requestRegionMove(
        *b,actorsB.car,destination,&error));
    AIVERSE_TEST_CHECK(!actor::host(*a).requestRegionMove(
        *a,actorsA.passenger,{325_fx,2_fx,4_fx},&error));
    const auto initialHash=a->checksum();
    const auto initialTick=a->tick();
    const auto initialRng=a->rng().state();
    const auto old=vehicles::driveOf(a->world(),actorsA.car)->controller.position;
    for(int i=0;i<6;++i) {
        a->step();
        AIVERSE_TEST_CHECK(a->tick()==initialTick);
        AIVERSE_TEST_CHECK(a->rng().state()==initialRng);
        AIVERSE_TEST_CHECK(a->checksum()==initialHash);
        AIVERSE_TEST_CHECK(vehicles::driveOf(a->world(),actorsA.car)
                            ->controller.position==old);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    remote=true;
    progressOne(*a);
    progressOne(*b);
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    auto& ecs=a->world();
    const auto* drive=vehicles::driveOf(ecs,actorsA.car);
    AIVERSE_TEST_CHECK(drive);
    AIVERSE_TEST_CHECK(drive->controller.position.x>320_fx);
    AIVERSE_TEST_CHECK(ecs.get<actor::Vehicle>(actorsA.car).pos.x>320_fx);
    AIVERSE_TEST_CHECK(drive->controller.velocity.lengthSq()<1_fx);
    AIVERSE_TEST_CHECK(drive->driver==actorsA.driver);
    AIVERSE_TEST_CHECK(drive->seats.occupants.size()==2);
    AIVERSE_TEST_CHECK(ecs.get<actor::CharacterBody>(actorsA.driver).pos.x>320_fx);
    AIVERSE_TEST_CHECK(ecs.get<actor::CharacterBody>(actorsA.passenger).pos.x>320_fx);
    const auto* reverse=ecs.tryGetConst<vehicles::VehicleOccupant>(actorsA.driver);
    AIVERSE_TEST_CHECK(reverse && reverse->vehicle==actorsA.car);
    AIVERSE_TEST_CHECK(reverse->position.x>320_fx);
    const auto* ledger=ecs.resourceConst<wd::RegionEntityLedger>();
    const wd::Cell region{10,0,0};
    for(Entity entity:{actorsA.car,actorsA.driver,actorsA.passenger}) {
        const auto* identity=ecs.tryGetConst<wd::RegionStableIdentity>(entity);
        AIVERSE_TEST_CHECK(identity && ledger->find(identity->id));
        AIVERSE_TEST_CHECK(ledger->find(identity->id)->owner==region);
    }

    // ActorHost's v10 disk serializer captures the real VehicleDrive
    // controller + seat state, and the W1 ledger/queue native ECS resources.
    const std::string saved=a->serialize();
    AIVERSE_TEST_CHECK(!saved.empty());
    auto loaded=createTyped();
    actor::host(*loaded).setContentReader(reader(remote));
    AIVERSE_TEST_CHECK(loaded->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(loaded->checksum()==a->checksum());
    const auto* loadedDrive=vehicles::driveOf(loaded->world(),actorsA.car);
    AIVERSE_TEST_CHECK(loadedDrive &&
                       loadedDrive->controller.position==drive->controller.position);
    AIVERSE_TEST_CHECK(loadedDrive->seats.occupants.size()==2);
    const Vec3 originalControllerPosition=loadedDrive->controller.position;
    loaded->world().enableCowSnapshots();
    SimSnapshot snap;
    loaded->saveState(snap);
    const auto checksum=loaded->checksum();
    AIVERSE_TEST_CHECK(actor::host(*loaded).requestRegionMove(
        *loaded,actorsA.car,{6_fx,2_fx,4_fx},&error));
    progressOne(*loaded);
    AIVERSE_TEST_CHECK(loaded->checksum()!=checksum);
    loaded->loadState(snap);
    AIVERSE_TEST_CHECK(loaded->checksum()==checksum);
    AIVERSE_TEST_CHECK(vehicles::driveOf(loaded->world(),actorsA.car)
                        ->controller.position==originalControllerPosition);
}

// Even after a car has driven back near players, the original map spawn
// remains a possible round-reset destination and must be collision-resident.
void testOriginalVehicleResetDestinationPreloaded() {
    Json rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,
        "mode":"deathmatch","mechanics":{"enabled":true},
        "weapon":{"range":20}},"regionOwnership":{}})");
    Json map=Json::parse(R"({"players":[{"team":1},{"team":2}],
        "spawns":[[4,2,4],[8,2,4]],"colliders":[],
        "vehicles":[{"pos":[324,2,4],"speed":15}]})");
    map.set("world",worldManifest());
    auto sim=std::make_unique<Simulation>(
        SimConfig{91,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    auto cars=actor::host(*sim).vehicleEntities(*sim);
    AIVERSE_TEST_CHECK(cars.size()==1);
    // The live car is now nearby. No camera, live vehicle, player, projectile
    // or queued teleport requires the distant original spawn.
    sim->world().get<actor::Vehicle>(cars[0]).pos={12_fx,2_fx,4_fx};
    std::atomic<bool> remote{false};
    actor::host(*sim).setContentReader(reader(remote));
    const auto hash=sim->checksum();
    const auto rng=sim->rng().state();
    sim->step();
    AIVERSE_TEST_CHECK(sim->tick()==0 && sim->checksum()==hash);
    AIVERSE_TEST_CHECK(sim->rng().state()==rng);
    remote=true;
    progressOne(*sim);
    AIVERSE_TEST_CHECK(sim->tick()==1);
}

void testStaticAndOccupiedMove() {
    auto sim=create(false);
    auto& host=actor::host(*sim);
    auto& ecs=sim->world();
    // No driving block: create a plain arcade car for the stage fixture.
    // It remains the existing actor::Vehicle, not a second vehicle model.
    Entity car=ecs.create();
    ecs.add<actor::Vehicle>(car,actor::Vehicle{500,{12_fx,2_fx,4_fx},{},Fixed{},15_fx,1,500});
    host.synchronizeRegionOwnership(*sim);
    const Entity driver=host.characterEntity(*sim,1);
    const Entity passenger=host.characterEntity(*sim,2);
    ecs.get<actor::CharacterBody>(driver).vehicle=500;
    ecs.get<actor::CharacterBody>(passenger).vehicle=500;
    std::string error;
    AIVERSE_TEST_CHECK(host.requestRegionMove(
        *sim,car,{36_fx,2_fx,4_fx},&error));
    // A second request for an occupant of this car must be rejected.
    AIVERSE_TEST_CHECK(!host.requestRegionMove(
        *sim,driver,{40_fx,2_fx,4_fx},&error));
    const auto original=sim->tick();
    sim->step();
    AIVERSE_TEST_CHECK(sim->tick()==original+1);
    AIVERSE_TEST_CHECK(sim->world().resourceConst<wd::RegionMoveRequests>()->applied==1);
    AIVERSE_TEST_CHECK(ecs.get<actor::Vehicle>(car).pos.x==36_fx);
    const wd::Cell region{1,0,0};
    auto* ledger=ecs.resourceConst<wd::RegionEntityLedger>();
    for(Entity e:{car,driver,passenger}) {
        const auto* tag=ecs.tryGetConst<wd::RegionStableIdentity>(e);
        AIVERSE_TEST_CHECK(tag && ledger->find(tag->id));
        AIVERSE_TEST_CHECK(ledger->find(tag->id)->owner==region);
    }
}
}
int main() {
    testFailedDestinationPreservesAuthoritativeState();
    testLateGameplayCommandWaitsForNextAdmission();
    testQueuedDestinationSurvivesSave();
    testStaticAndOccupiedMove();
    testOriginalVehicleResetDestinationPreloaded();
    testTypedSeatSafety();
    testTypedVehicleAdmissionAndRestore();
    std::cout<<"PASS staged region move admission, retry, save, and occupied group\n";
}
