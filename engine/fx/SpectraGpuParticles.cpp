#include "SpectraGpuParticles.h"
#include "SpectraGpuShaders.h"
#include "SpectraGpuFramePlan.h"
#include "SpectraGpuProgram.h"
#include "SpectraGpuMaterial.h"
#include "ParticleShaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace aiverse::spectra {
namespace {
bool finite(float x) {return std::isfinite(x);}
bool finite(fx::float3 v) {return finite(v.x)&&finite(v.y)&&finite(v.z);}
bool finite(fx::float4 v) {return finite(v.x)&&finite(v.y)&&finite(v.z)&&finite(v.w);}
gfx::BufferHandle buffer(gfx::Device& d,gfx::BufferType type,size_t size,
                         const void* data,const char* label) {
    gfx::BufferDesc desc;
    desc.type=type;desc.usage=gfx::Usage::Immutable;desc.size=size;
    desc.data=data;desc.label=label;
    return d.createBuffer(desc);
}
uint8_t byte(float x) {
    return uint8_t(std::clamp(int(std::lround(std::clamp(x,0.0f,1.0f)*255.f)),0,255));
}
} // namespace

ComputeBackend chooseComputeBackend(gfx::Backend backend,bool computeAvailable,
                                    ComputePreference preference) {
    if(preference==ComputePreference::CpuOnly||!computeAvailable)return ComputeBackend::Cpu;
    if(backend==gfx::Backend::Metal)return ComputeBackend::Metal;
    if(backend==gfx::Backend::Vulkan)return ComputeBackend::Vulkan;
    return ComputeBackend::Cpu;
}
bool supportsSpectraCompute(const gfx::Device& device,bool ownedAllocation){
    if(!device.supportsCompute())return false;
    if(device.info().backend==gfx::Backend::Metal)return true;
    if(device.info().backend!=gfx::Backend::Vulkan)return false;
    return device.supportsStorageUniformCompute(ownedAllocation?19u:15u)&&
        (!ownedAllocation||device.supportsGpuAuthoredIndirectDraw());
}
bool validComputeEmitter(const ComputeEmitterDesc& d,std::string* why) {
    const auto fail=[&](const char* msg){if(why)*why=msg;return false;};
    if(d.capacity==0||d.capacity>kGpuEmitterMaxSlots)return fail("GPU emitter capacity exceeds allocation safety ceiling");
    if(d.burst==0||d.burst>64)return fail("GPU emitter burst must be 1..64");
    if(d.lifetime<0.01f||d.lifetime>60.f||!finite(d.lifetime))return fail("invalid lifetime");
    if(d.speed<0.f||d.speed>500.f||!finite(d.speed))return fail("invalid speed");
    if(d.size<=0.f||d.size>100.f||!finite(d.size))return fail("invalid size");
    if(!finite(d.gravityY)||std::abs(d.gravityY)>1000.f)return fail("invalid gravity");
    if(!finite(d.drag)||d.drag<0||d.drag>50.f)return fail("invalid drag");
    if(d.burst>d.capacity)return fail("one burst exceeds available slots");
    if(why)why->clear();
    return true;
}

