#include "SpectraGpuCueBridge.h"
#include "SpectraParticleProgram.h"
#include <algorithm>
#include <utility>

namespace aiverse::spectra {
namespace {
bool finite(fx::float3 p) {
    return program::number(p.x) && program::number(p.y) && program::number(p.z);
}
bool finite(fx::float4 p) {
    return program::number(p.x) && program::number(p.y) &&
           program::number(p.z) && program::number(p.w);
}
bool valid(const GpuCueEvent& e) {
    if(!finite(e.position)||!finite(e.direction)||!finite(e.tint))return false;
    if(e.hasParameters)
        for(const float v:e.parameters)if(!program::number(v))return false;
    return true;
}
}
GpuCueBridge::GpuCueBridge(GpuPoolWorld& world,GpuCueBudgets budgets)
    : world_(world),budgets_(budgets) {
    budgets_.queued=std::min(budgets_.queued,8192u);
    budgets_.acceptedPerTick=std::min(budgets_.acceptedPerTick,1024u);
    budgets_.dispatchedPerFrame=std::min(budgets_.dispatchedPerFrame,1024u);
}
bool GpuCueBridge::bind(uint32_t kind,std::string effectName) {
    if(effectName.empty()||effectName.size()>128u) {
        reason_="SpectraFX cue needs a nonempty pooled effect name";
        return false;
    }
    bindings_[kind]=std::move(effectName);
    reason_.clear();
    return true;
}
bool GpuCueBridge::accept(uint64_t tick,std::span<const GpuCueEvent> events) {
    if(haveTick_ && tick<=stats_.lastTick) {
        ++stats_.duplicateTicks;
        reason_="SpectraFX cue tick already consumed or older than latest";
        return false;
    }
    haveTick_=true;
    stats_.lastTick=tick;
    ++stats_.committedTicks;
    uint32_t admitted=0;
    for(const auto& cue:events) {
        const auto binding=bindings_.find(cue.kind);
        if(binding==bindings_.end()||!valid(cue)) {
            ++stats_.droppedUnbound;
            continue;
        }
        if(admitted>=budgets_.acceptedPerTick||queued_.size()>=budgets_.queued) {
            ++stats_.droppedBudget;
            continue;
        }
        queued_.push_back({binding->second,cue,0});
        ++admitted;
        ++stats_.accepted;
    }
    stats_.queued=uint32_t(queued_.size());
    reason_.clear();
    return true;
}
uint32_t GpuCueBridge::flush() {
    uint32_t emitted=0;
    uint32_t attempts=0;
    std::deque<Queued> retry; // rejected this frame, kept in original order
    while(!queued_.empty()&&attempts<budgets_.dispatchedPerFrame) {
        Queued q=std::move(queued_.front());
        queued_.pop_front();
        ++attempts;
        const auto* params=q.cue.hasParameters?&q.cue.parameters:nullptr;
        if(world_.burst(q.effect,q.cue.position,q.cue.direction,q.cue.tint,params)) {
            ++stats_.submitted;
            ++emitted;
        } else {
            reason_=world_.reason();
            if(++q.attempts>=kMaxFlushAttempts) ++stats_.rejectedByWorld;
            else {
                ++stats_.deferredByWorld;
                retry.push_back(std::move(q));
            }
        }
    }
    // Re-queue ahead of never-attempted cues; size never exceeds the bound
    // because every retried cue was popped from queued_ this flush.
    while(!retry.empty()) {
        queued_.push_front(std::move(retry.back()));
        retry.pop_back();
    }
    stats_.queued=uint32_t(queued_.size());
    return emitted;
}
bool GpuCueBridge::resetForRollback() {
    if(!world_.resetForRollback()) {
        reason_=world_.reason();
        return false;
    }
    queued_.clear();
    haveTick_=false;
    stats_={};
    reason_.clear();
    return true;
}
} // namespace aiverse::spectra
