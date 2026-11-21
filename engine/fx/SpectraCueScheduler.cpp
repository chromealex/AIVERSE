#include "SpectraCueScheduler.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace aiverse::spectra {
namespace {

CueBudgets bounded(CueBudgets b) {
    b.maxQueued = std::min<size_t>(b.maxQueued, 8192);
    b.maxPerTick = std::min<size_t>(b.maxPerTick, 1024);
    b.maxPerFrame = std::min<size_t>(b.maxPerFrame, 1024);
    b.maxAttachments = std::min<size_t>(b.maxAttachments, 4096);
    // One-shot handles MUST be tracked for rollback: do not allow zero.
    b.maxTrackedOneShots = std::clamp<size_t>(b.maxTrackedOneShots, 1, 16384);
    return b;
}
bool valid(float x) { return std::isfinite(x); }
bool valid(fx::float3 a) { return valid(a.x) && valid(a.y) && valid(a.z); }
bool valid(fx::float4 a) { return valid(a.x) && valid(a.y) && valid(a.z) && valid(a.w); }
bool validPose(fx::float3 pos, fx::float3 dir, float scale) {
    return valid(pos) && valid(dir) && valid(scale) && scale > 0 && scale <= 1000.f;
}
bool validParams(const std::array<float,8>& params,uint8_t mask) {
    for(uint32_t i=0;i<8;++i)
        if((mask&(1u<<i)) &&
           (!valid(params[i]) || std::abs(params[i])>1.0e7f))return false;
    return true;
}
std::array<float,8> maskedParams(const std::array<float,8>& params,uint8_t mask) {
    std::array<float,8> out{};
    for(uint32_t i=0;i<8;++i)
        if(mask&(1u<<i))out[i]=params[i];
    return out;
}
bool changedParams(const std::array<float,8>& a,uint8_t am,
                   const std::array<float,8>& b,uint8_t bm) {
    if(am!=bm)return true;
    for(uint32_t i=0;i<8;++i)
        if((am&(1u<<i)) && a[i]!=b[i])return true;
    return false;
}
} // namespace

CueScheduler::CueScheduler(BatchWorld& world, CueBudgets budgets)
    : world_(world), budgets_(bounded(budgets)) {}