bool ComputeParticles::init(gfx::Device* dev,const ComputeEmitterDesc& config) {
    shutdown();
    desc_=config;
    shaderBirthSeedEnabled_=config.gpuBirthRandom;
    shaderBirthSeed_=config.gpuBirthSeed;
    shaderBirthOrdinal_=0;
    if(!validComputeEmitter(config,&reason_))return false;
    programParams_={};
    device_=dev;
    backend_=ComputeBackend::Cpu;
    const bool wantsProgram=!desc_.particleProgram.isNull();
    std::string vmReason;
    bool vmCompatible=true;
    if(wantsProgram) {
        if(!desc_.gpuProgram) {
            vmCompatible=false;
            vmReason="GPU VM is disabled for this source; CPU particle program required";
        } else {
            gpu_program::Payload candidate;
            vmCompatible=gpu_program::prepareForMetal(desc_.particleProgram,
                                                       candidate,vmReason,desc_.gpuBirthRandom);
        }
    }
    if(!desc_.shaderGraph.isNull() || !desc_.graphInputs.isNull()) {
        if(!desc_.gpuMaterial || desc_.shaderGraph.isNull()) {
            vmCompatible=false;
            vmReason="GPU shaderGraph requires an explicit gpuMaterial opt-in and a supported particle graph";
        } else {
            gpu_material::Material material;
            std::string materialReason;
            if(!gpu_material::compile(desc_.shaderGraph,desc_.graphInputs,
                                      material,materialReason,desc_.gpuBirthRandom)) {
                vmCompatible=false;
                vmReason=materialReason;
            }
        }
    }
    const bool canGpu=dev&&supportsSpectraCompute(*dev,desc_.gpuOwnedAllocation)&&
                      desc_.quality>=fx::FxQuality::Medium&&vmCompatible;
    const auto selected=dev
        ?chooseComputeBackend(dev->info().backend,canGpu,desc_.backend)
        :ComputeBackend::Cpu;
    if(desc_.backend==ComputePreference::RequireGpu && selected==ComputeBackend::Cpu) {
        reason_=vmReason.empty()
            ?"GPU compute is unavailable; RequireGpu forbids CPU fallback"
            :vmReason+"; RequireGpu forbids CPU fallback";
        device_=nullptr;return false;
    }
    if(selected!=ComputeBackend::Cpu) {
        if(initGpu()) {
            backend_=selected;initialized_=true;reason_.clear();
            return true;
        }
        const std::string gpuFailure=reason_;
        releaseGpu();
        if(desc_.backend==ComputePreference::RequireGpu) {
            reason_="GPU setup failed: "+gpuFailure;
            device_=nullptr;return false;
        }
        reason_="GPU unavailable, CPU fallback: "+gpuFailure;
    } else if(desc_.backend!=ComputePreference::CpuOnly) {
        reason_=vmReason.empty()?"GPU compute unavailable or low-tier quality; CPU fallback"
                                :vmReason+"; CPU fallback";
    }
    if(!initCpu()) {
        if(reason_.empty())reason_="CPU particle fallback init failed";
        device_=nullptr;return false;
    }
    initialized_=true;return true;
}
bool ComputeParticles::initCpu() {
    cpu_.setQuality(desc_.quality);
    if(!cpu_.init(device_)) {
        reason_+=" CPU init: "+cpu_.lastError();
        return false;
    }
    fx::EffectDef effect;
    fx::EmitterDef emitter;
    emitter.texture="builtin:soft";
    emitter.blend=fx::FxBlend::Additive;
    emitter.burst=int(desc_.burst);
    emitter.life={desc_.lifetime,desc_.lifetime};
    emitter.speed={desc_.speed,desc_.speed};
    emitter.size={desc_.size,desc_.size};
    emitter.gravity={0,desc_.gravityY,0};
    emitter.drag={desc_.drag,desc_.drag};
    emitter.spread=0;
    emitter.color={{0.f,{1,1,1,1}},{1.f,{1,1,1,0}}};
    if(!desc_.particleProgram.isNull())emitter.particleProgram=desc_.particleProgram;
    if(!desc_.shaderGraph.isNull())emitter.shaderGraph=desc_.shaderGraph;
    if(!desc_.graphInputs.isNull())emitter.graphInputs=desc_.graphInputs;
    effect.emitters.push_back(std::move(emitter));
    cpuEffect_=cpu_.define("__spectrafx_compute_cpu_fallback",effect);
    return cpuEffect_>=0;
}
bool ComputeParticles::initGpu() {
    readbackLifetime_=std::make_shared<uint64_t>(0);
    vmFrameRevision_=0;
    checkpointRevision_=0;
    const bool checkpointPossible=desc_.gpuOwnedAllocation||desc_.capacity<=kGpuCheckpointCaptureMaxSlots;
    checkpointCadence_.configure(checkpointPossible?desc_.checkpointEveryFrames:0,
                                 checkpointPossible?desc_.adaptiveCheckpoint:AdaptiveCheckpointPolicy{});
    asyncCheckpointPending_=false;
    lastCheckpoint_={};
    if(!device_) {reason_="no RHI Device";return false;}
    gpu_program::Payload vmPayload;
    if(!gpu_program::prepareForMetal(desc_.particleProgram,vmPayload,reason_,desc_.gpuBirthRandom))
        return false;
    gpu_material::Material graphMaterial;
    const bool materialActive=desc_.gpuMaterial&&!desc_.shaderGraph.isNull();
    if(materialActive && !gpu_material::compile(
        desc_.shaderGraph,desc_.graphInputs,graphMaterial,reason_,desc_.gpuBirthRandom))return false;
    const std::array<uint32_t,4> graphChannels=graphMaterial.sources;
    gpuIndirectActive_=desc_.gpuIndirect && device_->supportsGpuAuthoredIndirectDraw();
    gpuAllocationActive_=desc_.gpuOwnedAllocation&&gpuIndirectActive_;
    if(desc_.gpuOwnedAllocation&&!gpuAllocationActive_){
        reason_="GPU-owned allocation requires GPU indirect drawing";return false;
    }
    stats_.gpuOwnedAllocation=gpuAllocationActive_;
    gpuDemandGrowth_=gpuAllocationActive_&&device_->supportsStorageBufferCopy();
    const uint32_t cap=gpuDemandGrowth_?gpuInitialPhysicalCapacity(desc_.capacity):desc_.capacity;
    physicalCapacity_=cap;
    const bool vulkan=device_->info().backend==gfx::Backend::Vulkan;
    proceduralActive_=desc_.proceduralBillboards&&!materialActive&&
        (vulkan||device_->info().backend==gfx::Backend::Metal);
    desc_.allocationBudget=device_->acquireGpuAllocationBudget(desc_.allocationBudget);
    if(!desc_.allocationBudget){reason_="GPU allocation budget conflicts with canonical Device ledger";return false;}
    {
        auto& budget=*desc_.allocationBudget;
        budget.collect(device_->gpuCompletedFence());
        const size_t charge=estimateGpuEmitterAllocation(cap,proceduralActive_);
        if(!budget.reserve(charge)){++stats_.gpuAllocationBudgetDenied;reason_="SpectraFX physical GPU allocation budget exhausted";return false;}
        gpuAllocationCharge_=charge;
    }
    gpuResetDirty_=false;
    gpuStagingDirty_=false;
    const std::string allocationShader=gpuAllocationActive_?
        std::string("#define SPECTRA_GPU_OWNED_ALLOCATION 1\n")+shaders::kMetal:std::string{};
    std::string glsl=shaders::kGLSL;
    if(gpuAllocationActive_)glsl.insert(glsl.find('\n')+1,"#define SPECTRA_GPU_OWNED_ALLOCATION 1\n");
    compute_=device_->createCompute({gpuAllocationActive_?allocationShader.c_str():shaders::kMetal,
        glsl.c_str(),"SpectraFX GPU particle simulation",gfx::VulkanComputeLayout::StorageUniforms,
        gpuAllocationActive_?19u:15u});
    if(!compute_.valid()) {reason_=device_->lastError();return false;}

    std::vector<ParticleState> reset(cap);
    for(auto& s:reset) {s.positionAge={0,0,0,-1};s.velocityLife={0,0,0,1};}
    states_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(ParticleState),
                   reset.data(),"SpectraFX GPU particle states");
    seeds_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(ParticleState),
                  nullptr,"SpectraFX GPU particle spawn queue");
    slots_=buffer(*device_,gfx::BufferType::Storage,size_t(cap)*sizeof(uint32_t)*(gpuAllocationActive_?2u:1u),
                  nullptr,"SpectraFX GPU spawn slots");
    if(gpuAllocationActive_)
        gpuSourceOwners_=buffer(*device_,gfx::BufferType::Storage,size_t(cap)*2*sizeof(uint32_t),
                               nullptr,"SpectraFX GPU physical source owners");
    killSlots_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(uint32_t),
                      nullptr,"SpectraFX GPU kill indices");
    programStateCache_.assign(cap,ProgramParams{});
    seedProgramParams_=buffer(*device_,gfx::BufferType::Storage,
                               cap*sizeof(ProgramParams),nullptr,
                               "SpectraFX GPU VM spawn parameters");
    stateProgramParams_=buffer(*device_,gfx::BufferType::Storage,
                                cap*sizeof(ProgramParams),programStateCache_.data(),
                                "SpectraFX GPU VM live parameters");
    gpuVmPayload_=buffer(*device_,gfx::BufferType::Storage,sizeof(vmPayload),
                         &vmPayload,"SpectraFX GPU VM validated program");
    std::vector<ProgramAttributes> initialAttributes(cap);
    stateAttributes_=buffer(*device_,gfx::BufferType::Storage,
                            cap*sizeof(ProgramAttributes),
                            initialAttributes.data(),
                            "SpectraFX GPU VM particle custom attributes");
    std::vector<uint32_t> initialFree;
    if(gpuAllocationActive_){
        initialFree.resize(cap);
        for(uint32_t i=0;i<cap;++i)initialFree[i]=cap-1-i;
    }
    liveSlots_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(uint32_t),
                      gpuAllocationActive_?initialFree.data():nullptr,
                      gpuAllocationActive_?"SpectraFX persistent GPU free stack":"SpectraFX GPU live-slot gather map");
    graphSources_=buffer(*device_,gfx::BufferType::Storage,sizeof(graphChannels),
                         graphChannels.data(),"SpectraFX GPU material vertex sources");
    const uint32_t zero=0;
    const gfx::DrawIndexedIndirectCommand zeroArgs{0,1,0,0,0};
    const std::array<uint32_t,8> allocatorCounters{0,cap,0,0,0,0,0,0};
    if(gpuAllocationActive_){
        const std::array<uint32_t,3> emptyDispatch{1,1,1};
        gpuAliveCurrent_=buffer(*device_,gfx::BufferType::Storage,size_t(cap)*sizeof(uint32_t),nullptr,"SpectraFX GPU alive list A");
        gpuAliveNext_=buffer(*device_,gfx::BufferType::Storage,size_t(cap)*sizeof(uint32_t),nullptr,"SpectraFX GPU alive list B");
        gpuDispatchArgs_=buffer(*device_,gfx::BufferType::Storage,sizeof(emptyDispatch),emptyDispatch.data(),"SpectraFX GPU compute dispatch arguments");
    }
    aliveCounter_=buffer(*device_,gfx::BufferType::Storage,
                         gpuAllocationActive_?sizeof(allocatorCounters):sizeof(zero),
                         gpuAllocationActive_?static_cast<const void*>(allocatorCounters.data()):static_cast<const void*>(&zero),
                         "SpectraFX GPU atomic alive count");
    vmErrors_=buffer(*device_,gfx::BufferType::Storage,sizeof(zero),
                     &zero,"SpectraFX GPU VM error counter");
    gpuDrawArgs_=buffer(*device_,gfx::BufferType::Storage,sizeof(zeroArgs),
                        &zeroArgs,"SpectraFX GPU authored draw args");
    vertices_=buffer(*device_,gfx::BufferType::Storage,
                     size_t(cap)*(proceduralActive_?1u:4u)*sizeof(Vertex),nullptr,
                     proceduralActive_?"SpectraFX compact billboard records":"SpectraFX GPU quad vertices");

    const bool wideIndices=!proceduralActive_&&gpuEmitterUsesWideIndices(desc_.capacity);
    const uint32_t quadCount=proceduralActive_?1u:cap;
    // Preserve compact indices for small groups; large groups use UInt32.
    auto buildIndices=[&]<class Index>(){
        std::vector<Index> idx(size_t(quadCount)*6);
        for(uint32_t n=0;n<quadCount;++n){
            const uint32_t i=n*4;
            const size_t at=size_t(n)*6;
            idx[at]=Index(i);idx[at+1]=Index(i+1);idx[at+2]=Index(i+2);
            idx[at+3]=Index(i);idx[at+4]=Index(i+2);idx[at+5]=Index(i+3);
        }
        return buffer(*device_,gfx::BufferType::Index,idx.size()*sizeof(Index),
                      idx.data(),"SpectraFX GPU billboard indices");
    };
    indices_=wideIndices?buildIndices.operator()<uint32_t>():buildIndices.operator()<uint16_t>();

    // One soft-sprite texture shared across all GPU particles in this first
    // backend. Texture atlas/mesh/ribbons remain on the existing CPU backend.
    constexpr int n=32;
    std::array<uint8_t,n*n*4> pixels{};
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
        const float dx=(float(x)+0.5f-float(n)*0.5f)/(float(n)*0.5f);
        const float dy=(float(y)+0.5f-float(n)*0.5f)/(float(n)*0.5f);
        const float dist=std::sqrt(dx*dx+dy*dy);
        const float alpha=std::clamp((1.f-dist)*2.f,0.f,1.f);
        const size_t at=size_t(y*n+x)*4;
        pixels[at]=pixels[at+1]=pixels[at+2]=255;
        pixels[at+3]=byte(alpha);
    }
    gfx::TextureDesc texture;
    texture.width=n;texture.height=n;texture.data=pixels.data();
    texture.filter=gfx::Filter::Linear;texture.label="SpectraFX GPU soft sprite";
    texture_=device_->createTexture(texture);

    gfx::ShaderDesc shader;
    // Shader source storage must outlive createShader() on all RHI backends.
    gpu_material::Shaders materialShaders;
    if(materialActive)materialShaders=gpu_material::makeShaders(graphMaterial);
    shader.glslVertex=materialActive?materialShaders.glslVertex.c_str():
        proceduralActive_&&vulkan?shaders::kProceduralBillboardGLSLVertex:fx::shaders::kParticleVS;
    shader.glslFragment=materialActive?materialShaders.glslFragment.c_str():
        proceduralActive_&&vulkan?shaders::kProceduralBillboardGLSLFragment:fx::shaders::kParticleFS;
    shader.msl=materialActive?materialShaders.msl.c_str():
        proceduralActive_?shaders::kProceduralBillboardMetal:fx::shaders::kParticleMSL;
    shader.textureSlots=1;shader.uniformSlots=1;
    shader.label=materialActive?"SpectraFX GPU particle material":"SpectraFX GPU billboard";
    vertexShader_=device_->createShader(shader);
    if(vertexShader_.valid()) {
        gfx::PipelineDesc pd;
        pd.shader=vertexShader_;
        pd.attrs[0]={0,gfx::VertexFormat::Float3,0};
        pd.attrs[1]={1,gfx::VertexFormat::Float2,12};
        pd.attrs[2]={2,gfx::VertexFormat::UByte4N,20};
        if(materialActive)pd.attrs[3]={3,gfx::VertexFormat::Float4,24};
        pd.attrCount=proceduralActive_&&!vulkan?0:(materialActive?4:3);
        pd.stride=proceduralActive_&&!vulkan?0:sizeof(Vertex);
        pd.vertexPerInstance=proceduralActive_&&vulkan;
        pd.indexType=wideIndices?gfx::IndexType::UInt32:gfx::IndexType::UInt16;
        pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Additive;
        pd.depthTest=true;pd.depthWrite=false;
        pd.label="SpectraFX GPU additive";
        pipeline_=device_->createPipeline(pd);
    }
    if(!states_.valid()||!seeds_.valid()||!slots_.valid()||
       !killSlots_.valid()||!liveSlots_.valid()||
       !graphSources_.valid()||
       !aliveCounter_.valid()||!gpuDrawArgs_.valid()||!vmErrors_.valid()||
       !seedProgramParams_.valid()||!stateProgramParams_.valid()||
       !gpuVmPayload_.valid()||!stateAttributes_.valid()||!vertices_.valid()||
       !indices_.valid()||!texture_.valid()||!vertexShader_.valid()||!pipeline_.valid()||
       (gpuAllocationActive_&&(!gpuSourceOwners_.valid()||!gpuAliveCurrent_.valid()||
           !gpuAliveNext_.valid()||!gpuDispatchArgs_.valid()))) {
        reason_="GPU resource/pipeline allocation failed: "+device_->lastError();
        return false;
    }
    ages_.assign(cap,-1.f);activePages_.reset(cap);
    slotSourceTokens_.assign(cap,0);
    pendingKillSlots_.clear();
    killedSlotsAwaitingFrame_.clear();
    freeSlots_.clear();freeSlots_.reserve(cap);
    for(uint32_t i=cap;i>0;--i)freeSlots_.push_back(i-1);
    pendingStates_.reserve(cap);
    pendingSlots_.reserve(cap);
    pendingProgramParams_.reserve(cap);
    aliveSlots_.reserve(cap);
    stats_.gpuBytes=size_t(cap)*(sizeof(ParticleState)*2+3*sizeof(uint32_t))
                    + (proceduralActive_?size_t(cap)*40u+12u:size_t(cap)*(160u+6u*(wideIndices?4u:2u)))
                    +size_t(n*n*4)+2*sizeof(zero)+sizeof(zeroArgs)
                    +size_t(cap)*2*sizeof(ProgramParams)+sizeof(vmPayload)
                    +size_t(cap)*sizeof(ProgramAttributes)+sizeof(graphChannels)
                    +(gpuAllocationActive_?size_t(cap)*20u+10u*sizeof(uint32_t):0u);
    return true;
}
void ComputeParticles::markGpuAllocationUsed(){
    if(!gpuAllocationCharge_||!device_)return;
    const auto fence=device_->gpuSubmissionFence();
    gpuAllocationLastFence_=fence?std::max(gpuAllocationLastFence_,fence):GpuAllocationBudget::unknownFence;
}
void ComputeParticles::retireAllocationCharge(size_t bytes,uint64_t fence){
    if(desc_.allocationBudget&&bytes){
        desc_.allocationBudget->retire(bytes,fence);
        if(device_)desc_.allocationBudget->collect(device_->gpuCompletedFence());
    }
}
void ComputeParticles::releaseGpu() {
    checkpointHistory_.clear();
    readbackLifetime_.reset();
    asyncErrorPending_=false;
    asyncAllocationCountersPending_=false;
    asyncCheckpointPending_=false;
    if(device_) {
        if(pipeline_.valid())device_->destroyPipeline(pipeline_);
        if(vertexShader_.valid())device_->destroyShader(vertexShader_);
        if(compute_.valid())device_->destroyCompute(compute_);
        if(compactAllocationCompute_.valid())device_->destroyCompute(compactAllocationCompute_);
        if(states_.valid())device_->destroyBuffer(states_);
        if(seeds_.valid())device_->destroyBuffer(seeds_);
        if(slots_.valid())device_->destroyBuffer(slots_);
        if(gpuSourceOwners_.valid())device_->destroyBuffer(gpuSourceOwners_);
        if(gpuAliveCurrent_.valid())device_->destroyBuffer(gpuAliveCurrent_);
        if(gpuAliveNext_.valid())device_->destroyBuffer(gpuAliveNext_);
        if(gpuDispatchArgs_.valid())device_->destroyBuffer(gpuDispatchArgs_);
        if(killSlots_.valid())device_->destroyBuffer(killSlots_);
        if(liveSlots_.valid())device_->destroyBuffer(liveSlots_);
        if(graphSources_.valid())device_->destroyBuffer(graphSources_);
        if(aliveCounter_.valid())device_->destroyBuffer(aliveCounter_);
        if(gpuDrawArgs_.valid())device_->destroyBuffer(gpuDrawArgs_);
        if(vmErrors_.valid())device_->destroyBuffer(vmErrors_);
        if(seedProgramParams_.valid())device_->destroyBuffer(seedProgramParams_);
        if(stateProgramParams_.valid())device_->destroyBuffer(stateProgramParams_);
        if(gpuVmPayload_.valid())device_->destroyBuffer(gpuVmPayload_);
        if(stateAttributes_.valid())device_->destroyBuffer(stateAttributes_);
        if(vertices_.valid())device_->destroyBuffer(vertices_);
        if(indices_.valid())device_->destroyBuffer(indices_);
        if(texture_.valid())device_->destroyTexture(texture_);
    }
    retireAllocationCharge(gpuAllocationCharge_,gpuAllocationLastFence_);
    gpuAllocationCharge_=0;gpuAllocationLastFence_=0;gpuGrowthBudgetBlocked_=false;
    pipeline_={};vertexShader_={};compute_={};compactAllocationCompute_={};states_={};seeds_={};slots_={};
    killSlots_={};gpuSourceOwners_={};
    gpuAliveCurrent_={};gpuAliveNext_={};gpuDispatchArgs_={};
    pendingGpuOwners_.clear();pendingGpuKills_.clear();gpuBirthEstimates_.clear();
    gpuAllocationClock_=0;gpuEstimatedBirths_=0;gpuAllocationActive_=gpuAllocationSubmitted_=gpuCullViewUsed_=false;
    gpuDemandGrowth_=false;physicalCapacity_=0;
    gpuIdleSteps_=0;gpuBirthEpoch_=0;gpuEmptyProven_=false;gpuCompactTarget_=0;
    vertices_={};indices_={};texture_={};liveSlots_={};
    aliveCounter_={};gpuDrawArgs_={};vmErrors_={};gpuIndirectActive_=false;proceduralActive_=false;
    seedProgramParams_={};stateProgramParams_={};gpuVmPayload_={};
    stateAttributes_={};graphSources_={};
    gpuResetDirty_=false;
    gpuStagingDirty_=false;
    ages_.clear();activePages_.reset(0);freeSlots_.clear();pendingStates_.clear();pendingSlots_.clear();
    pendingKillSlots_.clear();killedSlotsAwaitingFrame_.clear();
    slotSourceTokens_.clear();
    pendingProgramParams_.clear();programStateCache_.clear();cpuHandles_.clear();
    programParamsDirty_=false;
    aliveSlots_.clear();
    drawCount_=0;
}
void ComputeParticles::shutdown() {
    checkpointHistory_.clear();
    shaderBirthSeed_=0;shaderBirthOrdinal_=0;shaderBirthSeedEnabled_=false;
    cpu_.shutdown();cpuEffect_=-1;releaseGpu();device_=nullptr;
    initialized_=false;viewReady_=false;verticesReady_=false;
    drawCount_=0;
    programParams_={};
    lastCheckpoint_={};checkpointRevision_=0;
    checkpointCadence_={};
    backend_=ComputeBackend::Cpu;stats_={};reason_.clear();
}
bool ComputeParticles::resetGpuAllocationSeed(uint32_t seed) {
    if(!initialized_ || backend_==ComputeBackend::Cpu) {
        reason_="Seeded GPU allocation requires active GPU backend";
        return false;
    }
    if(gpuAllocationActive_){reason_="GPU-owned slot order is nondeterministic";return false;}
    if(!tryClear())return false;
    // Fisher-Yates on the free-slot list. The seed controls allocation
    // order only; GPU shader random streams are not modified.
    uint32_t state=seed==0?0x9E3779B9u:seed;
    for(size_t i=freeSlots_.size();i>1;--i) {
        state^=state<<13;state^=state>>17;state^=state<<5;
        const size_t j=size_t(state)%i;
        std::swap(freeSlots_[i-1],freeSlots_[j]);
    }
    return true;
}
bool ComputeParticles::resetGpuShaderSeed(uint32_t seed) {
    if(!initialized_||backend_==ComputeBackend::Cpu)return false;
    if(!tryClear())return false;
    shaderBirthSeed_=seed;
    shaderBirthOrdinal_=0;
    shaderBirthSeedEnabled_=true;
    return true;
}
void ComputeParticles::clear() {
    (void)tryClear(); // preserve legacy ABI; status is available via reason()
}
bool ComputeParticles::tryClear() {
    if(!initialized_)return false;
    if(backend_==ComputeBackend::Cpu) {
        cpu_.clear();cpuHandles_.clear();
        stats_.alive=stats_.submittedSlots=0;
        return true;
    }
    if(!device_ || !device_->gpuHealthy() ||
       !device_->canUpdateSharedBuffers() ||
       !states_.valid() || !stateProgramParams_.valid() ||
       !stateAttributes_.valid()) {
        ++stats_.gpuResetFailures;
        reason_="SpectraFX checked GPU reset requires healthy device, open "
                "graphics frame and no active render/compute encoder";
        return false;
    }
    // A partially accepted sequence of uploads MUST NOT allow another GPU
    // simulation or a new burst until the entire reset is retried. Kill any
    // retained checkpoint immediately; a partial reset is never a snapshot.
    markGpuAllocationUsed();
    gpuResetDirty_=true;
    ++gpuBirthEpoch_;gpuEmptyProven_=false;gpuIdleSteps_=0;gpuCompactTarget_=0;
    verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
    lastCheckpoint_={};checkpointRevision_=0;
    ++vmFrameRevision_;
    const uint32_t cap=allocatedCapacity();
    std::vector<ParticleState> empty(cap);
    for(auto& particle:empty) {
        particle.positionAge={0,0,0,-1};
        particle.velocityLife={0,0,0,1};
    }
    const std::vector<ProgramParams> resetParams(cap);
    const std::vector<ProgramAttributes> resetAttributes(cap);
    const bool stateOk=device_->updateStorageBufferChecked(
        states_,empty.data(),empty.size()*sizeof(ParticleState));
    const bool paramOk=stateOk && device_->updateStorageBufferChecked(
        stateProgramParams_,resetParams.data(),
        resetParams.size()*sizeof(ProgramParams));
    const bool attributeOk=paramOk && device_->updateStorageBufferChecked(
        stateAttributes_,resetAttributes.data(),
        resetAttributes.size()*sizeof(ProgramAttributes));
    bool allocatorOk=attributeOk;
    if(allocatorOk&&gpuAllocationActive_){
        std::vector<uint32_t> free(cap);
        for(uint32_t i=0;i<cap;++i)free[i]=cap-1-i;
        const std::array<uint32_t,8> counters{0,cap,0,0,0,0,0,0};
        const std::array<uint32_t,3> emptyDispatch{1,1,1};
        allocatorOk=device_->updateStorageBufferChecked(liveSlots_,free.data(),free.size()*sizeof(uint32_t))&&
            device_->updateStorageBufferChecked(aliveCounter_,counters.data(),sizeof(counters))&&
            device_->updateStorageBufferChecked(gpuDispatchArgs_,emptyDispatch.data(),sizeof(emptyDispatch));
    }
    if(!allocatorOk) {
        ++stats_.gpuResetFailures;
        reason_="SpectraFX checked GPU reset rejected; previous slot ownership "
                "retained; retry reset before any new GPU frame: "+
                device_->lastError();
        return false;
    }
    pendingGpuOwners_.clear();pendingGpuKills_.clear();gpuBirthEstimates_.clear();
    gpuAllocationClock_=0;gpuEstimatedBirths_=0;gpuAllocationSubmitted_=false;
    // Only all-accepted ordered uploads may reset the CPU-owned slot mirror.
    pendingStates_.clear();pendingSlots_.clear();pendingProgramParams_.clear();
    pendingKillSlots_.clear();killedSlotsAwaitingFrame_.clear();
    std::fill(slotSourceTokens_.begin(),slotSourceTokens_.end(),0);
    programStateCache_.assign(cap,ProgramParams{});
    programParamsDirty_=false;
    ages_.assign(cap,-1.f);activePages_.reset(cap);
    freeSlots_.clear();freeSlots_.reserve(cap);
    for(uint32_t i=cap;i>0;--i)freeSlots_.push_back(i-1);
    aliveSlots_.clear();
    cpuHandles_.clear();
    drawCount_=0;verticesReady_=false;
    stats_.alive=stats_.submittedSlots=stats_.gpuKillFencesPending=0;
    stats_.gpuVmErrorsKnown=false;
    gpuResetDirty_=false;
    gpuStagingDirty_=false;
    // Reset succeeded independently of optional memory reclamation. Failure
    // to allocate smaller buffers must not turn a completed clear into a failure.
    if(gpuDemandGrowth_){
        gpuEmptyProven_=true;
        (void)shrinkEmptyGpuAllocation();
    }
    reason_.clear();
    return true;
}
bool ComputeParticles::setProgramParameter(uint32_t slot,float value,
                                            bool affectExisting) {
    if(!initialized_ || slot>=8 || !spectra::program::number(value) ||
       (desc_.particleProgram.isNull() && desc_.shaderGraph.isNull()))return false;
    if(gpuAllocationActive_&&affectExisting){
        reason_="GPU-owned allocation supports future birth parameters only";return false;
    }
    if(gpuAllocationActive_&&programParams_[slot]!=value)++vmFrameRevision_;
    programParams_[slot]=value;
    if(backend_==ComputeBackend::Cpu) {
        if(affectExisting)
            for(const fx::FxHandle h:cpuHandles_)
                (void)cpu_.setProgramParameter(h,slot,value,true);
        return true;
    }
    if(affectExisting) {
        const auto write=[&](ProgramParams& params) {
            render::float4& p=slot<4?params.p0:params.p1;
            switch(slot%4) {
            case 0:p.x=value;break;
            case 1:p.y=value;break;
            case 2:p.z=value;break;
            default:p.w=value;break;
            }
        };
        activePages_.forEach([&](uint32_t i){write(programStateCache_[i]);});
        for(ProgramParams& seed:pendingProgramParams_)write(seed);
        programParamsDirty_=true; // uploaded outside active render passes
    }
    return true;
}
bool ComputeParticles::burstAt(fx::float3 position,fx::float3 direction,fx::float4 tint,uint64_t sourceToken) {
    if(!initialized_||desc_.quality==fx::FxQuality::Off||!finite(position)||!finite(direction)||!finite(tint))return false;
    const float length=render::length(direction);
    const fx::float3 normal=length>1e-5f?direction*(1.0f/length):fx::float3{0,1,0};
    if(backend_==ComputeBackend::Cpu) {
        const fx::FxHandle h=cpu_.playWithProgramParameters(
            cpuEffect_,position,normal,1.f,tint,programParams_);
        if(!h.valid()) {stats_.dropped+=desc_.burst;return false;}
        // Drop finished instances (amortized) before falling back to
        // killing the oldest still-live handle at the hard cap.
        if(cpuHandles_.size()>=cpuHandlePruneAt_) {
            cpuHandles_.erase(std::remove_if(cpuHandles_.begin(),cpuHandles_.end(),
                [&](fx::FxHandle old){return !cpu_.alive(old);}),cpuHandles_.end());
            cpuHandlePruneAt_=std::max<size_t>(64,cpuHandles_.size()*2);
        }
        if(cpuHandles_.size()>=65536) {
            cpu_.kill(cpuHandles_.front());
            cpuHandles_.erase(cpuHandles_.begin());
        }
        cpuHandles_.push_back(h);
        return true;
    }
    if(gpuResetDirty_) {
        reason_="SpectraFX GPU reset incomplete; retry checked reset";
        return false;
    }
    if(gpuAllocationActive_){
        if(pendingStates_.size()+desc_.burst>desc_.capacity){
            stats_.dropped+=desc_.burst;return false; // staging budget, not observed GPU occupancy
        }
        for(uint32_t n=0;n<desc_.burst;++n){
            ParticleState particle{};
            particle.positionAge={position.x,position.y,position.z,0};
            particle.velocityLife={normal.x*desc_.speed,normal.y*desc_.speed,normal.z*desc_.speed,desc_.lifetime};
            particle.tint={tint.x,tint.y,tint.z,tint.w};particle.size={desc_.size,0,0,0};
            if(shaderBirthSeedEnabled_){
                particle.size.y=float(shaderBirthSeed_&0xffffu);particle.size.z=float(shaderBirthSeed_>>16);
                particle.size.w=float(shaderBirthOrdinal_++&0xffffffu);
            }
            pendingStates_.push_back(particle);pendingGpuOwners_.push_back(sourceToken);
            ProgramParams parameters{};
            parameters.p0={programParams_[0],programParams_[1],programParams_[2],programParams_[3]};
            parameters.p1={programParams_[4],programParams_[5],programParams_[6],programParams_[7]};
            pendingProgramParams_.push_back(parameters);
        }
        return true; // queued requests; GPU admits up to actual free count
    }
    if(freeSlots_.size()<desc_.burst) {
        stats_.dropped+=desc_.burst;return false;
    }
    for(uint32_t n=0;n<desc_.burst;++n) {
        const uint32_t slot=freeSlots_.back();freeSlots_.pop_back();
        ages_[slot]=desc_.lifetime;
        activePages_.insert(slot);
        slotSourceTokens_[slot]=sourceToken;
        ParticleState particle{};
        particle.positionAge={position.x,position.y,position.z,0};
        particle.velocityLife={normal.x*desc_.speed,normal.y*desc_.speed,normal.z*desc_.speed,desc_.lifetime};
        particle.tint={tint.x,tint.y,tint.z,tint.w};
        particle.size={desc_.size,0,0,0};
        if(shaderBirthSeedEnabled_) {
            // Exact 16-bit float components carry the full 32-bit seed.
            particle.size.y=float(shaderBirthSeed_&0xffffu);
            particle.size.z=float(shaderBirthSeed_>>16);
            particle.size.w=float(shaderBirthOrdinal_++ & 0xffffffu);
        }
        pendingStates_.push_back(particle);pendingSlots_.push_back(slot);
        ProgramParams parameters{};
        parameters.p0={programParams_[0],programParams_[1],
                       programParams_[2],programParams_[3]};
        parameters.p1={programParams_[4],programParams_[5],
                       programParams_[6],programParams_[7]};
        pendingProgramParams_.push_back(parameters);
        programStateCache_[slot]=parameters;
    }
    stats_.alive=desc_.capacity-uint32_t(freeSlots_.size());
    return true;
}
bool ComputeParticles::killSourceParticles(uint64_t sourceToken) {
    if(sourceToken==0 || !initialized_ || backend_==ComputeBackend::Cpu ||
       !device_ || !device_->gpuHealthy() || gpuResetDirty_ ||
       !device_->canUpdateSharedBuffers())return false;
    if(gpuAllocationActive_){
        if(pendingGpuKills_.size()>=256){reason_="GPU source kill queue full";return false;}
        if(std::find(pendingGpuKills_.begin(),pendingGpuKills_.end(),sourceToken)==pendingGpuKills_.end())
            pendingGpuKills_.push_back(sourceToken);
        size_t write=0;
        for(size_t i=0;i<pendingGpuOwners_.size();++i){
            if(pendingGpuOwners_[i]==sourceToken)continue;
            pendingGpuOwners_[write]=pendingGpuOwners_[i];pendingStates_[write]=pendingStates_[i];
            pendingProgramParams_[write]=pendingProgramParams_[i];++write;
        }
        pendingGpuOwners_.resize(write);pendingStates_.resize(write);pendingProgramParams_.resize(write);
        verticesReady_=false;return true;
    }
    // The birth queue is an ordered zip of physical slot, seed and params.
    // Removing unsent births is safe: the GPU has never seen those slots.
    for(size_t i=pendingSlots_.size();i>0;--i) {
        const size_t at=i-1;
        const uint32_t slot=pendingSlots_[at];
        if(slotSourceTokens_[slot]!=sourceToken)continue;
        pendingSlots_.erase(pendingSlots_.begin()+at);
        pendingStates_.erase(pendingStates_.begin()+at);
        pendingProgramParams_.erase(pendingProgramParams_.begin()+at);
        ages_[slot]=-1.f;slotSourceTokens_[slot]=0;
        activePages_.erase(slot);
        freeSlots_.push_back(slot);
        ++stats_.selectivelyKilled;
    }
    // Submitted slots remain reserved until the next successful GPU compute
    // submission. They become invisible to CPU gather immediately, and
    // receive a Metal-ordered dead-state upload before the next compact pass.
    activePages_.forEach([&](uint32_t i){
        if(slotSourceTokens_[i]!=sourceToken)return;
        slotSourceTokens_[i]=0;ages_[i]=-1.f;
        activePages_.erase(i);
        pendingKillSlots_.push_back(i);
        ++stats_.selectivelyKilled;
    });
    stats_.alive=uint32_t(ages_.size()-freeSlots_.size()-
                          pendingKillSlots_.size()-
                          killedSlotsAwaitingFrame_.size());
    verticesReady_=false;
    return true;
}
void ComputeParticles::setView(const fx::SpectraFxView& view) {
    if(!view.valid()) { viewReady_=false;return; }
    if(gpuCullViewUsed_ && verticesReady_ &&
       (std::memcmp(viewProj_.m,view.viewProj.m,sizeof(viewProj_.m))!=0 ||
        right_.x!=view.right.x || right_.y!=view.right.y || right_.z!=view.right.z ||
        up_.x!=view.up.x || up_.y!=view.up.y || up_.z!=view.up.z ||
        (desc_.quality==fx::FxQuality::Medium &&
         (viewWidth_!=view.width || viewHeight_!=view.height ||
          eye_.x!=view.eye.x || eye_.y!=view.eye.y || eye_.z!=view.eye.z)))) {
        verticesReady_=false; // old visible stream is not valid for a new camera
    }
    viewProj_=view.viewProj;eye_=view.eye;right_=view.right;up_=view.up;
    viewWidth_=view.width;viewHeight_=view.height;
    viewReady_=true;
    if(backend_==ComputeBackend::Cpu)cpu_.setView(view);
}
bool ComputeParticles::step(float dt) {
    if(!initialized_||!finite(dt)||dt<0)return false;
    dt=std::clamp(dt,0.f,0.1f);
    if(backend_==ComputeBackend::Cpu) {
        cpu_.update(dt);
        stats_.alive=uint32_t(cpu_.stats().particles);
        return true;
    }
    if(gpuResetDirty_) {
        verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
        reason_="SpectraFX GPU reset incomplete; retry checked reset";
        return false;
    }
    if(!device_->gpuHealthy()) {
        ++stats_.gpuDeviceUnhealthy;
        if(desc_.backend==ComputePreference::RequireGpu) {
            // Never claim a CPU backend when the caller explicitly required GPU.
            initialized_=false;verticesReady_=false;
            reason_="GPU unhealthy; RequireGpu forbids CPU fallback";
            return false;
        }
        switchToCpu("GPU reported unhealthy");
        // Any GPU-resident particles were lost: never report a seamless step.
        return false;
    }
    if(!device_->canUpdateSharedBuffers()) {
        ++stats_.gpuPassOrderRejections;
        verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
        reason_="SpectraFX GPU step must be before the render pass";
        return false;
    }
    markGpuAllocationUsed();
    if(gpuAllocationActive_)return stepGpuAllocated(dt);
    // Recycle only after a successful Metal frame completion callback,
    // never just an encoded/submitted blit.
    const uint64_t completed=device_->completedFrameSerial();
    if(completed!=0) {
        auto end=std::remove_if(killedSlotsAwaitingFrame_.begin(),
                              killedSlotsAwaitingFrame_.end(),
            [&](const KilledSlotAwaitingFrame& entry) {
                if(entry.submittedFrame>completed)return false;
                freeSlots_.push_back(entry.slot);
                ++stats_.killedSlotsReclaimed;
                return true;
            });
        killedSlotsAwaitingFrame_.erase(end,killedSlotsAwaitingFrame_.end());
    }
    stats_.gpuKillFencesPending=uint32_t(killedSlotsAwaitingFrame_.size());
    const uint32_t count=uint32_t(pendingStates_.size());
    stats_.gpuVmErrorsKnown=false;
    // Keep the physical state pool stable, but gather all surviving physical
    // slots into contiguous GPU vertices for a dense indexed draw. The CPU
    // knows lifetimes and slot ownership; it never reads GPU positions.
    uint32_t updatePrefix=0;
    uint32_t compactCount=0; // only a CPU estimate in GPU indirect mode
    aliveSlots_.clear();
    stats_.hostActiveSlotVisits=0;
    stats_.hostOccupiedPages=uint32_t(activePages_.occupiedPages());
    activePages_.forEach([&](uint32_t i){
        ++stats_.hostActiveSlotVisits;
        updatePrefix=i+1; // ordered occupied slots preserve GPU update prefix
        if(ages_[i]>dt){
            ++compactCount;
            if(!gpuIndirectActive_)aliveSlots_.push_back(i);
        }
    });
    if(updatePrefix==0 && count==0 && pendingKillSlots_.empty() &&
       !gpuStagingDirty_ && !programParamsDirty_) {
        drawCount_=0;
        stats_.submittedSlots=stats_.alive=0;
        verticesReady_=true;
        reason_.clear();
        return true; // no live GPU particles, no compute work or draw
    }
    const uint32_t killCount=uint32_t(pendingKillSlots_.size());
    // A zero-dt redraw may still need camera-facing geometry, but must
    // neither execute the per-particle Update VM nor invalidate an already
    // completed readback. Births, kills and edited live VM params DO mutate.
    const bool mutateParticles=dt>0.f || count>0 || killCount>0 ||
                               programParamsDirty_ || gpuStagingDirty_;
    if(killCount>0) {
        // One bounded 4-byte slot-index upload, no matter how fragmented the
        // physical GPU particle allocation is. Shader mode 6 invalidates
        // only these indices, before any new Spawn / Integrate / Compact.
        if(!device_->updateStorageBufferChecked(
                killSlots_,pendingKillSlots_.data(),
                size_t(killCount)*sizeof(uint32_t))) {
            ++stats_.gpuKillUploadFailures;
            verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
            reason_="SpectraFX GPU kill-index upload rejected: "+
                    device_->lastError();
            return false; // still owned; no slots returned to free list
        }
        stats_.gpuKillIndexBytes+=size_t(killCount)*sizeof(uint32_t);
    }
    // Every storage upload that supplies compute input must be checked.
    // A failure may occur AFTER prior blits were accepted into this frame.
    // Keep pending births/slot mirrors intact, invalidate prior snapshots,
    // and retry ALL inputs on the next safe render-frame submission.
    const auto stagingRejected=[&](const char* label) {
        ++stats_.gpuStagingUploadFailures;
        gpuStagingDirty_=true;
        ++vmFrameRevision_;
        lastCheckpoint_={};checkpointRevision_=0;
        verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
        reason_=std::string("SpectraFX checked GPU staging rejected (")+
                label+"): "+device_->lastError();
        return false;
    };
    const uint32_t resetVmErrors=0;
    if(!device_->updateStorageBufferChecked(
            vmErrors_,&resetVmErrors,sizeof(resetVmErrors)))
        return stagingRejected("VM error counter");
    if(programParamsDirty_ || gpuStagingDirty_) {
        if(!device_->updateStorageBufferChecked(
                stateProgramParams_,programStateCache_.data(),
                programStateCache_.size()*sizeof(ProgramParams)))
            return stagingRejected("live VM parameters");
        stats_.uploadedBytes+=programStateCache_.size()*sizeof(ProgramParams);
    }
    if(count>0) {
        if(!device_->updateStorageBufferChecked(
                seeds_,pendingStates_.data(),size_t(count)*sizeof(ParticleState)))
            return stagingRejected("spawn state");
        if(!device_->updateStorageBufferChecked(
                slots_,pendingSlots_.data(),size_t(count)*sizeof(uint32_t)))
            return stagingRejected("spawn slot IDs");
        if(!device_->updateStorageBufferChecked(
                seedProgramParams_,pendingProgramParams_.data(),
                size_t(count)*sizeof(ProgramParams)))
            return stagingRejected("spawn VM parameters");
        stats_.uploadedBytes+=size_t(count)*(sizeof(ParticleState)+
                               sizeof(uint32_t)+sizeof(ProgramParams));
    }
    if(compactCount>0 && !gpuIndirectActive_) {
        if(!device_->updateStorageBufferChecked(
                liveSlots_,aliveSlots_.data(),size_t(compactCount)*sizeof(uint32_t)))
            return stagingRejected("live gather slot IDs");
        stats_.uploadedBytes+=size_t(compactCount)*sizeof(uint32_t);
    }
    if(!device_->beginCompute(compute_)) {
        ++stats_.gpuComputeBeginFailures;
        gpuStagingDirty_=true;
        ++vmFrameRevision_;
        lastCheckpoint_={};checkpointRevision_=0;
        verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
        reason_="Metal compute encoder unavailable after checked staging; "
                "retry input uploads before rendering";
        return false; // pending births, ages and slot free lists unchanged
    }
    device_->setComputeBuffer(0,states_);
    device_->setComputeBuffer(1,seeds_);
    device_->setComputeBuffer(2,slots_);
    device_->setComputeBuffer(3,vertices_);
    device_->setComputeBuffer(4,liveSlots_);
    device_->setComputeBuffer(5,aliveCounter_);
    device_->setComputeBuffer(6,gpuDrawArgs_);
    device_->setComputeBuffer(7,seedProgramParams_);
    device_->setComputeBuffer(9,stateProgramParams_);
    device_->setComputeBuffer(10,gpuVmPayload_);
    device_->setComputeBuffer(11,stateAttributes_);
    device_->setComputeBuffer(12,vmErrors_);
    device_->setComputeBuffer(13,graphSources_);
    device_->setComputeBuffer(14,killSlots_);
    Uniforms u{};
    u.gravityDt={desc_.gravityY,dt,desc_.drag,proceduralActive_?1.f:0.f};
    u.rightSize={right_.x,right_.y,right_.z,desc_.size};
    u.up={up_.x,up_.y,up_.z,0};
    u.meta={0,desc_.capacity,count,compactCount};
    // Scatter kill first in the SAME compute encoder as the rest of this
    // frame; each index is unique and never aliases a pending new birth.
    bool dispatchOk=true;
    if(killCount>0) {
        u.meta[0]=6;
        u.meta[2]=killCount;
        device_->setComputeUniforms(&u,sizeof(u));
        dispatchOk=device_->dispatchChecked(killCount);
        if(dispatchOk){++stats_.gpuDispatches;++stats_.gpuKillScatterDispatches;}
        u.meta[2]=count;
    }
    // Canonical phase order: Spawn VM runs inside mode 0, Update VM
    // inside mode 1, and vertex publication follows integration. No
    // separate Stage-23 VM pass must run on these same physical particles.
    const auto frame=gpu_pipeline::plan(desc_.capacity,count,updatePrefix,
                                        compactCount,gpuIndirectActive_,
                                        mutateParticles);
    for(uint32_t i=0;dispatchOk&&i<frame.count;++i) {
        const auto& phase=frame.dispatches[i];
        u.meta[0]=phase.shaderMode;
        device_->setComputeUniforms(&u,sizeof(u));
        dispatchOk=device_->dispatchChecked(phase.threads);
        if(!dispatchOk)break;
        ++stats_.gpuDispatches;
        if(phase.phase==gpu_pipeline::Phase::Compact)
            ++stats_.gpuCompactDispatches;
    }
    device_->endCompute();
    if(!dispatchOk){gpuResetDirty_=true;verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
        reason_="GPU dispatch rejected; checked reset required";return false;}
    // Only a successful ordered compute submission can consume a dirty
    // upload batch. Retrying a partial staging failure never leaks old data.
    gpuStagingDirty_=false;
    programParamsDirty_=false;
    // A frozen frame only rebuilds geometry and indirect arguments; it must
    // not invalidate an unchanged GPU particle+VM checkpoint revision.
    if(mutateParticles)++vmFrameRevision_;
    pendingStates_.clear();pendingSlots_.clear();pendingProgramParams_.clear();
    // Keep killed slots quarantined until their Metal frame completes.
    // On unsupported RHI completion serial, conservatively do not recycle.
    const uint64_t submittedFrame=device_->frameSerial();
    for(const uint32_t slot:pendingKillSlots_)
        killedSlotsAwaitingFrame_.push_back({slot,submittedFrame});
    pendingKillSlots_.clear();
    stats_.gpuKillFencesPending=uint32_t(killedSlotsAwaitingFrame_.size());
    // Commit CPU lifespan/free-slot changes only after GPU dispatch submission.
    activePages_.forEach([&](uint32_t i){
        ++stats_.hostActiveSlotVisits;
        ages_[i]-=dt;
        if(ages_[i]<=0.f){
            ages_[i]=-1.f;slotSourceTokens_[i]=0;activePages_.erase(i);freeSlots_.push_back(i);
        }
    });
    stats_.alive=compactCount;
    drawCount_=compactCount;
    stats_.submittedSlots=drawCount_;
    verticesReady_=true;reason_.clear();
    // Queue chunked readback only after the VM/integration/compact dispatches.
    // This is optional; never block the render loop on checkpoint collection.
    if(checkpointCadence_.due(vmFrameRevision_)) {
        const bool accepted=!asyncCheckpointPending_ && requestGpuCheckpoint();
        if(accepted)checkpointCadence_.accepted(vmFrameRevision_);
        else checkpointCadence_.deferred(vmFrameRevision_);
        stats_.checkpointAdaptiveBackoffs=checkpointCadence_.backoffs();
        stats_.checkpointAdaptiveRecoveries=checkpointCadence_.recoveries();
    }
    return true;
}
void ComputeParticles::bindGpuAllocationBuffers(){
    device_->setComputeBuffer(0,states_);device_->setComputeBuffer(1,seeds_);
    device_->setComputeBuffer(2,slots_);device_->setComputeBuffer(3,vertices_);
    device_->setComputeBuffer(4,liveSlots_);device_->setComputeBuffer(5,aliveCounter_);
    device_->setComputeBuffer(6,gpuDrawArgs_);device_->setComputeBuffer(7,seedProgramParams_);
    device_->setComputeBuffer(9,stateProgramParams_);device_->setComputeBuffer(10,gpuVmPayload_);
    device_->setComputeBuffer(11,stateAttributes_);device_->setComputeBuffer(12,vmErrors_);
    device_->setComputeBuffer(13,graphSources_);device_->setComputeBuffer(14,killSlots_);
    device_->setComputeBuffer(15,gpuSourceOwners_);
    device_->setComputeBuffer(16,gpuAliveCurrent_);
    device_->setComputeBuffer(17,gpuAliveNext_);
    device_->setComputeBuffer(18,gpuDispatchArgs_);
}
bool ComputeParticles::growGpuAllocation(uint32_t required){
    const uint32_t old=physicalCapacity_;
    if(required<=old)return true;
    if(!gpuDemandGrowth_||!device_||!device_->gpuHealthy())return false;
    const uint32_t next=gpuNextPhysicalCapacity(old,required,desc_.capacity);
    if(!next||next<=old)return false;
    return replaceGpuAllocation(next,false);
}
bool ComputeParticles::replaceGpuAllocation(uint32_t next,bool empty,bool compact){
    const uint32_t old=physicalCapacity_;
    const bool shrinking=empty||compact;
    if(!gpuDemandGrowth_||!device_||!device_->gpuHealthy()||!device_->canUpdateSharedBuffers()||
       !next||next>desc_.capacity||(shrinking?next>=old:next<=old))return false;
    // Charge the full candidate envelope before allocation, including shared
    // constants conservatively. Keep the old envelope until GPU completion.
    size_t candidateCharge=0;uint64_t candidateFence=0;
    if(desc_.allocationBudget){
        auto& budget=*desc_.allocationBudget;budget.collect(device_->gpuCompletedFence());
        candidateCharge=estimateGpuEmitterAllocation(next,proceduralActive_);
        if(!budget.reserve(candidateCharge)){
            ++stats_.gpuAllocationBudgetDenied;
            if(shrinking)++stats_.gpuPhysicalShrinkFailures;else{++stats_.gpuPhysicalGrowthFailures;gpuGrowthBudgetBlocked_=true;}
            return false;
        }
    }
    struct Replacement {
        gfx::BufferHandle* owner;
        size_t stride;
        bool preserve;
        const char* label;
        gfx::BufferHandle fresh{};
    };
    // Scratch data is regenerated this frame. Only persistent particle state,
    // ownership, current alive list and free stack need copying.
    Replacement replacements[]{
        {&states_,sizeof(ParticleState),true,"SpectraFX grown states"},
        {&seeds_,sizeof(ParticleState),false,"SpectraFX grown spawn queue"},
        {&slots_,8,false,"SpectraFX grown spawn owners"},
        {&gpuSourceOwners_,8,true,"SpectraFX grown physical owners"},
        {&killSlots_,4,false,"SpectraFX grown kill indices"},
        {&seedProgramParams_,sizeof(ProgramParams),false,"SpectraFX grown seed params"},
        {&stateProgramParams_,sizeof(ProgramParams),true,"SpectraFX grown state params"},
        {&stateAttributes_,sizeof(ProgramAttributes),true,"SpectraFX grown attributes"},
        {&liveSlots_,4,true,"SpectraFX grown free stack"},
        {&gpuAliveCurrent_,4,true,"SpectraFX grown current alive"},
        {&gpuAliveNext_,4,false,"SpectraFX grown next alive"},
        {&vertices_,sizeof(Vertex)*(proceduralActive_?1u:4u),false,"SpectraFX grown geometry"}
    };
    gfx::BufferHandle counters{},indices{};
    const auto discard=[&](){
        for(auto& r:replacements)if(r.fresh.valid())device_->destroyBuffer(r.fresh);
        if(counters.valid())device_->destroyBuffer(counters);
        if(indices.valid())device_->destroyBuffer(indices);
        retireAllocationCharge(candidateCharge,candidateFence);
        if(shrinking)++stats_.gpuPhysicalShrinkFailures;else ++stats_.gpuPhysicalGrowthFailures;
        return false;
    };
    std::vector<uint32_t> emptyFree;
    if(empty){
        emptyFree.resize(next);
        for(uint32_t i=0;i<next;++i)emptyFree[i]=next-1-i;
    }
    for(auto& r:replacements){
        // Unused state is never visited by the dense alive list; births fully
        // initialize the slot before it becomes visible to update/render.
        const void* data=empty&&r.owner==&liveSlots_?emptyFree.data():nullptr;
        r.fresh=buffer(*device_,gfx::BufferType::Storage,size_t(next)*r.stride,data,r.label);
        if(!r.fresh.valid())return discard();
    }
    const std::array<uint32_t,8> emptyCounters{0,next,0,0,0,0,0,0};
    counters=buffer(*device_,gfx::BufferType::Storage,sizeof(emptyCounters),empty?emptyCounters.data():nullptr,"SpectraFX replacement allocator counters");
    if(!counters.valid())return discard();
    const bool wide=gpuEmitterUsesWideIndices(desc_.capacity);
    if(!proceduralActive_){
        const auto build=[&]<class Index>(){
            std::vector<Index> data(size_t(next)*6);
            for(uint32_t n=0;n<next;++n){
                const uint32_t i=n*4;const size_t at=size_t(n)*6;
                data[at]=Index(i);data[at+1]=Index(i+1);data[at+2]=Index(i+2);
                data[at+3]=Index(i);data[at+4]=Index(i+2);data[at+5]=Index(i+3);
            }
            return buffer(*device_,gfx::BufferType::Index,data.size()*sizeof(Index),data.data(),"SpectraFX grown billboard indices");
        };
        indices=wide?build.operator()<uint32_t>():build.operator()<uint16_t>();
        if(!indices.valid())return discard();
    }
    size_t copied=0;
    if(compact){
        if(!compactAllocationCompute_.valid())
            compactAllocationCompute_=device_->createCompute({shaders::kCompactAllocationMetal,shaders::kCompactAllocationGLSL,
                "SpectraFX live allocation compaction",gfx::VulkanComputeLayout::StorageUniforms,14});
        if(!compactAllocationCompute_.valid()||!device_->beginCompute(compactAllocationCompute_))return discard();
        const auto fence=device_->gpuSubmissionFence();
        candidateFence=fence?fence:GpuAllocationBudget::unknownFence;
        markGpuAllocationUsed();
        device_->setComputeBuffer(0,states_);
        device_->setComputeBuffer(1,stateProgramParams_);
        device_->setComputeBuffer(2,stateAttributes_);
        device_->setComputeBuffer(3,gpuSourceOwners_);
        device_->setComputeBuffer(4,gpuAliveCurrent_);
        device_->setComputeBuffer(5,aliveCounter_);
        device_->setComputeBuffer(6,replacements[0].fresh);
        device_->setComputeBuffer(7,replacements[6].fresh);
        device_->setComputeBuffer(9,replacements[7].fresh);
        device_->setComputeBuffer(10,replacements[3].fresh);
        device_->setComputeBuffer(11,replacements[9].fresh);
        device_->setComputeBuffer(12,replacements[8].fresh);
        device_->setComputeBuffer(13,counters);
        const std::array<uint32_t,4> u{old,next,0,0};
        device_->setComputeUniforms(u.data(),sizeof(u));
        const bool ok=device_->dispatchChecked(next);
        device_->endCompute();
        if(!ok)return discard();
        ++stats_.gpuDispatches;
    }else if(!empty){
    // Even a partially encoded copy can retain candidate resources.
    const auto fence=device_->gpuSubmissionFence();
    candidateFence=fence?fence:GpuAllocationBudget::unknownFence;
    markGpuAllocationUsed();
    copied=8*sizeof(uint32_t);
    for(auto& r:replacements)if(r.preserve){
        const size_t bytes=size_t(old)*r.stride;
        if(!device_->copyStorageBufferChecked(*r.owner,r.fresh,bytes))return discard();
        copied+=bytes;
    }
    if(!device_->copyStorageBufferChecked(aliveCounter_,counters,8*sizeof(uint32_t)))return discard();
    // All mutations below target unpublished replacement free stack/counters.
    // On a rejected dispatch, old allocator state is still authoritative.
    if(!device_->beginCompute(compute_))return discard();
    bindGpuAllocationBuffers();
    device_->setComputeBuffer(4,replacements[8].fresh);
    device_->setComputeBuffer(5,counters);
    Uniforms u{};u.meta={14,next,old,0};
    device_->setComputeUniforms(&u,sizeof(u));
    bool ok=device_->dispatchChecked(next-old);
    if(ok){
        u.meta[0]=15;device_->setComputeUniforms(&u,sizeof(u));
        ok=device_->dispatchChecked(1);
    }
    device_->endCompute();
    if(!ok)return discard();
    stats_.gpuDispatches+=2;
    }
    for(auto& r:replacements){device_->destroyBuffer(*r.owner);*r.owner=r.fresh;r.fresh={};}
    device_->destroyBuffer(aliveCounter_);aliveCounter_=counters;counters={};
    if(indices.valid()){device_->destroyBuffer(indices_);indices_=indices;indices={};}
    size_t bytesPerSlot=0;for(const auto& r:replacements)bytesPerSlot+=r.stride;
    if(!proceduralActive_)bytesPerSlot+=6u*(wide?4u:2u);
    if(shrinking){
        if(compact)++stats_.gpuLiveCompactions;
        const size_t reclaimed=size_t(old-next)*bytesPerSlot;
        stats_.gpuBytes-=reclaimed;stats_.gpuReclaimedBytes+=reclaimed;
        ++stats_.gpuPhysicalShrinks;
    }else{
        stats_.gpuBytes+=size_t(next-old)*bytesPerSlot;
        stats_.gpuGrowthCopiedBytes+=copied;
        ++stats_.gpuPhysicalGrowths;
    }
    retireAllocationCharge(gpuAllocationCharge_,gpuAllocationLastFence_);
    gpuAllocationCharge_=candidateCharge;gpuAllocationLastFence_=candidateFence;
    physicalCapacity_=next;
    ++vmFrameRevision_;
    verticesReady_=false;
    return true;
}
bool ComputeParticles::shrinkEmptyGpuAllocation(){
    if(!gpuDemandGrowth_||!gpuEmptyProven_||gpuResetDirty_||gpuStagingDirty_||
       !pendingStates_.empty()||!pendingGpuKills_.empty())return false;
    const uint32_t target=gpuInitialPhysicalCapacity(desc_.capacity);
    if(physicalCapacity_<=target)return true;
    // Empty proof guarantees current dispatch args already describe zero
    // alive (one guarded threadgroup). The next birth rewrites draw args.
    return replaceGpuAllocation(target,true);
}
void ComputeParticles::serviceGpuIdleReclaim(uint32_t pendingBirths){
    if(!gpuDemandGrowth_)return;
    if(pendingBirths||!pendingGpuKills_.empty()){
        gpuIdleSteps_=0;
        if(pendingBirths){gpuEmptyProven_=false;gpuCompactTarget_=0;}
        return;
    }
    if(gpuEmptyProven_){
        gpuAllocationSubmitted_=false;
        gpuBirthEstimates_.clear();gpuEstimatedBirths_=0;
        if(physicalCapacity_>gpuInitialPhysicalCapacity(desc_.capacity)&&
           ++gpuIdleSteps_>=kGpuIdleReclaimSteps){
            gpuIdleSteps_=0;(void)shrinkEmptyGpuAllocation();
        }
        return;
    }
    if(gpuCompactTarget_){
        const uint32_t target=gpuCompactTarget_;gpuCompactTarget_=0;
        gpuIdleSteps_=0;
        if(target<physicalCapacity_&&!gpuResetDirty_&&!gpuStagingDirty_)
            (void)replaceGpuAllocation(target,false,true);
        return;
    }
    // Sparse live pools also get a bounded proof even if conservative
    // lifespan estimates overstate occupancy after selective kills.
    if(!gpuAllocationSubmitted_||(gpuEstimatedBirths_&&physicalCapacity_<=gpuInitialPhysicalCapacity(desc_.capacity))){
        gpuIdleSteps_=0;return;
    }
    if(++gpuIdleSteps_<kGpuIdleReclaimSteps)return;
    gpuIdleSteps_=0;
    const auto epoch=gpuBirthEpoch_;
    if(requestAllocationCounters([this,epoch](bool ok,AllocationCounters observed){
        if(ok&&epoch==gpuBirthEpoch_&&!gpuResetDirty_&&!gpuStagingDirty_&&
           observed.alive<=physicalCapacity_&&observed.free==physicalCapacity_-observed.alive){
            if(observed.alive==0){
                gpuEmptyProven_=true;
                gpuIdleSteps_=kGpuIdleReclaimSteps-1;
            }else if(physicalCapacity_>gpuInitialPhysicalCapacity(desc_.capacity)&&
                     observed.alive<=physicalCapacity_/4){
                gpuCompactTarget_=gpuNextPhysicalCapacity(gpuInitialPhysicalCapacity(desc_.capacity),
                    observed.alive*2,desc_.capacity);
            }
        }
    }))++stats_.gpuIdleProofRequests;
}
bool ComputeParticles::stepGpuAllocated(float dt){
    stats_.hostActiveSlotVisits=stats_.hostOccupiedPages=0;
    uint32_t count=uint32_t(pendingStates_.size());
    serviceGpuIdleReclaim(count);
    if(!gpuAllocationSubmitted_&&!count&&pendingGpuKills_.empty()){
        drawCount_=0;verticesReady_=true;stats_.alive=stats_.submittedSlots=0;return true;
    }
    const auto reject=[&](const char* why){
        ++stats_.gpuStagingUploadFailures;gpuStagingDirty_=true;
        verticesReady_=false;drawCount_=0;reason_=why;return false;
    };
    if(count&&gpuDemandGrowth_){
        const uint32_t required=uint32_t(std::min<uint64_t>(desc_.capacity,gpuEstimatedBirths_+count));
        gpuGrowthBudgetBlocked_=false;
        if(!growGpuAllocation(required)){
            if(!gpuGrowthBudgetBlocked_){
                verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
                reason_="GPU physical capacity growth rejected; pending births retained";return false;
            }
            // Budget pressure is not a device failure. Keep simulating all
            // resident particles; bound staging to the existing physical pool.
            // GPU reserve still admits only actually free slots.
            const auto bounded=std::min(count,allocatedCapacity());
            stats_.gpuBudgetLimitedBirths+=count-bounded;stats_.dropped+=count-bounded;
            pendingStates_.resize(bounded);pendingGpuOwners_.resize(bounded);pendingProgramParams_.resize(bounded);
            count=bounded;
        }
    }
    markGpuAllocationUsed(); // replacement may have installed a new charge
    const uint32_t cap=allocatedCapacity();
    const uint32_t zero=0;
    if(!device_->updateStorageBufferChecked(vmErrors_,&zero,sizeof(zero)))return reject("GPU allocator VM counter upload rejected");
    if(count){
        std::vector<uint32_t> tokens;tokens.reserve(size_t(count)*2);
        for(uint64_t token:pendingGpuOwners_){tokens.push_back(uint32_t(token));tokens.push_back(uint32_t(token>>32));}
        if(!device_->updateStorageBufferChecked(seeds_,pendingStates_.data(),size_t(count)*sizeof(ParticleState))||
           !device_->updateStorageBufferChecked(slots_,tokens.data(),tokens.size()*sizeof(uint32_t))||
           !device_->updateStorageBufferChecked(seedProgramParams_,pendingProgramParams_.data(),size_t(count)*sizeof(ProgramParams)))
            return reject("GPU allocator birth upload rejected");
        stats_.uploadedBytes+=size_t(count)*(sizeof(ParticleState)+sizeof(ProgramParams)+8u);
    }
    if(!device_->beginCompute(compute_))return reject("GPU allocator compute begin rejected");
    bindGpuAllocationBuffers();
    Uniforms u{};
    u.gravityDt={desc_.gravityY,dt,desc_.drag,proceduralActive_?1.f:0.f};
    u.rightSize={right_.x,right_.y,right_.z,desc_.size};u.up={up_.x,up_.y,up_.z,(dt>0.f||count>0)?1.f:0.f};
    u.meta={0,cap,count,0};
    const bool cull=viewReady_&&std::all_of(viewProj_.m,viewProj_.m+16,[](float x){return std::isfinite(x);});
    if(cull){
        std::memcpy(u.cullViewProj,viewProj_.m,sizeof(u.cullViewProj));
        u.cullControl={1.f,float(viewWidth_),float(viewHeight_),0.f};
        // High is the full-density reference. Medium reuses the existing
        // quality distance as a soft LOD scale, never a simulation cutoff.
        if(desc_.quality==fx::FxQuality::Medium){
            const float farDistance=fx::limitsFor(desc_.quality).cullDistance;
            u.cullControl.w=farDistance;
            u.lodEye={eye_.x,eye_.y,eye_.z,farDistance*0.25f};
        }
    }
    const bool indirect=device_->supportsGpuComputeIndirect();
    auto dispatch=[&](uint32_t mode,uint32_t threads,bool list){
        u.meta[0]=mode;device_->setComputeUniforms(&u,sizeof(u));
        const bool ok=list&&indirect?device_->dispatchGpuIndirect(gpuDispatchArgs_):device_->dispatchChecked(threads);
        if(ok){++stats_.gpuDispatches;if(list&&indirect)++stats_.gpuAliveIndirectDispatches;}
        return ok;
    };
    bool ok=true;
    for(uint64_t token:pendingGpuKills_){
        u.meta[2]=uint32_t(token);u.meta[3]=uint32_t(token>>32);
        if(!dispatch(9,cap,true)){ok=false;break;}
    }
    u.meta[2]=count;u.meta[3]=0;
    const auto frame=gpu_pipeline::gpuAllocationPlan(cap,count,dt>0.f||count>0);
    for(uint32_t i=0;ok&&i<frame.count;++i){
        const auto& phase=frame.dispatches[i];
        const bool list=phase.phase==gpu_pipeline::Phase::UpdateAliveList||phase.phase==gpu_pipeline::Phase::RenderAliveList;
        ok=dispatch(phase.shaderMode,phase.threads,list);
        if(ok&&phase.phase==gpu_pipeline::Phase::UpdateAliveList)++stats_.gpuCompactDispatches;
        if(ok&&phase.phase==gpu_pipeline::Phase::ReserveFreeSlots)++stats_.gpuFreeReservationDispatches;
    }
    device_->endCompute();
    if(!ok){
        // Some mutations may already have been encoded: never retry births
        // against an uncertain allocator. Checked reset is required.
        gpuResetDirty_=true;verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
        reason_="GPU alive-list dispatch rejected; checked reset required";return false;
    }
    std::swap(gpuAliveCurrent_,gpuAliveNext_);
    gpuCullViewUsed_=cull;
    gpuAllocationSubmitted_|=count!=0;
    if(count){
        ++gpuBirthEpoch_;gpuEmptyProven_=false;gpuIdleSteps_=0;gpuCompactTarget_=0;
        gpuBirthEstimates_.push_back({gpuAllocationClock_+desc_.lifetime,count});
        gpuEstimatedBirths_+=count;stats_.gpuAllocationRequests+=count;
    }
    gpuAllocationClock_+=dt;
    while(!gpuBirthEstimates_.empty()&&gpuBirthEstimates_.front().expires<=gpuAllocationClock_){
        gpuEstimatedBirths_-=gpuBirthEstimates_.front().count;gpuBirthEstimates_.pop_front();
    }
    const bool mutateParticles=dt>0.f||count||!pendingGpuKills_.empty();
    pendingStates_.clear();pendingProgramParams_.clear();pendingGpuOwners_.clear();pendingGpuKills_.clear();
    gpuStagingDirty_=false;verticesReady_=true;stats_.gpuVmErrorsKnown=false;
    if(mutateParticles)++vmFrameRevision_;
    stats_.alive=uint32_t(std::min<uint64_t>(cap,gpuEstimatedBirths_));
    stats_.submittedSlots=stats_.alive; // estimate, actual indirect count stays GPU-side
    drawCount_=gpuAllocationSubmitted_?cap:0;reason_.clear();
    if(cap<=kGpuCheckpointCaptureMaxSlots&&checkpointCadence_.due(vmFrameRevision_)){
        if(!asyncCheckpointPending_&&requestGpuCheckpoint())checkpointCadence_.accepted(vmFrameRevision_);
        else checkpointCadence_.deferred(vmFrameRevision_);
        stats_.checkpointAdaptiveBackoffs=checkpointCadence_.backoffs();
        stats_.checkpointAdaptiveRecoveries=checkpointCadence_.recoveries();
    }
    return true;
}
void ComputeParticles::draw(const fx::FxFog& fog) {
    stats_.drawCalls=0;
    if(!initialized_||!viewReady_||desc_.quality==fx::FxQuality::Off)return;
    if(backend_==ComputeBackend::Cpu) {
        cpu_.render(fog);stats_.drawCalls=uint32_t(cpu_.stats().drawCalls);return;
    }
    if(gpuResetDirty_ || !verticesReady_ || !pipeline_.valid() ||
       drawCount_==0)return;
    markGpuAllocationUsed();
    DrawUniforms u{};
    std::memcpy(u.viewProj,viewProj_.m,sizeof(u.viewProj));
    u.fog[0]=fog.color.x;u.fog[1]=fog.color.y;u.fog[2]=fog.color.z;u.fog[3]=fog.density;
    u.camera[0]=eye_.x;u.camera[1]=eye_.y;u.camera[2]=eye_.z;u.camera[3]=1;
    u.params[0]=1;u.params[1]=0;u.params[2]=0;u.params[3]=0;
    u.right[0]=right_.x;u.right[1]=right_.y;u.right[2]=right_.z;
    u.up[0]=up_.x;u.up[1]=up_.y;u.up[2]=up_.z;
    device_->setPipeline(pipeline_);
    device_->setVertexBuffer(vertices_);
    device_->setIndexBuffer(indices_);
    device_->setUniforms(0,&u,sizeof(u));
    device_->setTexture(0,texture_);
    if(gpuIndirectActive_) {
        // No CPU readback, and no unsafe direct-draw fallback when GPU
        // authored arguments cannot be submitted. CPU's count is only an
        // estimate and may not match the actual atomic compacted count.
        if(!device_->drawIndexedGpuIndirect(gpuDrawArgs_,0)) {
            reason_="SpectraFX GPU-authored indirect draw was rejected by RHI";
            return;
        }
        ++stats_.gpuIndirectDraws;
    } else {
        if(proceduralActive_)device_->drawIndexed(6,0,0,drawCount_);
        else device_->drawIndexed(drawCount_*6);
    }
    stats_.drawCalls=1;
}
bool ComputeParticles::requestAllocationCounters(std::function<void(bool,AllocationCounters)> callback){
    if(!callback||!gpuAllocationActive_||!gpuActive()||!device_||!device_->gpuHealthy()||
       !device_->canUpdateSharedBuffers()||gpuResetDirty_||gpuStagingDirty_||
       asyncAllocationCountersPending_||!readbackLifetime_)return false;
    markGpuAllocationUsed();
    const auto revision=vmFrameRevision_;
    const auto capacity=allocatedCapacity();
    std::weak_ptr<uint64_t> lifetime(readbackLifetime_);
    asyncAllocationCountersPending_=true;
    if(!device_->readBufferAsync(aliveCounter_,8*sizeof(uint32_t),
        [this,lifetime,revision,capacity,callback=std::move(callback)](std::vector<uint8_t> bytes) mutable{
            if(!lifetime.lock())return;
            asyncAllocationCountersPending_=false;
            AllocationCounters result{};result.revision=revision;
            bool ok=bytes.size()==8*sizeof(uint32_t);
            if(ok){
                std::array<uint32_t,8> raw{};
                std::memcpy(raw.data(),bytes.data(),bytes.size());
                result.alive=raw[0];result.free=raw[1];result.reservedBase=raw[2];result.admitted=raw[3];result.visible=raw[6];result.lodRejected=raw[7];
                ok=result.visible<=result.alive&&result.lodRejected<=result.alive-result.visible&&result.free<=capacity&&result.alive<=capacity&&result.reservedBase<=capacity&&
                   result.admitted<=capacity-result.reservedBase&&result.alive==capacity-result.free;
            }
            callback(ok,result);
        })){
        asyncAllocationCountersPending_=false;return false;
    }
    return true;
}
bool ComputeParticles::auditGpuAllocation(AllocationAudit& out){
    out={};
    if(!gpuActive()||!gpuAllocationActive_||!device_||!device_->gpuHealthy()||
       gpuResetDirty_||gpuStagingDirty_||!device_->canUpdateSharedBuffers())return false;
    const auto cap=allocatedCapacity();
    std::array<uint32_t,8> raw{};
    AllocationAudit result;
    if(!device_->readBuffer(aliveCounter_,raw.data(),sizeof(raw))||
       !device_->readBuffer(gpuDrawArgs_,&result.draw,sizeof(result.draw))||
       !device_->readBuffer(vmErrors_,&result.vmErrors,sizeof(result.vmErrors)))return false;
    if(raw[0]>cap||raw[1]!=cap-raw[0]||raw[6]>raw[0]||raw[7]>raw[0]-raw[6]||
       raw[2]>cap||raw[3]>cap-raw[2])return false;
    result.capacity=cap;
    result.counters.alive=raw[0];result.counters.free=raw[1];
    result.counters.reservedBase=raw[2];result.counters.admitted=raw[3];
    result.counters.visible=raw[6];result.counters.lodRejected=raw[7];
    result.counters.revision=vmFrameRevision_;
    // Reset may leave previously published draw args unreferenced. Only
    // validate them for a currently submitted allocator frame.
    if(gpuAllocationSubmitted_ && (result.draw.indexCount!=(proceduralActive_?6u:raw[6]*6u)||
       result.draw.instanceCount!=(proceduralActive_?raw[6]:1u)||
       result.draw.firstIndex||result.draw.baseVertex||result.draw.firstInstance))return false;
    std::vector<uint32_t> alive(raw[0]),free(raw[1]);
    std::vector<uint32_t> owners(size_t(cap)*2);
    if((!alive.empty()&&!device_->readBuffer(gpuAliveCurrent_,alive.data(),alive.size()*4))||
       (!free.empty()&&!device_->readBuffer(liveSlots_,free.data(),free.size()*4))||
       (!alive.empty()&&!device_->readBuffer(gpuSourceOwners_,owners.data(),owners.size()*4)))return false;
    std::vector<uint8_t> seen(cap,0);
    for(uint32_t slot:alive){
        if(slot>=cap||seen[slot]++)return false;
        result.liveOwners.push_back(uint64_t(owners[size_t(slot)*2])|(uint64_t(owners[size_t(slot)*2+1])<<32));
    }
    for(uint32_t slot:free)if(slot>=cap||seen[slot]++)return false;
    out=std::move(result);
    return true;
}
bool ComputeParticles::readGpuVmErrors(uint32_t& count) {
    stats_.gpuVmErrorsKnown=false;
    if(!initialized_ || backend_==ComputeBackend::Cpu || !device_ ||
       !vmErrors_.valid() || !device_->gpuHealthy() ||
       gpuResetDirty_ || gpuStagingDirty_ ||
       !device_->canUpdateSharedBuffers())return false;
    uint32_t observed=0;
    if(!device_->readBuffer(vmErrors_,&observed,sizeof(observed)))return false;
    count=observed;
    stats_.gpuVmErrorsLastRead=observed;
    stats_.gpuVmErrorsKnown=true;
    return true;
}
bool ComputeParticles::requestGpuVmErrors(
    std::function<void(bool,uint32_t)> callback) {
    if(!callback || !initialized_ || backend_==ComputeBackend::Cpu ||
       !device_ || !vmErrors_.valid() || !device_->gpuHealthy() ||
       gpuResetDirty_ || gpuStagingDirty_ ||
       !device_->canUpdateSharedBuffers() || asyncErrorPending_ ||
       !readbackLifetime_)return false;
    const uint64_t revision=vmFrameRevision_;
    std::weak_ptr<uint64_t> lifetime(readbackLifetime_);
    asyncErrorPending_=true;
    if(!device_->readBufferAsync(vmErrors_,sizeof(uint32_t),
         [this,lifetime,revision,callback=std::move(callback)](
             std::vector<uint8_t> bytes) mutable {
             if(!lifetime.lock())return; // shutdown/device loss
             asyncErrorPending_=false;
             uint32_t errors=0;
             const bool ok=bytes.size()==sizeof(errors);
             if(ok)std::memcpy(&errors,bytes.data(),sizeof(errors));
             // Result describes the requested frame, not a later frame.
             if(ok && revision==vmFrameRevision_ && gpuActive()) {
                 stats_.gpuVmErrorsLastRead=errors;
                 stats_.gpuVmErrorsKnown=true;
             }
             callback(ok,errors);
         })) {
        asyncErrorPending_=false;
        return false;
    }
    return true;
}

