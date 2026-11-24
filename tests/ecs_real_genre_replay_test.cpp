// Real GameCore + actor::ActorHost integration gate, not only mock genre hosts.
// The same deterministic engine snapshot/save/replay contract must survive
// actual genre adapters and gameplay commands (FPS state lives on ECS only).
#include "engine/data/JsonCompose.h"
#include "engine/sim/Simulation.h"
#include "engine/game/GameCore.h"
#include "engine/actor/ActorModule.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
enum class Genre { Rts, Fps };
void require(bool ok,const std::string& reason) {
    if(!ok)throw std::runtime_error(reason);
}

std::unique_ptr<Simulation> make(Genre genre,bool cow) {
    const std::string root=AIVERSE_SOURCE_DIR;
    Json rules,map;
    std::unique_ptr<GameModule> module;
    uint32_t tickRate=20;
    if(genre==Genre::Rts) {
        const std::string base=std::string(AIVERSE_TEMPLATES_DIR)+"/mushrooms/";
        rules=readComposedJson(base+"rules.json",fileReader());
        map=Json::parseFile(base+"maps/duel.json");
        module=std::make_unique<game::GameCore>();
    } else {
        const std::string base=root+"/examples/fps/arena/";
        rules=Json::parseFile(base+"rules.json");
        map=Json::parseFile(base+"map.json");
        rules.member("actor").set("mode",Json::string("deathmatch"));
        tickRate=actor::readConfig(rules).tickRate;
        module=std::make_unique<game::GameCore>();
    }
    auto sim=std::make_unique<Simulation>(
        SimConfig{91,tickRate,0,true},std::move(module));
    sim->setup(rules,map);
    if(cow)sim->world().enableCowSnapshots(true);
    return sim;
}

struct RtsSubjects {
    Entity leftHome=kNullEntity,rightHome=kNullEntity;
    Entity leftTarget=kNullEntity,rightTarget=kNullEntity;
};
RtsSubjects findRtsSubjects(const Simulation& simulation) {
    const World& world=const_cast<Simulation&>(simulation).world();
    RtsSubjects found;
    world.eachExistingConst<game::Garrison>(
        [&](Entity entity,const game::Garrison&) {
            if(!world.alive(entity))return;
            const auto* owner=world.tryGetConst<Owner>(entity);
            const auto* transform=world.tryGetConst<Transform>(entity);
            if(!owner || !transform)return;
            if(owner->player==1 && !found.leftHome.valid())
                found.leftHome=entity;
            if(owner->player==2 && !found.rightHome.valid())
                found.rightHome=entity;
            if(owner->player==0 && transform->pos.x<11_fx &&
               !found.leftTarget.valid())
                found.leftTarget=entity;
            if(owner->player==0 && transform->pos.x>30_fx &&
               !found.rightTarget.valid())
                found.rightTarget=entity;
        });
    require(found.leftHome.valid() && found.rightHome.valid() &&
            found.leftTarget.valid() && found.rightTarget.valid(),
            "RTS mushrooms fixture missing garrisons or targets");
    return found;
}

std::vector<Command> commands(Genre genre,uint32_t tick,
                              const RtsSubjects& subjects) {
    std::vector<Command> batch;
    if(genre==Genre::Rts) {
        if(tick==3) {
            for(uint8_t player:{uint8_t(1),uint8_t(2)}) {
                Command cmd;
                cmd.tick=tick;cmd.player=player;cmd.seq=1;
                cmd.type=game::kCmdSend;
                cmd.units={player==1?subjects.leftHome:subjects.rightHome};
                cmd.target=player==1?subjects.leftTarget:subjects.rightTarget;
                cmd.param=50;
                batch.push_back(std::move(cmd));
            }
        }
    } else {
        for(uint8_t player:{uint8_t(1),uint8_t(6)}) {
            actor::Input input;
            input.sequence=tick+1;
            input.viewTick=tick;
            input.forward=(tick%9<5)?400:0;
            input.right=(tick%7<3)?150:0;
            input.yaw=player==1?Fixed{}:Fixed::fromRatio(1,2);
            if(tick%11==0)input.buttons=actor::Fire;
            Command cmd=actor::command(player,input);
            cmd.tick=tick;
            batch.push_back(std::move(cmd));
        }
    }
    return batch;
}
void advance(Simulation& sim,Genre genre,uint32_t tick,
             const RtsSubjects& subjects,bool reverseArrival) {
    auto input=commands(genre,tick,subjects);
    if(reverseArrival) {
        for(auto it=input.rbegin();it!=input.rend();++it)sim.inject(*it);
    } else {
        for(auto& cmd:input)sim.inject(std::move(cmd));
    }
    const auto before=sim.tick();
    require(before==tick,"test simulated unexpected tick");
    sim.step();
    require(sim.tick()==before+1,"real genre simulation did not advance");
}