bool CueScheduler::registerEffect(const std::string& name, const fx::EffectDef& definition) {
    if(name.empty() || name.size()>128) { error_="invalid SpectraFX cue effect name";return false; }
    if(!world_.ready()) { error_="SpectraFX BatchWorld is not initialized";return false; }
    const int id=world_.defineEffect(definition);
    if(id<0) {
        error_=world_.error();
        return false;
    }
    effects_[name]={id};
    error_.clear();
    return true;
}
bool CueScheduler::registerGraph(const std::string& name, const Json& graph,
                                 const Json& overrides) {
    fx::EffectDef compiled;
    std::vector<std::string> errors;
    if(!fx::compileSpectraGraph(graph,overrides,compiled,errors)) {
        error_=errors.empty()?"SpectraFX graph validation failed":errors.front();
        return false; // previous asset remains registered on bad hot-reload
    }
    return registerEffect(name,compiled);
}
bool CueScheduler::bindCue(uint32_t type, const std::string& name) {
    if(effects_.find(name)==effects_.end()) {
        error_="SpectraFX cue binding references an unregistered effect: "+name;
        return false;
    }
    bindings_[type]=name;
    error_.clear();
    return true;
}
void CueScheduler::unbindCue(uint32_t type) {
    bindings_.erase(type);
}
bool CueScheduler::acceptTick(uint64_t tick, std::span<const CueEvent> events) {
    if(haveTick_ && tick<=stats_.lastTick) {
        ++stats_.duplicateTicks;
        return false;
    }
    haveTick_=true;
    stats_.lastTick=tick;
    ++stats_.acceptedTicks;
    size_t tickAccepted=0;
    for(const CueEvent& cue:events) {
        const auto binding=bindings_.find(cue.type);
        if(binding==bindings_.end()) {++stats_.droppedUnbound;continue;}
        const auto effect=effects_.find(binding->second);
        if(effect==effects_.end() || effect->second.id<0 ||
           !validPose(cue.position,cue.direction,cue.scale) ||
           !valid(cue.tint) || !validParams(cue.programParams,cue.programParamMask)) {
            ++stats_.droppedUnbound;continue;
        }
        if(tickAccepted>=budgets_.maxPerTick || queue_.size()>=budgets_.maxQueued) {
            ++stats_.droppedBudget;continue;
        }
        queue_.push_back({effect->second.id,cue}); // definition ID snapshot
        ++tickAccepted;
        ++stats_.acceptedCues;
    }
    stats_.queued=queue_.size();
    return true;
}
void CueScheduler::trackOneShot(fx::FxHandle handle) {
    if(oneShots_.size()>=budgets_.maxTrackedOneShots) {
        // Kill the oldest cue rather than forgetting its handle; rollback must
        // never leak any presentation-owned particle state into a new match.
        world_.kill(oneShots_.front());
        oneShots_.pop_front();
        ++stats_.evictedOneShots;
    }
    oneShots_.push_back(handle);
    stats_.trackedOneShots=oneShots_.size();
}
size_t CueScheduler::flush() {
    if(!world_.ready()) {
        stats_.droppedBudget+=queue_.size();
        queue_.clear();
        stats_.queued=0;
        error_="SpectraFX BatchWorld is not initialized";
        return 0;
    }
    size_t spawned=0;
    size_t attempts=0;
    while(!queue_.empty() && attempts<budgets_.maxPerFrame) {
        Queued item=std::move(queue_.front());
        queue_.pop_front();
        ++attempts;
        const fx::FxHandle handle=world_.playWithProgramParameters(
            item.effectId,item.cue.position,item.cue.direction,
            item.cue.scale,item.cue.tint,
            maskedParams(item.cue.programParams,item.cue.programParamMask));
        if(handle.valid()) {
            trackOneShot(handle);
            ++spawned;
            ++stats_.emittedCues;
        } else ++stats_.failedSpawns;
    }
    stats_.queued=queue_.size();
    return spawned;
}
void CueScheduler::retire(const Active& active) {
    if(active.killOnDetach)world_.kill(active.handle);
    else world_.stop(active.handle);
    ++stats_.stoppedSources;
}
void CueScheduler::syncAttachments(std::span<const AttachedCue> sources) {
    // Release vanished sources BEFORE accepting newcomers. Otherwise a full
    // attachment budget would incorrectly reject replacement entities for
    // one frame even though the retired sources no longer exist.
    std::set<uint64_t> desired;
    for(const AttachedCue& src:sources)
        if(!src.effect.empty() && validPose(src.position,src.direction,src.scale) &&
           validParams(src.programParams,src.programParamMask) &&
           effects_.count(src.effect)) desired.insert(src.key);
    for(auto it=active_.begin();it!=active_.end();) {
        if(desired.count(it->first)){++it;continue;}
        retire(it->second);
        it=active_.erase(it);
    }
    std::set<uint64_t> seen;
    for(const AttachedCue& src:sources) {
        if(src.effect.empty() || !validPose(src.position,src.direction,src.scale) ||
           !validParams(src.programParams,src.programParamMask))
            continue;
        const auto effect=effects_.find(src.effect);
        if(effect==effects_.end()) {++stats_.droppedUnbound;continue;}
        if(!seen.emplace(src.key).second)continue; // avoid duplicate source IDs
        auto existing=active_.find(src.key);
        const bool needRestart=existing!=active_.end() &&
             (existing->second.effectName!=src.effect ||
              existing->second.effectId!=effect->second.id ||
              existing->second.revision!=src.revision ||
              existing->second.scale!=src.scale);
        if(needRestart) {
            retire(existing->second);
            active_.erase(existing);
            existing=active_.end();
            ++stats_.restartedSources;
        }
        if(existing!=active_.end()) {
            existing->second.killOnDetach=src.killOnDetach;
            if(world_.playing(existing->second.handle)) {
                if(changedParams(existing->second.programParams,
                                 existing->second.programParamMask,
                                 src.programParams,src.programParamMask)) {
                    // Removed overrides return to zero, not the previous
                    // source's potentially stale value.
                    for(uint32_t i=0;i<8;++i)
                        if((existing->second.programParamMask|src.programParamMask)&(1u<<i))
                            (void)world_.setProgramParameter(existing->second.handle,i,
                                (src.programParamMask&(1u<<i))?src.programParams[i]:0.f,true);
                    existing->second.programParams=src.programParams;
                    existing->second.programParamMask=src.programParamMask;
                }
                world_.move(existing->second.handle,src.position,src.direction);
                continue;
            }
            // A stopped one-shot is not an attached continuous source.
            // On restart we must own/replace its previous handle.
            world_.kill(existing->second.handle);
            active_.erase(existing);
            ++stats_.restartedSources;
        }
        if(active_.size()>=budgets_.maxAttachments) {
            ++stats_.droppedBudget;
            continue;
        }
        fx::FxHandle handle=world_.playWithProgramParameters(
            effect->second.id,src.position,src.direction,src.scale,
            {1,1,1,1},maskedParams(src.programParams,src.programParamMask));
        if(!handle.valid()) {
            ++stats_.failedSpawns;
            continue;
        }
        Active active{handle,effect->second.id,src.effect,
                      src.revision,src.scale,src.killOnDetach};
        active.programParams=src.programParams;
        active.programParamMask=src.programParamMask;
        active_.emplace(src.key,std::move(active));
    }
    stats_.attached=active_.size();
}
void CueScheduler::resetForRollback() {
    // Force-kill, even when ordinary detach policy was stop/fade. Otherwise
    // surviving particles could leak into the restored tick and double-play.
    for(const auto& [key,active]:active_) {
        (void)key;
        world_.kill(active.handle);
    }
    active_.clear();
    for(const auto& handle:oneShots_)world_.kill(handle);
    oneShots_.clear();
    queue_.clear();
    haveTick_=false;
    stats_={};
    error_.clear();
}
} // namespace aiverse::spectra
