// NEREID: reproducible sampler benchmark (diagnostic, not a flaky CTest gate).
// Rebuilds the transient index once per tick, then samples an identical batch.
// Timers stay entirely in this tool and never affect deterministic gameplay.
#include "engine/world/nereid/NereidQueryIndex.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;

static uint64_t sampleSignature(const nw::WaterSample& s) {
    return (s.found ? s.body.packed() : uint64_t(0)) ^
           (s.found ? uint64_t(s.surfaceY.raw) : 0);
}
struct Result { double microsecondsPerTick=0;uint64_t signature=0; };
template<class Query>
static Result measure(int ticks,const std::vector<Vec3>& probes,Query query) {
    const auto begin=std::chrono::steady_clock::now();
    uint64_t checksum=0;
    for(int i=0;i<ticks;++i)
        for(const auto& probe:probes)
            checksum+=sampleSignature(query(probe));
    const auto end=std::chrono::steady_clock::now();
    return {std::chrono::duration<double,std::micro>(end-begin).count()/ticks,checksum};
}
static void caseRun(int bodies,int probesCount) {
    World w;
    for(int i=0;i<bodies;++i) {
        const Entity e=w.create();
        nw::WaterBodyComponent b;
        b.footprint=nw::WaterFootprint::Disc;
        b.center={Fixed::fromInt((i%32)*20),Fixed{},
                  Fixed::fromInt((i/32)*20)};
        b.radius=9_fx;b.surfaceY=10_fx;b.bottomY=-4_fx;
        b.priority=i%3;
        w.add<nw::WaterBodyComponent>(e,b);
    }
    std::vector<Vec3> probes;probes.reserve(size_t(probesCount));
    for(int i=0;i<probesCount;++i)
        probes.push_back({Fixed::fromInt((i*17)%650),5_fx,
                          Fixed::fromInt((i*29)%180)});
    constexpr int ticks=12;
    // Warm both paths; checksums are not reported as timing results.
    const nw::WaterQueryIndex warmIndex(w);
    for(const auto& p:probes) {
        const auto a=nw::sampleWater(w,p),b=warmIndex.sample(p);
        if(a.found!=b.found || a.body!=b.body || a.depth!=b.depth)
            throw std::runtime_error("NEREID benchmark parity failure");
    }
    const Result linear=measure(ticks,probes,
        [&](Vec3 p) {return nw::sampleWater(w,p);});
    const auto indexedStart=std::chrono::steady_clock::now();
    uint64_t sum=0;
    for(int tick=0;tick<ticks;++tick) {
        const nw::WaterQueryIndex index(w); // real per-tick rebuild cost
        for(const auto& p:probes)sum+=sampleSignature(index.sample(p));
    }
    const auto indexedEnd=std::chrono::steady_clock::now();
    const Result indexed{
        std::chrono::duration<double,std::micro>(indexedEnd-indexedStart).count()/ticks,
        sum};
    if(linear.signature!=indexed.signature)
        throw std::runtime_error("NEREID indexed checksum parity failure");
    const double ratio=indexed.microsecondsPerTick>0?
        linear.microsecondsPerTick/indexed.microsecondsPerTick:0;
    std::cout<<"bodies="<<bodies<<" probes="<<probesCount
             <<" linear_us/tick="<<linear.microsecondsPerTick
             <<" indexed_us/tick="<<indexed.microsecondsPerTick
             <<" speedup="<<ratio
             <<" sample_digest="<<indexed.signature<<"\n";
}
int main() {
    for(int bodies:{16,64,256})
        for(int probes:{8,128,1024})caseRun(bodies,probes);
    std::cout<<"PASS NEREID sampling benchmark parity (speedup is observational)\n";
}
