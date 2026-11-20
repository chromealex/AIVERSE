// c++ -std=c++20 -O2 -pthread -I. tests/ecs_cow_snapshot_test.cpp -o /tmp/ecs-cow-test
#include "engine/core/ECS.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <string>
using namespace aiverse;
struct Position {int64_t x=0;};
struct Health {int64_t hp=100;};
struct Resource {int64_t gold=1;};
struct Named {std::string name;};
static void check(World& w,Entity e,int64_t x,int64_t hp,int64_t gold) {
    AIVERSE_TEST_CHECK(w.alive(e));
    AIVERSE_TEST_CHECK(w.get<Position>(e).x==x);
    AIVERSE_TEST_CHECK(w.get<Health>(e).hp==hp);
    AIVERSE_TEST_CHECK(w.resource<Resource>()->gold==gold);
}
int main() {
    World w;
    w.enableCowSnapshots(true);
    Entity a=w.create(),b=w.create();
    w.add<Position>(a,Position{10});w.add<Position>(b,Position{20});
    w.add<Health>(a,Health{100});w.add<Health>(b,Health{200});
    w.setResource<Resource>(Resource{7});
    World s1=w;
    AIVERSE_TEST_CHECK(w.sharedPoolCount()==2);
    AIVERSE_TEST_CHECK(w.sharedResourceCount()==1);
    w.get<Position>(a).x=30;
    AIVERSE_TEST_CHECK(w.sharedPoolCount()==1);
    AIVERSE_TEST_CHECK(s1.sharedPoolCount()==1);
    w.get<Health>(b).hp=300;
    w.resource<Resource>()->gold=8;
    check(s1,a,10,100,7);
    check(w,a,30,100,8);
    World s2=w;
    w.each<Position>([](Entity,Position& p){p.x+=1;});
    w.destroy(a);
    Entity recycled=w.create();
    AIVERSE_TEST_CHECK(recycled.index==a.index && recycled.gen==a.gen+1);
    w.add<Position>(recycled,Position{400});
    w.add<Health>(recycled,Health{500});
    AIVERSE_TEST_CHECK(s1.alive(a)&&s2.alive(a));
    check(s2,a,30,100,8);
    AIVERSE_TEST_CHECK(!w.alive(a));
    AIVERSE_TEST_CHECK(s1.get<Position>(b).x==20);
    AIVERSE_TEST_CHECK(s2.get<Health>(b).hp==300);
    w.copyFrom(s1);
    check(w,a,10,100,7);
    w.get<Position>(a).x=42;
    check(s1,a,10,100,7);
    // Re-enable legacy deep-copy mode after detaching shared contents.
    w.enableCowSnapshots(false);
    World legacy=w;
    w.get<Position>(a).x=77;
    AIVERSE_TEST_CHECK(legacy.get<Position>(a).x==42);
    AIVERSE_TEST_CHECK(!w.cowSnapshotsEnabled());
    // Page-level COW: one modified component detaches only its page.
    {
        World large;
        for(uint32_t i=0;i<800;++i) {
            Entity e=large.create();
            large.add<Position>(e,Position{int64_t(i)});
            large.add<Named>(e,Named{std::to_string(i)});
        }
        large.enableCowSnapshots(true);
        World checkpoint=large;
        Entity e10{10,0},e511{511,0},e300{300,0};
        large.get<Position>(e10).x=999;
        AIVERSE_TEST_CHECK(large.sharedCowPageCount()>=3);
        AIVERSE_TEST_CHECK(static_cast<const World&>(checkpoint).tryGetConst<Position>(e10)->x==10);
        large.get<Position>(e511).x=888;
        AIVERSE_TEST_CHECK(static_cast<const World&>(checkpoint).tryGetConst<Position>(e511)->x==511);
        large.destroy(e300);
        AIVERSE_TEST_CHECK(!large.alive(e300) && checkpoint.alive(e300));
        Entity reused=large.create();
        AIVERSE_TEST_CHECK(reused.index==e300.index && reused.gen==e300.gen+1);
        large.add<Position>(reused,Position{123456});
        large.add<Named>(reused,Named{"new"});
        AIVERSE_TEST_CHECK(static_cast<const World&>(checkpoint).tryGetConst<Position>(e300)->x==300);
        AIVERSE_TEST_CHECK(static_cast<const World&>(checkpoint).tryGetConst<Named>(e300)->name=="300");
        AIVERSE_TEST_CHECK(large.get<Named>(reused).name=="new");
        large.copyFrom(checkpoint);
        AIVERSE_TEST_CHECK(large.alive(e300));
        AIVERSE_TEST_CHECK(large.get<Position>(e10).x==10);
        AIVERSE_TEST_CHECK(large.get<Named>(e300).name=="300");
        large.enableCowSnapshots(false);
        AIVERSE_TEST_CHECK(!large.cowSnapshotsEnabled());
        AIVERSE_TEST_CHECK(large.get<Position>(e511).x==511);
    }
    std::cout<<"PASS COW world snapshots, resource detachment, generations, rollback, legacy fallback\n";
}
