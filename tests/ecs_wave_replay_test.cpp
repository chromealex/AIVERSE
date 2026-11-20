// Experimental World replay parity test; not a complete Simulation lockstep test.
// c++ -std=c++20 -O2 -pthread -I. tests/ecs_wave_replay_test.cpp -o /tmp/aiverse-ecs-replay
// /tmp/aiverse-ecs-replay
#include "engine/experiments/EcsGuardedWorld.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <vector>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position {int64_t x=0;};
struct Velocity {int64_t x=0;};

static uint64_t fingerprint(World& world) {
    uint64_t h=1469598103934665603ull;
    auto& positions=world.pool<Position>();
    for(size_t i=0;i<positions.size();++i) {
        const auto e=positions.entityAt(i);
        for(uint64_t v:{e.packed(),uint64_t(positions.dataAt(i).x)})h=(h^v)*1099511628211ull;
        if(auto* velocity=world.tryGet<Velocity>(e))
            h=(h^uint64_t(velocity->x))*1099511628211ull;
    }
    auto state=world.entityState();
    for(auto g:state.gens)h=(h^g)*1099511628211ull;
    for(auto id:state.free)h=(h^id)*1099511628211ull;
    return h;
}
static World makeSeed() {
    World world;
    for(uint32_t i=0;i<2000;++i) {
        auto e=world.create();
        world.add<Position>(e,Position{int64_t(i)});
        world.add<Velocity>(e,Velocity{int64_t(i%3+1)});
    }
    return world;
}
static uint64_t replay(World world,unsigned workerCount) {
    PersistentWorkers workers{workerCount};
    WaveExecutor executor{workers};
    GuardedWorld guarded{world};
    const auto p=componentId<Position>();
    const auto v=componentId<Velocity>();
    for(int tick=0;tick<10;++tick) {
        DependencyPlan plan;
        // These two systems write different component types and are independent.
        plan.add({"movement",{v},{p},{},{},false,false});
        plan.add({"accelerate",{}, {v},{},{},false,false});
        plan.add({"structural",{}, {p,v},{},{},true,false});
        DeterministicCommands commands(2);
        std::vector<WaveExecutor::SystemFn> jobs {
            [&] {
                guarded.eachWrite<Position>([&](Entity e,Position& position) {
                    auto* velocity=guarded.read<Velocity>(e);
                    if(velocity)position.x+=velocity->x;
                });
            },
            [&] {
                guarded.eachWrite<Velocity>([](Entity,Velocity& velocity) {
                    velocity.x+=1;
                });
            },
            [&] {
                // This system is serialized behind the component updates.
                // Record the operation; do not mutate while iterating.
                Entity first=kNullEntity;
                guarded.eachRead<Position>([&](Entity e,const Position&) {
                    if(!first.valid())first=e;
                });
                if(first.valid() && tick%2==0)
                    commands.enqueue(0,[first](World& w){w.destroyLater(first);});
                commands.enqueue(1,[tick](World& w) {
                    auto e=w.create();
                    w.add<Position>(e,Position{10000+tick});
                    w.add<Velocity>(e,Velocity{3});
                });
            }
        };
        runChecked(executor,plan,jobs,[&](size_t){commands.flush(world);});
    }
    return fingerprint(world);
}
int main() {
    auto original=makeSeed();
    const auto originalChecksum=fingerprint(original);
    auto snapshot=original;
    const uint64_t sequential=replay(snapshot,0);
    AIVERSE_TEST_CHECK(fingerprint(original)==originalChecksum);
    AIVERSE_TEST_CHECK(sequential==replay(original,3));
    AIVERSE_TEST_CHECK(sequential==replay(original,7));
    AIVERSE_TEST_CHECK(sequential==replay(snapshot,3));
    std::cout<<"PASS deterministic world snapshot replay across 0/3/7 workers\n";
}