namespace {
bool finishOwnedCheckpoint(GpuCheckpoint&,const uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*);
}
bool ComputeParticles::requestGpuCheckpoint(std::function<void(bool)> callback) {
    if(!initialized_ || backend_==ComputeBackend::Cpu || !device_ ||
       !device_->gpuHealthy() || !device_->canUpdateSharedBuffers() ||
       !readbackLifetime_ || asyncCheckpointPending_ ||
       gpuResetDirty_ || gpuStagingDirty_ ||
       !states_.valid() || !stateProgramParams_.valid() ||
       !stateAttributes_.valid() ||
       !pendingStates_.empty() || !pendingKillSlots_.empty() || !pendingGpuKills_.empty() ||
        !killedSlotsAwaitingFrame_.empty())return false;

    const bool owned=gpuAllocationActive_;
    const size_t n=owned?allocatedCapacity():ages_.size();
    if(!n||n>kGpuCheckpointCaptureMaxSlots)return false;
    const std::array<size_t,7> lengths{
        n*sizeof(ParticleState),n*sizeof(ProgramParams),n*sizeof(ProgramAttributes),
        owned?n*8:0,owned?n*4:0,owned?n*4:0,owned?8*sizeof(uint32_t):0
    };
    constexpr size_t kChunkBytes=65536;
    size_t chunks=0;
    for(size_t bytes:lengths)chunks+=(bytes+kChunkBytes-1)/kChunkBytes;
    if(chunks==0 || chunks>16)return false;

    struct Pending {
        std::array<std::vector<uint8_t>,7> bytes;
        bool owned=false;
        size_t physicalSlots=0;
        std::vector<float> remainingLifetime;
        std::vector<uint32_t> freeSlotOrder;
        std::vector<uint64_t> sourceTokens;
        std::array<float,8> birthParameters{};
        uint64_t frameRevision=0, frameSerial=0;
        uint32_t shaderBirthSeed=0, shaderBirthOrdinal=0;
        bool shaderBirthSeedEnabled=false;
        size_t left=0;
        bool failed=false, acceptedAll=false;
        std::function<void(bool)> completion;
        std::shared_ptr<CheckpointReadbackBudget> budget;
        size_t reservedBytes=0, returnedBytes=0;
        uint32_t reservedChunks=0;
        ~Pending() {
            // All accepted callbacks release their captures before this
            // reservation is settled, even on partial enqueue or teardown.
            if(budget)budget->settle(reservedBytes,reservedChunks,returnedBytes);
        }
    };
    auto pending=std::make_shared<Pending>();
    pending->owned=owned;pending->physicalSlots=n;
    if(!owned){pending->remainingLifetime=ages_;pending->freeSlotOrder=freeSlots_;pending->sourceTokens=slotSourceTokens_;}
    pending->birthParameters=programParams_;
    pending->shaderBirthSeed=shaderBirthSeed_;
    pending->shaderBirthOrdinal=shaderBirthOrdinal_;
    pending->shaderBirthSeedEnabled=shaderBirthSeedEnabled_;
    pending->frameRevision=vmFrameRevision_;
    pending->frameSerial=device_->frameSerial();
    pending->completion=std::move(callback);
    pending->left=chunks;
    size_t totalBytes=0;
    for(size_t i=0;i<lengths.size();++i){pending->bytes[i].resize(lengths[i]);totalBytes+=lengths[i];}
    if(desc_.checkpointBudget &&
       !desc_.checkpointBudget->tryReserve(
            pending->frameSerial,totalBytes,uint32_t(chunks))) {
        ++stats_.checkpointBudgetDenied;
        return false; // cadence skips this capture; never block rendering
    }
    pending->budget=desc_.checkpointBudget;
    pending->reservedBytes=totalBytes;
    pending->reservedChunks=uint32_t(chunks);
    const std::weak_ptr<uint64_t> lifetime=readbackLifetime_;
    asyncCheckpointPending_=true;
    ++stats_.checkpointRequests;

    markGpuAllocationUsed();
    const std::array<gfx::BufferHandle,7> buffers{
        states_,stateProgramParams_,stateAttributes_,gpuSourceOwners_,liveSlots_,gpuAliveCurrent_,aliveCounter_
    };
    bool allAccepted=true;
    size_t acceptedChunks=0;
    for(size_t b=0;b<buffers.size() && allAccepted;++b) {
        for(size_t offset=0;offset<lengths[b];offset+=kChunkBytes) {
            const size_t length=std::min(kChunkBytes,lengths[b]-offset);
            const bool accepted=device_->readBufferRangeAsync(
                buffers[b],offset,length,
                [this,lifetime,pending,b,offset,length](
                    std::vector<uint8_t> data) {
                    const bool live=bool(lifetime.lock());
                    if(data.size()!=length)pending->failed=true;
                    else {
                        pending->returnedBytes+=length;
                        if(live) {
                            stats_.checkpointBytesReturned+=length;
                            std::memcpy(pending->bytes[b].data()+offset,
                                        data.data(),length);
                        }
                    }
                    if(!live)return; // destroyed/fallback; budget still settles
                    if(--pending->left!=0)return;
                    asyncCheckpointPending_=false;
                    bool complete=pending->acceptedAll && !pending->failed &&
                        gpuActive() && pending->owned==gpuAllocationActive_ &&
                        (pending->owned||pending->remainingLifetime.size()==ages_.size());
                    if(complete) {
                        GpuCheckpoint result;
                        const size_t total=pending->physicalSlots;
                        result.slots.resize(total);
                        result.remainingLifetime=pending->remainingLifetime;
                        result.freeSlotOrder=pending->freeSlotOrder;
                        result.sourceTokens=pending->sourceTokens;
                        result.birthParameters=pending->birthParameters;
                        result.hostStateComplete=true;
                        result.frameSerial=pending->frameSerial;
                        result.shaderBirthSeed=pending->shaderBirthSeed;
                        result.shaderBirthOrdinal=pending->shaderBirthOrdinal;
                        result.shaderBirthSeedEnabled=pending->shaderBirthSeedEnabled;
                        for(size_t i=0;i<total;++i) {
                            auto& dst=result.slots[i];
                            std::memcpy(&dst.positionAge,
                                pending->bytes[0].data()+i*sizeof(ParticleState),
                                sizeof(ParticleState));
                            std::memcpy(dst.programParams.data(),
                                pending->bytes[1].data()+i*sizeof(ProgramParams),
                                sizeof(ProgramParams));
                            std::memcpy(dst.attributes.data(),
                                pending->bytes[2].data()+i*sizeof(ProgramAttributes),
                                sizeof(ProgramAttributes));
                        }
                        if(pending->owned)complete=finishOwnedCheckpoint(result,pending->bytes[3].data(),
                            pending->bytes[4].data(),pending->bytes[5].data(),pending->bytes[6].data());
                        if(complete){
                            result.complete=true;
                            lastCheckpoint_=std::move(result);
                            checkpointRevision_=pending->frameRevision;
                            ++stats_.checkpointCompleted;
                        }
                    }
                    if(!complete&&pending->acceptedAll)++stats_.checkpointFailed;
                    if(pending->acceptedAll) {
                        checkpointCadence_.completed(complete);
                        stats_.checkpointAdaptiveBackoffs=checkpointCadence_.backoffs();
                        stats_.checkpointAdaptiveRecoveries=checkpointCadence_.recoveries();
                    }
                    if(pending->completion)pending->completion(complete);
                });
            if(!accepted) {allAccepted=false;break;}
            ++acceptedChunks;
        }
    }
    if(!allAccepted) {
        // Already accepted readbacks remain in-flight. They finish without
        // publishing a checkpoint or invoking a callback for a rejected API
        // request. A new request is permitted once they have all completed.
        pending->failed=true;
        pending->completion={};
        pending->left=acceptedChunks;
        if(acceptedChunks==0)asyncCheckpointPending_=false;
        ++stats_.checkpointFailed;
        return false;
    }
    pending->acceptedAll=true;
    return true;
}

