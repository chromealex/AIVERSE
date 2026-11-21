#include "SpectraGpuPoolWorld.h"
#include "SpectraParticleProgram.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <cmath>
#include <set>
#include <utility>

namespace aiverse::spectra {
namespace {
std::atomic<uint64_t> nextSourceWorldEpoch{1};
bool valid(float v) {return program::number(v);}
bool valid(fx::float3 v) {return valid(v.x)&&valid(v.y)&&valid(v.z);}
bool valid(fx::float4 v) {return valid(v.x)&&valid(v.y)&&valid(v.z)&&valid(v.w);}
bool validParams(const std::array<float,8>& params) {
    for(float value:params)if(!valid(value))return false;
    return true;
}
// Multiply-free hysteresis threshold: no overflow even for SIZE_MAX.
size_t releaseBelow(size_t threshold,uint32_t percentage) {
    return (threshold/100u)*percentage+
           ((threshold%100u)*percentage)/100u;
}
} // namespace

bool GpuPoolWorld::init(gfx::Device* device, PoolWorldBudgets budgets) {
    shutdown();
    budgets.maxGpuGroups=std::min<uint32_t>(budgets.maxGpuGroups,64);
    budgets.maxGroupCapacity=std::clamp<uint32_t>(budgets.maxGroupCapacity,1,4096);
    budgets.maxBurstsPerFrame=std::min<uint32_t>(budgets.maxBurstsPerFrame,4096);
    budgets.maxSources=std::clamp<uint32_t>(budgets.maxSources,1,65536);
    if(budgets.loadShedding.enabled) {
        budgets.loadShedding.recoverBelowPercent=
            std::clamp<uint32_t>(
                budgets.loadShedding.recoverBelowPercent,1u,100u);
        budgets.loadShedding.maxContinuousBurstsPerFrame=std::min(
            budgets.loadShedding.maxContinuousBurstsPerFrame,
            budgets.maxBurstsPerFrame);
        budgets.loadShedding.reserveManualBursts=std::min(
            budgets.loadShedding.reserveManualBursts,budgets.maxBurstsPerFrame);
    }
    budgets_=budgets;
    stats_.sourceCapacity=budgets_.maxSources;
    device_=device;
    if(!cpu_.init(device_,budgets_.quality)) {
        reason_="SpectraFX pooled CPU world init failed: "+cpu_.error();
        device_=nullptr;
        return false;
    }
    checkpointBudget_=budgets_.sharedCheckpointBudget
        ?budgets_.sharedCheckpointBudget
        :std::make_shared<CheckpointReadbackBudget>(budgets_.checkpointReadback);
    sourceWorldEpoch_=nextSourceWorldEpoch.fetch_add(1,std::memory_order_relaxed);
    initialized_=true;
    reason_.clear();
    return true;
}

void GpuPoolWorld::shutdown() {
    // Destroy GPU resources before the caller's graphics Device.
    gpuGroups_.clear();
    resetAccepted_.clear();
    resetReport_={};
    bindings_.clear();
    definitions_.clear();
    sources_.clear();freeSourceSlots_.clear();
    sourceWorldEpoch_=0;nextBindingRevision_=1;nextSourceToken_=1;
    checkpointBudget_.reset();
    cpu_.shutdown();
    device_=nullptr;
    initialized_=frameOpen_=stepped_=drawn_=groupsDirty_=false;
    nextGroupStart_=nextContinuousSource_=0;
    particlePressureLatched_=readbackPressureLatched_=false;
    stats_={};
    reason_.clear();
}

std::string GpuPoolWorld::groupKey(const fx::EffectDef& effect,
                                  const GraphRuntimeOptions& options,
                                  uint32_t capacity) {
    const std::string serialized=fx::effectToJson(effect).dump(-1);
    // Length prefix prevents ambiguous keys if authored shader graphs contain
    // textual substrings that resemble the policy suffix.
    return std::to_string(serialized.size())+":"+serialized+
        "|capacity="+std::to_string(capacity)+
        "|quality="+std::to_string(unsigned(options.quality))+
        "|vm="+std::to_string(unsigned(options.gpuProgram))+
        "|material="+std::to_string(unsigned(options.gpuMaterial))+
        "|indirect="+std::to_string(unsigned(options.gpuIndirect))+
        "|checkpointCadence="+std::to_string(options.checkpointEveryFrames)+
        "|adaptive="+std::to_string(unsigned(options.adaptiveCheckpoint.enabled))+
        "|adaptiveMax="+std::to_string(options.adaptiveCheckpoint.maxIntervalFrames)+
        "|adaptiveRecover="+std::to_string(options.adaptiveCheckpoint.recoverAfterCompletions);
}

void GpuPoolWorld::recountBindings() {
    // Any definition change may orphan a GPU group; rescan at the next frame.
    groupsDirty_=true;
    stats_.gpuGroups=uint32_t(std::count_if(gpuGroups_.begin(),gpuGroups_.end(),
        [](const Group& group){return group.live();}));
    std::set<int> distinctCpu;
    for(const auto& [name,binding]:bindings_) {
        (void)name;
        if(!binding.gpu && binding.cpuId>=0)distinctCpu.insert(binding.cpuId);
    }
    stats_.cpuDefinitions=uint32_t(distinctCpu.size());
}

bool GpuPoolWorld::defineGraph(const std::string& name,const Json& graph,
                              const Json& overrides,
                              const GraphRuntimeOptions& options) {
    if(!initialized_ || frameOpen_ || presentationBlocked()) {
        reason_="define pooled SpectraFX graph requires a healthy, unblocked presentation world";
        return false;
    }
    InstanceParameterTable named;
    std::vector<std::string> errors;
    if(graph.getInt("version")==2 &&
       !loadInstanceParameterTable(graph["instanceParameters"],named,errors)) {
        reason_=errors.empty()?"invalid GPU pool instanceParameters":errors.front();
        return false;
    }
    fx::EffectDef definition;
    if(!fx::compileSpectraGraph(graph,overrides,definition,errors)) {
        reason_=errors.empty()?"SpectraFX graph compilation failed":errors.front();
        return false;
    }
    return defineCompiled(name,definition,options,named);
}

bool GpuPoolWorld::defineEffect(const std::string& name,
                               const fx::EffectDef& effect,
                               const GraphRuntimeOptions& options) {
    if(!initialized_ || frameOpen_ || presentationBlocked()) {
        reason_="define pooled SpectraFX effect requires a healthy, unblocked presentation world";
        return false;
    }
    return defineCompiled(name,effect,options,{});
}

bool GpuPoolWorld::defineCompiled(const std::string& name,
                                 const fx::EffectDef& effect,
                                 const GraphRuntimeOptions& options,
                                 const InstanceParameterTable& named) {
    if(name.empty()||name.size()>128) {
        reason_="SpectraFX pool effect name must be 1..128 bytes";
        return false;
    }
    GraphRuntimeOptions actual=options;
    // Never exceed the global per-group memory/quality budget even when a
    // caller requests a larger emitter allocation or higher quality tier.
    actual.capacity=std::min(options.capacity,budgets_.maxGroupCapacity);
    actual.quality=std::min(options.quality,budgets_.quality);
    // A pooled Device must use ONE limiter for all its GPU groups, including
    // groups with different checkpoint cadences.
    actual.checkpointBudget=checkpointBudget_;
    const GraphGpuEligibility eligible=inspectGraphGpuEligibility(effect,actual);
    const bool wantsGpu=options.preference!=ComputePreference::CpuOnly;
    const bool metalAvailable=device_&&device_->supportsCompute()&&
        device_->info().backend==gfx::Backend::Metal &&
        actual.quality>=fx::FxQuality::Medium;
    Binding next;
    for(const auto& [paramName,definition]:named) {
        (void)paramName;
        next.defaults[definition.slot]=definition.initial;
    }
    next.named=named;
    // Preserve only authored/compiled CPU-side data so a new pool can
    // re-declare the effect after the host rebuilds gfx::Device.
    PoolRecoveryEffect remembered;
    remembered.name=name;
    remembered.effect=effect;
    remembered.options=options;
    remembered.options.checkpointBudget.reset();
    remembered.named=named;
    remembered.defaults=next.defaults;
    next.usesRuntimeParams=effect.emitters.size()==1 &&
        (!effect.emitters[0].particleProgram.isNull() ||
         !effect.emitters[0].shaderGraph.isNull());

    if(wantsGpu && eligible.eligible && metalAvailable) {
        const std::string key=groupKey(effect,actual,actual.capacity);
        for(size_t i=0;i<gpuGroups_.size();++i)
            if(gpuGroups_[i].live() && gpuGroups_[i].key==key) {
                next.gpu=true;next.gpuIndex=i;
                next.revision=nextBindingRevision_++;
                bindings_[name]=std::move(next);
                definitions_[name]=remembered;
                recountBindings();
                reason_.clear();
                return true; // share GPU storage, shader and draw for this effect
            }
        // Retired groups (orphaned by redefinition) do not count toward the
        // unique-group budget; a fully released slot is reused in place so
        // existing binding/source group indices stay stable.
        size_t liveGroups=0,freeSlot=gpuGroups_.size();
        for(size_t i=0;i<gpuGroups_.size();++i) {
            if(gpuGroups_[i].live())++liveGroups;
            else if(!gpuGroups_[i].particles && freeSlot==gpuGroups_.size())freeSlot=i;
        }
        if(liveGroups<budgets_.maxGpuGroups) {
            auto particles=std::make_unique<ComputeParticles>();
            ComputeEmitterDesc desc=eligible.gpu;
            desc.backend=ComputePreference::RequireGpu;
            if(particles->init(device_,desc) && particles->gpuActive()) {
                next.gpu=true;next.gpuIndex=freeSlot;
                if(freeSlot==gpuGroups_.size())
                    gpuGroups_.push_back({key,std::move(particles)});
                else gpuGroups_[freeSlot]=Group{key,std::move(particles)};
                next.revision=nextBindingRevision_++;
                bindings_[name]=std::move(next);
                definitions_[name]=remembered;
                recountBindings();
                reason_.clear();
                return true;
            }
            reason_="GPU pool group initialization failed: "+particles->reason();
        } else reason_="SpectraFX max unique GPU resource groups reached";
    } else if(wantsGpu) {
        reason_=!eligible.eligible?eligible.reason:
            "Metal compute or required quality unavailable for pooled GPU emitter";
    }

    if(options.preference==ComputePreference::RequireGpu) {
        if(reason_.empty())reason_="GPU resource group required but unsupported";
        return false; // no fallback and no false group assignment
    }

    const int id=cpu_.defineEffect(effect); // retains entire original EffectDef
    if(id<0) {
        reason_+="; shared CPU fallback rejected full effect: "+cpu_.error();
        return false;
    }
    next.cpuId=id;
    next.revision=nextBindingRevision_++;
    bindings_[name]=std::move(next);
    definitions_[name]=remembered;
    recountBindings();
    reason_.clear();
    return true;
}

bool GpuPoolWorld::setLoadShedding(PoolLoadSheddingPolicy policy) {
    if(!initialized_ || frameOpen_ ||
       (policy.enabled &&
       (policy.maxContinuousBurstsPerFrame>budgets_.maxBurstsPerFrame ||
        policy.reserveManualBursts>budgets_.maxBurstsPerFrame ||
        policy.recoverBelowPercent<1 ||
        policy.recoverBelowPercent>100))) {
        reason_="SpectraFX load policy must fit global burst budget and change between frames";
        return false;
    }
    budgets_.loadShedding=policy;
    particlePressureLatched_=readbackPressureLatched_=false;
    stats_.particlePressureLatched=false;
    stats_.readbackPressureLatched=false;
    reason_.clear();
    return true;
}

bool GpuPoolWorld::setNamedParameter(const std::string& effectName,
                                     const std::string& parameterName,float value) {
    auto it=bindings_.find(effectName);
    if(it==bindings_.end()) {
        reason_="unknown SpectraFX pool effect '"+effectName+"'";
        return false;
    }
    const auto param=it->second.named.find(parameterName);
    if(param==it->second.named.end()||
       !checkInstanceParameterValue(param->second,value)) {
        reason_="invalid or out-of-range pooled instance parameter '"+parameterName+"'";
        return false;
    }
    it->second.defaults[param->second.slot]=value;
    if(auto stored=definitions_.find(effectName);stored!=definitions_.end())
        stored->second.defaults[param->second.slot]=value;
    reason_.clear();
    return true;
}

const GpuPoolWorld::Source* GpuPoolWorld::getSource(PoolSourceId id) const {
    if(!initialized_ || !id.valid() || id.worldEpoch!=sourceWorldEpoch_ ||
       id.slot>=sources_.size())return nullptr;
    const Source& source=sources_[id.slot];
    return source.active && source.generation==id.generation?&source:nullptr;
}
GpuPoolWorld::Source* GpuPoolWorld::getSource(PoolSourceId id) {
    return const_cast<Source*>(
        static_cast<const GpuPoolWorld*>(this)->getSource(id));
}
PoolSourceId GpuPoolWorld::createSource(const std::string& effectName) {
    auto binding=bindings_.find(effectName);
    if(!initialized_ || presentationBlocked() || binding==bindings_.end()) {
        reason_="SpectraFX createSource: unready/blocked pool or unknown effect";
        return {};
    }
    if(nextSourceToken_==0) {
        reason_="SpectraFX source token counter exhausted";return {};
    }
    uint32_t slot=0;
    while(!freeSourceSlots_.empty()) {
        slot=freeSourceSlots_.back();freeSourceSlots_.pop_back();
        if(sources_[slot].exhausted ||
           sources_[slot].generation==std::numeric_limits<uint32_t>::max()) {
            sources_[slot].exhausted=true;
            continue;
        }
        Source& source=sources_[slot];
        const uint32_t generation=source.generation+1;
        source=Source{};
        source.generation=generation;
        source.active=true;
        source.effectName=effectName;
        source.bindingRevision=binding->second.revision;
        source.values=binding->second.defaults;
        source.token=nextSourceToken_++;
        source.gpu=binding->second.gpu;
        source.gpuIndex=binding->second.gpuIndex;
        ++stats_.activeSources;
        reason_.clear();
        return {sourceWorldEpoch_,slot,source.generation};
    }
    if(sources_.size()>=budgets_.maxSources) {
        reason_="SpectraFX maximum active/reusable source slots exceeded";
        return {};
    }
    slot=uint32_t(sources_.size());
    sources_.push_back({});
    Source& source=sources_.back();
    source.generation=1;
    source.active=true;
    source.effectName=effectName;
    source.bindingRevision=binding->second.revision;
    source.values=binding->second.defaults;
    source.token=nextSourceToken_++;
    source.gpu=binding->second.gpu;
    source.gpuIndex=binding->second.gpuIndex;
    ++stats_.activeSources;
    reason_.clear();
    return {sourceWorldEpoch_,slot,source.generation};
}
bool GpuPoolWorld::sourceBindingCurrent(PoolSourceId id) const {
    const Source* source=getSource(id);
    if(!source)return false;
    const auto binding=bindings_.find(source->effectName);
    return binding!=bindings_.end() &&
           source->bindingRevision==binding->second.revision;
}
bool GpuPoolWorld::sourceActive(PoolSourceId id) const {
    const Source* source=getSource(id);
    if(!source || source->paused)return false;
    const auto binding=bindings_.find(source->effectName);
    return binding!=bindings_.end() &&
           source->bindingRevision==binding->second.revision;
}
bool GpuPoolWorld::pauseSource(PoolSourceId id) {
    Source* source=getSource(id);
    if(!source)return false;
    source->paused=true;source->accumulatedRate=0;
    source->accumulatedDistance=0;source->lastStepPosition=source->position;
    return true;
}
bool GpuPoolWorld::resumeSource(PoolSourceId id) {
    Source* source=getSource(id);
    if(!source)return false;
    const auto binding=bindings_.find(source->effectName);
    if(binding==bindings_.end() ||
       binding->second.revision!=source->bindingRevision)return false;
    source->paused=false;return true;
}
bool GpuPoolWorld::retireSource(PoolSourceId id) {
    Source* source=getSource(id);
    if(!source)return false;
    source->active=false;source->paused=false;
    source->effectName.clear();source->bindingRevision=0;
    source->values={};source->token=0;source->cpuHandles.clear();
    source->continuous={};source->transformAttached=false;
    if(!source->exhausted)freeSourceSlots_.push_back(id.slot);
    if(stats_.activeSources>0)--stats_.activeSources;
    if(source->gpu)groupsDirty_=true;
    return true;
}
bool GpuPoolWorld::killSourceParticles(PoolSourceId id) {
    Source* source=getSource(id);
    if(!source)return false;
    if(source->gpu) {
        if(source->gpuIndex>=gpuGroups_.size() ||
           !gpuGroups_[source->gpuIndex].particles ||
           !gpuGroups_[source->gpuIndex].particles->killSourceParticles(source->token)) {
            reason_="SpectraFX selective GPU kill must be before the scene render pass";
            return false;
        }
    } else {
        for(const fx::FxHandle h:source->cpuHandles)cpu_.kill(h);
        source->cpuHandles.clear();
    }
    ++stats_.selectiveKillRequests;
    reason_.clear();
    return true;
}
bool GpuPoolWorld::canStopSource(PoolSourceId id,bool killLiving) const {
    const Source* source=getSource(id);
    if(!source)return false;
    if(!killLiving || !source->gpu)return true;
    return device_ && device_->gpuHealthy() &&
           device_->canUpdateSharedBuffers() &&
           source->gpuIndex<gpuGroups_.size() &&
           gpuGroups_[source->gpuIndex].particles &&
           gpuGroups_[source->gpuIndex].particles->gpuActive();
}
bool GpuPoolWorld::stopSource(PoolSourceId id,bool killLiving) {
    Source* source=getSource(id);
    if(!source)return false;
    if(killLiving && !killSourceParticles(id))return false;
    source->paused=true;
    source->accumulatedRate=0;source->accumulatedDistance=0;
    source->lastStepPosition=source->position;
    return true;
}
bool GpuPoolWorld::setSourceTransform(PoolSourceId id,fx::float3 position,
                                      fx::float3 direction,fx::float4 tint) {
    Source* source=getSource(id);
    if(!source || !valid(position) || !valid(direction) || !valid(tint))return false;
    if(!source->transformAttached)source->lastStepPosition=position;
    source->position=position;source->direction=direction;source->tint=tint;
    source->transformAttached=true;
    return true;
}
bool GpuPoolWorld::detachSourceTransform(PoolSourceId id) {
    Source* source=getSource(id);
    if(!source)return false;
    source->transformAttached=false;
    source->accumulatedRate=source->accumulatedDistance=0;
    return true;
}
bool GpuPoolWorld::configureContinuous(PoolSourceId id,
                                       PoolContinuousEmission emission) {
    Source* source=getSource(id);
    if(!source || !valid(emission.burstsPerSecond) ||
       !valid(emission.metresPerBurst) || !valid(emission.lifetimeSeconds) ||
       emission.burstsPerSecond<0 || emission.burstsPerSecond>120 ||
       emission.metresPerBurst<0 || emission.metresPerBurst>1.0e5f ||
       emission.lifetimeSeconds<0 || emission.lifetimeSeconds>86400 ||
       emission.maxBurstsPerFrame<1 || emission.maxBurstsPerFrame>64)return false;
    source->continuous=emission;
    source->accumulatedRate=source->accumulatedDistance=source->elapsed=0;
    source->lastStepPosition=source->position;
    return true;
}
bool GpuPoolWorld::setSourceNamedParameter(
    PoolSourceId id,const std::string& name,float value) {
    Source* source=getSource(id);
    if(!source)return false;
    const auto binding=bindings_.find(source->effectName);
    if(binding==bindings_.end() ||
       binding->second.revision!=source->bindingRevision)return false;
    const auto param=binding->second.named.find(name);
    if(param==binding->second.named.end() ||
       !checkInstanceParameterValue(param->second,value))return false;
    source->values[param->second.slot]=value;
    return true; // future emissions only; existing GPU particles not changed
}
bool GpuPoolWorld::emitSource(PoolSourceId id,fx::float3 position,
                              fx::float3 direction,fx::float4 tint) {
    if(!sourceActive(id)) {
        reason_="SpectraFX emission rejected: retired, paused or stale source";
        return false;
    }
    Source& source=sources_[id.slot];
    // The source can be reallocated only by explicit retire; no reference is
    // invalidated by burstImpl, which does not mutate the sources vector.
    const auto values=source.values;
    fx::FxHandle cpuHandle{};
    const bool emitted=burstImpl(source.effectName,position,direction,tint,
                                  &values,source.token,&cpuHandle);
    if(emitted && !source.gpu && cpuHandle.valid()) {
        // Drop handles whose instances finished (no particles left) so a
        // long-lived source does not grow to the cap and pay O(n) erases.
        if(source.cpuHandles.size()>=source.cpuHandlePruneAt) {
            auto& handles=source.cpuHandles;
            handles.erase(std::remove_if(handles.begin(),handles.end(),
                [&](fx::FxHandle h){return !cpu_.alive(h);}),handles.end());
            source.cpuHandlePruneAt=std::max<size_t>(64,handles.size()*2);
        }
        if(source.cpuHandles.size()>=65536) {
            cpu_.kill(source.cpuHandles.front()); // do not lose kill ownership
            source.cpuHandles.erase(source.cpuHandles.begin());
        }
        source.cpuHandles.push_back(cpuHandle);
    }
    return emitted;
}

bool GpuPoolWorld::burst(const std::string& effectName,fx::float3 position,
                         fx::float3 direction,fx::float4 tint,
                         const std::array<float,8>* sourceParams) {
    return burstImpl(effectName,position,direction,tint,sourceParams,0,nullptr);
}
bool GpuPoolWorld::burstImpl(const std::string& effectName,fx::float3 position,
                            fx::float3 direction,fx::float4 tint,
                            const std::array<float,8>* sourceParams,
                            uint64_t sourceToken,fx::FxHandle* cpuHandle) {
    const auto it=bindings_.find(effectName);
    if(!initialized_||presentationBlocked()||!frameOpen_||stepped_||
       it==bindings_.end()||
       stats_.frameBursts>=budgets_.maxBurstsPerFrame||
       !valid(position)||!valid(direction)||!valid(tint)) {
        ++stats_.rejectedBursts;
        reason_="invalid SpectraFX pool burst, missing frame/effect or frame budget reached";
        return false;
    }
    const Binding& binding=it->second;
    const auto& params=sourceParams?*sourceParams:binding.defaults;
    if(!validParams(params)) {
        ++stats_.rejectedBursts;
        reason_="nonfinite or unbounded SpectraFX pool instance parameters";
        return false;
    }
    for(const auto& [paramName,definition]:binding.named) {
        if(!checkInstanceParameterValue(definition,params[definition.slot])) {
            ++stats_.rejectedBursts;
            reason_="out-of-range SpectraFX pooled runtime parameter '"+paramName+"'";
            return false;
        }
    }
    bool ok=false;
    if(binding.gpu) {
        ComputeParticles& particles=*gpuGroups_[binding.gpuIndex].particles;
        ok=true;
        if(binding.usesRuntimeParams) {
            // Only affect future births: GPU custom attribute and parameter
            // snapshots already residing in this group remain unchanged.
            for(uint32_t i=0;i<8;++i)
                if(!particles.setProgramParameter(i,params[i],false)) {
                    ok=false;
                    reason_="failed to set pooled GPU per-source parameter";
                    break;
                }
        }
        if(ok)ok=particles.burstAt(position,direction,tint,sourceToken);
        if(!ok && reason_.empty())reason_=particles.reason();
    } else {
        const fx::FxHandle handle=cpu_.playWithProgramParameters(
            binding.cpuId,position,direction,1.f,tint,params);
        ok=handle.valid();
        if(ok && cpuHandle)*cpuHandle=handle;
        if(!ok)reason_="shared CPU pooled one-shot was rejected";
    }
    if(ok) {
        ++stats_.submittedBursts;
        ++stats_.frameBursts;
        reason_.clear();
    } else ++stats_.rejectedBursts;
    return ok;
}

bool GpuPoolWorld::beginPresentationFrame(const fx::SpectraFxView& view) {
    if(!initialized_||presentationBlocked()||frameOpen_||
       !view.valid()) {
        reason_="SpectraFX pool begin frame requires a healthy, unblocked world and viewport";
        return false;
    }
    // Frame boundary: earlier frames' commands are committed and nothing of
    // this frame references a retired group yet.
    retireUnusedGroups();
    frameOpen_=true;stepped_=drawn_=false;
    stats_.frameBursts=stats_.gpuDrawCalls=stats_.cpuDrawCalls=0;
    cpu_.setView(view);
    for(auto& group:gpuGroups_) {
        group.stepOk=false;
        if(group.live())group.particles->setView(view);
    }
    reason_.clear();
    return true;
}

void GpuPoolWorld::retireUnusedGroups() {
    if(groupsDirty_) {
        groupsDirty_=false;
        std::vector<uint8_t> used(gpuGroups_.size(),0u);
        for(const auto& [name,binding]:bindings_) {
            (void)name;
            if(binding.gpu && binding.gpuIndex<used.size())used[binding.gpuIndex]=1u;
        }
        // Stale-revision sources keep their old group (and its living
        // particles, selective kill) until they are retired.
        for(const Source& source:sources_)
            if(source.active && source.gpu && source.gpuIndex<used.size())
                used[source.gpuIndex]=1u;
        for(size_t i=0;i<gpuGroups_.size();++i)
            if(gpuGroups_[i].live() && !used[i]) {
                gpuGroups_[i].retired=true;
                gpuGroups_[i].stepOk=false;
            }
        stats_.gpuGroups=uint32_t(std::count_if(gpuGroups_.begin(),gpuGroups_.end(),
            [](const Group& group){return group.live();}));
    }
    if(!device_)return;
    for(auto& group:gpuGroups_) {
        if(!group.retired || !group.particles)continue;
        // Never destroy on elapsed frames: only a completed ordered GPU
        // marker (Gfx.h retirement contract) proves the old frames finished.
        // Unsupported devices (token 0) keep the idle group allocated.
        if(group.retireToken==0) {
            group.retireToken=device_->beginResourceRetirement();
            continue;
        }
        if(device_->resourceRetirementStatus(group.retireToken)!=
           gfx::ResourceRetirementStatus::Complete)continue;
        device_->endResourceRetirement(group.retireToken);
        group.retireToken=0;
        group.particles.reset();
        group.key.clear();
    }
}

void GpuPoolWorld::tickContinuous(float dt) {
    // Called once from step, before stepped_ is set. This scheduler is
    // rendering-only; it never integrates an authoritative gameplay state.
    const auto& policy=budgets_.loadShedding;
    const uint32_t remainingGlobal=budgets_.maxBurstsPerFrame>
        stats_.frameBursts ? budgets_.maxBurstsPerFrame-stats_.frameBursts:0;
    uint32_t budget=remainingGlobal;
    stats_.pressureActive=false;
    if(policy.enabled) {
        if(policy.maxContinuousBurstsPerFrame>0)
            budget=std::min(budget,policy.maxContinuousBurstsPerFrame);
        budget=std::min(budget,
            remainingGlobal>policy.reserveManualBursts ?
            remainingGlobal-policy.reserveManualBursts : 0u);
        const uint64_t occupancy=uint64_t(stats_.cpuParticles)+
                                 uint64_t(stats_.gpuParticlesEstimated);
        if(policy.estimatedParticleSoftLimit==0) {
            particlePressureLatched_=false;
        } else if(!particlePressureLatched_) {
            particlePressureLatched_=
                occupancy>=policy.estimatedParticleSoftLimit;
        } else if(occupancy<=releaseBelow(
                     policy.estimatedParticleSoftLimit,
                     policy.recoverBelowPercent)) {
            particlePressureLatched_=false;
        }
        const size_t readbackInFlight=checkpointBudget_?
            checkpointBudget_->stats().bytesInFlight:0;
        if(policy.maxReadbackBytesInFlight==0) {
            readbackPressureLatched_=false;
        } else if(!readbackPressureLatched_) {
            readbackPressureLatched_=
                readbackInFlight>=policy.maxReadbackBytesInFlight;
        } else if(readbackInFlight<=releaseBelow(
                     policy.maxReadbackBytesInFlight,
                     policy.recoverBelowPercent)) {
            readbackPressureLatched_=false;
        }
        if(particlePressureLatched_ || readbackPressureLatched_)budget=0;
        stats_.pressureActive=particlePressureLatched_ ||
            readbackPressureLatched_ || budget<remainingGlobal;
    } else {
        particlePressureLatched_=readbackPressureLatched_=false;
    }
    stats_.particlePressureLatched=particlePressureLatched_;
    stats_.readbackPressureLatched=readbackPressureLatched_;
    if(particlePressureLatched_ || readbackPressureLatched_)
        ++stats_.pressureHeldFrames;
    stats_.continuousFrameBudget=budget;
    uint32_t emittedTotal=0;
    const size_t count=sources_.size();
    const size_t first=count?nextContinuousSource_%count:0;
    if(count)nextContinuousSource_=(first+1)%count;
    // Rotating order prevents a busy lower-index source from always using
    // the entire shared continuous budget before later sources run.
    for(size_t scan=0;scan<count;++scan) {
        const uint32_t slot=uint32_t((first+scan)%count);
        Source& source=sources_[slot];
        if(!source.active)continue;
        if(source.paused || !source.transformAttached) {
            source.lastStepPosition=source.position;
            continue;
        }
        const auto& cfg=source.continuous;
        if(cfg.burstsPerSecond<=0.f && cfg.metresPerBurst<=0.f) {
            source.lastStepPosition=source.position;
            continue;
        }
        float frameDt=dt;
        if(cfg.lifetimeSeconds>0.f) {
            frameDt=std::min(dt,std::max(0.f,cfg.lifetimeSeconds-source.elapsed));
            source.elapsed=std::min(cfg.lifetimeSeconds,source.elapsed+dt);
        }
        if(frameDt<=0.f) {
            if(cfg.lifetimeSeconds>0.f &&
               source.elapsed>=cfg.lifetimeSeconds)source.paused=true;
            source.lastStepPosition=source.position;
            continue;
        }
        const PoolSourceId handle{sourceWorldEpoch_,slot,source.generation};
        if(!sourceActive(handle)) {
            source.paused=true; // a redefined binding cannot start new births
            source.lastStepPosition=source.position;
            continue;
        }
        const fx::float3 start=source.lastStepPosition;
        const fx::float3 end=source.position;
        const fx::float3 displacement=(end-start)*
            (dt>0.f?frameDt/dt:0.f);
        const float distance=render::length(displacement);
        if(!valid(distance)) {
            source.lastStepPosition=source.position;
            continue;
        }
        const double rateTotal=double(source.accumulatedRate)+
                               double(frameDt)*cfg.burstsPerSecond;
        const uint32_t rateCount=uint32_t(std::min(
            std::floor(rateTotal),double(std::numeric_limits<uint32_t>::max())));
        source.accumulatedRate=float(rateTotal-std::floor(rateTotal));
        uint32_t distanceCount=0;
        const float oldDistance=source.accumulatedDistance;
        if(cfg.metresPerBurst>0.f) {
            const double total=double(oldDistance)+double(distance);
            const double num=std::floor(total/double(cfg.metresPerBurst));
            distanceCount=uint32_t(std::min(
                num,double(std::numeric_limits<uint32_t>::max())));
            source.accumulatedDistance=float(std::fmod(total,
                double(cfg.metresPerBurst)));
        } else source.accumulatedDistance=0.f;
        // Discard excess rather than accumulating an unbounded catch-up queue.
        const uint64_t wanted=uint64_t(rateCount)+distanceCount;
        const uint32_t globalRemaining=budgets_.maxBurstsPerFrame>
            stats_.frameBursts ? budgets_.maxBurstsPerFrame-stats_.frameBursts:0;
        const uint32_t baseLimit=std::min(cfg.maxBurstsPerFrame,globalRemaining);
        const uint32_t adjustedLimit=std::min(
            baseLimit,budget>emittedTotal?budget-emittedTotal:0u);
        const uint32_t baseEligible=uint32_t(
            std::min<uint64_t>(wanted,baseLimit));
        const uint32_t eligible=uint32_t(
            std::min<uint64_t>(wanted,adjustedLimit));
        if(policy.enabled && baseEligible>eligible)
            stats_.pressureSkipped+=baseEligible-eligible;
        stats_.continuousSkipped+=wanted-eligible;
        for(uint32_t i=0;i<eligible;++i) {
            fx::float3 pos=end;
            if(i>=rateCount && cfg.metresPerBurst>0.f && distance>1e-6f) {
                const uint32_t j=i-rateCount;
                const float along=cfg.metresPerBurst-oldDistance+
                    float(j)*cfg.metresPerBurst;
                pos=start+displacement*std::clamp(along/distance,0.f,1.f);
            }
            if(emitSource(handle,pos,source.direction,source.tint)) {
                ++stats_.continuousBursts;
                ++emittedTotal;
            } else {
                // No retries this frame; an exhausted group must not delay
                // simulation or another source's emission.
                stats_.continuousSkipped+=eligible-i;
                break;
            }
        }
        source.lastStepPosition=source.position;
        if(cfg.lifetimeSeconds>0.f &&
           source.elapsed>=cfg.lifetimeSeconds)source.paused=true;
    }
}

bool GpuPoolWorld::step(float dt) {
    if(!initialized_||presentationBlocked()||!frameOpen_||
       stepped_||!valid(dt)||dt<0.f||dt>1.f) {
        reason_="SpectraFX pool step must run once before render with dt in [0,1]";
        return false;
    }
    tickContinuous(dt);
    stepped_=true;
    cpu_.update(dt);
    bool allOk=true;
    // Checkpoint cadence is per GPU group. Rotating the simulation submission
    // order gives later groups a chance when the shared readback budget fills.
    const size_t groupCount=gpuGroups_.size();
    const size_t first=groupCount?nextGroupStart_%groupCount:0;
    if(groupCount)nextGroupStart_=(first+1)%groupCount;
    for(size_t j=0;j<groupCount;++j) {
        auto& group=gpuGroups_[(first+j)%groupCount];
        if(!group.live()) {group.stepOk=false;continue;}
        group.stepOk=group.particles->step(dt);
        if(!group.stepOk) {
            allOk=false;
            reason_="GPU pool group step: "+group.particles->reason();
        }
    }
    stats_.gpuParticlesEstimated=0;
    stats_.gpuBytes=0;
    for(const auto& group:gpuGroups_) {
        if(!group.live())continue;
        const auto& stats=group.particles->stats();
        stats_.gpuParticlesEstimated+=stats.alive;
        stats_.gpuBytes+=stats.gpuBytes;
    }
    stats_.cpuParticles=uint32_t(cpu_.stats().particles);
    return allOk;
}

void GpuPoolWorld::draw(const fx::FxFog& fog) {
    if(!initialized_||presentationBlocked()||
       !frameOpen_||!stepped_||drawn_)return;
    drawn_=true;
    cpu_.draw(fog);
    stats_.cpuDrawCalls=uint32_t(cpu_.stats().drawCalls);
    stats_.gpuDrawCalls=0;
    for(auto& group:gpuGroups_) {
        if(!group.live() || !group.stepOk)continue; // never replay stale last-frame vertices
        group.particles->draw(fog);
        stats_.gpuDrawCalls+=group.particles->stats().drawCalls;
    }
}

void GpuPoolWorld::endPresentationFrame() {
    frameOpen_=stepped_=drawn_=false;
}

bool GpuPoolWorld::resetForRollback() {
    if(!initialized_) {
        reason_="SpectraFX rollback requires an initialized pool";
        return false;
    }
    // Any attempted rollback becomes an explicit fail-closed reset gate.
    // An unaccepted reset must never allow old particle emissions to resume.
    if(!resetReport_.retryRequired) {
        resetAccepted_.assign(gpuGroups_.size(),0u);
        resetReport_.groupsAccepted=0;
    }
    resetReport_.retryRequired=true;
    resetReport_.groupCount=uint32_t(gpuGroups_.size());
    resetReport_.failedGroup=std::numeric_limits<uint32_t>::max();
    ++resetReport_.attempts;

    if(!device_ || !device_->gpuHealthy()) {
        resetReport_.deviceLost=true;
        reason_="SpectraFX graphics Device lost; do not replay ECS VFX. "
                "Shutdown this pool while old Device still lives, then rebuild "
                "the effect definitions on a recovered graphics Device";
        return false;
    }
    // A previously LOST device cannot be assumed recovered simply because
    // gpuHealthy later flipped true. The native host must rebuild resources.
    if(resetReport_.deviceLost) {
        reason_="SpectraFX GPU resources were lost; explicit renderer recreation required";
        return false;
    }
    if(frameOpen_ || !device_->canUpdateSharedBuffers()) {
        reason_="SpectraFX rollback must execute inside Device::beginFrame, "
                "outside render/compute encoders";
        return false;
    }

    // All groups use one graphics queue, but each checked reset comprises
    // ordered RHI writes. A failed later group cannot undo an earlier group's
    // accepted writes; the accepted mask makes retries resume safely.
    for(size_t i=0;i<gpuGroups_.size();++i) {
        auto& group=gpuGroups_[i];
        group.stepOk=false;
        if(resetAccepted_[i])continue;
        if(!group.live()) { // retired: never drawn again, nothing to clear
            resetAccepted_[i]=1u;
            ++resetReport_.groupsAccepted;
            continue;
        }
        if(!group.particles->tryClear()) {
            resetReport_.failedGroup=uint32_t(i);
            reason_="SpectraFX pooled rollback partially prepared ("+
                std::to_string(resetReport_.groupsAccepted)+"/"+
                std::to_string(resetReport_.groupCount)+
                " groups accepted), failed group "+std::to_string(i)+": "+
                group.particles->reason();
            return false;
        }
        resetAccepted_[i]=1u;
        ++resetReport_.groupsAccepted;
    }

    // Only after ALL GPU reset commands are accepted do we invalidate
    // presentation-owned CPU effect handles and ECS source generations.
    cpu_.clear();
    stats_.frameBursts=stats_.gpuDrawCalls=stats_.cpuDrawCalls=0;
    stats_.cpuParticles=stats_.gpuParticlesEstimated=0;
    for(uint32_t i=0;i<uint32_t(sources_.size());++i) {
        Source& source=sources_[i];
        if(!source.active)continue;
        source.active=false;source.paused=false;
        source.effectName.clear();source.bindingRevision=0;
        source.values={};source.token=0;source.cpuHandles.clear();
        source.continuous={};source.transformAttached=false;
        if(!source.exhausted)freeSourceSlots_.push_back(i);
    }
    stats_.activeSources=0;
    groupsDirty_=true;
    resetReport_.retryRequired=false;
    resetReport_.failedGroup=std::numeric_limits<uint32_t>::max();
    reason_.clear();
    return true;
}

PoolRecoveryManifest GpuPoolWorld::exportRecoveryManifest() const {
    PoolRecoveryManifest manifest;
    manifest.budgets=budgets_;
    // The old per-device readback scheduler belongs to the old Device.
    // Import always provisions a fresh checkpoint budget.
    manifest.budgets.sharedCheckpointBudget.reset();
    for(const auto& [name,definition]:definitions_) {
        (void)name;
        manifest.effects.push_back(definition);
    }
    return manifest;
}

bool GpuPoolWorld::importRecoveryManifest(
    gfx::Device* recoveredDevice,const PoolRecoveryManifest& manifest) {
    if(!recoveredDevice || !recoveredDevice->gpuHealthy()) {
        reason_="SpectraFX recovery needs a new healthy graphics Device";
        return false;
    }
    // The caller must use a NEW pool and re-create the ECS presenter after
    // this import. Old GPU resources and renderer source handles cannot
    // be resurrected from a CPU-only authored-effect catalog.
    if(initialized_ || !gpuGroups_.empty() || !bindings_.empty()) {
        reason_="SpectraFX importRecoveryManifest requires a fresh uninitialized pool";
        return false;
    }
    PoolWorldBudgets requested=manifest.budgets;
    requested.sharedCheckpointBudget.reset();
    if(!init(recoveredDevice,requested))return false;
    for(const auto& entry:manifest.effects) {
        if(!defineCompiled(entry.name,entry.effect,entry.options,entry.named)) {
            const std::string why="SpectraFX failed to rebuild effect '"+entry.name+
                "' on recovered Device: "+reason_;
            shutdown();
            reason_=why;
            return false; // no partial renderer is publicly usable
        }
        auto live=bindings_.find(entry.name);
        if(live!=bindings_.end())live->second.defaults=entry.defaults;
        auto stored=definitions_.find(entry.name);
        if(stored!=definitions_.end())stored->second.defaults=entry.defaults;
    }
    reason_.clear();
    return true;
}

bool GpuPoolWorld::requestGpuCheckpoint(
    size_t groupIndex,std::function<void(bool)> completion) {
    if(!initialized_ || presentationBlocked() ||
       groupIndex>=gpuGroups_.size() ||
       !gpuGroups_[groupIndex].live() ||
       !gpuGroups_[groupIndex].particles->gpuActive()) {
        reason_="SpectraFX GPU checkpoint request requires an active healthy group";
        return false;
    }
    if(!gpuGroups_[groupIndex].particles->requestGpuCheckpoint(
            std::move(completion))) {
        reason_="SpectraFX GPU checkpoint readback is pending, not ready or "
                "outside a safe graphics frame";
        return false;
    }
    reason_.clear();
    return true;
}

bool GpuPoolWorld::exportFreshCheckpoint(
    size_t groupIndex,std::vector<uint8_t>& bytes,
    size_t maxBytes,std::string* error) const {
    if(presentationBlocked() || !initialized_ ||
       groupIndex>=gpuGroups_.size()) {
        if(error)*error="SpectraFX checkpoint export needs a healthy active pool and valid group";
        return false;
    }
    const auto& group=gpuGroups_[groupIndex];
    if(!group.live() || !group.particles->gpuActive()) {
        if(error)*error="SpectraFX checkpoint group has no Metal GPU simulation";
        return false;
    }
    return group.particles->exportFreshCheckpoint(bytes,maxBytes,error);
}

bool GpuPoolWorld::gpuGroupSummary(
    size_t index,PoolGpuGroupSummary& result) const {
    if(!initialized_ || index>=gpuGroups_.size() ||
       !gpuGroups_[index].live())return false;
    const Group& group=gpuGroups_[index];
    PoolGpuGroupSummary next;
    next.index=index;
    next.gpuActive=group.particles->gpuActive();
    next.stepAccepted=group.stepOk;
    next.checkpoint=group.particles->checkpointStatus();
    next.estimatedAlive=group.particles->stats().alive;
    next.allocatedGpuBytes=group.particles->stats().gpuBytes;
    next.resetAccepted=index<resetAccepted_.size() &&
                       resetAccepted_[index]!=0;
    next.exportReady=!presentationBlocked() && next.gpuActive &&
        next.checkpoint.restoreEligible && !next.checkpoint.pending &&
        device_ && device_->gpuHealthy();
    result=std::move(next);
    return true;
}

PoolCheckpointStats GpuPoolWorld::checkpointStats() const {
    PoolCheckpointStats result;
    if(checkpointBudget_)result.budget=checkpointBudget_->stats();
    result.gpuDeviceHealthKnown=device_ &&
        device_->info().backend==gfx::Backend::Metal &&
        device_->supportsCompute();
    if(result.gpuDeviceHealthKnown)
        result.gpuDeviceHealthy=device_->gpuHealthy();
    for(const auto& group:gpuGroups_) {
        if(!group.live())continue;
        const GpuCheckpointStatus state=group.particles->checkpointStatus();
        const ComputeStats& stats=group.particles->stats();
        result.requests+=stats.checkpointRequests;
        result.completed+=stats.checkpointCompleted;
        result.failed+=stats.checkpointFailed;
        result.budgetDenied+=stats.checkpointBudgetDenied;
        result.adaptiveBackoffs+=stats.checkpointAdaptiveBackoffs;
        result.adaptiveRecoveries+=stats.checkpointAdaptiveRecoveries;
        result.killedSlotsAwaitingFence+=state.killsAwaitingGpuFence;
        result.killedSlotsReclaimed+=stats.killedSlotsReclaimed;
        result.gpuDeviceUnhealthy+=stats.gpuDeviceUnhealthy;
        result.gpuKillUploadFailures+=stats.gpuKillUploadFailures;
        result.gpuStagingUploadFailures+=stats.gpuStagingUploadFailures;
        result.gpuComputeBeginFailures+=stats.gpuComputeBeginFailures;
        result.gpuResetFailures+=stats.gpuResetFailures;
        result.gpuPassOrderRejections+=stats.gpuPassOrderRejections;
        result.killScatterDispatches+=stats.gpuKillScatterDispatches;
        result.killIndexBytes+=stats.gpuKillIndexBytes;
        result.maximumCurrentIntervalFrames=std::max(
            result.maximumCurrentIntervalFrames,state.checkpointIntervalFrames);
        result.bytesReturned+=stats.checkpointBytesReturned;
        if(!state.gpuActive)continue;
        ++result.activeGroups;
        if(state.pending)++result.pendingGroups;
        if(state.stagingRecoveryPending)++result.stagingRecoveryGroups;
        if(!state.complete)++result.missingGroups;
        else if(state.fresh)++result.freshGroups;
        else ++result.staleGroups;
        if(state.restoreEligible)++result.restoreEligibleGroups;
        if(!presentationBlocked() && state.restoreEligible && !state.pending &&
           device_ && device_->gpuHealthy())
            ++result.exportReadyGroups;
    }
    return result;
}

bool GpuPoolWorld::isGpuEffect(const std::string& name) const {
    const auto it=bindings_.find(name);
    return it!=bindings_.end() && it->second.gpu &&
        it->second.gpuIndex<gpuGroups_.size() &&
        gpuGroups_[it->second.gpuIndex].live() &&
        gpuGroups_[it->second.gpuIndex].particles->gpuActive();
}

} // namespace aiverse::spectra
