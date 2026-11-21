// SpectraFX Stage 23 experimental VM pass. Standalone Metal compute utility,
// not an automatic GraphRuntime backend. Caller owns the device and 64-byte
// ParticleState buffer, and owns scheduling before the particle render pass.
#pragma once
#include "SpectraGpuProgram.h"
#include "SpectraGpuProgramShaders.h"
#include "SpectraGpuSourceParameters.h"
#include "engine/gfx/Gfx.h"
#include <array>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

namespace aiverse::spectra::gpu_program {

struct DispatchUniforms {
    uint32_t particleCount = 0;
    uint32_t stage = 0;
    float dt = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(DispatchUniforms)==16);

class MetalVmPass final {
public:
    MetalVmPass() = default;
    ~MetalVmPass() { shutdown(); }
    MetalVmPass(const MetalVmPass&)=delete;
    MetalVmPass& operator=(const MetalVmPass&)=delete;

    bool init(gfx::Device* device, const Payload& program,
              uint32_t maxParticleSlots=4096) {
        shutdown();
        if(!device || device->info().backend!=gfx::Backend::Metal ||
           !device->supportsCompute()) {
            reason_="SpectraFX VM needs Metal compute";
            return false;
        }
        if(maxParticleSlots==0 || maxParticleSlots>65536 ||
           program.abiVersion!=kAbiVersion ||
           program.statementCount>kMaxStatements ||
           program.instructionCount>kMaxInstructions) {
            reason_="invalid SpectraFX GPU VM ABI or budgets";
            return false;
        }
        // Standalone MetalVmPass uses a different kernel from
        // ComputeParticles and has no custom-attribute storage binding.
        // The integrated GPU particle kernel supports attribute0..7, but
        // accepting such a program here would silently drop its semantics.
        for(uint32_t i=0;i<program.statementCount;++i) {
            const auto& st=program.statements[i];
            if(st.target>=uint32_t(program::Field::Attribute0) &&
               st.target<=uint32_t(program::Field::Attribute7)) {
                reason_="standalone MetalVmPass needs an attribute buffer for custom VM writes";
                return false;
            }
        }
        for(uint32_t i=0;i<program.instructionCount;++i) {
            const auto& op=program.instructions[i];
            if(op.opcode==uint32_t(program::Op::Read) &&
               op.field>=uint32_t(program::Field::Attribute0) &&
               op.field<=uint32_t(program::Field::Attribute7)) {
                reason_="standalone MetalVmPass needs an attribute buffer for custom VM reads";
                return false;
            }
        }
        if(!device->canUpdateSharedBuffers()) {
            reason_="create SpectraFX VM outside render/compute passes";
            return false;
        }
        device_=device;
        compute_=device_->createCompute({
            kMetalVmKernel, "", "SpectraFX experimental GPU particle VM"});
        if(!compute_.valid()) {
            reason_="Metal VM compute creation: "+device_->lastError();
            shutdownResources();
            return false;
        }
        auto create=[&](size_t bytes,const void* initial,const char* label) {
            gfx::BufferDesc desc;
            desc.type=gfx::BufferType::Storage;
            desc.usage=gfx::Usage::Immutable;
            desc.size=bytes;
            desc.data=initial;
            desc.label=label;
            return device_->createBuffer(desc);
        };
        code_=create(sizeof(Payload),&program,"SpectraFX VM bytecode");
        std::array<float,8> zeros{};
        params_=create(sizeof(zeros),zeros.data(),"SpectraFX VM params");
        uint32_t zero=0;
        errors_=create(sizeof(zero),&zero,"SpectraFX VM error count");
        snapshots_=create(size_t(maxParticleSlots)*sizeof(ParticleParams),
                          nullptr,"SpectraFX per-particle VM parameters");
        maxParticleSlots_=maxParticleSlots;
        uploadedSnapshots_.clear();
        if(!code_.valid()||!params_.valid()||!errors_.valid()||!snapshots_.valid()) {
            reason_="Metal VM buffer creation: "+device_->lastError();
            shutdownResources();
            return false;
        }
        reason_.clear();
        return true;
    }

    // Requires a buffer of at least particleCount * 64 bytes with the
    // ComputeParticles ParticleState layout. This pass never allocates or
    // retains ECS handles or per-source state. Host must ensure uniqueness
    // and that no overlapping encoder or render pass is active.
    bool dispatch(gfx::BufferHandle particleStates, uint32_t particleCount,
                  program::Stage stage, float dt,
                  const std::array<float,8>& values) {
        return dispatchImpl(particleStates,particleCount,stage,dt,values,nullptr);
    }