GpuCheckpointStatus ComputeParticles::checkpointStatus() const {
    GpuCheckpointStatus status;
    status.gpuActive=gpuActive();
    status.pending=asyncCheckpointPending_;
    status.complete=lastCheckpoint_.complete;
    status.unsentBirths=!pendingStates_.empty();
    status.stagingRecoveryPending=gpuStagingDirty_;
    status.currentRevision=vmFrameRevision_;
    status.capturedRevision=checkpointRevision_;
    status.physicalSlots=gpuAllocationActive_?allocatedCapacity():static_cast<uint32_t>(ages_.size());
    status.killsAwaitingGpuFence=uint32_t(killedSlotsAwaitingFrame_.size());
    status.checkpointIntervalFrames=checkpointCadence_.intervalFrames();
    status.nextCheckpointRevision=checkpointCadence_.nextDueRevision();
    if(status.complete) {
        status.frameSerial=lastCheckpoint_.frameSerial;
        status.framesBehind=status.currentRevision>=status.capturedRevision
            ?status.currentRevision-status.capturedRevision : 0;
        status.fresh=status.currentRevision==status.capturedRevision;
    }
    status.restoreEligible=status.gpuActive && status.fresh &&
        !gpuResetDirty_ && !gpuStagingDirty_ &&
        !status.unsentBirths && pendingKillSlots_.empty() && pendingGpuKills_.empty() &&
        killedSlotsAwaitingFrame_.empty() && lastCheckpoint_.gpuOwnedAllocation==gpuAllocationActive_ &&
        lastCheckpoint_.slots.size()==status.physicalSlots &&
        lastCheckpoint_.remainingLifetime.size()==status.physicalSlots;
    return status;
}

