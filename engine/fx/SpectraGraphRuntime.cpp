#include "SpectraGraphRuntime.h"
#include "SpectraGpuProgram.h"
#include "SpectraGpuMaterial.h"
#include <utility>

namespace aiverse::spectra {

GraphGpuEligibility inspectGraphGpuEligibility(
    const fx::EffectDef& effect, const GraphRuntimeOptions& options) {
    GraphGpuEligibility result;
    result.gpu.capacity=options.capacity;
    result.gpu.quality=options.quality;
    result.gpu.backend=options.preference;
    result.gpu.gpuIndirect=options.gpuIndirect;
    result.gpu.gpuProgram=options.gpuProgram;
    result.gpu.gpuMaterial=options.gpuMaterial;
    result.gpu.checkpointEveryFrames=options.checkpointEveryFrames;
    result.gpu.adaptiveCheckpoint=options.adaptiveCheckpoint;
    result.gpu.checkpointBudget=options.checkpointBudget;

    const auto reject=[&](const char* why) {
        result.reason=why;
        return result;
    };
    if(effect.emitters.size()!=1)
        return reject("GPU v1 needs exactly one sprite emitter");
    if(!effect.forceFields.empty())
        return reject("GPU v1 has no effect force fields");

    const fx::EmitterDef& e=effect.emitters[0];
    const bool hasMaterial=!e.shaderGraph.isNull()||!e.graphInputs.isNull();
    if(hasMaterial) {
        if(!options.gpuMaterial || e.shaderGraph.isNull())
            return reject("shaderGraph requires opt-in gpuMaterial and a complete particle material");
        gpu_material::Material compiled;
        std::string why;
        if(!gpu_material::compile(e.shaderGraph,e.graphInputs,compiled,why)) {
            result.reason=why.empty()?"GPU shaderGraph unsupported":why;
            return result;
        }
    }
    // VM capability is opt-in and must cover the entire program. A prepared
    // payload alone never implies a passing real Metal GPU shader test.
    if(!e.particleProgram.isNull()) {
        if(!options.gpuProgram)
            return reject("particle VM needs explicit GPU program opt-in; CPU fallback");
        gpu_program::Payload payload;
        std::string why;
        if(!gpu_program::prepareForMetal(e.particleProgram,payload,why)) {
            result.reason=why.empty()?"GPU VM cannot execute this program":why;
            return result;
        }
    }
    // Nothing is inferred from a node's name or visual layout. We require a
    // materially identical EffectDef, not just a compatible-looking output.
    if(e.burst<=0||e.burst>64)
        return reject("GPU v1 supports only one-shot bursts of 1..64 particles");
    if(!e.life.constant()||!e.speed.constant()||!e.size.constant()||!e.drag.constant())
        return reject("GPU v1 cannot execute random ranges or curves");
    if(e.texture!="builtin:soft"||e.blend!=fx::FxBlend::Additive||
       e.align!=fx::FxAlign::Camera||e.shape!=fx::FxShape::Point)
        return reject("GPU v1 supports only additive soft camera-facing point sprites");
    if(e.hidden || e.noise.enabled() ||
       e.collision.enabled || e.trail.enabled || e.light.enabled ||
       e.trigger.enabled || !e.subEmitters.empty() || e.force.enabled ||
       e.velocityOverLife.enabled || e.inheritVelocity.enabled ||
       !e.meshes.empty() || !e.sprites.empty())
        return reject("GPU v1 cannot execute custom shaders, modules, trails or sub-effects");
    if(!e.life.curveMin.empty() || !e.speed.curveMin.empty() ||
       !e.size.curveMin.empty() || !e.drag.curveMin.empty())
        return reject("GPU v1 cannot execute MinMaxCurve variants");
    if(e.spread!=0.0f)
        return reject("GPU v1 has no spread/random direction");
    if(e.gravity.x!=0.0f||e.gravity.z!=0.0f)
        return reject("GPU v1 implements only world Y gravity");

    // GPU fading is hardcoded to linear white→transparent. Verify all color
    // keys exactly; rounding in JSON serialization alone must not make an
    // authored custom gradient eligible by accident.
    const fx::EmitterDef baseline;
    if(e.color.size()!=baseline.color.size())
        return reject("GPU v1 requires the default white alpha fade");
    for(size_t i=0;i<e.color.size();++i) {
        const auto& a=e.color[i],&b=baseline.color[i];
        if(a.t!=b.t || a.rgba.x!=b.rgba.x || a.rgba.y!=b.rgba.y ||
           a.rgba.z!=b.rgba.z || a.rgba.w!=b.rgba.w)
            return reject("GPU v1 requires the default white alpha fade");
    }

    // Compare the COMPLETE normalized effect schema with a fresh emitter
    // containing only fields actually implemented by the GPU kernel. This
    // rejects nested modules, texture animation, custom colors, spawn delay,
    // local-space motion, trails, lights, custom shaders, etc. rather than
    // dropping them silently. We also check all controlling numeric fields
    // before conversion to the GPU config, not just their JSON rendering.
    fx::EmitterDef supported;
    supported.burst=e.burst;
    supported.life=e.life;
    supported.speed=e.speed;
    supported.size=e.size;
    supported.spread=0;
    supported.gravity=e.gravity;
    supported.drag=e.drag;
    // A full-program eligibility check above has already validated every
    // operation/stage. Keep it in the normalized effect comparison so other
    // authored emitter fields cannot disappear.
    supported.particleProgram=e.particleProgram;
    if(hasMaterial) {
        supported.shaderGraph=e.shaderGraph;
        supported.graphInputs=e.graphInputs;
    }
    fx::EffectDef limited;
    limited.emitters.push_back(std::move(supported));
    if(!(fx::effectToJson(effect)==fx::effectToJson(limited)))
        return reject("GPU v1 cannot preserve every emitter module; use the full CPU effect");

    result.gpu.burst=uint32_t(e.burst);
    result.gpu.lifetime=e.life.min;
    result.gpu.speed=e.speed.min;
    result.gpu.size=e.size.min;
    result.gpu.gravityY=e.gravity.y;
    result.gpu.drag=e.drag.min;
    result.gpu.particleProgram=e.particleProgram;
    if(hasMaterial) {
        result.gpu.shaderGraph=e.shaderGraph;
        result.gpu.graphInputs=e.graphInputs;
    }
    std::string why;
    if(!validComputeEmitter(result.gpu,&why)) {
        result.reason="GPU v1 configuration: "+why;
        return result;
    }
    result.eligible=true;
    return result;
}

bool GraphRuntime::initGraph(gfx::Device* dev,const Json& graph,
                             const Json& overrides,const GraphRuntimeOptions& options) {
    shutdown();
    InstanceParameterTable schema;
    DataInterfaceBindings bindings;
    if(graph.getInt("version")==2 &&
       (!loadInstanceParameterTable(graph["instanceParameters"],schema,errors_) ||
        !loadDataInterfaceBindings(graph["dataInterfaces"],schema,bindings,errors_))) {
        reason_=errors_.empty()?"invalid graph runtime bindings":errors_.front();
        return false;
    }
    fx::EffectDef compiled;
    if(!fx::compileSpectraGraph(graph,overrides,compiled,errors_)) {
        reason_="SpectraFX graph compilation failed";
        return false;
    }
    const bool canServeNamedOnGpu=compiled.emitters.size()==1 &&
        ((options.gpuProgram && !compiled.emitters[0].particleProgram.isNull()) ||
         (options.gpuMaterial && !compiled.emitters[0].shaderGraph.isNull()));
    if(!schema.empty() && !canServeNamedOnGpu &&
       options.preference==ComputePreference::RequireGpu) {
        reason_="runtime instance parameters need an enabled GPU VM program; RequireGpu rejected";
        return false;
    }
    GraphRuntimeOptions actual=options;
    if(!schema.empty() && !canServeNamedOnGpu)
        actual.preference=ComputePreference::CpuOnly;
    if(!initEffect(dev,compiled,actual))return false;
    namedParams_=std::move(schema);
    dataBindings_=std::move(bindings);
    for(const auto& [name,definition]:namedParams_) {
        (void)name;
        if(!setProgramParameter(definition.slot,definition.initial,false))return false;
    }
    return true;
}

bool GraphRuntime::initGraph(BatchWorld& shared, const Json& graph,
                            const Json& overrides,
                            const GraphRuntimeOptions& options) {
    shutdown();
    InstanceParameterTable schema;
    DataInterfaceBindings bindings;
    if(graph.getInt("version")==2 &&
       (!loadInstanceParameterTable(graph["instanceParameters"],schema,errors_) ||
        !loadDataInterfaceBindings(graph["dataInterfaces"],schema,bindings,errors_))) {
        reason_=errors_.empty()?"invalid graph runtime bindings":errors_.front();
        return false;
    }
    fx::EffectDef compiled;
    if (!fx::compileSpectraGraph(graph, overrides, compiled, errors_)) {
        reason_ = errors_.empty() ? "SpectraFX graph compile failed" : errors_.front();
        return false;
    }
    if(!initEffect(shared,compiled,options))return false;
    namedParams_=std::move(schema);
    dataBindings_=std::move(bindings);
    for(const auto& [name,definition]:namedParams_) {
        (void)name;
        if(!setProgramParameter(definition.slot,definition.initial,false))return false;
    }
    return true;
}

bool GraphRuntime::initEffect(BatchWorld& shared, const fx::EffectDef& effect,
                             const GraphRuntimeOptions& options) {
    shutdown();
    if (options.preference == ComputePreference::RequireGpu) {
        reason_ = "shared cross-instance batching currently uses the CPU ParticleSystem; "
                  "RequireGpu cannot silently fall back";
        return false;
    }
    if (!shared.ready()) {
        reason_ = "SpectraFX shared BatchWorld is not initialized";
        return false;
    }
    const int id = shared.defineEffect(effect);
    if (id < 0) {
        reason_ = shared.error();
        return false;
    }
    sharedWorld_ = &shared;
    cpuEffect_ = id;
    backend_ = ComputeBackend::Cpu;
    initialized_ = true;
    return true;
}

bool GraphRuntime::initEffect(gfx::Device* dev,const fx::EffectDef& effect,
                              const GraphRuntimeOptions& options) {
    shutdown();
    const GraphGpuEligibility eligibility=inspectGraphGpuEligibility(effect,options);
    const bool requestedGpu=options.preference!=ComputePreference::CpuOnly;
    const bool canMetal=dev &&
        chooseComputeBackend(dev->info().backend,dev->supportsCompute(),
                             options.preference)==ComputeBackend::Metal;
    if(requestedGpu && eligibility.eligible && canMetal &&
       options.quality>=fx::FxQuality::Medium) {
        if(gpu_.init(dev,eligibility.gpu) && gpu_.gpuActive()) {
            useComputeWrapper_=true;
            backend_=ComputeBackend::Metal;
            initialized_=true;
            reason_.clear();
            return true;
        }
        reason_="SpectraFX Metal initialization failed: "+gpu_.reason();
        gpu_.shutdown();
    } else if(requestedGpu && !eligibility.eligible) {
        reason_=eligibility.reason;
    } else if(requestedGpu) {
        reason_="Metal compute unavailable or GPU quality is below Medium";
    } else {
        reason_="CPU explicitly selected";
    }
    if(options.preference==ComputePreference::RequireGpu) {
        if(reason_.empty())reason_="GPU required but not eligible";
        return false;
    }
    // Exact original EffectDef is used here. Do not pass it through
    // ComputeParticles' simplified fallback emitter, which would discard
    // unsupported modules.
    if(!initCpu(dev,effect,options.quality))return false;
    initialized_=true;backend_=ComputeBackend::Cpu;
    return true;
}

bool GraphRuntime::initCpu(gfx::Device* device,const fx::EffectDef& effect,
                           fx::FxQuality quality) {
    cpu_.setQuality(quality);
    if(!cpu_.init(device)) {
        reason_+="; CPU ParticleSystem::init: "+cpu_.lastError();
        return false;
    }
    cpuEffect_=cpu_.define("__spectrafx_graph_instance",effect);
    if(cpuEffect_<0) {
        reason_+="; CPU ParticleSystem rejected compiled effect";
        cpu_.shutdown();
        return false;
    }
    return true;
}

void GraphRuntime::shutdown() {
    if (sharedWorld_) {
        for (const auto h : sharedHandles_) sharedWorld_->kill(h);
    }
    sharedHandles_.clear();
    cpuHandles_.clear();
    paramValues_={};
    paramSet_={};
    namedParams_.clear();
    dataBindings_.clear();
    dataRegistry_=nullptr;
    dataStats_={};
    sharedWorld_ = nullptr;
    gpu_.shutdown();
    cpu_.shutdown();
    cpuEffect_=-1;
    backend_=ComputeBackend::Cpu;
    useComputeWrapper_=false;
    initialized_=false;
    reason_.clear();
    errors_.clear();
}
bool GraphRuntime::setProgramParameter(uint32_t slot,float value,
                                       bool affectExisting) {
    if(!initialized_ || slot>=8 || !spectra::program::number(value)) {
        reason_="SpectraFX per-source VM parameters need an initialized runtime and finite scalar";
        return false;
    }
    if(useComputeWrapper_) {
        const bool ok=gpu_.setProgramParameter(slot,value,affectExisting);
        if(!ok)reason_="Metal VM parameter not supported by this GPU effect";
        else {
            // Mirror the accepted GPU uniform so Data Interface retries and
            // rollback use actual prior values rather than stale CPU zeros.
            paramValues_[slot]=value;
            paramSet_[slot]=true;
            reason_.clear();
        }
        return ok;
    }
    paramValues_[slot]=value;
    paramSet_[slot]=true;
    if(affectExisting) {
        if(sharedWorld_) {
            for(const auto h:sharedHandles_)
                (void)sharedWorld_->setProgramParameter(h,slot,value,true);
        } else {
            for(const auto h:cpuHandles_)
                (void)cpu_.setProgramParameter(h,slot,value,true);
        }
    }
    reason_.clear();
    return true;
}
bool GraphRuntime::setNamedProgramParameter(const std::string& name,float value,
                                            bool affectExisting) {
    const auto it=namedParams_.find(name);
    if(it==namedParams_.end()) {
        reason_="unknown SpectraFX instance parameter '"+name+"'";
        return false;
    }
    if(!checkInstanceParameterValue(it->second,value)) {
        reason_="out-of-range SpectraFX instance parameter '"+name+"'";
        return false;
    }
    return setProgramParameter(it->second.slot,value,affectExisting);
}
bool GraphRuntime::applyDataInterfaces(fx::float3 position) {
    if(dataBindings_.empty())return true;
    std::array<float,8> prepared{};
    std::string error;
    DataInterfaceSampleStats sampleStats;
    if(!sampleDataInterfaces(dataBindings_,namedParams_,dataRegistry_,
                             position,paramValues_,prepared,error,&sampleStats)) {
        dataStats_=sampleStats;
        reason_=std::move(error);
        return false;
    }
    // All provider reads and range checks have succeeded before touching
    // GPU/CPU uniform storage or spawning particles. If the backend rejects
    // a slot, restore earlier slots best-effort and refuse this burst.
    const auto old=paramValues_;
    for(const auto& [name,b]:dataBindings_) {
        (void)b;
        const uint32_t slot=namedParams_.at(name).slot;
        if(!setProgramParameter(slot,prepared[slot],false)) {
            const std::string backendError=reason_;
            for(const auto& [rollbackName,ignored]:dataBindings_) {
                (void)ignored;
                const uint32_t prior=namedParams_.at(rollbackName).slot;
                if(prior==slot)break;
                (void)setProgramParameter(prior,old[prior],false);
            }
            reason_="SpectraFX dataInterfaces: backend rejected parameter '"+name+"': "+backendError;
            return false;
        }
    }
    dataStats_=sampleStats;
    return true;
}
bool GraphRuntime::burstAt(fx::float3 position,fx::float3 dir,fx::float4 tint) {
    if(!initialized_)return false;
    if(!applyDataInterfaces(position))return false;
    if(sharedWorld_) {
        const fx::FxHandle handle=sharedWorld_->playWithProgramParameters(
            cpuEffect_,position,dir,1.f,tint,paramValues_);
        if(!handle.valid())return false;
        // Bound bookkeeping even for very high event rates; prevent old
        // continuous emitters from escaping this source's reset ownership.
        if(sharedHandles_.size()>=65536) {
            sharedWorld_->kill(sharedHandles_.front());
            sharedHandles_.pop_front();
        }
        sharedHandles_.push_back(handle);
        return true;
    }
    if(useComputeWrapper_)return gpu_.burstAt(position,dir,tint);
    const fx::FxHandle handle=cpu_.playWithProgramParameters(
        cpuEffect_,position,dir,1.f,tint,paramValues_);
    if(!handle.valid())return false;
    if(cpuHandles_.size()>=65536) {
        cpu_.kill(cpuHandles_.front());
        cpuHandles_.pop_front();
    }
    cpuHandles_.push_back(handle);
    return true;
}
void GraphRuntime::setView(const fx::SpectraFxView& view) {
    if(!initialized_||sharedWorld_)return; // host sets view ONCE on BatchWorld
    if(useComputeWrapper_)gpu_.setView(view);
    else cpu_.setView(view);
}
bool GraphRuntime::step(float dt) {
    if(!initialized_)return false;
    if(sharedWorld_)return true; // host calls sharedWorld_->update(dt) exactly once
    if(!useComputeWrapper_) {cpu_.update(dt);return true;}
    const bool success=gpu_.step(dt);
    // This wrapper owns any subsequent CPU fallback. Report the real state,
    // including RequireGpu failure and a lost-device transition.
    backend_=gpu_.backend();
    initialized_=gpu_.ready();
    if(!success) reason_=gpu_.reason();
    return success;
}
void GraphRuntime::draw(const fx::FxFog& fog) {
    if(!initialized_||sharedWorld_)return; // host makes ONE batched draw submission
    if(useComputeWrapper_)gpu_.draw(fog);
    else cpu_.render(fog);
}
bool GraphRuntime::resetDeterministicSeed(uint32_t seed) {
    if(!initialized_ || sharedWorld_ || useComputeWrapper_) {
        reason_="Deterministic replay seed requires a dedicated CPU ParticleSystem";
        return false;
    }
    cpu_.resetDeterministicSeed(seed);
    cpuHandles_.clear();
    reason_.clear();
    return true;
}

void GraphRuntime::clear() {
    if(!initialized_)return;
    if(sharedWorld_) {
        // Only this graph's instances disappear; all other GraphRuntime
        // handles and particles in the shared world remain untouched.
        for(const auto h:sharedHandles_)sharedWorld_->kill(h);
        sharedHandles_.clear();
        return;
    }
    if(useComputeWrapper_)gpu_.clear();
    else {
        cpu_.clear();
        cpuHandles_.clear();
    }
}

} // namespace aiverse::spectra
