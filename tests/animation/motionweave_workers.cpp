// Deferred checks for persistent workers, barriers and exceptions.
#include <aiverselib/animation/MotionWeaveWorkers.h>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace aiverse::lib::animation;
int main(){
    MotionWeaveWorkerPool pool(3);
    std::vector<std::atomic<int>> seen(257);
    for(int frame=0;frame<5;++frame) {
        pool.parallelFor(seen.size(),[&](size_t i){++seen[i];});
        for(auto& count:seen)assert(count.load()==frame+1);
    }
    bool threw=false;
    try {pool.parallelFor(100,[](size_t i){
        if(i==23)throw std::runtime_error("expected task failure");
    });}catch(const std::runtime_error&){threw=true;}
    assert(threw);
    std::atomic<int> count=0;
    pool.parallelFor(15,[&](size_t){++count;});
    assert(count==15);
    pool.parallelFor(0,[&](size_t){++count;});
    assert(count==15);
    MotionWeaveWorkerPool sequential(0);
    sequential.parallelFor(7,[&](size_t){++count;});
    assert(count==22);
}