bool ComputeParticles::exportFreshCheckpoint(
    std::vector<uint8_t>& bytes,size_t maxBytes,std::string* why) const {
    const auto reject=[&](const char* reason) {
        if(why)*why=reason;
        return false; // never modify caller's previous byte buffer
    };
    if(!gpuActive() || !device_ || !device_->gpuHealthy() ||
       gpuResetDirty_)return reject("GPU device unavailable or reset incomplete");
    const GpuCheckpointStatus status=checkpointStatus();
    if(!status.restoreEligible || status.pending)
        return reject("no complete fresh checkpoint at current GPU revision");
    if(!encodeGpuCheckpointDiagnostic(lastCheckpoint_,checkpointRevision_,
                                       bytes,maxBytes))
        return reject("checkpoint exceeds diagnostic byte limit or has invalid slots");
    if(why)why->clear();
    return true;
}

bool ComputeParticles::exportRestoreCheckpoint(const std::string& identity,std::vector<uint8_t>& bytes) const {
    const auto status=checkpointStatus();
    if(!status.restoreEligible||status.pending||!device_||!device_->gpuHealthy())return false;
    // External source tokens are process-local and cannot be imported safely.
    for(auto token:lastCheckpoint_.sourceTokens)if(token!=0)return false;
    return encodeRestoreCheckpoint(lastCheckpoint_,identity,bytes);
}
bool ComputeParticles::importRestoreCheckpoint(const std::string& expectedIdentity,const std::vector<uint8_t>& bytes) {
    GpuCheckpoint snapshot;
    if(!decodeRestoreCheckpoint(bytes,expectedIdentity,snapshot))return false;
    for(auto token:snapshot.sourceTokens)if(token!=0)return false;
    return restoreGpuSnapshot(snapshot);
}
bool ComputeParticles::restoreFreshGpuCheckpoint() {
    const auto status=checkpointStatus();
    if(!status.restoreEligible||status.pending||!device_||
       !device_->gpuHealthy()||!device_->canUpdateSharedBuffers()||
       !lastCheckpoint_.hostStateComplete)return false;
    return restoreGpuSnapshot(lastCheckpoint_);
}
bool ComputeParticles::retainFreshGpuCheckpoint() {
    const auto status=checkpointStatus();
    if(!status.restoreEligible||status.pending||!lastCheckpoint_.hostStateComplete)return false;
    if(checkpointHistory_.size()>=8)checkpointHistory_.pop_front();
    checkpointHistory_.push_back({nextHistoricalCheckpointId_++,lastCheckpoint_});
    return true;
}
std::vector<uint64_t> ComputeParticles::historicalGpuCheckpointIds() const {
    std::vector<uint64_t> ids;
    for(const auto& entry:checkpointHistory_)ids.push_back(entry.id);
    return ids;
}
bool ComputeParticles::restoreHistoricalGpuCheckpoint(uint64_t id) {
    for(const auto& entry:checkpointHistory_)if(entry.id==id)return restoreGpuSnapshot(entry.snapshot);
    reason_="Unknown or evicted historical checkpoint";return false;
}
bool ComputeParticles::restoreGpuSnapshot(const GpuCheckpoint& source) {
    if(!gpuActive()||!device_||!device_->gpuHealthy()||!device_->canUpdateSharedBuffers()||
       gpuResetDirty_||gpuStagingDirty_||asyncCheckpointPending_||programParamsDirty_||
       !pendingStates_.empty()||!pendingKillSlots_.empty()||!killedSlotsAwaitingFrame_.empty()||
       !source.complete||!source.hostStateComplete||source.gpuOwnedAllocation!=gpuAllocationActive_||
       !pendingGpuKills_.empty())return false;
    if(gpuAllocationActive_)return restoreOwnedGpuSnapshot(source);
    const auto snapshot=source;
    const size_t n=ages_.size();
    if(snapshot.slots.size()!=n||snapshot.remainingLifetime.size()!=n)return false;
    if(snapshot.sourceTokens.size()!=n||snapshot.freeSlotOrder.size()>n)return false;
    std::vector<bool> free(n,false);
    for(uint32_t slot:snapshot.freeSlotOrder){
        if(slot>=n||free[slot])return false;
        free[slot]=true;
    }
    std::vector<ParticleState> states(n);
    std::vector<ProgramParams> params(n);
    std::vector<ProgramAttributes> attrs(n);
    for(size_t i=0;i<n;++i){
        const auto& p=snapshot.slots[i];
        const float remaining=snapshot.remainingLifetime[i];
        if(!finite(remaining)||free[i]!=(remaining<=0.f))return false;
        if(!finite(p.positionAge)||!finite(p.velocityLife)||!finite(p.tint)||!finite(p.size))return false;
        for(float v:p.programParams)if(!finite(v))return false;
        for(float v:p.attributes)if(!finite(v))return false;
        states[i].positionAge=p.positionAge;states[i].velocityLife=p.velocityLife;
        states[i].tint=p.tint;states[i].size=p.size;
        params[i].p0={p.programParams[0],p.programParams[1],p.programParams[2],p.programParams[3]};
        params[i].p1={p.programParams[4],p.programParams[5],p.programParams[6],p.programParams[7]};
        attrs[i].a0={p.attributes[0],p.attributes[1],p.attributes[2],p.attributes[3]};
        attrs[i].a1={p.attributes[4],p.attributes[5],p.attributes[6],p.attributes[7]};
    }
    for(float v:snapshot.birthParameters)if(!finite(v))return false;
    markGpuAllocationUsed();
    gpuResetDirty_=true;verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
    lastCheckpoint_={};checkpointRevision_=0;++vmFrameRevision_;
    if(!device_->updateStorageBufferChecked(states_,states.data(),n*sizeof(ParticleState))||
       !device_->updateStorageBufferChecked(stateProgramParams_,params.data(),n*sizeof(ProgramParams))||
       !device_->updateStorageBufferChecked(stateAttributes_,attrs.data(),n*sizeof(ProgramAttributes))){
        reason_="Checkpoint restore upload failed; checked reset required";
        return false;
    }
    ages_=snapshot.remainingLifetime;freeSlots_=snapshot.freeSlotOrder;
    activePages_.reset(uint32_t(n));
    for(uint32_t i=0;i<uint32_t(n);++i)if(ages_[i]>0.f)activePages_.insert(i);
    slotSourceTokens_=snapshot.sourceTokens;programParams_=snapshot.birthParameters;
    programStateCache_=std::move(params);programParamsDirty_=false;
    shaderBirthSeed_=snapshot.shaderBirthSeed;shaderBirthOrdinal_=snapshot.shaderBirthOrdinal;
    shaderBirthSeedEnabled_=snapshot.shaderBirthSeedEnabled;
    aliveSlots_.clear();
    if(!gpuIndirectActive_)for(size_t i=0;i<n;++i)if(!free[i])aliveSlots_.push_back(uint32_t(i));
    stats_.alive=uint32_t(n-freeSlots_.size());
    gpuResetDirty_=false;gpuStagingDirty_=false;reason_.clear();
    return true;
}

