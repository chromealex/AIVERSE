// W1.5 world-space GameCore collision + derived bot navigation gate.
#include "engine/actor/ActorModule.h"
#include "engine/game/GameCore.h"
#include "engine/pack/Sha256.h"
#include "engine/world/CollisionStream.h"
#include "tests/AlwaysCheck.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace aiverse;
namespace wd=aiverse::world;

namespace {
const std::map<std::string,std::string> kChunks{
    {"a.json",R"({"terrain":{"resolution":2,"heights":[0,0,0,0]}})"},
    {"b.json",R"({"terrain":{"resolution":2,"heights":[0,0,0,0]}})"}
};
std::string digest(const std::string& bytes){
    const auto h=pack::sha256(bytes.data(),bytes.size());
    return pack::toHex(h.data(),h.size());
}
Json manifest(){
    auto j=Json::parse(R"({"async":true,"version":1,"cellSize":16,
        "maxResidentBytes":4194304,"maxResidentChunks":2,"maxStagingBytes":2097152,
        "maxJobs":2,"retryUpdates":1,"assets":{},"chunks":[]})");
    int i=0;
    for(const auto& [name,data]:kChunks){
        j.member("assets").set(name,Json::string(digest(data)));
        Json chunk=Json::object();
        chunk.set("file",Json::string(name));
        chunk.set("cell",Json::parse("["+std::to_string(i++)+",0,0]"));
        chunk.set("reservedBytes",Json::number(int64_t(1048576)));
        j.member("chunks").push(std::move(chunk));
    }
    return j;
}
wd::AssetReader reader(std::atomic<bool>& allow){
    return [&allow](const std::string& path,std::string& bytes){
        if(!allow.load())return false;
        auto it=kChunks.find(path);
        if(it==kChunks.end())return false;
        bytes=it->second;
        return true;
    };
}
std::unique_ptr<Simulation> create(){
    auto rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,
        "mode":"deathmatch","bots":{},"weapon":{"range":20}}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2,"bot":true}],
        "spawns":[[4,2,4],[12,2,12]],"colliders":[],
        "buildings":[{"pos":[32,0,32],"seed":7,"building":{}}]})");
    map.set("world",manifest());
    auto sim=std::make_unique<Simulation>(
        SimConfig{13,60,0,true},std::make_unique<game::GameCore>());
    sim->setup(rules,map);
    return sim;
}
void runOne(Simulation& sim) {
    const auto start=std::chrono::steady_clock::now();
    const auto tick=sim.tick();
    while(sim.tick()==tick){
        sim.step();
        AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void revisionOnPublication() {
    wd::SpatialWorld collision;
    std::atomic<bool> allow{true};
    wd::CollisionStream stream(manifest(),reader(allow),collision);
    AIVERSE_TEST_CHECK(stream.revision()==0);
    wd::Bounds box{{0_fx,-1_fx,0_fx},{31_fx,4_fx,15_fx}};
    const auto start=std::chrono::steady_clock::now();
    while(!stream.prepareRegions({&box,1})){
        AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AIVERSE_TEST_CHECK(stream.revision()==2 && stream.covers(box));
    AIVERSE_TEST_CHECK(stream.prepareRegions({&box,1}));
    AIVERSE_TEST_CHECK(stream.revision()==2); // unchanged geometry
    stream.prepareRegions({});
    AIVERSE_TEST_CHECK(stream.revision()==4 && !stream.covers(box));
}
void worldSpaceAdmission() {
    std::atomic<bool> allow{false};
    auto one=create(),two=create();
    auto& a=actor::host(*one);
    auto& b=actor::host(*two);
    a.setContentReader(reader(allow));
    b.setContentReader(reader(allow));
    const auto initial=one->checksum();
    const auto rng=one->rng().state();
    one->step();
    AIVERSE_TEST_CHECK(one->tick()==0 && one->checksum()==initial);
    AIVERSE_TEST_CHECK(one->rng().state()==rng && a.navigationRebuilds()==0);
    actor::Input input;
    input.sequence=1;
    input.forward=1000;
    input.yaw=Fixed::fromRatio(1,4);
    auto command=actor::command(1,input);
    command.tick=0;
    one->inject(command);
    two->inject(command);
    one->step();
    AIVERSE_TEST_CHECK(one->tick()==0 && one->executedCommands().empty());
    AIVERSE_TEST_CHECK(a.navigationRebuilds()==0);
    allow=true;
    for(int i=0;i<12;++i) {
        runOne(*one);
        runOne(*two);
        AIVERSE_TEST_CHECK(one->checksum()==two->checksum());
    }
    AIVERSE_TEST_CHECK(a.navigationRebuilds()==1 && b.navigationRebuilds()==1);
    AIVERSE_TEST_CHECK(a.navigationCollisionRevision()>=2);
    const auto revision=a.navigationCollisionRevision();
    for(int i=0;i<12;++i) {
        one->step();two->step();
        AIVERSE_TEST_CHECK(one->checksum()==two->checksum());
    }
    AIVERSE_TEST_CHECK(a.navigationRebuilds()==1 && a.navigationCollisionRevision()==revision);
    const auto saved=one->serialize();
    auto restored=create();
    actor::host(*restored).setContentReader(reader(allow));
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(one->checksum()==restored->checksum());
    for(int i=0;i<12;++i){
        runOne(*one);runOne(*restored);
        AIVERSE_TEST_CHECK(one->checksum()==restored->checksum());
    }
    AIVERSE_TEST_CHECK(actor::host(*restored).navigationRebuilds()==1);
}
}
int main(){
    revisionOnPublication();
    worldSpaceAdmission();
    std::cout<<"PASS world-space collision/nav readiness admission and fresh restore\n";
}