uint64_t run(Genre genre,bool cow) {
    constexpr uint32_t checkpoint=20,totalTicks=46;
    auto a=make(genre,cow);
    auto b=make(genre,cow);
    const RtsSubjects subjects=genre==Genre::Rts?findRtsSubjects(*a):RtsSubjects{};
    require(a->checksum()==b->checksum(),"real genre initial peer checksum mismatch");
    for(uint32_t t=0;t<checkpoint;++t) {
        advance(*a,genre,t,subjects,false);
        advance(*b,genre,t,subjects,true);
        require(a->checksum()==b->checksum(),"peer divergence before save at tick "+std::to_string(t));
    }

    SimSnapshot snapshot;
    a->saveState(snapshot);
    const uint64_t savedHash=a->checksum();
    const size_t sharedResources=a->world().sharedResourceCount();
    if(cow)require(sharedResources>=1,"COW snapshot shared no resources");
    require(a->checksum()==savedHash &&
            (!cow || a->world().sharedResourceCount()==sharedResources),
            "checksum detached COW resources");
    const std::string saved=a->serialize();
    require(saved.size()>8 && uint8_t(saved[4])==9,
            "real genre without registered native types must retain v9 save");
    require(a->checksum()==savedHash,"serialization mutated live simulation");
    auto restored=make(genre,cow);
    std::string error;
    require(restored->deserialize(saved,&error),
            "real genre deserialize rejected v9 state: "+error);
    require(restored->checksum()==savedHash,"restored disk checksum differs");
    require(restored->serialize()==saved,"disk round-trip changed canonical bytes");

    // Malformed disk input may not damage a successfully restored simulation.
    std::string trailing=saved;
    trailing.push_back(char(0x55));
    require(!restored->deserialize(trailing,&error),"accepted save with trailing byte");
    require(restored->checksum()==savedHash && restored->serialize()==saved,
            "failed disk load mutated live gameplay");

    std::vector<uint64_t> history;
    history.reserve(totalTicks-checkpoint);
    for(uint32_t t=checkpoint;t<totalTicks;++t) {
        advance(*a,genre,t,subjects,false);
        advance(*b,genre,t,subjects,true);
        advance(*restored,genre,t,subjects,true);
        const uint64_t hash=a->checksum();
        require(b->checksum()==hash,"peer divergence after save at tick "+std::to_string(t));
        require(restored->checksum()==hash,
                "disk-load replay divergence at tick "+std::to_string(t));
        history.push_back(hash);
    }
    const uint64_t finalHash=a->checksum();
    a->loadState(snapshot);
    require(a->checksum()==savedHash,"rollback failed to restore checkpoint");
    for(uint32_t t=checkpoint;t<totalTicks;++t) {
        advance(*a,genre,t,subjects,false);
        require(a->checksum()==history[t-checkpoint],
                "snapshot replay diverged at tick "+std::to_string(t));
    }
    require(a->checksum()==finalHash,"real genre rollback final hash mismatch");
    return finalHash;
}
} // namespace

int main() {
    try {
        // Compare deep-copy and COW for each real module on the same command timeline.
        const uint64_t rts=run(Genre::Rts,false);
        require(run(Genre::Rts,true)==rts,"RTS COW changed replay");
        const uint64_t fpsHash=run(Genre::Fps,false);
        require(run(Genre::Fps,true)==fpsHash,"FPS COW changed replay");
        std::cout<<"PASS real RTS/FPS commands, v9 disk save, peer reorder, rollback and COW\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL real genre save/replay: "<<e.what()<<'\n';
        return 1;
    }
}
