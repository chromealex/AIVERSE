// Deferred repeated-dispatch/exception contract for persistent worker pool.
#include <aiverselib/animation/MotionWeaveWorkers.h>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace aiverse::lib::animation;
int main(){
    MotionWeaveWorkerPool pool(4);
    std::vector<std::atomic<unsigned>> visits(512);
    for(unsigned frame=1;frame<=40;++frame){
        pool.parallelFor(visits.size(),[&](size_t i){visits[i].fetch_add(1);});
        for(const auto& value:visits)assert(value.load()==frame);
    }
    bool failed=false;
    try {
        pool.parallelFor(120,[](size_t i) {
            if(i==50)throw std::runtime_error("worker failure");
        });
    }catch(const std::runtime_error&){failed=true;}
    assert(failed);
    std::atomic<int> after=0;
    pool.parallelFor(300,[&](size_t){++after;});
    assert(after.load()==300);
}