    // Per-particle snapshots from generation-safe presentation-only sources.
    // The host must have spawned/killed each corresponding CPU mirror slot
    // and must use the same physical slot indices in the GPU state buffer.
    bool dispatchSources(gfx::BufferHandle particleStates,uint32_t particleCount,
                         program::Stage stage,float dt,
                         const SourceParameters& sources) {
        if(particleCount>sources.particleCapacity() ||
           particleCount>maxParticleSlots_) {
            reason_="GPU VM source snapshots exceed allocated particle slots";
            return false;
        }
        return dispatchImpl(particleStates,particleCount,stage,dt,{},&sources);
    }

    bool ready() const {return device_&&compute_.valid()&&code_.valid()
                                &&params_.valid()&&errors_.valid()&&snapshots_.valid();}
    const std::string& reason() const {return reason_;}
    // GPU error counter is device-resident; dispatch success does not imply
    // every VM expression succeeded. Host must read and handle errors.
    gfx::BufferHandle errorCounter() const {return errors_;}
    // Diagnostic only: read between completed frames. Failure is not zero.
    bool readErrors(uint32_t& count) {
        if(!ready() || !device_->canUpdateSharedBuffers())return false;
        return device_->readBuffer(errors_,&count,sizeof(count));
    }
    void shutdown() {shutdownResources();reason_.clear();}
private:
    bool dispatchImpl(gfx::BufferHandle particleStates, uint32_t particleCount,
                      program::Stage stage, float dt,
                      const std::array<float,8>& values,
                      const SourceParameters* sources) {
        if(!ready()||!particleStates.valid()) {
            reason_="Metal VM not initialized or state buffer missing";
            return false;
        }
        if(stage!=program::Stage::Spawn && stage!=program::Stage::Update) {
            reason_="Metal VM accepts only Spawn or Update";
            return false;
        }
        if(!program::number(dt)||dt<0.f) {
            reason_="Metal VM timestep must be finite/nonnegative";
            return false;
        }
        for(float v:values)if(!program::number(v)) {
            reason_="Metal VM parameter is nonfinite or out of range";
            return false;
        }
        if(!device_->gpuHealthy()||!device_->canUpdateSharedBuffers()) {
            reason_="Metal VM dispatch requires a healthy frame outside render/compute passes";
            return false;
        }
        if(particleCount==0)return true;
        const uint32_t zero=0;
        device_->updateBuffer(params_,values.data(),sizeof(values));
        if(sources) {
            // Diff against this pass's last submitted snapshots. Blits run in
            // command queue order ahead of this compute dispatch.
            const auto& next=sources->particleBuffer();
            const size_t oldCount=uploadedSnapshots_.size();
            auto changed=[&](size_t i) {
                if(i>=oldCount)return true;
                const auto& a=next[i];
                const auto& b=uploadedSnapshots_[i];
                return a.values!=b.values || a.sourceSlot!=b.sourceSlot ||
                       a.generation!=b.generation || a.active!=b.active ||
                       a.reserved!=b.reserved;
            };
            size_t begin=0;
            while(begin<particleCount) {
                if(!changed(begin)) {++begin;continue;}
                size_t end=begin+1;
                while(end<particleCount && changed(end))++end;
                device_->updateBuffer(snapshots_,next.data()+begin,
                    (end-begin)*sizeof(ParticleParams),
                    begin*sizeof(ParticleParams));
                begin=end;
            }
            if(uploadedSnapshots_.size()<particleCount)
                uploadedSnapshots_.resize(particleCount);
            std::copy_n(next.begin(),particleCount,uploadedSnapshots_.begin());
        }
        device_->updateBuffer(errors_,&zero,sizeof(zero));
        if(!device_->beginCompute(compute_)) {
            reason_="Metal VM compute encoder unavailable";
            return false;
        }
        device_->setComputeBuffer(0,particleStates);
        device_->setComputeBuffer(1,code_);
        device_->setComputeBuffer(2,params_);
        device_->setComputeBuffer(3,errors_);
        device_->setComputeBuffer(4,snapshots_);
        DispatchUniforms u{particleCount,uint32_t(stage),dt,sources?1u:0u};
        device_->setComputeUniforms(&u,sizeof(u));
        device_->dispatch(particleCount);
        device_->endCompute();
        reason_.clear();
        return true;
    }

    void shutdownResources() {
        if(device_) {
            if(compute_.valid())device_->destroyCompute(compute_);
            if(code_.valid())device_->destroyBuffer(code_);
            if(params_.valid())device_->destroyBuffer(params_);
            if(errors_.valid())device_->destroyBuffer(errors_);
            if(snapshots_.valid())device_->destroyBuffer(snapshots_);
        }
        compute_={};code_={};params_={};errors_={};snapshots_={};
        maxParticleSlots_=0;uploadedSnapshots_.clear();device_=nullptr;
    }
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle compute_{};
    gfx::BufferHandle code_{},params_{},errors_{},snapshots_{};
    uint32_t maxParticleSlots_=0;
    std::vector<ParticleParams> uploadedSnapshots_;
    std::string reason_;
};
} // namespace aiverse::spectra::gpu_program
