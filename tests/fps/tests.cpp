#include "engine/net/authority/Session.h"
#include "engine/game/GameCore.h"
#include "engine/net/Rollback.h"
#include "engine/bots/Persistence.h"
#include <cstdio>
#include <algorithm>
#include <array>
#include "engine/audio/Audio.h"
#include <chrono>
#include "engine/net/LatestChannel.h"
#include "engine/world/ChunkScene.h"
#include "engine/net/Udp.h"
#include "engine/render/CameraBehavior.h"
#include <stdexcept>
#include <optional>
#include <fstream>
#include "engine/actor/Project.h"
using namespace aiverse;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);} while(0)
namespace {
std::unique_ptr<Simulation> simulation(Json rules,Json map) {
    auto sim=std::make_unique<Simulation>(SimConfig{7,actor::readConfig(rules).tickRate,0},std::make_unique<game::GameCore>());sim->setup(rules,map);return sim;
}
actor::State& state(Simulation& sim){return *sim.world().resource<actor::State>();}
actor::ActorHost& fpsModule(Simulation& sim){return actor::host(sim);}
// Writable view of the character in `slot` (1..; the former State::actors[slot-1]).
actor::CharacterRef character(Simulation& sim,uint8_t slot){return fpsModule(sim).character(sim,slot);}
// Copies of the dynamic objects in id order (the former State vectors).
std::vector<actor::Projectile> projectilesOf(Simulation& sim){return fpsModule(sim).projectiles(sim);}
std::vector<actor::Loot> lootOf(Simulation& sim){return fpsModule(sim).loot(sim);}
std::vector<actor::Vehicle> vehiclesOf(Simulation& sim){return fpsModule(sim).vehicles(sim);}
// Writable component of the i-th object in id order (the entity is the state).
actor::Projectile& projectileAt(Simulation& sim,size_t i){return *sim.world().tryGet<actor::Projectile>(fpsModule(sim).projectileEntities(sim).at(i));}
actor::Loot& lootAt(Simulation& sim,size_t i){return *sim.world().tryGet<actor::Loot>(fpsModule(sim).lootEntities(sim).at(i));}
actor::Vehicle& vehicleAt(Simulation& sim,size_t i){return *sim.world().tryGet<actor::Vehicle>(fpsModule(sim).vehicleEntities(sim).at(i));}
// Adds a dynamic object entity with the next object id (spawned outside the gameplay code).
template<class T> Entity spawnObject(Simulation& sim,T value){value.id=state(sim).nextObject++;const Entity e=sim.world().create();sim.world().add<T>(e,std::move(value));return e;}
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
void audioBank(const std::string& path) {
    audio::AudioEngine audio;audio::AudioConfig config;config.openDevice=false;config.sampleRate=44100;audio.init(config);
    std::string error;CHECK(audio.loadBank(Json::parseFile(path),[](const std::string&,std::string&){return false;},&error));CHECK(audio.warnings().empty());
    for(const char* sound:{"shot0","shot1","shot2","shot3","step","reload"})CHECK(audio.play(sound)!=audio::kNoVoice);
    float buffer[2048]{};audio.render(buffer,1024);bool audible=false;for(float sample:buffer)audible|=sample!=0;CHECK(audible);audio.shutdown();
}
void cameraControls() {
    using namespace render;
    for(float yaw:{0.f,0.25f,0.5f,0.75f}) {
        render::CameraLook look{yaw,0};auto before=look.camera({});auto forward=normalize(before.target-before.eye());auto right=normalize(cross(forward,{0,1,0}));
        look.motion(10,0);auto camera=look.camera({});auto changed=normalize(camera.target-camera.eye());CHECK(dot(changed,right)>0);
        look={yaw,0};look.motion(0,-10);camera=look.camera({});CHECK((camera.target-camera.eye()).y>0);
        actor::CharacterSnapshot actor;actor::Input input;input.yaw=Fixed::fromRaw(int64_t(yaw*65536));input.right=1000;
        world::SpatialWorld empty;actor::moveActor(actor,input,{},empty,Fixed::fromRatio(1,60),1000_fx);
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
    rules.member("actor").set("mode",Json::string("deathmatch"));
    net::authority::Server server(rules,map);std::vector<std::unique_ptr<net::authority::Client>> clients;
    for(uint8_t i=1;i<=100;++i){auto pair=net::makeLoopbackPair({20,5,i});CHECK(server.attach(i,std::move(pair.first)));clients.push_back(std::make_unique<net::authority::Client>(rules,map,std::move(pair.second)));}
    auto start=std::chrono::steady_clock::now();
    for(uint32_t tick=0;tick<300;++tick) {
        auto now=uint64_t(tick)*34;
        for(auto& c:clients){c->poll(now);actor::Input input;input.forward=tick<150?1000:0;c->submit(input);}
        server.poll(now);server.step();
    }
    for(const auto& c:clients){CHECK(c->ready());CHECK(c->snapshot().ack>200);CHECK(c->snapshot().characters.size()<10);}
    const auto& stats=server.snapshotStats();CHECK(stats.packets>1000&&stats.bytes*2<stats.fullBytes&&stats.fullPackets<stats.packets/10);
    std::printf("snapshot deltas: %llu packets, %llu bytes vs %llu full (%llu sent full)\n",(unsigned long long)stats.packets,(unsigned long long)stats.bytes,(unsigned long long)stats.fullBytes,(unsigned long long)stats.fullPackets);
    CHECK(server.simulation().executedCommands().empty());
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::printf("100 clients / 300 ticks / sparse 16 km world: %lld ms (debug/local loopback, not WAN capacity)\n",(long long)elapsed);
}
void parallelMotorParityFixture(Json rules,Json map) {
    auto& settings=rules.member("actor");
    settings.set("maxPlayers",Json::number(int64_t(100)));
    settings.set("mode",Json::string("deathmatch"));
    settings.erase("mechanics");
    settings.erase("melee");
    settings.erase("royale");
    map.set("colliders",Json::parse(
        R"([{"id":1,"min":[-128,-2,-128],"max":[128,0,128]}])"));
    auto players=Json::array(),spawns=Json::array();
    for(int i=0;i<100;++i) {
        auto p=Json::object();
        p.set("name",Json::string("Motor "+std::to_string(i)));
        p.set("team",Json::number(int64_t(i<50?1:2)));
        p.set("bot",Json::fromBool(false));
        players.push(std::move(p));
        auto spawn=Json::array();
        spawn.push(Json::number(int64_t(-80+16*(i%10))));
        spawn.push(Json::numberText("0.01"));
        spawn.push(Json::number(int64_t(-80+16*(i/10))));
        spawns.push(std::move(spawn));
    }
    map.set("players",std::move(players));
    map.set("spawns",std::move(spawns));
    auto sim=simulation(rules,map);
    Hasher history;
    const auto movementStart=std::chrono::steady_clock::now();
    for(uint32_t t=0;t<120;++t) {
        for(uint32_t i=0;i<100;++i) {
            actor::Input input;
            input.sequence=t+1;
            input.forward=500;
            input.right=(int(i%3)-1)*200;
            input.buttons=(t==10?actor::Jump:0u) |
                          (t>=30&&t<40?actor::Crouch:0u) |
                          (t>=60?actor::Sprint:0u);
            auto command=actor::command(uint8_t(i+1),input);
            command.tick=t;
            sim->inject(command);
        }
        sim->step();
        history.u64(sim->checksum());
    }
    const auto movementMs=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-movementStart).count();
    std::printf("FPS_MOTOR_TICKS_MS=%.3f\n",movementMs);
    SimSnapshot snapshot;
    sim->saveState(snapshot);
    auto current=sim->checksum();
    sim->step();
    sim->loadState(snapshot);
    CHECK(sim->checksum()==current);
    std::printf("FPS_MOTOR_PARITY_HASH=%016llx\n",
        (unsigned long long)history.value());
    // Combat also runs in the speculative motor phase. Earlier actor damage
    // must invalidate a later actor's movement plan before canonical commit.
    auto combatSim=simulation(rules,map);
    Hasher combatHistory;
    for(uint32_t t=0;t<80;++t) {
        for(uint32_t i=0;i<100;++i) {
            actor::Input input;
            input.sequence=t+1;
            input.viewTick=t;
            input.yaw=i<50?Fixed{}:Fixed::fromRatio(1,2);
            input.forward=300;
            if(i%6==0)input.buttons=actor::Fire;
            auto cmd=actor::command(uint8_t(i+1),input);
            cmd.tick=t;
            combatSim->inject(cmd);
        }
        combatSim->step();
        combatHistory.u64(combatSim->checksum());
    }
    std::printf("FPS_COMBAT_PARITY_HASH=%016llx\n",
        (unsigned long long)combatHistory.value());
}
void cowSnapshotsFps(Json rules,Json map) {
    rules.member("actor").set("mode",Json::string("deathmatch"));
    auto baseline=simulation(rules,map),incremental=simulation(rules,map);
    incremental->world().enableCowSnapshots(true);
    for(uint32_t tick=0;tick<120;++tick) {
        actor::Input input;
        input.sequence=tick+1;
        input.forward=tick<70?700:0;
        input.right=tick>=70?200:0;
        input.buttons=(tick==20?uint32_t(actor::Jump):0u);
        auto cmd=actor::command(1,input);
        cmd.tick=tick;
        baseline->inject(cmd);
        incremental->inject(cmd);
        baseline->step();incremental->step();
        CHECK(baseline->checksum()==incremental->checksum());
        if(tick==40 || tick==80) {
            SimSnapshot a,b;
            baseline->saveState(a);
            incremental->saveState(b);
            const size_t sharedResources=incremental->world().sharedResourceCount();
            CHECK(sharedResources>=1);
            const auto checksum=incremental->checksum();
            CHECK(incremental->world().sharedResourceCount()==sharedResources);
            CHECK(baseline->checksum()==checksum);
            // Exercise two full rollbacks without losing original state
            // or accidentally retaining mutable shared singleton resources.
            baseline->step();incremental->step();
            baseline->loadState(a);incremental->loadState(b);
            CHECK(incremental->checksum()==checksum);
            CHECK(baseline->checksum()==checksum);
        }
    }
    CHECK(incremental->world().cowSnapshotsEnabled());
    auto archived=incremental->serialize();
    CHECK(!archived.empty());
    auto loaded=simulation(rules,map);
    CHECK(loaded->deserialize(archived));
    CHECK(loaded->checksum()==incremental->checksum());
}

void incrementalRollbackFps(Json rules,Json map) {
    // Actual FPS module under RollbackSession: a confirmed remote input
    // arrives after several predicted ticks, forcing replay in both modes.
    rules.member("actor").set("mode",Json::string("deathmatch"));
    auto deep=simulation(rules,map),cow=simulation(rules,map);
    auto run=[&](Simulation& sim,bool incremental) {
        net::SessionConfig config;
        config.inputDelay=2;
        config.rollbackWindow=8;
        config.incrementalWorldSnapshots=incremental;
        uint64_t hash=0;
        {
            net::RollbackSession session(sim,config,{2},
                [](uint32_t,const std::vector<Command>&){},
                [](uint32_t,uint64_t){});
            CHECK(sim.world().cowSnapshotsEnabled()==incremental);
            const double tickDuration=1.0/sim.tickRate();
            for(int i=0;i<5;++i)
                session.update(tickDuration,uint64_t(i)*17);
            CHECK(sim.tick()==5);
            actor::Input input;
            input.forward=600;
            input.sequence=1;
            input.viewTick=2;
            auto delayed=actor::command(1,input);
            delayed.tick=2;delayed.seq=1;
            session.confirm(0,3,{delayed});
            session.update(0,85);
            CHECK(session.stats().rollbacks==1);
            CHECK(session.stats().resimulatedTicks>=3);
            for(int i=5;i<8;++i)
                session.update(tickDuration,uint64_t(i)*17);
            hash=sim.checksum();
            CHECK(sim.tick()==8);
        }
        CHECK(!sim.world().cowSnapshotsEnabled());
        return hash;
    };
    CHECK(run(*deep,false)==run(*cow,true));
}
void reloadTimeline(Json rules,Json map) {
    // Real FPS tick uses the same stepWeaponReload policy as generic ECS hosts.
    // Check input/reload ordering, weapon switching, save and rollback replay.
    rules.member("actor").set("mode",Json::string("deathmatch"));
    rules.member("actor").set("weapons",Json::parse(
        R"([{"automatic":false,"damage":10,"magazine":3,"reserve":8,"reloadTicks":3,"intervalTicks":1,"switchTicks":1},
            {"automatic":true,"damage":5,"magazine":4,"reserve":8,"reloadTicks":2,"intervalTicks":1,"switchTicks":1}])"));
    map.member("players").itemsMut().resize(2);
    map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);

