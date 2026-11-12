#include "engine/net/authority/Session.h"
#include <cstdio>
#include <chrono>
#include "engine/net/LatestChannel.h"
#include "engine/world/ChunkScene.h"
#include "engine/net/Udp.h"
#include "modules/fps/view/FirstPersonCamera.h"
#include <stdexcept>
using namespace aiverse;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);} while(0)
namespace {
std::unique_ptr<Simulation> simulation(Json rules,Json map) {
    auto sim=std::make_unique<Simulation>(SimConfig{7,fps::readConfig(rules).tickRate,0},std::make_unique<fps::Module>());sim->setup(rules,map);return sim;
}
fps::State& state(Simulation& sim){return *sim.world().resource<fps::State>();}
void spatial() {
    world::SpatialWorld w(32);CHECK(w.add({1,{{-2_fx,0_fx,-2_fx},{2_fx,2_fx,2_fx}}}));
    auto hit=w.cast({0_fx,1_fx,-10_fx},{0_fx,0_fx,20_fx});CHECK(hit.hit&&hit.id==1);CHECK(fxAbs(hit.fraction-Fixed::fromRatio(4,10))<Fixed::fromRaw(3));
    CHECK(w.cell({Fixed::fromRatio(-1,10),{}, {}}).x==-1);
    CHECK(!w.cast({10_fx,1_fx,-10_fx},{0_fx,0_fx,20_fx}).hit);
    CHECK(w.add({2,{{-100000_fx,-2_fx,-100000_fx},{100000_fx,-1_fx,100000_fx}}}));
    CHECK(w.cast({90000_fx,1_fx,90000_fx},{0_fx,-4_fx,0_fx}).id==2);
    w.remove(1);CHECK(w.query({{-3_fx,0_fx,-3_fx},{3_fx,3_fx,3_fx}}).empty());
    CHECK(world::relativeTo({100000_fx,{},{}},{3125,0,0},32).x==0_fx);
    world::Streamer stream;stream.desire({{0,0,0},{1,0,0}});int loads=0,unloads=0;
    stream.pump(1,[&](auto){++loads;return true;},[&](auto){++unloads;});CHECK(!stream.ready());
    stream.pump(1,[&](auto){++loads;return true;},[&](auto){++unloads;});CHECK(stream.ready()&&loads==2);
    stream.desire({{2,0,0}});stream.pump(1,[](auto){return false;},[&](auto){++unloads;});CHECK(unloads==0);
    stream.pump(1,[](auto){return true;},[&](auto){++unloads;});CHECK(stream.ready()&&unloads==2);
}
void cameraControls() {
    using namespace render;
    for(float yaw:{0.f,0.25f,0.5f,0.75f}) {
        fps::view::Look look{yaw,0};auto before=look.camera({});auto forward=normalize(before.target-before.eye());auto right=normalize(cross(forward,{0,1,0}));
        look.motion(10,0);auto camera=look.camera({});auto changed=normalize(camera.target-camera.eye());CHECK(dot(changed,right)>0);
        look={yaw,0};look.motion(0,-10);camera=look.camera({});CHECK((camera.target-camera.eye()).y>0);
        fps::Actor actor;fps::Input input;input.yaw=Fixed::fromRaw(int64_t(yaw*65536));input.right=1000;
        world::SpatialWorld empty;fps::moveActor(actor,input,{},empty,Fixed::fromRatio(1,60),1000_fx);
        float3 movement{float(actor.pos.x.toDouble()),0,float(actor.pos.z.toDouble())};CHECK(dot(movement,right)>0);
    }
}
void latestChannel() {
    net::LatestChannel sender,receiver;std::vector<net::Bytes> packets;
    sender.queue(net::Bytes(2500,7));sender.flush(123,[&](const auto& b){packets.push_back(b);});
    auto receive=[&](const net::Bytes& b){ByteReader r(b.data(),b.size());net::PacketKind kind;uint32_t token;CHECK(net::readPacketHeader(r,kind,token));CHECK(kind==net::PacketKind::Latest&&token==123);CHECK(receiver.receive(r));};
    CHECK(packets.size()==3);receive(packets[2]);receive(packets[0]);net::Bytes out;CHECK(!receiver.recv(out));
    auto old=packets;packets.clear();sender.queue(net::Bytes(1500,9));sender.flush(123,[&](const auto& b){packets.push_back(b);});
    receive(packets[1]);receive(old[1]);CHECK(!receiver.recv(out));receive(packets[0]);CHECK(receiver.recv(out)&&out==net::Bytes(1500,9));
    receive(old[0]);receive(old[1]);receive(old[2]);CHECK(!receiver.recv(out));
    for(size_t n=0;n<packets[0].size();++n){ByteReader r(packets[0].data(),n);net::PacketKind kind;uint32_t token;if(net::readPacketHeader(r,kind,token)){net::LatestChannel malformed;CHECK(!malformed.receive(r));}}
}
void chunks() {
    world::ChunkScene scene;scene.configure(Json::parse(R"({"cellSize":256,"chunks":[{"cell":[0,0,0],"file":"a.json"},{"cell":[10,0,0],"file":"b.json"}]})"));
    int reads=0;auto read=[&](const auto&,std::string& bytes){++reads;bytes=R"({"boxes":[{"min":[0,0,0],"max":[1,1,1]}]})";return true;};
    scene.update({},1,2,read);CHECK(scene.residentCount()==1&&reads==1);
    scene.update({2560_fx,{},{}},1,2,read);CHECK(scene.residentCount()==1&&reads==2);CHECK(scene.chunks().begin()->first.x==10);
}
void stress(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    net::authority::Server server(rules,map);std::vector<std::unique_ptr<net::authority::Client>> clients;
    for(uint8_t i=1;i<=100;++i){auto pair=net::makeLoopbackPair({20,5,i});CHECK(server.attach(i,std::move(pair.first)));clients.push_back(std::make_unique<net::authority::Client>(rules,map,std::move(pair.second)));}
    auto start=std::chrono::steady_clock::now();
    for(uint32_t tick=0;tick<300;++tick) {
        auto now=uint64_t(tick)*34;
        for(auto& c:clients){c->poll(now);fps::Input input;input.forward=tick<150?1000:0;c->submit(input);}
        server.poll(now);server.step();
    }
    for(const auto& c:clients){CHECK(c->ready());CHECK(c->snapshot().ack>200);CHECK(c->snapshot().actors.size()<10);}
    CHECK(server.simulation().executedCommands().empty());
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::printf("100 clients / 300 ticks / sparse 16 km world: %lld ms (debug/local loopback, not WAN capacity)\n",(long long)elapsed);
}
void deterministic(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    auto a=simulation(rules,map),b=simulation(rules,map);
    for(uint32_t t=0;t<240;++t) {
        fps::Input in;in.sequence=t+1;in.forward=t<100?1000:0;in.buttons=t==130?uint32_t(fps::Jump):0u;
        auto cmd=fps::command(1,in);cmd.tick=t;a->inject(cmd);b->inject(cmd);a->step();b->step();CHECK(a->checksum()==b->checksum());
    }
    CHECK(state(*a).actors[0].pos.y<1_fx);CHECK(state(*a).actors[0].pos.z> -12_fx);
    auto saved=a->serialize();CHECK(!saved.empty());auto restored=simulation(rules,map);CHECK(restored->deserialize(saved));CHECK(restored->checksum()==a->checksum());
    SimSnapshot snapshot;a->saveState(snapshot);auto hash=a->checksum();a->step();a->loadState(snapshot);CHECK(hash==a->checksum());
    for(int i=0;i<120;++i){a->step();restored->step();CHECK(a->checksum()==restored->checksum());}
    auto before=state(*a).actors[0];fps::Input bad;bad.forward=1001;bad.sequence=10000;
    auto c=fps::command(1,bad);a->module().applyCommand(*a,c);CHECK(before==state(*a).actors[0]);
    c=fps::command(201,{});a->module().applyCommand(*a,c);CHECK(before==state(*a).actors[0]);
}
void combat(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    auto& players=map.member("players").itemsMut();players.resize(2);players[1].set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);
    for(uint32_t t=0;t<25;++t){fps::Input i;i.buttons=fps::Fire;i.sequence=t+1;i.viewTick=t;auto c=fps::command(1,i);c.tick=t;sim->inject(c);sim->step();}
    CHECK(state(*sim).actors[1].health==0);CHECK(state(*sim).actors[0].kills==1);CHECK(state(*sim).actors[0].ammo<30);
    map.set("spawns",Json::parse("[[0,0.01,-10],[0,0.01,10]]"));sim=simulation(rules,map);
    for(uint32_t t=0;t<25;++t){fps::Input i;i.buttons=fps::Fire;i.sequence=t+1;auto c=fps::command(1,i);c.tick=t;sim->inject(c);sim->step();}
    CHECK(state(*sim).actors[1].health==100);
}
void weapons(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    rules.member("fps").set("weapons",Json::parse(R"([{"automatic":false,"damage":10,"magazine":3,"reloadTicks":3,"intervalTicks":1,"switchTicks":2},{"damage":1,"magazine":8,"intervalTicks":1,"switchTicks":2},{"damage":1,"magazine":8,"pellets":9,"spread":0.006,"switchTicks":2}])"));
    map.member("players").itemsMut().resize(2);map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);
    auto step=[&](uint32_t buttons){fps::Input i;i.buttons=buttons;i.sequence=sim->tick()+1;i.viewTick=sim->tick();auto c=fps::command(1,i);c.tick=sim->tick();sim->inject(c);sim->step();};
    for(int i=0;i<5;++i)step(fps::Fire);
    CHECK(state(*sim).actors[0].shots==1);CHECK(state(*sim).actors[1].health==90);
    step(0);step(fps::Fire);CHECK(state(*sim).actors[0].ammo==1);
    step(fps::Reload);step(0);step(0);step(0);CHECK(state(*sim).actors[0].ammo==3);CHECK(state(*sim).actors[0].reserve==118);
    step(fps::selectWeapon(1)|fps::Fire);CHECK(state(*sim).actors[0].shots==2);
    step(fps::Fire);step(fps::Fire);step(fps::Fire);CHECK(state(*sim).actors[0].shots==4);
    step(fps::selectWeapon(0));CHECK(state(*sim).actors[0].ammo==3);
    step(fps::selectWeapon(1));CHECK(state(*sim).actors[0].ammo==6);
    auto before=state(*sim).actors[0].weaponIndex;step(fps::selectWeapon(3));CHECK(state(*sim).actors[0].weaponIndex==before);
    auto bytes=sim->serialize();auto restored=simulation(rules,map);CHECK(restored->deserialize(bytes));CHECK(restored->checksum()==sim->checksum());
    auto invalid=rules;invalid.member("fps").set("weapons",Json::parse("[]"));bool rejected=false;try{fps::readConfig(invalid);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    // Shotgun consumes one shell per nine independently occluded rays.
    step(fps::selectWeapon(2));step(0);step(fps::Fire);CHECK(state(*sim).actors[0].ammo==7);
}
#include "systems.inc"
#include "projectiles.inc"
void network(Json rules,Json map) {
    rules.member("fps").set("weapons",Json::parse(R"([{"magazine":12},{"magazine":30}])"));
    rules.member("fps").set("mode",Json::string("deathmatch"));
    rules.member("fps").set("interestRadius",Json::number(int64_t(50)));
    net::authority::Server server(rules,map);auto pair=net::makeLoopbackPair({30,8,42});
    CHECK(server.attach(1,std::move(pair.first)));net::authority::Client client(rules,map,std::move(pair.second));
    for(uint64_t now=0;now<6000;now+=5) {
        client.poll(now);server.poll(now);
        if(now%20==0){fps::Input i;i.right=now<2000?1000:0;i.buttons=fps::selectWeapon(1);client.submit(i);server.step();}
    }
    CHECK(client.ready());CHECK(client.predicted().weaponIndex==1);CHECK(client.predicted().storedAmmo[0]==12);CHECK(client.snapshot().ack>100);CHECK(client.predicted().pos.x< -4_fx);
    CHECK(server.rejected()==0);CHECK(client.pending()<20);
    auto packet=net::authority::encodeSnapshot(client.snapshot());net::authority::Snapshot decoded;
    CHECK(net::authority::decodeSnapshot(packet,decoded));CHECK(decoded.tick==client.snapshot().tick);
    for(size_t n=0;n<packet.size();++n){auto corrupt=packet;corrupt.resize(n);CHECK(!net::authority::decodeSnapshot(corrupt,decoded));}
    packet.push_back(0);CHECK(!net::authority::decodeSnapshot(packet,decoded));
    auto crowded=client.snapshot();crowded.actors.clear();
    for(int slot=1;slot<=200;++slot){auto actor=client.predicted();actor.slot=uint8_t(slot);crowded.actors.push_back(actor);}
    packet=net::authority::encodeSnapshot(crowded);CHECK(packet.size()<=131072);CHECK(net::authority::decodeSnapshot(packet,decoded));
    // A remote entity outside the interest radius is absent from the entire replacement snapshot.
    state(server.simulation()).actors[1].pos.x=100_fx;server.broadcast();
    for(uint64_t now=6000;now<6200;now+=5){client.poll(now);server.poll(now);}
    server.step();server.broadcast();
    for(uint64_t now=6200;now<6400;now+=5){client.poll(now);server.poll(now);}
    for(const auto& a:client.snapshot().actors)CHECK(a.slot!=2);
}
}
int main(int argc,char** argv) {
    try {std::string root=argc>1?argv[1]:"examples/fps/arena";auto rules=Json::parseFile(root+"/rules.json"),map=Json::parseFile(root+"/map.json");spatial();cameraControls();latestChannel();chunks();deterministic(rules,map);combat(rules,map);weapons(rules,map);systems();projectilePrograms();network(rules,map);auto br=root+"/../battle-royale";stress(Json::parseFile(br+"/rules.json"),Json::parseFile(br+"/map.json"));std::puts("FPS spatial, motor, combat, save/rollback and authority tests passed");}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
