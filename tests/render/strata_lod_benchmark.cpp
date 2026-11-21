// STRATA Stage18.4 — deterministic CPU LOD trace, NOT a GPU/FPS benchmark.
// Run standalone to emit comparable per-frame CSV for three policies.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGeometry.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include <utility>
using namespace aiverse::render::strata;

namespace {
constexpr uint32_t kFrames=240;
constexpr float kThreshold=1.0f;
// Fixed camera-derived projected-error trace: stable endpoints plus a
// threshold-crossing interval. No randomness, real-time clock or IO input.
float projectedError(uint32_t frame) {
    if(frame<40)return 0.65f;
    if(frame<80)return 1.35f;
    if(frame<200)return (frame%2==0) ? .96f : 1.04f;
    return .65f;
}
struct Result {
    uint64_t triangles=0, transitions=0, checksum=1469598103934665603ull;
    uint64_t cpuNanoseconds=0;
};
void mix(uint64_t& h,uint64_t value) { h=(h^value)*1099511628211ull; }
Result run(const std::vector<ClusterNode>& nodes,
           const std::vector<uint32_t>& roots,
           float hysteresis,bool legacy,bool measure) {
    Result result;
    std::vector<uint8_t> history;
    std::vector<uint32_t> previous;
    for(uint32_t frame=0;frame<kFrames;++frame) {
        std::array<float,3> error{projectedError(frame),0.f,0.f};
        SelectionOutput output;
        const auto begin=std::chrono::steady_clock::now();
        if(legacy) {
            output.nodes=roots; // fixed source/root fallback, no LOD selection
        } else {
            SelectionInput input{};
            input.errorPixels=error.data();
            input.errorCount=uint32_t(error.size());
            input.thresholdPixels=kThreshold;
            input.maxVisibleNodes=16;
            input.hysteresisFraction=hysteresis;
            if(!history.empty()) {
                input.previousRefined=history.data();
                input.previousRefinedCount=uint32_t(history.size());
            }
            assert(select(nodes,roots,input,output));
        }
        const auto end=std::chrono::steady_clock::now();
        if(measure)result.cpuNanoseconds+=uint64_t(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count());
        if(!previous.empty() && output.nodes!=previous)++result.transitions;
        previous=output.nodes;
        for(const auto id:output.nodes) {
            result.triangles+=nodes[id].indexCount/3;
            mix(result.checksum,(uint64_t(frame)<<32)|id);
        }
        if(!legacy && hysteresis>0.f)history=output.refinedNodes;
    }
    return result;
}
}
int main() {
    std::vector<ClusterNode> nodes(3);
    nodes[0].firstChild=1;
    nodes[0].childCount=2;
    nodes[0].indexCount=6;
    nodes[0].resident=true;
    for(uint32_t i=1;i<3;++i) {
        nodes[i].firstChild=invalidNode;
        nodes[i].indexCount=9;
        nodes[i].resident=true;
    }
    const std::vector<uint32_t> roots{0};
    assert(validate(nodes,roots));
    const auto fixed=run(nodes,roots,0.f,true,false);
    const auto cold=run(nodes,roots,0.f,false,false);
    const auto temporal=run(nodes,roots,.2f,false,false);
    assert(fixed.transitions==0);
    assert(cold.transitions>temporal.transitions);
    assert(cold.triangles>=fixed.triangles);
    assert(temporal.triangles>=fixed.triangles);
    assert(run(nodes,roots,.2f,false,false).checksum==temporal.checksum);
    // Timing samples are CPU wall only, non-comparable to GPU or FPS.
    std::cout<<"mode,frames,transitions,triangles,checksum,cpu_wall_ns\n";
    for(const auto& item : std::array<std::pair<const char*,int>,3>{{
        {"legacy_root",0},{"strata_h0",1},{"strata_h02",2}}}) {
        const auto measured=run(nodes,roots,item.second==2?.2f:0.f,
                                item.second==0,true);
        std::cout<<item.first<<","<<kFrames<<","<<measured.transitions<<","
                 <<measured.triangles<<","<<measured.checksum<<","
                 <<measured.cpuNanoseconds<<"\n";
    }
    return 0;
}
