// Deferred checks for persistent workers, barriers and exceptions.
#include <aiverselib/animation/MotionWeaveWorkers.h>
#include <atomic>
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <vector>
using namespace aiverse::lib::animation;
int main(){
    MotionWeaveWorkerPool pool(3);
    std::vector<std::atomic<int>> seen(257);
    for(int frame=0;frame<5;++frame) {
        pool.parallelFor(seen.size(),[&](size_t i){++seen[i];});
        for(auto& count:seen)AIVERSE_TEST_CHECK(count.load()==frame+1);
    }
    bool threw=false;
    try {pool.parallelFor(100,[](size_t i){
        if(i==23)throw std::runtime_error("expected task failure");
    });}catch(const std::runtime_error&){threw=true;}
    AIVERSE_TEST_CHECK(threw);
    std::atomic<int> count=0;
    pool.parallelFor(15,[&](size_t){++count;});
    AIVERSE_TEST_CHECK(count==15);
    pool.parallelFor(0,[&](size_t){++count;});
    AIVERSE_TEST_CHECK(count==15);
    MotionWeaveWorkerPool sequential(0);
    sequential.parallelFor(7,[&](size_t){++count;});
    AIVERSE_TEST_CHECK(count==22);
    // Nested dispatch from a task (caller lane and worker lanes) runs
    // inline instead of deadlocking on the submission mutex.
    std::atomic<int> nested=0;
    pool.parallelFor(16,[&](size_t){
        pool.parallelFor(4,[&](size_t){++nested;});
    });
    AIVERSE_TEST_CHECK(nested==64);
    sequential.parallelFor(3,[&](size_t){
        sequential.parallelFor(2,[&](size_t){++nested;});
    });
    AIVERSE_TEST_CHECK(nested==70);
    // The pool is still usable after nested dispatch.
    pool.parallelFor(15,[&](size_t){++count;});
    AIVERSE_TEST_CHECK(count==37);
}