    std::vector<actor::Input> inputByTick(25);
    for(uint32_t tick=0;tick<inputByTick.size();++tick) {
        auto& input=inputByTick[tick];
        input.sequence=tick+1;input.viewTick=tick;
        if(tick==0 || tick==3 || tick==5 || tick==14)input.buttons=actor::Fire;
        if(tick==2 || tick==12)input.buttons=actor::Reload;
        if(tick==7)input.buttons=actor::selectWeapon(1);
        if(tick==10)input.buttons=actor::selectWeapon(0);
    }
    SimSnapshot beforeFinish;
    std::vector<std::vector<actor::CharacterSnapshot>> timeline(inputByTick.size());
    auto runTick=[&](uint32_t tick) {
        auto cmd=actor::command(1,inputByTick[tick]);
        cmd.tick=tick;
        sim->inject(cmd);
        sim->step();
        const auto player=character(*sim,1);
        if(tick==2)CHECK(player.ammo==2 && player.reloadUntil==5 && player.reserve==8);
        if(tick==3)CHECK(player.ammo==2 && player.reloadUntil==5);
        if(tick==5)CHECK(player.ammo==2 && player.reloadUntil==0 && player.reserve==7);
        if(tick==7)CHECK(player.weaponIndex==1);
        if(tick==10)CHECK(player.weaponIndex==0);
    };
    for(uint32_t tick=0;tick<inputByTick.size();++tick) {
        runTick(tick);
        timeline[tick]=fpsModule(*sim).snapshots(*sim);
        if(tick==4)sim->saveState(beforeFinish);
    }
    const auto finalChecksum=sim->checksum();
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==finalChecksum);
    sim->loadState(beforeFinish);
    CHECK(fpsModule(*sim).snapshots(*sim)==timeline[4]);
    for(uint32_t tick=5;tick<inputByTick.size();++tick) {
        runTick(tick);
        CHECK(fpsModule(*sim).snapshots(*sim)==timeline[tick]);
    }
    CHECK(sim->checksum()==finalChecksum);
}

