// Exercise checked parallel jobs while full COW snapshots share all pools.
#include "engine/experiments/EcsGuardedWorld.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position {int64_t x=0;};
struct Velocity {int64_t v=1;};
struct Audit {int64_t changes=0;};
struct Resource {int64_t value=20;};
static uint64_t hash(const World& world) {
    uint64_t h=1469598103934665603ull;
    world.eachExistingConst<Position>([&](Entity e,const Position& p){
        h=(h^e.packed())*1099511628211ull;
        h=(h^uint64_t(p.x))*1099511628211ull;
        auto* audit=world.tryGetConst<Audit>(e);
        h=(h^uint64_t(audit?audit->changes:0))*1099511628211ull;
    });
    auto* res=world.resourceConst<Resource>();
    return (h^uint64_t(res?res->value:0))*1099511628211ull;
}
static uint64_t exercise(unsigned workers) {
    World world;
    world.enableCowSnapshots(true);
    for(uint32_t i=0;i<10000;++i) {
        Entity e=world.create();
        world.add<Position>(e,Position{int64_t(i)});
        world.add<Audit>(e);
        if(i%3==0)world.add<Velocity>(e,Velocity{int64_t(i%7)});
    }
    world.setResource<Resource>(Resource{20});
    World before=world;
    const auto original=hash(before);
    GuardedWorld view{world};
    PersistentWorkers pool{workers};
    SystemAccess access{"integrated COW motor",{componentId<Velocity>()},
        {componentId<Position>(),componentId<Audit>()},
        {componentId<Resource>()},{},false,false};
    for(int step=0;step<6;++step) {
        AccessScope scope(access);
        view.eachAdaptiveWrite<Position>(pool,ExecutionMode::Auto,{64,16},
            [&](Entity e,Position& p){
                auto* velocity=view.read<Velocity>(e);
                if(velocity)p.x+=velocity->v;
                auto* audit=view.write<Audit>(e);
                AIVERSE_TEST_CHECK(audit);
                ++audit->changes;
                AIVERSE_TEST_CHECK(view.readResource<Resource>()->value==20);
            });
    }
    AIVERSE_TEST_CHECK(hash(before)==original);
    const auto final=hash(world);
    world.copyFrom(before);
    AIVERSE_TEST_CHECK(hash(world)==original);
    return final;
}
int main(){
    const auto expected=exercise(0);
    for(unsigned workers:{1u,3u,7u})AIVERSE_TEST_CHECK(exercise(workers)==expected);
    std::cout<<"PASS COW shared-pool snapshots with guarded parallel read/write and rollback\n";
}
