// Deferred repeated-dispatch/exception contract for persistent worker pool.
#include <aiverselib/animation/MotionWeaveWorkers.h>
#include <atomic>
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <vector>
using namespace aiverse::lib::animation;
int main(){
    MotionWeaveWorkerPool pool(4);
    std::vector<std::atomic<unsigned>> visits(512);
    for(unsigned frame=1;frame<=40;++frame){
        pool.parallelFor(visits.size(),[&](size_t i){visits[i].fetch_add(1);});
        for(const auto& value:visits)AIVERSE_TEST_CHECK(value.load()==frame);
    }
    bool failed=false;
    try {
        pool.parallelFor(120,[](size_t i) {
            if(i==50)throw std::runtime_error("worker failure");
        });
    }catch(const std::runtime_error&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    std::atomic<int> after=0;
    pool.parallelFor(300,[&](size_t){++after;});
    AIVERSE_TEST_CHECK(after.load()==300);
}
