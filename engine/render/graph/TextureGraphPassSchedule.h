// Bounded ordered compute-pass policy. No device ownership or implied GPU fences.
#pragma once
#include <cstddef>
#include <vector>

namespace aiverse::render::texture_graph::gpu {
struct ScheduledPass {
    size_t pipeline=0, output=0;
    std::vector<size_t> inputs;
};
struct PassSchedule {
    std::vector<size_t> slotBytes;
    std::vector<size_t> externalReady;
    std::vector<ScheduledPass> passes;
    size_t resultSlot=0;
};
// Preflight the complete sequence before issuing any commands. Slot writes
// replace their previous value; no in-place read/write or implicit input.
inline bool validPassSchedule(const PassSchedule& schedule,size_t budget){
    const auto count=schedule.slotBytes.size();
    if(!count||count>32||schedule.passes.empty()||schedule.passes.size()>32||
       schedule.resultSlot>=count||schedule.externalReady.size()>count)return false;
    size_t bytes=0;
    for(auto size:schedule.slotBytes){
        if(!size||size>budget-bytes)return false;
        bytes+=size;
    }
    std::vector<bool> initialized(count);
    for(auto slot:schedule.externalReady){
        if(slot>=count||initialized[slot])return false;
        initialized[slot]=true;
    }
    bool resultWritten=false;
    for(const auto& pass:schedule.passes){
        if(pass.pipeline>=32||pass.output>=count||pass.inputs.size()>31)return false;
        for(auto input:pass.inputs)
            if(input>=count||input==pass.output||!initialized[input])return false;
        initialized[pass.output]=true;
        if(pass.output==schedule.resultSlot)resultWritten=true;
    }
    return resultWritten;
}
struct PassEncodeResult {bool complete=false;size_t encoded=0;};
// The owner callback must encode in order and establish the backend's
// actual visibility barriers. Completion here is CPU encoding only.
// After failure the owner must retain submitted resources until retirement.
template<class Encode>
PassEncodeResult encodePassSchedule(const PassSchedule& schedule,size_t budget,Encode encode){
    if(!validPassSchedule(schedule,budget))return {};
    PassEncodeResult result;
    for(const auto& pass:schedule.passes){
        if(!encode(pass))return result;
        ++result.encoded;
    }
    result.complete=true;
    return result;
}
} // namespace aiverse::render::texture_graph::gpu
