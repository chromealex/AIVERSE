// c++ -std=c++20 -O2 -pthread -I. tests/ecs_guarded_parallel_test.cpp -o /tmp/aiverse-ecs-guarded-parallel
// /tmp/aiverse-ecs-guarded-parallel
#include "engine/experiments/EcsGuardedWorld.h"
#include "tests/AlwaysCheck.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position {int64_t x=0;};
struct Velocity {int64_t x=0;};
struct Audit {int64_t x=0;};
struct Shared {int64_t x=0;};
static uint64_t checksum(World& world) {
    uint64_t h=1469598103934665603ull;
    world.eachExisting<Position>([&](Entity e,Position& p) {
        h=(h^e.packed())*1099511628211ull;
        h=(h^uint64_t(p.x))*1099511628211ull;
    });
    return h;
}
static uint64_t run(unsigned workers,unsigned density) {
    World world;
    for(uint32_t i=0;i<100000;++i) {
        auto e=world.create();
        world.add<Position>(e,Position{int64_t(i)});
        if(i%100<density)world.add<Velocity>(e,Velocity{int64_t(i%7)});
    }
    GuardedWorld view{world};
    PersistentWorkers pool{workers};
    SystemAccess access{"checked movement",{componentId<Velocity>()},
        {componentId<Position>()},{},{},false,false};
    for(int step=0;step<8;++step) {
        AccessScope scope(access);
        view.eachAdaptiveWrite<Position>(pool,ExecutionMode::Auto,
            ParallelPolicy{8192,2048},
            [&](Entity e,Position& p) {
                if(const auto* v=view.read<Velocity>(e))p.x+=v->x;
            });
    }
    return checksum(world);
}
static void testForeignWriteRejection() {
    World world;
    auto a=world.create(),b=world.create();
    world.add<Position>(a);
    world.add<Position>(b);
    GuardedWorld view{world};
    PersistentWorkers workers{3};
    SystemAccess access{"bad",{},{componentId<Position>()},{},{},false,false};
    bool caught=false;
    try {
        AccessScope scope(access);
        view.eachAdaptiveWrite<Position>(workers,ExecutionMode::Parallel,{},
            [&](Entity e,Position&) {
                // Another entity is writable only by its owning lane/iteration.
                auto other=(e==a)?b:a;
                (void)view.write<Position>(other);
            });
    }catch(const AccessViolation&) {caught=true;}
    AIVERSE_TEST_CHECK(caught);
    // No writer failure should leave PersistentWorkers unusable.
    {
        AccessScope scope(access);
        view.eachAdaptiveWrite<Position>(workers,ExecutionMode::Parallel,{},
            [](Entity,Position& position){position.x+=1;});
    }
    AIVERSE_TEST_CHECK(world.get<Position>(a).x==1 && world.get<Position>(b).x==1);
}
static void testNoSharedMutationWithinJobs() {
    World world;
    for(int i=0;i<64;++i)world.add<Position>(world.create());
    world.setResource<Shared>(Shared{});
    GuardedWorld view{world};
    PersistentWorkers workers{3};
    SystemAccess access{"restricted",{}, {componentId<Position>()},
        {},{componentId<Shared>()},true,false};
    std::atomic<size_t> rejected{0};
    {
        AccessScope scope(access);
        view.eachAdaptiveWrite<Position>(workers,ExecutionMode::Parallel,{},
            [&](Entity,Position&) {
                try{(void)view.create();}
                catch(const AccessViolation&){++rejected;}
                try{(void)view.writeResource<Shared>();}
                catch(const AccessViolation&){++rejected;}
                try{view.eachWrite<Position>([](Entity,Position&){});}
                catch(const AccessViolation&){++rejected;}
                try {
                    view.eachAdaptiveWrite<Position>(workers,ExecutionMode::Parallel,{},
                        [](Entity,Position&){});
                } catch(const AccessViolation&){++rejected;}
            });
    }
    AIVERSE_TEST_CHECK(rejected==64*4);
    AIVERSE_TEST_CHECK(world.countExisting<Position>()==64);
}

int main() {
    for(unsigned density:{1u,25u,100u}) {
        const auto expected=run(0,density);
        AIVERSE_TEST_CHECK(run(1,density)==expected);
        AIVERSE_TEST_CHECK(run(3,density)==expected);
        AIVERSE_TEST_CHECK(run(7,density)==expected);
    }
    testForeignWriteRejection();
    testNoSharedMutationWithinJobs();
    std::cout<<"PASS checked adaptive per-entity jobs: worker scopes, checksum, foreign-write rejection\n";
}
