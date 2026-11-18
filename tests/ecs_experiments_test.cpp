// c++ -std=c++20 -O3 -pthread -I. tests/ecs_experiments_test.cpp -o /tmp/aiverse-ecs-experiments-test
// /tmp/aiverse-ecs-experiments-test
#include "engine/experiments/EcsExperiments.h"
#include "engine/experiments/EcsDependencyPlan.h"
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <vector>
// Assertions must remain active in Release/NDEBUG CI builds. Plain assert()
// disappears under -DNDEBUG, silently skips test expressions and turns
// variables/helpers used only by checks into -Werror unused warnings.
#define CHECK(...) do { if (!(__VA_ARGS__)) { \
    std::cerr << "ECS regression failed at " << __FILE__ << ":" << __LINE__ \
              << ": " << #__VA_ARGS__ << "\n"; std::abort(); } } while(false)
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position { int64_t x=0; };
struct Velocity { int64_t x=1; };
static uint64_t result(World& world) {
    uint64_t hash=1469598103934665603ull;
    auto& p=world.pool<Position>();
    for(size_t i=0;i<p.size();++i) {
        hash=(hash^p.entityAt(i).packed())*1099511628211ull;
        hash=(hash^uint64_t(p.dataAt(i).x))*1099511628211ull;
    }
    return hash;
}
int main() {
    constexpr uint32_t count=100000;
    World initial;
    for(uint32_t i=0;i<count;++i) {
        auto e=initial.create();
        initial.add<Position>(e,Position{int64_t(i)});
        if(i%4==0) initial.add<Velocity>(e,Velocity{});
    }
    World sequential=initial,parallel=initial;
    auto q=PreparedAll<Position,Velocity>{};
    q.each(sequential,[](Entity,Position& p,Velocity& v){p.x+=v.x;});
    {
        PersistentWorkers workers(3);
        // Independent per-entity writes only; filters inspect immutable pool.
        eachParallel<Position>(parallel,workers,[&](Entity e,Position& p){
            if(auto* velocity=parallel.tryGet<Velocity>(e)) p.x+=velocity->x;
        });
        CHECK(result(sequential)==result(parallel));
        for(int iter=0;iter<5;++iter) {
            q.each(sequential,[](Entity,Position& p,Velocity& v){p.x+=v.x;});
            eachParallel<Position>(parallel,workers,[&](Entity e,Position& p){
                if(auto* velocity=parallel.tryGet<Velocity>(e)) p.x+=velocity->x;
            });
            CHECK(result(sequential)==result(parallel));
        }
    }
    // Deferred structural operations have a well-defined lane order.
    {
        DeterministicCommands buffer(2);
        Entity e=parallel.create();
        buffer.enqueue(1,[e](World& w){w.add<Velocity>(e,Velocity{7});});
        buffer.enqueue(0,[e](World& w){w.add<Position>(e,Position{9});});
        buffer.flush(parallel);
        CHECK(parallel.has<Position>(e)&&parallel.has<Velocity>(e));
        CHECK(parallel.get<Position>(e).x==9);
        CHECK(parallel.get<Velocity>(e).x==7);
        buffer.enqueue(0,[e](World& w){w.destroyLater(e);});
        buffer.flush(parallel);
        CHECK(!parallel.alive(e));
        auto reused=parallel.create();
        CHECK(reused.index==e.index && reused.gen!=e.gen);
    }
    // Enqueue during a command callback must not invalidate the draining vector;
    // the newly enqueued command is intentionally deferred to the next flush.
    {
        World w;
        DeterministicCommands commands(2);
        int count=0;
        commands.enqueue(0,[&](World&) {
            ++count;
            commands.enqueue(0,[&](World&){count+=10;});
        });
        commands.enqueue(1,[&](World&){count+=2;});
        commands.flush(w);
        CHECK(count==3);
        commands.flush(w);
        CHECK(count==13);
        commands.enqueue(0,[&](World&){count+=100;});
        commands.discard();
        commands.flush(w);
        CHECK(count==13);
    }
    // Recursion detection must also hold without worker threads.
    {
        PersistentWorkers sequential{0};
        bool rejected=false;
        sequential.parallelFor(4,[&](size_t,size_t) {
            try {sequential.parallelFor(1,[](size_t,size_t){});}
            catch(const std::logic_error&) {rejected=true;}
        });
        CHECK(rejected);
        // The fallback pool is reusable after a rejected nested call.
        int completed=0;
        sequential.parallelFor(1,[&](size_t,size_t){++completed;});
        CHECK(completed==1);
    }
    // Each deterministic lane has one producer until flush/discard.
    // A different thread must be rejected rather than racing on vector storage.
    {
        World w;
        DeterministicCommands commands(2);
        int sequence=0;
        commands.enqueue(0,[&](World&){sequence=sequence*10+1;});
        std::atomic<bool> rejected{false};
        std::thread competing([&] {
            try {
                commands.enqueue(0,[&](World&){sequence=999;});
            } catch(const std::logic_error&) {rejected.store(true);}
        });
        competing.join();
        CHECK(rejected.load());
        std::thread independent([&] {
            commands.enqueue(1,[&](World&){sequence=sequence*10+2;});
        });
        independent.join();
        commands.flush(w);
        CHECK(sequence==12);
        // Ownership is reset after draining, another thread can own that lane.
        std::thread next([&] {
            commands.enqueue(0,[&](World&){sequence=sequence*10+3;});
        });
        next.join();
        commands.flush(w);
        CHECK(sequence==123);
    }
    // Dirty snapshot applies only marked pages; unmarked data is preserved.
    {
        std::vector<Position> before(1024),updated(1024);
        for(size_t i=0;i<before.size();++i) before[i].x=int64_t(i);
        updated=before;
        updated[3].x=100;
        updated[800].x=200;
        DirtyPageSnapshot<Position,128> delta;
        delta.begin(updated.size());
        delta.mark(3);delta.mark(800);
        delta.capture(updated);
        CHECK(delta.pageCount()==2);
        CHECK(delta.bytes()==256*sizeof(Position));
        delta.apply(before);
        for(size_t i=0;i<before.size();++i) CHECK(before[i].x==updated[i].x);
    }
    // Adaptive threshold uses caller for small jobs; parallel range assignment stays deterministic.
    {
        PersistentWorkers workers(3);
        std::vector<Position> first(500),second(500);
        World a=initial,b=initial;
        eachAdaptive<Position>(a,workers,ExecutionMode::Auto,{200000,2048},
            [](Entity,Position& p){p.x+=2;});
        eachAdaptive<Position>(b,workers,ExecutionMode::Sequential,{},
            [](Entity,Position& p){p.x+=2;});
        CHECK(result(a)==result(b));
        eachAdaptive<Position>(a,workers,ExecutionMode::Auto,{1024,64},
            [](Entity,Position& p){p.x+=3;});
        eachAdaptive<Position>(b,workers,ExecutionMode::Sequential,{},
            [](Entity,Position& p){p.x+=3;});
        CHECK(result(a)==result(b));

        // Exceptions from workers AND caller must propagate; the worker pool remains reusable.
        bool caught=false;
        try {
            workers.parallelFor(10000,[](size_t begin,size_t end){
                if(begin<end)throw std::runtime_error("worker or caller failed");
            });
        } catch(const std::runtime_error&) {caught=true;}
        CHECK(caught);
        std::atomic<size_t> processed{0};
        workers.parallelFor(10000,[&](size_t begin,size_t end){processed.fetch_add(end-begin);});
        CHECK(processed==10000);
        // Nested use of the same pool must fail fast, not deadlock.
        std::atomic<size_t> nestedRejected{0};
        workers.parallelFor(512,[&](size_t begin,size_t end) {
            if(begin==end)return;
            try {
                workers.parallelFor(8,[](size_t,size_t){});
            } catch(const std::logic_error&) {
                nestedRejected.fetch_add(1);
            }
        });
        CHECK(nestedRejected==4);

    }
    {
        DependencyPlan plan;
        plan.add({"move",{}, {1}, {}, {}, false,false});
        plan.add({"independent",{}, {2}, {}, {}, false,false});
        plan.add({"read_positions",{1}, {}, {}, {}, false,false});
        plan.add({"write_positions",{}, {1}, {}, {}, false,false});
        plan.add({"rng",{}, {}, {}, {7}, false,false});
        plan.add({"rng_read",{}, {}, {7}, {}, false,false});
        plan.add({"spawn",{}, {}, {}, {}, true,false});
        auto waves=plan.waves();
        CHECK(waves.size()>=3);
        CHECK(waves[0].size()>=2);
        auto indexOf=[&](size_t i) {
            for(size_t w=0;w<waves.size();++w)
                for(size_t j:waves[w])if(j==i)return w;
            return waves.size();
        };
        CHECK(indexOf(2)>indexOf(0));
        CHECK(indexOf(3)>indexOf(2));
        CHECK(indexOf(5)>indexOf(4));
        CHECK(indexOf(6)>indexOf(5));
    }
    std::cout<<"PASS prepared, adaptive jobs, exceptions, dependency waves, deterministic commands, dirty pages\n";
}
