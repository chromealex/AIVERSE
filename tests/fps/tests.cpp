#include "engine/net/authority/Session.h"
#include "engine/net/Rollback.h"
#include "libraries/include/aiverselib/bots/Persistence.h"
#include <cstdio>
#include <algorithm>
#include <array>
#include "engine/audio/Audio.h"
#include <chrono>
#include "engine/net/LatestChannel.h"
#include "engine/world/ChunkScene.h"
#include "engine/net/Udp.h"
#include "modules/fps/view/FirstPersonCamera.h"
#include <stdexcept>
#include <fstream>
#include "modules/fps/Project.h"
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
void audioBank(const std::string& path) {
    audio::AudioEngine audio;audio::AudioConfig config;config.openDevice=false;config.sampleRate=44100;audio.init(config);
    std::string error;CHECK(audio.loadBank(Json::parseFile(path),[](const std::string&,std::string&){return false;},&error));CHECK(audio.warnings().empty());
    for(const char* sound:{"shot0","shot1","shot2","shot3","step","reload"})CHECK(audio.play(sound)!=audio::kNoVoice);
    float buffer[2048]{};audio.render(buffer,1024);bool audible=false;for(float sample:buffer)audible|=sample!=0;CHECK(audible);audio.shutdown();
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
    const auto& stats=server.snapshotStats();CHECK(stats.packets>1000&&stats.bytes*2<stats.fullBytes&&stats.fullPackets<stats.packets/10);
    std::printf("snapshot deltas: %llu packets, %llu bytes vs %llu full (%llu sent full)\n",(unsigned long long)stats.packets,(unsigned long long)stats.bytes,(unsigned long long)stats.fullBytes,(unsigned long long)stats.fullPackets);
    CHECK(server.simulation().executedCommands().empty());
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::printf("100 clients / 300 ticks / sparse 16 km world: %lld ms (debug/local loopback, not WAN capacity)\n",(long long)elapsed);
}
void parallelMotorParityFixture(Json rules,Json map) {
    auto& settings=rules.member("fps");
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
            fps::Input input;
            input.sequence=t+1;
            input.forward=500;
            input.right=(int(i%3)-1)*200;
            input.buttons=(t==10?fps::Jump:0u) |
                          (t>=30&&t<40?fps::Crouch:0u) |
                          (t>=60?fps::Sprint:0u);
            auto command=fps::command(uint8_t(i+1),input);
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
            fps::Input input;
            input.sequence=t+1;
            input.viewTick=t;
            input.yaw=i<50?Fixed{}:Fixed::fromRatio(1,2);
            input.forward=300;
            if(i%6==0)input.buttons=fps::Fire;
            auto cmd=fps::command(uint8_t(i+1),input);
            cmd.tick=t;
            combatSim->inject(cmd);
        }
        combatSim->step();
        combatHistory.u64(combatSim->checksum());
    }
    std::printf("FPS_COMBAT_PARITY_HASH=%016llx\n",
        (unsigned long long)combatHistory.value());
    auto& motorModule=static_cast<fps::Module&>(sim->module());
    auto& combatModule=static_cast<fps::Module&>(combatSim->module());
    std::printf("FPS_MOTOR_BATCHES=%llu\n",
        (unsigned long long)(motorModule.experimentalMotorBatches()+
                             combatModule.experimentalMotorBatches()));
    std::printf("FPS_MOTOR_COMMITS=%llu\n",
        (unsigned long long)(motorModule.experimentalMotorCommits()+
                             combatModule.experimentalMotorCommits()));


}
void cowSnapshotsFps(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    auto baseline=simulation(rules,map),incremental=simulation(rules,map);
    incremental->world().enableCowSnapshots(true);
    for(uint32_t tick=0;tick<120;++tick) {
        fps::Input input;
        input.sequence=tick+1;
        input.forward=tick<70?700:0;
        input.right=tick>=70?200:0;
        input.buttons=(tick==20?uint32_t(fps::Jump):0u);
        auto cmd=fps::command(1,input);
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
    rules.member("fps").set("mode",Json::string("deathmatch"));
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
            fps::Input input;
            input.forward=600;
            input.sequence=1;
            input.viewTick=2;
            auto delayed=fps::command(1,input);
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
void ecsActorAuthorityParity(Json rules,Json map) {
    // Legacy gameplay versus ECS ActorRuntime canonical state, with the
    // same input commands, fixed-point motor, weapons, save and rollback.
    rules.member("fps").set("mode",Json::string("deathmatch"));
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    CHECK(!static_cast<fps::Module&>(legacy->module()).actorEcsAuthority());
    CHECK(static_cast<fps::Module&>(ecs->module()).actorEcsAuthority());
    auto& module=static_cast<fps::Module&>(ecs->module());
    SimSnapshot restore;
    for(uint32_t tick=0;tick<160;++tick) {
        for(uint32_t slot=1;slot<=2;++slot) {
            fps::Input input;
            input.sequence=tick+1;input.viewTick=tick;
            input.forward=tick<95?500:0;
            input.right=slot==1?250:-250;
            input.buttons=tick==12?uint32_t(fps::Jump):0;
            if(slot==1&&tick>=80&&tick<95)input.buttons|=fps::Fire;
            auto cmd=fps::command(uint8_t(slot),input);cmd.tick=tick;
            legacy->inject(cmd);ecs->inject(cmd);
        }
        legacy->step();ecs->step();
        CHECK(legacy->checksum()==ecs->checksum());
        for(size_t i=0;i<state(*ecs).actors.size();++i) {
            const auto entity=module.actorEntity(*ecs,uint8_t(i+1));
            CHECK(ecs->world().alive(entity));
            const auto* runtime=ecs->world().tryGetConst<fps::ActorRuntime>(entity);
            CHECK(runtime&&runtime->value==state(*ecs).actors[i]);
        }
        if(tick==49)ecs->saveState(restore);
    }
    // The same v9 save must load across the storage-mode boundary.
    const auto legacyBytes=legacy->serialize();
    auto ecsFromLegacy=simulation(ecsRules,map);
    CHECK(ecsFromLegacy->deserialize(legacyBytes));
    CHECK(ecsFromLegacy->checksum()==legacy->checksum());
    auto bytes=ecs->serialize();
    auto reloaded=simulation(ecsRules,map);
    CHECK(reloaded->deserialize(bytes));
    CHECK(reloaded->checksum()==ecs->checksum());
    // Rewind ECS snapshots and replay the same no-input continuation.
    auto checkpoint=ecs->checksum();
    SimSnapshot latest;ecs->saveState(latest);
    for(int i=0;i<8;++i)ecs->step();
    const auto after=ecs->checksum();
    ecs->loadState(latest);CHECK(ecs->checksum()==checkpoint);
    for(int i=0;i<8;++i)ecs->step();
    CHECK(ecs->checksum()==after);
    // Writing directly into authoritative ECS must affect the next tick's
    // working set; State::actors must not overwrite it on hydration.
    auto e=module.actorEntity(*ecs,1);
    auto* runtime=ecs->world().tryGet<fps::ActorRuntime>(e);
    CHECK(runtime);
    runtime->value.pos.x+=5_fx;
    ecs->step();
    CHECK(state(*ecs).actors[0].pos.x==ecs->world().tryGetConst<fps::ActorRuntime>(e)->value.pos.x);
}

void ecsReloadAuthorityParity(Json rules,Json map) {
    // Real FPS tick uses the same stepWeaponReload policy as generic ECS hosts.
    // Check no input/reload ordering, weapon switching or save v9 regression.
    rules.member("fps").set("mode",Json::string("deathmatch"));
    rules.member("fps").set("weapons",Json::parse(
        R"([{"automatic":false,"damage":10,"magazine":3,"reserve":8,"reloadTicks":3,"intervalTicks":1,"switchTicks":1},
            {"automatic":true,"damage":5,"magazine":4,"reserve":8,"reloadTicks":2,"intervalTicks":1,"switchTicks":1}])"));
    map.member("players").itemsMut().resize(2);
    map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);

    std::vector<fps::Input> inputByTick(25);
    for(uint32_t tick=0;tick<inputByTick.size();++tick) {
        auto& input=inputByTick[tick];
        input.sequence=tick+1;input.viewTick=tick;
        if(tick==0 || tick==3 || tick==5 || tick==14)input.buttons=fps::Fire;
        if(tick==2 || tick==12)input.buttons=fps::Reload;
        if(tick==7)input.buttons=fps::selectWeapon(1);
        if(tick==10)input.buttons=fps::selectWeapon(0);
    }
    SimSnapshot legacyBeforeFinish,ecsBeforeFinish;
    auto runTick=[&](uint32_t tick) {
        const auto cmd=fps::command(1,inputByTick[tick]);
        auto old=cmd,newCmd=cmd;
        old.tick=newCmd.tick=tick;
        legacy->inject(old);ecs->inject(newCmd);
        legacy->step();ecs->step();
        CHECK(legacy->checksum()==ecs->checksum());
        CHECK(state(*legacy).actors==state(*ecs).actors);
        const auto& player=state(*ecs).actors[0];
        const auto entity=static_cast<fps::Module&>(ecs->module()).actorEntity(*ecs,1);
        const auto* canonical=ecs->world().tryGetConst<fps::ActorRuntime>(entity);
        CHECK(canonical && canonical->value==player);
        if(tick==2)CHECK(player.ammo==2 && player.reloadUntil==5 && player.reserve==8);
        if(tick==3)CHECK(player.ammo==2 && player.reloadUntil==5);
        if(tick==5)CHECK(player.ammo==2 && player.reloadUntil==0 && player.reserve==7);
        if(tick==7)CHECK(player.weaponIndex==1);
        if(tick==10)CHECK(player.weaponIndex==0);
    };
    for(uint32_t tick=0;tick<inputByTick.size();++tick) {
        runTick(tick);
        if(tick==4) {
            legacy->saveState(legacyBeforeFinish);
            ecs->saveState(ecsBeforeFinish);
        }
    }
    const auto finalChecksum=ecs->checksum();
    const auto oldSave=legacy->serialize(),ecsSave=ecs->serialize();
    auto ecsFromLegacy=simulation(ecsRules,map);
    auto legacyFromEcs=simulation(rules,map);
    CHECK(ecsFromLegacy->deserialize(oldSave));
    CHECK(legacyFromEcs->deserialize(ecsSave));
    CHECK(ecsFromLegacy->checksum()==finalChecksum);
    CHECK(legacyFromEcs->checksum()==finalChecksum);
    legacy->loadState(legacyBeforeFinish);ecs->loadState(ecsBeforeFinish);
    CHECK(legacy->checksum()==ecs->checksum());
    for(uint32_t tick=5;tick<inputByTick.size();++tick)runTick(tick);
    CHECK(ecs->checksum()==finalChecksum);
}

void ecsDamageAuthorityParity(Json rules,Json map) {
    // Direct ECS damage/attacker scoring must match legacy shot resolution
    // tick-by-tick, including death, hit counts, and kill events.
    rules.member("fps").set("mode",Json::string("deathmatch"));
    map.member("players").itemsMut().resize(2);
    map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    for(uint32_t tick=0;tick<30;++tick) {
        fps::Input input;
        input.sequence=tick+1;input.viewTick=tick;input.buttons=fps::Fire;
        auto cmd=fps::command(1,input);cmd.tick=tick;
        legacy->inject(cmd);ecs->inject(cmd);
        legacy->step();ecs->step();
        CHECK(legacy->checksum()==ecs->checksum());
        const auto& left=state(*legacy).actors;
        const auto& right=state(*ecs).actors;
        CHECK(left[0].hits==right[0].hits);
        CHECK(left[0].kills==right[0].kills);
        CHECK(left[1].health==right[1].health);
        for(uint8_t slot=1;slot<=2;++slot) {
            auto* runtime=ecs->world().tryGetConst<fps::ActorRuntime>(
                static_cast<fps::Module&>(ecs->module()).actorEntity(*ecs,slot));
            CHECK(runtime && runtime->value==right[size_t(slot-1)]);
        }
    }
    CHECK(state(*legacy).actors[1].health==0);
    CHECK(state(*legacy).actors[0].kills==1);
    CHECK(state(*ecs).actors[0].hits>0);
}
// Live, simultaneous FPS hitscan is intentionally synchronous: killing a
// target earlier in the actor tick can suppress its later fire. The shared
// intent resolver must preserve the full legacy order, including shotgun
// pellets, historical views, save v9 and replay after a rollback.
void ecsSimultaneousHitscanRollbackParity(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    rules.member("fps").set("weapons",Json::parse(
        R"([{"automatic":true,"damage":8,"magazine":24,"reserve":0,"reloadTicks":4,"intervalTicks":1,"pellets":3,"spread":0,"bulletSpeed":0}])"));
    map.member("players").itemsMut().resize(2);
    map.member("players").at(1).set("team",Json::number(int64_t(2)));
    map.set("spawns",Json::parse("[[10,0.01,-10],[10,0.01,10]]"));
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    std::vector<std::array<fps::Input,2>> inputs(12);
    for(uint32_t tick=0;tick<inputs.size();++tick) {
        for(uint32_t player=0;player<2;++player) {
            auto& in=inputs[tick][player];
            in.sequence=tick+1;
            in.viewTick=tick>1?tick-1:0; // intentionally rewound target poses
            in.buttons=tick<8?fps::Fire:0u;
            in.yaw=player==0?Fixed{}:Fixed::fromRatio(1,2);
        }
    }
    const auto runTick=[&](uint32_t tick) {
        for(uint32_t player=0;player<2;++player) {
            auto cmd=fps::command(uint8_t(player+1),inputs[tick][player]);
            cmd.tick=tick;
            legacy->inject(cmd);
            ecs->inject(cmd);
        }
        legacy->step();
        ecs->step();
        CHECK(legacy->checksum()==ecs->checksum());
        CHECK(state(*legacy).actors==state(*ecs).actors);
        for(uint8_t slot=1;slot<=2;++slot) {
            const auto e=static_cast<fps::Module&>(ecs->module()).actorEntity(*ecs,slot);
            const auto* actor=ecs->world().tryGetConst<fps::ActorRuntime>(e);
            CHECK(actor&&actor->value==state(*ecs).actors[size_t(slot-1)]);
        }
    };
    SimSnapshot legacySnapshot,ecsSnapshot;
    for(uint32_t tick=0;tick<inputs.size();++tick) {
        runTick(tick);
        if(tick==2) {
            legacy->saveState(legacySnapshot);
            ecs->saveState(ecsSnapshot);
        }
    }
    CHECK(state(*ecs).actors[0].shots>0);
    CHECK(state(*ecs).actors[0].hits+state(*ecs).actors[1].hits>0);
    const auto expected=ecs->checksum();
    const auto oldSave=legacy->serialize(),ecsSave=ecs->serialize();
    auto ecsRestored=simulation(ecsRules,map),legacyRestored=simulation(rules,map);
    CHECK(ecsRestored->deserialize(oldSave));
    CHECK(legacyRestored->deserialize(ecsSave));
    CHECK(ecsRestored->checksum()==expected);
    CHECK(legacyRestored->checksum()==expected);
    // A real completed-tick rollback must reproduce the same two-shooter
    // combat after restoring both storage modes from the original snapshot.
    legacy->loadState(legacySnapshot);
    ecs->loadState(ecsSnapshot);
    CHECK(legacy->checksum()==ecs->checksum());
    for(uint32_t tick=3;tick<inputs.size();++tick)runTick(tick);
    CHECK(ecs->checksum()==expected);
}
void ecsDuplicateDynamicIdRejected(Json rules,Json map) {
    rules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto sim=simulation(rules,map);
    fps::Loot loot;loot.id=7001;loot.type=1;loot.count=1;
    auto a=sim->world().create(),b=sim->world().create();
    sim->world().add<fps::LootRuntime>(a,fps::LootRuntime{loot,0});
    sim->world().add<fps::LootRuntime>(b,fps::LootRuntime{loot,1});
    bool rejected=false;
    try {sim->step();}
    catch(const std::logic_error&){rejected=true;}
    CHECK(rejected);
    CHECK(sim->tick()==0);
}
void ecsDynamicAuthorityParity(Json rules,Json map) {
    rules.member("fps").set("mode",Json::string("deathmatch"));
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    fps::Projectile projectile;
    projectile.id=9001;projectile.owner=1;projectile.expires=1000;
    projectile.pos={40_fx,10_fx,40_fx};
    fps::Loot loot;
    loot.id=9002;loot.type=3;loot.count=2;loot.pos={40_fx,0_fx,40_fx};
    fps::Vehicle vehicle;
    vehicle.id=9003;vehicle.health=450;vehicle.pos={50_fx,0_fx,50_fx};
    state(*legacy).projectiles.push_back(projectile);
    state(*legacy).loot.push_back(loot);
    state(*legacy).vehicles.push_back(vehicle);
    auto& world=ecs->world();
    // Native ECS spawn should not require manual State edits or explicit
    // DynamicEntityIndex mutation: the pre-tick bridge discovers components.
    const Entity p=world.create(),l=world.create(),v=world.create();
    world.add<fps::ProjectileRuntime>(p,fps::ProjectileRuntime{projectile,0});
    world.add<fps::LootRuntime>(l,fps::LootRuntime{loot,0});
    world.add<fps::VehicleRuntime>(v,fps::VehicleRuntime{vehicle,0});
    CHECK(legacy->checksum()==ecs->checksum()); // authoritative ECS without a tick
    auto* index=world.resource<fps::DynamicEntityIndex>();
    CHECK(index && index->byKey.empty());

    SimSnapshot snapshot;
    ecs->saveState(snapshot);
    for(int i=0;i<8;++i) {
        legacy->step();ecs->step();
        if(i==0) {
            const auto* indexView=world.resourceConst<fps::DynamicEntityIndex>();
            CHECK(indexView);
            CHECK(indexView->byKey.contains((uint64_t(1)<<32)|projectile.id));
            CHECK(indexView->byKey.contains((uint64_t(2)<<32)|loot.id));
            CHECK(indexView->byKey.contains((uint64_t(3)<<32)|vehicle.id));
            CHECK(indexView->byKey.at((uint64_t(1)<<32)|projectile.id)==p);
            CHECK(indexView->byKey.at((uint64_t(2)<<32)|loot.id)==l);
            CHECK(indexView->byKey.at((uint64_t(3)<<32)|vehicle.id)==v);
        }
        CHECK(legacy->checksum()==ecs->checksum());
        CHECK(state(*ecs).projectiles.size()==state(*legacy).projectiles.size());
        CHECK(state(*ecs).projectiles[0].id==state(*legacy).projectiles[0].id);
        CHECK(state(*ecs).loot.size()==state(*legacy).loot.size());
        CHECK(state(*ecs).vehicles.size()==state(*legacy).vehicles.size());
    }
    auto* authoritativeLoot=world.tryGet<fps::LootRuntime>(l);
    CHECK(authoritativeLoot);
    authoritativeLoot->value.count=9;
    state(*legacy).loot[0].count=9;
    CHECK(ecs->checksum()==legacy->checksum()); // direct ECS edit immediately visible
    ecs->step();legacy->step();
    CHECK(state(*ecs).loot[0].count==9);
    CHECK(ecs->checksum()==legacy->checksum());
    const auto encoded=ecs->serialize();
    auto saved=simulation(ecsRules,map);
    CHECK(saved->deserialize(encoded));
    CHECK(saved->checksum()==ecs->checksum());
    // Structural removal is controlled by ECS membership, not State arrays.
    world.destroy(l);
    state(*legacy).loot.clear();
    ecs->step();legacy->step();
    CHECK(state(*ecs).loot.empty());
    CHECK(ecs->checksum()==legacy->checksum());
    // A snapshot taken before the removal still restores all runtime payloads.
    ecs->loadState(snapshot);
    CHECK(ecs->world().alive(l));
    CHECK(ecs->world().tryGetConst<fps::LootRuntime>(l)!=nullptr);
    CHECK(ecs->world().tryGetConst<fps::ProjectileRuntime>(p)!=nullptr);
    CHECK(ecs->world().tryGetConst<fps::VehicleRuntime>(v)!=nullptr);
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
#include "projectile_spawn.inc"
#include "melee.inc"
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
#include "royale.inc"
#include "gear.inc"
#include "vehicles.inc"
#include "buildings.inc"
int main(int argc,char** argv) {
    try {std::string root=argc>1?argv[1]:"examples/fps/arena";auto rules=Json::parseFile(root+"/rules.json"),map=Json::parseFile(root+"/map.json");audioBank(root+"/../../../engine/assets/fps_audio.json");spatial();cameraControls();latestChannel();chunks();parallelMotorParityFixture(rules,map);cowSnapshotsFps(rules,map);incrementalRollbackFps(rules,map);ecsActorAuthorityParity(rules,map);ecsReloadAuthorityParity(rules,map);ecsDamageAuthorityParity(rules,map);ecsSimultaneousHitscanRollbackParity(rules,map);ecsDuplicateDynamicIdRejected(rules,map);ecsDynamicAuthorityParity(rules,map);deterministic(rules,map);combat(rules,map);weapons(rules,map);systems();projectilePrograms();ecsProjectileProgramStageParity();ecsProjectileSpawnAuthorityParity();ecsProjectileRoundResetParity();ecsOrdinaryProjectileAuthorityParity();ecsProjectileBranchAuthorityParity();melee();royale();gear();typedVehicles(root);raceMode(root);buildings();rotatedColliders();houseBots();houseNavigation();upstairsBot();network(rules,map);auto br=root+"/../battle-royale";stress(Json::parseFile(br+"/rules.json"),Json::parseFile(br+"/map.json"));std::puts("FPS spatial, motor, combat, save/rollback and authority tests passed");}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
