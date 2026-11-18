// c++ -std=c++20 -O2 -I. tests/ecs_snapshot_delta_test.cpp -o /tmp/aiverse-ecs-delta
// /tmp/aiverse-ecs-delta
#include "engine/experiments/EcsSnapshotDelta.h"
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
// Always-on regression checks: Release CI defines NDEBUG, which removes
// standard assert() calls and can hide test side effects and fail -Werror.
#define CHECK(...) do { if (!(__VA_ARGS__)) { \
    std::cerr << "ECS regression failed at " << __FILE__ << ":" << __LINE__ \
              << ": " << #__VA_ARGS__ << "\n"; std::abort(); } } while(false)
using namespace aiverse::ecs_experiment;
int main() {
    std::string baseline(1024*1024,'X');
    for(size_t i=0;i<baseline.size();++i)baseline[i]=char(i%251);
    auto same=SerializedDelta::encode(baseline,baseline);
    CHECK(same.storedBytes()==0 && same.changedPages()==0);
    CHECK(same.restore(baseline)==baseline);
    auto next=baseline;
    next[32]='Q';next[1024*30+4]='R';next[900000]='Z';
    auto small=SerializedDelta::encode(baseline,next);
    CHECK(!small.isFull() && small.changedPages()==3);
    CHECK(small.storedBytes()<4096 && small.restore(baseline)==next);
    bool mismatch=false;
    try {
        auto damaged=baseline;damaged[0]='!';
        (void)small.restore(damaged);
    } catch(const std::runtime_error&){mismatch=true;}
    CHECK(mismatch);
    // Shrink, grow and empty states are valid, including arbitrary binary bytes.
    for(const std::string& changed:{
            std::string{},
            baseline.substr(0,100),
            baseline+std::string(1000,'\0'),
            std::string(1024*1024,'!')}) {
        const auto delta=SerializedDelta::encode(baseline,changed);
        CHECK(delta.restore(baseline)==changed);
    }
    auto empty=SerializedDelta::encode(std::string{},std::string("new bytes\0xx",12));
    CHECK(empty.restore(std::string{})==std::string("new bytes\0xx",12));
    std::cout<<"PASS serialized snapshot delta, changed pages, grow/shrink, full fallback, base checksum\n";
}