void damageScoring(Json rules,Json map) {
    // Shot resolution scores damage, hit counts, death and kills on the character components.
    rules.member("actor").set("mode",Json::string("deathmatch"));
    map.member("players").itemsMut().resize(2);
    map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);
    for(uint32_t tick=0;tick<30;++tick) {
        actor::Input input;
        input.sequence=tick+1;input.viewTick=tick;input.buttons=actor::Fire;
        auto cmd=actor::command(1,input);cmd.tick=tick;
        sim->inject(cmd);
        sim->step();
    }
    CHECK(character(*sim,2).health==0);
    CHECK(character(*sim,1).kills==1);
    CHECK(character(*sim,1).hits>0);
}
// Live, simultaneous FPS hitscan is intentionally synchronous: killing a
// target earlier in the character tick can suppress its later fire. The
// order must hold for shotgun pellets, historical views, saves and replay
// after a rollback.
void simultaneousHitscanRollback(Json rules,Json map) {
    rules.member("actor").set("mode",Json::string("deathmatch"));
    rules.member("actor").set("weapons",Json::parse(
        R"([{"automatic":true,"damage":8,"magazine":24,"reserve":0,"reloadTicks":4,"intervalTicks":1,"pellets":3,"spread":0,"bulletSpeed":0}])"));
    map.member("players").itemsMut().resize(2);
    map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);
    std::vector<std::array<actor::Input,2>> inputs(12);
    for(uint32_t tick=0;tick<inputs.size();++tick) {
        for(uint32_t player=0;player<2;++player) {
            auto& in=inputs[tick][player];
            in.sequence=tick+1;
            in.viewTick=tick>1?tick-1:0; // intentionally rewound target poses
            in.buttons=tick<8?actor::Fire:0u;
            in.yaw=player==0?Fixed{}:Fixed::fromRatio(1,2);
        }
    }
    std::vector<std::vector<actor::CharacterSnapshot>> timeline(inputs.size());
    std::vector<uint64_t> hashes(inputs.size());
    const auto runTick=[&](uint32_t tick) {
        for(uint32_t player=0;player<2;++player) {
            auto cmd=actor::command(uint8_t(player+1),inputs[tick][player]);
            cmd.tick=tick;
            sim->inject(cmd);
        }
        sim->step();
    };
    SimSnapshot snapshot;
    for(uint32_t tick=0;tick<inputs.size();++tick) {
        runTick(tick);
        timeline[tick]=fpsModule(*sim).snapshots(*sim);
        hashes[tick]=sim->checksum();
        if(tick==2)sim->saveState(snapshot);
    }
    CHECK(character(*sim,1).shots>0);
    CHECK(character(*sim,1).hits+character(*sim,2).hits>0);
    const auto expected=sim->checksum();
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==expected);
    // A real completed-tick rollback must reproduce the same two-shooter
    // combat after restoring the original snapshot.
    sim->loadState(snapshot);
    CHECK(sim->checksum()==hashes[2]);
    for(uint32_t tick=3;tick<inputs.size();++tick) {
        runTick(tick);
        CHECK(sim->checksum()==hashes[tick]);
        CHECK(fpsModule(*sim).snapshots(*sim)==timeline[tick]);
    }
    CHECK(sim->checksum()==expected);
}
void dynamicObjectEntities(Json rules,Json map) {
    // Projectiles, loot and vehicles are entities with the Projectile / Loot / Vehicle components:
    // natively created, edited and destroyed entities are the simulation state (accessors, checksum,
    // save, rollback) without any other registration.
    rules.member("actor").set("mode",Json::string("deathmatch"));
    auto sim=simulation(rules,map);
    auto& module=fpsModule(*sim);
    auto& world=sim->world();
    const size_t loot0=module.loot(*sim).size(),vehicles0=module.vehicles(*sim).size();
    const auto initial=sim->checksum();
    actor::Projectile projectile;
    projectile.owner=1;projectile.expires=1000;
    projectile.pos={40_fx,10_fx,40_fx};
    actor::Loot loot;
    loot.type=3;loot.count=2;loot.pos={40_fx,0_fx,40_fx};
    actor::Vehicle vehicle;
    vehicle.health=450;vehicle.pos={50_fx,0_fx,50_fx};
    const Entity p=spawnObject(*sim,projectile),l=spawnObject(*sim,loot),v=spawnObject(*sim,vehicle);
    const uint32_t projectileId=world.tryGetConst<actor::Projectile>(p)->id,lootId=world.tryGetConst<actor::Loot>(l)->id,vehicleId=world.tryGetConst<actor::Vehicle>(v)->id;
    CHECK(projectileId<lootId&&lootId<vehicleId);
    CHECK(sim->checksum()!=initial); // the new entities are state without a tick
    {
        const auto shots=module.projectileEntities(*sim);
        CHECK(std::find(shots.begin(),shots.end(),p)!=shots.end());
        const auto items=module.lootEntities(*sim);
        CHECK(!items.empty()&&items.back()==l);
        CHECK(module.vehicleEntity(*sim,vehicleId)==v);
        CHECK(module.loot(*sim).size()==loot0+1&&module.loot(*sim).back().id==lootId);
        CHECK(module.vehicles(*sim).size()==vehicles0+1&&module.vehicles(*sim).back().id==vehicleId);
    }
    auto findLoot=[&](uint32_t id)->std::optional<actor::Loot>{for(const auto& item:module.loot(*sim))if(item.id==id)return item;return std::nullopt;};
    SimSnapshot snapshot;
    sim->saveState(snapshot);
    for(int i=0;i<8;++i) {
        sim->step();
        CHECK(findLoot(lootId).has_value());
        CHECK(module.vehicleEntity(*sim,vehicleId)==v);
    }
    auto* liveLoot=world.tryGet<actor::Loot>(l);
    CHECK(liveLoot);
    const auto beforeEdit=sim->checksum();
    liveLoot->count=9;
    CHECK(sim->checksum()!=beforeEdit); // direct ECS edit immediately visible
    sim->step();
    CHECK(findLoot(lootId)&&findLoot(lootId)->count==9);
    auto saved=simulation(rules,map);
    CHECK(saved->deserialize(sim->serialize()));
    CHECK(saved->checksum()==sim->checksum());
    // Structural removal is ECS membership.
    world.destroy(l);
    sim->step();
    CHECK(!findLoot(lootId).has_value());
    CHECK(module.loot(*sim).size()==loot0);
    // A snapshot taken before the removal still restores all objects.
    sim->loadState(snapshot);
    CHECK(sim->world().alive(l));
    CHECK(sim->world().tryGetConst<actor::Loot>(l)!=nullptr);
    CHECK(sim->world().tryGetConst<actor::Projectile>(p)!=nullptr);
    CHECK(sim->world().tryGetConst<actor::Vehicle>(v)!=nullptr);
    CHECK(findLoot(lootId)&&findLoot(lootId)->count==2);
}
void deterministic(Json rules,Json map) {
    rules.member("actor").set("mode",Json::string("deathmatch"));
    auto a=simulation(rules,map),b=simulation(rules,map);
    for(uint32_t t=0;t<240;++t) {
        actor::Input in;in.sequence=t+1;in.forward=t<100?1000:0;in.buttons=t==130?uint32_t(actor::Jump):0u;
        auto cmd=actor::command(1,in);cmd.tick=t;a->inject(cmd);b->inject(cmd);a->step();b->step();CHECK(a->checksum()==b->checksum());
    }
    CHECK(character(*a,1).pos.y<1_fx);CHECK(character(*a,1).pos.z> -12_fx);
    auto saved=a->serialize();CHECK(!saved.empty());auto restored=simulation(rules,map);CHECK(restored->deserialize(saved));CHECK(restored->checksum()==a->checksum());
    SimSnapshot snapshot;a->saveState(snapshot);auto hash=a->checksum();a->step();a->loadState(snapshot);CHECK(hash==a->checksum());
    for(int i=0;i<120;++i){a->step();restored->step();CHECK(a->checksum()==restored->checksum());}
    auto before=fpsModule(*a).snapshot(*a,1);actor::Input bad;bad.forward=1001;bad.sequence=10000;
    auto c=actor::command(1,bad);a->module().applyCommand(*a,c);CHECK(before==fpsModule(*a).snapshot(*a,1));
    c=actor::command(201,{});a->module().applyCommand(*a,c);CHECK(before==fpsModule(*a).snapshot(*a,1));
}
void combat(Json rules,Json map) {
    rules.member("actor").set("mode",Json::string("deathmatch"));
    auto& players=map.member("players").itemsMut();players.resize(2);players[1].set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);
    for(uint32_t t=0;t<25;++t){actor::Input i;i.buttons=actor::Fire;i.sequence=t+1;i.viewTick=t;auto c=actor::command(1,i);c.tick=t;sim->inject(c);sim->step();}
    CHECK(character(*sim,2).health==0);CHECK(character(*sim,1).kills==1);CHECK(character(*sim,1).ammo<30);
    map.set("spawns",Json::parse("[[0,0.01,-10],[0,0.01,10]]"));sim=simulation(rules,map);
    for(uint32_t t=0;t<25;++t){actor::Input i;i.buttons=actor::Fire;i.sequence=t+1;auto c=actor::command(1,i);c.tick=t;sim->inject(c);sim->step();}
    CHECK(character(*sim,2).health==100);
}
void weapons(Json rules,Json map) {
    rules.member("actor").set("mode",Json::string("deathmatch"));
    rules.member("actor").set("weapons",Json::parse(R"([{"automatic":false,"damage":10,"magazine":3,"reloadTicks":3,"intervalTicks":1,"switchTicks":2},{"damage":1,"magazine":8,"intervalTicks":1,"switchTicks":2},{"damage":1,"magazine":8,"pellets":9,"spread":0.006,"switchTicks":2}])"));
    map.member("players").itemsMut().resize(2);map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto sim=simulation(rules,map);
    auto step=[&](uint32_t buttons){actor::Input i;i.buttons=buttons;i.sequence=sim->tick()+1;i.viewTick=sim->tick();auto c=actor::command(1,i);c.tick=sim->tick();sim->inject(c);sim->step();};
    for(int i=0;i<5;++i)step(actor::Fire);
    CHECK(character(*sim,1).shots==1);CHECK(character(*sim,2).health==90);
    step(0);step(actor::Fire);CHECK(character(*sim,1).ammo==1);
    step(actor::Reload);step(0);step(0);step(0);CHECK(character(*sim,1).ammo==3);CHECK(character(*sim,1).reserve==118);
    step(actor::selectWeapon(1)|actor::Fire);CHECK(character(*sim,1).shots==2);
    step(actor::Fire);step(actor::Fire);step(actor::Fire);CHECK(character(*sim,1).shots==4);
    step(actor::selectWeapon(0));CHECK(character(*sim,1).ammo==3);
    step(actor::selectWeapon(1));CHECK(character(*sim,1).ammo==6);
    auto before=character(*sim,1).weaponIndex;step(actor::selectWeapon(3));CHECK(character(*sim,1).weaponIndex==before);
    auto bytes=sim->serialize();auto restored=simulation(rules,map);CHECK(restored->deserialize(bytes));CHECK(restored->checksum()==sim->checksum());
    auto invalid=rules;invalid.member("actor").set("weapons",Json::parse("[]"));bool rejected=false;try{actor::readConfig(invalid);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    // Shotgun consumes one shell per nine independently occluded rays.
    step(actor::selectWeapon(2));step(0);step(actor::Fire);CHECK(character(*sim,1).ammo==7);
}
#include "systems.inc"
#include "projectiles.inc"
#include "projectile_spawn.inc"
#include "melee.inc"
void network(Json rules,Json map) {
    rules.member("actor").set("weapons",Json::parse(R"([{"magazine":12},{"magazine":30}])"));
    rules.member("actor").set("mode",Json::string("deathmatch"));
    rules.member("actor").set("interestRadius",Json::number(int64_t(50)));
    net::authority::Server server(rules,map);auto pair=net::makeLoopbackPair({30,8,42});
    CHECK(server.attach(1,std::move(pair.first)));net::authority::Client client(rules,map,std::move(pair.second));
    for(uint64_t now=0;now<6000;now+=5) {
        client.poll(now);server.poll(now);
        if(now%20==0){actor::Input i;i.right=now<2000?1000:0;i.buttons=actor::selectWeapon(1);client.submit(i);server.step();}
    }
    CHECK(client.ready());CHECK(client.predicted().weaponIndex==1);CHECK(client.predicted().storedAmmo[0]==12);CHECK(client.snapshot().ack>100);CHECK(client.predicted().pos.x< -4_fx);
    CHECK(server.rejected()==0);CHECK(client.pending()<20);
    auto packet=net::authority::encodeSnapshot(client.snapshot());net::authority::Snapshot decoded;
    CHECK(net::authority::decodeSnapshot(packet,decoded));CHECK(decoded.tick==client.snapshot().tick);
    for(size_t n=0;n<packet.size();++n){auto corrupt=packet;corrupt.resize(n);CHECK(!net::authority::decodeSnapshot(corrupt,decoded));}
    packet.push_back(0);CHECK(!net::authority::decodeSnapshot(packet,decoded));
    auto crowded=client.snapshot();crowded.characters.clear();
    for(int slot=1;slot<=200;++slot){auto actor=client.predicted();actor.slot=uint8_t(slot);crowded.characters.push_back(actor);}
    packet=net::authority::encodeSnapshot(crowded);CHECK(packet.size()<=131072);CHECK(net::authority::decodeSnapshot(packet,decoded));
    // A remote entity outside the interest radius is absent from the entire replacement snapshot.
    character(server.simulation(),2).pos.x=100_fx;server.broadcast();
    for(uint64_t now=6000;now<6200;now+=5){client.poll(now);server.poll(now);}
    server.step();server.broadcast();
    for(uint64_t now=6200;now<6400;now+=5){client.poll(now);server.poll(now);}
    for(const auto& a:client.snapshot().characters)CHECK(a.slot!=2);
}
}
// Real authority server lifecycle: disk creation, WAL replay, checkpoint
// rotation, content mismatch refusal and fail-closed publication.
static void durableAuthorityRestart(){
#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    namespace fs=std::filesystem;
    struct Temp {
        fs::path dir;
        ~Temp(){std::error_code ec;if(!dir.empty())fs::remove_all(dir,ec);}
    } temp;
    for(unsigned i=0;i<100;++i){
        auto p=fs::temp_directory_path()/("aiverse-authority-world-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));
        std::error_code ec;
        if(fs::create_directory(p,ec)){temp.dir=std::move(p);break;}
    }
    CHECK(!temp.dir.empty());
    auto rules=Json::parse(R"({"actor":{"tickRate":60,"maxPlayers":2,"mode":"deathmatch"},"regionOwnership":{}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[4,2,4],[8,2,4]],"colliders":[]})");
    std::array<uint8_t,16> uuid{};uuid[0]=17;
    using Server=net::authority::Server;
    actor::CharacterSnapshot expected;
    uint64_t rng=0;
    {
        Server server(rules,map,1);
        CHECK(server.enablePersistence(temp.dir,uuid,false));
        for(unsigned i=0;i<3;++i)CHECK(server.step());
        CHECK(server.checkpointWorld());
        actor::Input input;input.sequence=1;input.forward=1000;input.viewTick=3;
        auto cmd=actor::command(1,input);cmd.tick=3;
        server.simulation().inject(cmd);
        CHECK(server.step());
        CHECK(server.step());
        expected=server.module().snapshot(server.simulation(),1);
        rng=server.simulation().rng().state();
        CHECK(server.simulation().tick()==5);
        Server duplicate(rules,map,1);
        CHECK(!duplicate.enablePersistence(temp.dir,uuid,true));
    }
    {
        Server wrong(rules,map,2);
        CHECK(!wrong.enablePersistence(temp.dir,uuid,true));
    }
    {
        Server restored(rules,map,1);
        CHECK(restored.enablePersistence(temp.dir,uuid,true));
        CHECK(restored.simulation().tick()==5);
        CHECK(restored.simulation().rng().state()==rng);
        CHECK(restored.module().snapshot(restored.simulation(),1)==expected);
        CHECK(restored.step());
        CHECK(restored.simulation().tick()==6);
        struct UnsupportedResource{int value=7;};
        restored.simulation().world().setResource<UnsupportedResource>();
        const auto packets=restored.snapshotStats().packets;
        CHECK(!restored.step()&&restored.persistenceFailed());
        restored.broadcast();
        CHECK(restored.snapshotStats().packets==packets);
    }
    Server final(rules,map,1);
    CHECK(final.enablePersistence(temp.dir,uuid,true));
    CHECK(final.simulation().tick()==6);
#endif
}

#include "royale.inc"
#include "gear.inc"
#include "vehicles.inc"
#include "buildings.inc"
#include "locomotion.inc"
int main(int argc,char** argv) {
    try {std::string root=argc>1?argv[1]:"examples/fps/arena";auto rules=Json::parseFile(root+"/rules.json"),map=Json::parseFile(root+"/map.json");audioBank(root+"/../../../engine/assets/fps_audio.json");spatial();cameraControls();locomotionModes();latestChannel();chunks();parallelMotorParityFixture(rules,map);cowSnapshotsFps(rules,map);incrementalRollbackFps(rules,map);reloadTimeline(rules,map);damageScoring(rules,map);simultaneousHitscanRollback(rules,map);dynamicObjectEntities(rules,map);deterministic(rules,map);combat(rules,map);weapons(rules,map);systems();projectilePrograms();projectileProgramStage();projectileSpawns();projectileRoundReset();ordinaryProjectiles();projectileBranches();melee();royale();gear();typedVehicles(root);raceMode(root);buildings();rotatedColliders();houseBots();houseNavigation();upstairsBot();network(rules,map);durableAuthorityRestart();auto br=root+"/../battle-royale";stress(Json::parseFile(br+"/rules.json"),Json::parseFile(br+"/map.json"));std::puts("FPS spatial, motor, combat, save/rollback and authority tests passed");}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