namespace {
bool finishOwnedCheckpoint(GpuCheckpoint& result,const uint8_t* owners,const uint8_t* freeBytes,
                           const uint8_t* aliveBytes,const uint8_t* counterBytes){
    const size_t n=result.slots.size();
    std::array<uint32_t,8> raw{};std::memcpy(raw.data(),counterBytes,sizeof(raw));
    if(raw[0]>n||raw[1]!=n-raw[0]||raw[6]>raw[0]||raw[7]>raw[0]-raw[6])return false;
    result.gpuOwnedAllocation=true;
    result.freeSlotOrder.resize(raw[1]);result.aliveSlotOrder.resize(raw[0]);
    if(raw[1])std::memcpy(result.freeSlotOrder.data(),freeBytes,size_t(raw[1])*4);
    if(raw[0])std::memcpy(result.aliveSlotOrder.data(),aliveBytes,size_t(raw[0])*4);
    result.remainingLifetime.assign(n,-1.f);result.sourceTokens.assign(n,0);
    std::vector<uint8_t> seen(n);
    for(uint32_t slot:result.freeSlotOrder){
        if(slot>=n||seen[slot]++)return false;
        result.slots[slot]={};result.slots[slot].positionAge.w=-1.f;
    }
    for(uint32_t slot:result.aliveSlotOrder){
        if(slot>=n||seen[slot]++)return false;
        uint32_t words[2]{};std::memcpy(words,owners+size_t(slot)*8,8);
        result.sourceTokens[slot]=uint64_t(words[0])|(uint64_t(words[1])<<32);
        auto& p=result.slots[slot];result.remainingLifetime[slot]=p.velocityLife.w-p.positionAge.w;
    }
    return validOwnedGpuCheckpoint(result);
}
}
bool ComputeParticles::restoreOwnedGpuSnapshot(const GpuCheckpoint& source){
    const auto snapshot=source;
    const uint32_t n=uint32_t(snapshot.slots.size());
    if(!n||n>kGpuCheckpointCaptureMaxSlots||n>desc_.capacity||!validOwnedGpuCheckpoint(snapshot))return false;
    if(n!=allocatedCapacity()){
        if(!gpuDemandGrowth_||gpuNextPhysicalCapacity(gpuInitialPhysicalCapacity(desc_.capacity),n,desc_.capacity)!=n)return false;
        if(n<allocatedCapacity()){
            if(!replaceGpuAllocation(n,true))return false;
        }else if(!growGpuAllocation(n)||allocatedCapacity()!=n)return false;
    }
    std::vector<ParticleState> states(n);
    std::vector<ProgramParams> params(n);
    std::vector<ProgramAttributes> attrs(n);
    std::vector<uint32_t> owners(size_t(n)*2);
    std::vector<float> expiry;
    for(uint32_t i=0;i<n;++i){
        const auto& p=snapshot.slots[i];
        if(snapshot.remainingLifetime[i]<=0.f){states[i].positionAge.w=-1.f;continue;}
        states[i]={p.positionAge,p.velocityLife,p.tint,p.size};
        std::memcpy(&params[i],p.programParams.data(),sizeof(ProgramParams));
        std::memcpy(&attrs[i],p.attributes.data(),sizeof(ProgramAttributes));
        owners[size_t(i)*2]=uint32_t(snapshot.sourceTokens[i]);
        owners[size_t(i)*2+1]=uint32_t(snapshot.sourceTokens[i]>>32);
        expiry.push_back(snapshot.remainingLifetime[i]);
    }
    const uint32_t alive=uint32_t(snapshot.aliveSlotOrder.size());
    const std::array<uint32_t,8> counters{alive,n-alive,0,0,alive,alive,0,0};
    const std::array<uint32_t,3> dispatch{std::max(1u,(alive+63u)/64u),1,1};
    const gfx::DrawIndexedIndirectCommand noDraw{0,1,0,0,0};
    markGpuAllocationUsed();
    gpuResetDirty_=true;verticesReady_=false;drawCount_=0;stats_.submittedSlots=0;
    ++gpuBirthEpoch_;++vmFrameRevision_;gpuCompactTarget_=0;gpuIdleSteps_=0;gpuEmptyProven_=false;
    lastCheckpoint_={};checkpointRevision_=0;
    const auto upload=[&](gfx::BufferHandle b,const void* data,size_t bytes){
        return !bytes||device_->updateStorageBufferChecked(b,data,bytes);
    };
    if(!upload(states_,states.data(),n*sizeof(ParticleState))||
       !upload(stateProgramParams_,params.data(),n*sizeof(ProgramParams))||
       !upload(stateAttributes_,attrs.data(),n*sizeof(ProgramAttributes))||
       !upload(gpuSourceOwners_,owners.data(),owners.size()*4)||
       !upload(liveSlots_,snapshot.freeSlotOrder.data(),snapshot.freeSlotOrder.size()*4)||
       !upload(gpuAliveCurrent_,snapshot.aliveSlotOrder.data(),snapshot.aliveSlotOrder.size()*4)||
       !upload(aliveCounter_,counters.data(),sizeof(counters))||
       !upload(gpuDispatchArgs_,dispatch.data(),sizeof(dispatch))||
       !upload(gpuDrawArgs_,&noDraw,sizeof(noDraw))){
        reason_="GPU-owned checkpoint restore upload failed; checked reset required";return false;
    }
    programParams_=snapshot.birthParameters;shaderBirthSeed_=snapshot.shaderBirthSeed;
    shaderBirthOrdinal_=snapshot.shaderBirthOrdinal;shaderBirthSeedEnabled_=snapshot.shaderBirthSeedEnabled;
    gpuBirthEstimates_.clear();gpuAllocationClock_=0;gpuEstimatedBirths_=alive;
    std::sort(expiry.begin(),expiry.end());
    for(float life:expiry){
        if(!gpuBirthEstimates_.empty()&&gpuBirthEstimates_.back().expires==life)++gpuBirthEstimates_.back().count;
        else gpuBirthEstimates_.push_back({life,1});
    }
    gpuAllocationSubmitted_=alive!=0;gpuEmptyProven_=alive==0;
    stats_.alive=alive;stats_.gpuVmErrorsKnown=false;gpuResetDirty_=gpuStagingDirty_=false;
    reason_.clear();return true;
}

