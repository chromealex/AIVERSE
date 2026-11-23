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
void testStaticAndOccupiedMove() {
    auto sim=create(false);
    auto& host=actor::host(*sim);
    auto& ecs=sim->world();
    auto cars=host.vehicleEntities(*sim);
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
    testStaticAndOccupiedMove();
    std::cout<<"PASS staged region move admission, retry, save, and occupied group\n";
}
