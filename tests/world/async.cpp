#include "engine/world/CollisionStream.h"
#include "engine/pack/Sha256.h"
#include "modules/fps/FpsModule.h"
#include "engine/net/authority/Session.h"
#include "engine/platform/ContentReader.h"
#include "modules/fps/Project.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::world;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(0)
namespace {
void spin(const std::function<bool()>& done) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!done()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
std::string hash(const std::string& s){auto h=pack::sha256(s.data(),s.size());return pack::toHex(h.data(),h.size());}
Json manifest(const std::map<std::string,std::string>& files) {
    auto m=Json::parse(R"({"async":true,"cellSize":16,"maxResidentBytes":8388608,"maxStagingBytes":8388608,"maxResidentChunks":4,"retryUpdates":1,"assets":{},"chunks":[]})");
    int cell=0;for(const auto& [file,text]:files){m.member("assets").set(file,Json::string(hash(text)));auto e=Json::object();e.set("file",Json::string(file));e.set("cell",Json::parse("["+std::to_string(cell++)+",0,0]"));e.set("reservedBytes",Json::number(int64_t(1024*1024)));m.member("chunks").push(e);}
    return m;
}
void asynchronous() {
    struct Payload:AsyncChunkStream::Resource{};
    std::promise<void> release,started;auto go=release.get_future().share();auto begun=started.get_future();
    const auto owner=std::this_thread::get_id();std::atomic<bool> background=false;bool first=true;
    AsyncChunkStream stream({10,1,1},10,1,[&](Cell){background=std::this_thread::get_id()!=owner;if(first){first=false;started.set_value();go.wait();}return std::make_shared<Payload>();});
    stream.add({},10);stream.add({1,0,0},10);StreamObserver interest{{},0};std::set<Cell> published;
    auto publish=[&](Cell c,const auto&){CHECK(std::this_thread::get_id()==owner);published.insert(c);return true;};auto retire=[&](Cell c){published.erase(c);};
    auto s=stream.update({&interest,1},{},1,1,publish,retire);CHECK(s.ready==0&&s.stagingBytes==10&&!s.complete());
    begun.wait();CHECK(background);
    interest.cell={1,0,0};s=stream.update({&interest,1},{},1,1,publish,retire);CHECK(s.stagingBytes==10&&published.empty());
    release.set_value();spin([&]{s=stream.update({&interest,1},{},1,1,publish,retire);return s.complete();});
    CHECK(published.size()==1&&published.contains(interest.cell)&&s.discarded==1&&s.stagingBytes==0);
    stream.shutdown(retire);CHECK(published.empty());
}
void triangleQueries() {
    SpatialWorld world;
    CHECK(world.addTriangle(1,{{0_fx,0_fx,0_fx},{16_fx,2_fx,16_fx},{16_fx,2_fx,0_fx}}));
    CHECK(world.addTriangle(2,{{0_fx,0_fx,0_fx},{0_fx,0_fx,16_fx},{16_fx,2_fx,16_fx}}));
    auto hit=world.cast({8_fx,10_fx,8_fx},{0_fx,-20_fx,0_fx});CHECK(hit.hit);CHECK(fxAbs(hit.fraction-Fixed::fromRatio(9,20))<Fixed::fromRatio(1,1000));CHECK(hit.normal.y>Fixed::fromRatio(9,10));
    CHECK(!world.cast({20_fx,10_fx,8_fx},{0_fx,-20_fx,0_fx}).hit);
    CHECK(!world.blocked({{1_fx,3_fx,1_fx},{2_fx,4_fx,2_fx}}));
    CHECK(world.blocked({{7_fx,0_fx,7_fx},{9_fx,2_fx,9_fx}}));
    CHECK(!world.addTriangle(3,{{},{},{}}));
    world.remove(1);world.remove(2);CHECK(world.colliders().empty());
    // A triangle does not fill the other half of its bounding box.
    CHECK(world.addTriangle(1,{{0_fx,0_fx,0_fx},{16_fx,0_fx,0_fx},{0_fx,0_fx,16_fx}}));
    CHECK(!world.cast({15_fx,5_fx,15_fx},{0_fx,-10_fx,0_fx}).hit);
    CHECK(world.cast({2_fx,5_fx,2_fx},{0_fx,-10_fx,0_fx}).hit);
    CHECK(!world.cast({2_fx,1_fx,2_fx},{1_fx,0_fx,0_fx},{Fixed::fromRatio(3,10),1_fx,Fixed::fromRatio(3,10)}).hit);
}
void terrainCollision() {
    std::map<std::string,std::string> files={{"a.json",R"({"terrain":{"resolution":2,"heights":[0,2,0,2]}})"},{"b.json",R"({"terrain":{"resolution":2,"heights":[2,4,2,4]}})"}};
    auto m=manifest(files);SpatialWorld world;CollisionStream stream(m,[&](const auto& file,auto& text){text=files.at(file);return true;},world);
    Bounds region{{0_fx,-10_fx,0_fx},{32_fx,10_fx,16_fx}};
    CHECK(!stream.covers(region));spin([&]{return stream.prepareRegions({&region,1});});CHECK(stream.covers(region));CHECK(world.colliders().size()==4);
    auto hit=world.cast({24_fx,10_fx,8_fx},{0_fx,-20_fx,0_fx});CHECK(hit.hit&&fxAbs(hit.fraction-Fixed::fromRatio(7,20))<Fixed::fromRatio(1,1000));
    fps::Actor actor;actor.health=100;actor.pos={8_fx,3_fx,8_fx};fps::MotorConfig motor;
    for(int i=0;i<120;++i)fps::moveActor(actor,{},motor,world,Fixed::fromRatio(1,60),1000_fx);
    CHECK(actor.grounded);CHECK(actor.pos.y>0_fx&&actor.pos.y<2_fx);
    fps::Input input;input.yaw=Fixed::fromRatio(1,4);input.forward=1000;
    auto before=actor.pos;for(int i=0;i<60;++i)fps::moveActor(actor,input,motor,world,Fixed::fromRatio(1,60),1000_fx);
    CHECK(actor.pos.x>before.x+3_fx&&actor.pos.y>before.y);
    stream.prepareRegions({});CHECK(world.colliders().empty());
}
void corruptionAndSeams() {
    std::map<std::string,std::string> files={{"a.json",R"({"terrain":{"resolution":2,"heights":[0,0,0,0]}})"},{"b.json",R"({"terrain":{"resolution":2,"heights":[1,1,1,1]}})"}};
    auto m=manifest(files);SpatialWorld world;std::atomic<bool> corrupt=true;
    CollisionStream stream(m,[&](const auto& file,auto& text){text=corrupt?"bad":files.at(file);return true;},world);
    Bounds a{{0_fx,-1_fx,0_fx},{15_fx,4_fx,15_fx}};
    spin([&]{stream.prepareRegions({&a,1});return !stream.status().error.empty();});CHECK(!stream.covers(a)&&world.colliders().empty());
    corrupt=false;spin([&]{return stream.prepareRegions({&a,1});});
    Bounds both{{0_fx,-1_fx,0_fx},{31_fx,4_fx,15_fx}};
    spin([&]{stream.prepareRegions({&both,1});return !stream.status().error.empty();});CHECK(!stream.covers(both));CHECK(world.colliders().size()==2);
}
void exampleIntegration() {
    auto reader=platform::makeContentReader("examples/world/streamed");
    auto project=fps::readProject(reader);
    auto walk=[&](bool doorway){
        auto map=project.map;
        if(!doorway)map.member("spawns").at(0)=Json::parse("[25,2,32]");
        net::authority::Server server(project.rules,map,3,reader);
        auto pair=net::makeLoopbackPair();CHECK(server.attach(1,std::move(pair.first)));
        net::authority::Client client(project.rules,map,std::move(pair.second),reader);
        uint64_t now=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(server.simulation().tick()<1500){
            CHECK(std::chrono::steady_clock::now()<deadline);now+=17;
            client.updateStreaming();client.poll(now);server.poll(now);
            fps::Input input;input.forward=1000;client.submit(input);server.poll(now);
            if(!server.step())std::this_thread::sleep_for(std::chrono::milliseconds(1));
            client.poll(now);
        }
        const auto& actor=server.module().state(server.simulation()).actors[0];
        CHECK(client.ready()&&actor.grounded);
        if(doorway)CHECK(actor.pos.z>128_fx);else CHECK(actor.pos.z<63_fx&&actor.pos.z>60_fx);
        CHECK(server.module().streamingStatus()->selection.reservedBytes<=41943040);
    };
    walk(true);walk(false);
}
void simulationGate() {
    std::map<std::string,std::string> files={{"a.json",R"({"terrain":{"resolution":2,"heights":[0,0,0,0]}})"}};
    auto rules=Json::parse(R"({"fps":{"tickRate":60,"maxPlayers":2,"mode":"deathmatch"}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[4,2,4],[12,2,12]],"colliders":[]})");map.set("world",manifest(files));
    auto create=[&]{auto sim=std::make_unique<Simulation>(SimConfig{3,60,0},std::make_unique<fps::Module>());sim->setup(rules,map);return sim;};
    auto a=create(),b=create();auto initial=a->checksum();a->step();CHECK(a->tick()==0&&a->checksum()==initial);
    std::atomic<bool> allow=false;
    auto read=[&](const std::string& f,std::string& s){if(!allow)return false;s=files.at(f);return true;};
    static_cast<fps::Module&>(a->module()).setContentReader(read);static_cast<fps::Module&>(b->module()).setContentReader(read);
    fps::Input input;input.sequence=1;input.forward=1000;input.yaw=Fixed::fromRatio(1,4);
    auto command=fps::command(1,input);command.tick=0;a->inject(command);b->inject(command);
    a->step();CHECK(a->tick()==0&&a->executedCommands().empty());allow=true;
    auto step=[](Simulation& sim){auto before=sim.tick();spin([&]{sim.step();return sim.tick()!=before;});};
    for(int i=0;i<60;++i){step(*a);step(*b);CHECK(a->checksum()==b->checksum());}
    auto save=a->serialize();auto restored=create();static_cast<fps::Module&>(restored->module()).setContentReader(read);CHECK(restored->deserialize(save));
    for(int i=0;i<30;++i){step(*a);step(*restored);CHECK(a->checksum()==restored->checksum());}
}
}
int main(){try{std::puts("async");asynchronous();std::puts("triangles");triangleQueries();std::puts("terrain");terrainCollision();std::puts("corruption");corruptionAndSeams();std::puts("simulation");simulationGate();std::puts("example network and motor");exampleIntegration();std::puts("Async world: background preparation, cancellation, budgets, triangles, terrain motor, hashes, seams and restore passed");}
catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