bool ComputeParticles::captureGpuCheckpoint(GpuCheckpoint& out) {
    out={};if(asyncCheckpointPending_)return false;
    if(!initialized_ || backend_==ComputeBackend::Cpu || !device_ ||
       !device_->gpuHealthy() || !device_->canUpdateSharedBuffers() ||
       !states_.valid() || !stateProgramParams_.valid() ||
       !stateAttributes_.valid() ||
       gpuResetDirty_ || gpuStagingDirty_ ||
       !pendingStates_.empty() || !pendingKillSlots_.empty() || !pendingGpuKills_.empty() ||
        !killedSlotsAwaitingFrame_.empty())return false;
    const bool owned=gpuAllocationActive_;
    const size_t n=owned?allocatedCapacity():ages_.size();
    if(!n||n>kGpuCheckpointCaptureMaxSlots)return false;
    // A checkpoint must include all physical slots, not merely those that
    // survived the previous draw or the CPU-estimated indirect draw count.
    std::vector<ParticleState> state(n);
    std::vector<ProgramParams> params(n);
    std::vector<ProgramAttributes> attributes(n);
    if(!device_->readBuffer(states_,state.data(),n*sizeof(ParticleState)) ||
       !device_->readBuffer(stateProgramParams_,params.data(),
                            n*sizeof(ProgramParams)) ||
       !device_->readBuffer(stateAttributes_,attributes.data(),
                            n*sizeof(ProgramAttributes)))return false;
    GpuCheckpoint next;
    next.slots.resize(n);
    next.remainingLifetime=ages_;
    next.freeSlotOrder=freeSlots_;
    next.sourceTokens=slotSourceTokens_;
    next.birthParameters=programParams_;
    next.shaderBirthSeed=shaderBirthSeed_;
    next.shaderBirthOrdinal=shaderBirthOrdinal_;
    next.shaderBirthSeedEnabled=shaderBirthSeedEnabled_;
    next.hostStateComplete=true;
    next.frameSerial=device_->frameSerial();
    for(size_t i=0;i<n;++i) {
        auto& d=next.slots[i];
        d.positionAge=state[i].positionAge;
        d.velocityLife=state[i].velocityLife;
        d.tint=state[i].tint;
        d.size=state[i].size;
        d.programParams={params[i].p0.x,params[i].p0.y,params[i].p0.z,params[i].p0.w,
                         params[i].p1.x,params[i].p1.y,params[i].p1.z,params[i].p1.w};
        d.attributes={attributes[i].a0.x,attributes[i].a0.y,attributes[i].a0.z,attributes[i].a0.w,
                      attributes[i].a1.x,attributes[i].a1.y,attributes[i].a1.z,attributes[i].a1.w};
    }
    if(owned){
        std::vector<uint8_t> owners(n*8),freeBytes(n*4),aliveBytes(n*4);
        std::array<uint8_t,32> counters{};
        if(!device_->readBuffer(gpuSourceOwners_,owners.data(),owners.size())||
           !device_->readBuffer(liveSlots_,freeBytes.data(),freeBytes.size())||
           !device_->readBuffer(gpuAliveCurrent_,aliveBytes.data(),aliveBytes.size())||
           !device_->readBuffer(aliveCounter_,counters.data(),counters.size())||
           !finishOwnedCheckpoint(next,owners.data(),freeBytes.data(),aliveBytes.data(),counters.data()))return false;
    }
    next.complete=true;
    out=next;
    lastCheckpoint_=std::move(next);
    checkpointRevision_=vmFrameRevision_;
    return true;
}

void ComputeParticles::switchToCpu(const std::string& why) {
    ++stats_.gpuFallbacks;
    stats_.gpuVmErrorsKnown=false;
    // Restore only a complete checkpoint captured after the LAST submitted
    // GPU frame, with no pending unsent births. A stale checkpoint must never
    // resurrect particles that have died or silently omit recent births.
    const GpuCheckpointStatus capture=checkpointStatus();
    const bool canRestore=capture.restoreEligible&&(!gpuAllocationActive_||
        std::none_of(lastCheckpoint_.sourceTokens.begin(),lastCheckpoint_.sourceTokens.end(),[](uint64_t token){return token!=0;}));
    std::vector<fx::FxImportedParticle> restored;
    if(canRestore) {
        restored.reserve(lastCheckpoint_.slots.size());
        for(size_t i=0;i<lastCheckpoint_.slots.size();++i) {
            const auto& src=lastCheckpoint_.slots[i];
            if(src.positionAge.w<0.f || lastCheckpoint_.remainingLifetime[i]<=0.f)
                continue;
            fx::FxImportedParticle p;
            p.pos={src.positionAge.x,src.positionAge.y,src.positionAge.z};
            p.vel={src.velocityLife.x,src.velocityLife.y,src.velocityLife.z};
            p.age=src.positionAge.w;
            p.lifetime=src.velocityLife.w;
            p.size=src.size.x;
            p.tint={src.tint.x,src.tint.y,src.tint.z,src.tint.w};
            p.programParams=src.programParams;
            p.customAttributes=src.attributes;
            restored.push_back(p);
        }
    }
    // A slot intentionally queued for selective kill is not a lost particle.
    const uint32_t owned=gpuAllocationActive_?stats_.alive:uint32_t(ages_.size()-freeSlots_.size()-
                                  pendingKillSlots_.size()-
                                  killedSlotsAwaitingFrame_.size());
    releaseGpu();backend_=ComputeBackend::Cpu;
    const bool ok=initCpu();
    bool imported=false;
    if(ok && canRestore)
        imported=cpu_.importLiveParticles(cpuEffect_,restored);
    const uint32_t recovered=imported?uint32_t(restored.size()):0u;
    stats_.recoveredOnFallback+=recovered;
    stats_.lostOnFallback+=owned>=recovered?owned-recovered:0u;
    reason_=why+(ok
        ?(imported?"; restored particles from current GPU checkpoint"
                   :capture.complete && !capture.fresh
                    ?"; CPU fallback: stale GPU checkpoint ("+
                      std::to_string(capture.framesBehind)+" submissions behind); particles lost"
                    :capture.unsentBirths
                     ?"; CPU fallback: unsent GPU births; particles lost"
                     :"; CPU fallback active; checkpoint missing/incompatible")
        :"; CPU fallback init failed");
    initialized_=ok;stats_.alive=recovered;stats_.gpuBytes=0;
    stats_.submittedSlots=0;
    lastCheckpoint_={};checkpointRevision_=0;
}

} // namespace aiverse::spectra
