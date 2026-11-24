// Request-local executor, embedded in TextureGraphGpuPreview. It does not
// own a Device or a thread. All calls occur at the existing owner frame boundary.
#pragma once
#include "engine/render/graph/TextureGraphGpuDag.h"
#include "engine/render/graph/TextureGraphGpuStepState.h"
#include "engine/render/graph/TextureGraphPassSchedule.h"
#include <memory>

namespace aiverse::editor {
namespace tg=render::texture_graph;
namespace tgg=tg::gpu;
class TextureGraphGpuExecution {
public:
    enum class State { Idle, Running, Ready, Failed, Cancelled };
private:
    enum class Phase { Node, Pack, Read };
    struct Range {bool done=false;std::vector<uint8_t> bytes;};
    State state_=State::Idle;
    Phase phase_=Phase::Node;
    tgg::StepFence fence_;
    tgg::EarlyPackDagPlan plan_;
    std::vector<gfx::BufferHandle> slots_;
    gfx::BufferHandle packed_{};
    std::vector<gfx::ComputeHandle> kernels_;
    gfx::ComputeHandle pack_{};
    std::shared_ptr<Range> read_;
    size_t expected_=0,received_=0,step_=0,completed_=0,passes_=0,blocks_=0,allocated_=0;
    int size_=0;
    std::vector<uint8_t> pixels_;
    std::vector<render::Image> results_;
    std::string reason_,backend_;
    void fail(std::string reason){
        state_=State::Failed;reason_=std::move(reason);
        results_.clear();pixels_.clear();completed_=0;
    }
    // Never destroy resources merely because CPU encoding or a deadline ended.
    void forgetHandles(){
        slots_.clear();kernels_.clear();packed_={};pack_={};read_.reset();
        fence_.abandon();allocated_=0;plan_={};
    }
    void releaseRetired(gfx::Device& dev){
        if(fence_.busy())return;
        for(auto p:kernels_)if(p.valid())dev.destroyCompute(p);
        if(pack_.valid())dev.destroyCompute(pack_);
        for(auto b:slots_)if(b.valid())dev.destroyBuffer(b);
        if(packed_.valid())dev.destroyBuffer(packed_);
        forgetHandles();
    }
    bool reserveSubmission(gfx::Device& dev){
        const auto token=dev.gpuSubmissionFence();
        // No GPU commands when timeline support is absent. A token already
        // complete cannot cover new work in the current recording frame.
        return token>dev.gpuCompletedFence()&&fence_.submit(token);
    }
    bool encode(gfx::Device& dev,gfx::ComputeHandle kernel,gfx::BufferHandle output,
            const std::vector<gfx::BufferHandle>& inputs){
        if(!kernel.valid()||!output.valid()||inputs.size()>tgg::maxNodeInputs)return false;
        for(auto in:inputs)if(!in.valid()||in==output)return false;
        if(!reserveSubmission(dev))return false;
        if(!dev.beginCompute(kernel))return false;
        dev.setComputeBuffer(0,output);
        for(size_t i=0;i<inputs.size();++i)dev.setComputeBuffer(int(i+1),inputs[i]);
        const bool ok=dev.dispatchChecked(uint32_t(size_t(size_)*size_));
        dev.endCompute();
        if(!ok||(backend_=="metal"&&!dev.computeDispatchSucceeded()))return false;
        ++passes_;return true;
    }
    bool queueRead(gfx::Device& dev){
        if(received_>=pixels_.size()||!reserveSubmission(dev))return false;
        expected_=std::min(tgg::readbackBlockBytes,pixels_.size()-received_);
        auto range=std::make_shared<Range>();
        if(!dev.readBufferRangeAsync(packed_,received_,expected_,[range](auto bytes){
            range->bytes=std::move(bytes);range->done=true;
        }))return false;
        read_=std::move(range);return true;
    }
    void acceptOutput(const tg::Program& program){
        const auto& step=plan_.slots.steps.at(step_);
        const auto reference=tg::evaluate(program,step.node,size_);
        if(reference.rgba.size()!=pixels_.size())throw std::runtime_error("materialized output size mismatch");
        for(size_t i=0;i<pixels_.size();++i)
            if((i%4==3&&pixels_[i]!=255)||std::abs(int(pixels_[i])-int(reference.rgba[i]))>1)
                throw std::runtime_error("materialized GPU CPU-reference mismatch");
        for(auto index:step.packOutputIndices){
            auto& image=results_.at(index);
            if(!image.rgba.empty())throw std::logic_error("duplicate materialized output write");
            image.width=image.height=size_;image.rgba=pixels_;++completed_;
        }
        pixels_.clear();++step_;phase_=Phase::Node;
    }
public:
    State state()const{return state_;}
    bool busy()const{return state_==State::Running||fence_.busy();}
    const std::string& reason()const{return reason_;}
    const std::string& backend()const{return backend_;}
    size_t completed()const{return completed_;}
    size_t passes()const{return passes_;}
    size_t blocks()const{return blocks_;}
    size_t allocatedBytes()const{return allocated_;}
    std::vector<render::Image> takeResults(){
        if(state_!=State::Ready||busy())throw std::logic_error("materialized results not ready");
        return std::move(results_);
    }
    void reset(){
        if(busy()||!slots_.empty()||packed_.valid()||!kernels_.empty()||pack_.valid())
            throw std::logic_error("reset of live materialized GPU request");
        state_=State::Idle;phase_=Phase::Node;step_=completed_=passes_=blocks_=received_=expected_=0;
        results_.clear();pixels_.clear();reason_.clear();backend_.clear();size_=0;plan_={};
    }
    void cancel(){
        if(state_==State::Idle)return;
        state_=State::Cancelled;reason_="materialized request cancelled";
        results_.clear();pixels_.clear();completed_=0;
    }
    void begin(gfx::Device& dev,tgg::EarlyPackDagPlan plan,int n,size_t outputCount){
        reset();size_=n;plan_=std::move(plan);
        const bool metal=dev.info().backend==gfx::Backend::Metal,vulkan=dev.info().backend==gfx::Backend::Vulkan;
        if(!plan_.ready()||!outputCount||outputCount>9||(!metal&&!vulkan)||!dev.supportsCompute()||!dev.gpuHealthy()){
            fail("materialized plan or backend unavailable");forgetHandles();return;
        }
        backend_=metal?"metal":"vulkan";
        try{
            // Validate each step against the shared pass policy and the
            // exact logical residents, including duplicate input ports.
            std::vector<bool> live(plan_.slots.slotCount);
            std::vector<bool> outputs(outputCount);
            const auto packedSlot=plan_.slots.slotCount;
            for(size_t i=0;i<plan_.slots.steps.size();++i){
                const auto& step=plan_.slots.steps[i];
                tgg::PassSchedule s;
                s.slotBytes.assign(live.size(),plan_.slots.slotBytes);
                s.slotBytes.push_back(plan_.slots.packedBytes);
                for(size_t j=0;j<live.size();++j)if(live[j])s.externalReady.push_back(j);
                if(step.outputSlot>=live.size()||live[step.outputSlot]||step.inputSlots.size()>tgg::maxNodeInputs)
                    throw std::logic_error("materialized output aliases a live slot");
                s.passes.push_back({0,step.outputSlot,step.inputSlots});
                s.resultSlot=step.outputSlot;
                if(!step.packOutputIndices.empty()){
                    s.passes.push_back({1,packedSlot,{step.outputSlot}});s.resultSlot=packedSlot;
                    for(auto index:step.packOutputIndices){
                        if(index>=outputs.size()||outputs[index])throw std::logic_error("materialized output mapping");
                        outputs[index]=true;
                    }
                }
                if(!tgg::validPassSchedule(s,tgg::maxDagSlotBytes))throw std::logic_error("materialized pass preflight failed");
                live[step.outputSlot]=true;
                for(auto slot:step.releaseSlotsAfter){
                    if(slot>=live.size()||!live[slot])throw std::logic_error("materialized slot release mismatch");
                    live[slot]=false;
                }
            }
            if(std::any_of(live.begin(),live.end(),[](bool v){return v;})||
               std::any_of(outputs.begin(),outputs.end(),[](bool v){return !v;}))
                throw std::logic_error("materialized plan incomplete");
            results_.resize(outputCount);slots_.resize(plan_.slots.slotCount);
            kernels_.resize(plan_.nodeKernels.size());
            if(!reserveSubmission(dev)){fail("GPU completion timeline unavailable");forgetHandles();return;}
            auto alloc=[&](size_t bytes){
                gfx::BufferDesc d;d.type=gfx::BufferType::Storage;d.usage=gfx::Usage::Immutable;d.size=bytes;d.label="TextureGraph materialized";
                return dev.createBuffer(d);
            };
            for(auto& slot:slots_){
                slot=alloc(plan_.slots.slotBytes);
                if(!slot.valid())throw std::runtime_error("materialized slot allocation failed");
                allocated_+=plan_.slots.slotBytes;
            }
            packed_=alloc(plan_.slots.packedBytes);
            if(!packed_.valid())throw std::runtime_error("materialized pack allocation failed");
            allocated_+=plan_.slots.packedBytes;
            auto pipeline=[&](const tgg::Source& s){
                return dev.createCompute({metal?s.msl.c_str():nullptr,vulkan?s.glsl.c_str():nullptr,"TextureGraph materialized"});
            };
            for(size_t i=0;i<kernels_.size();++i){
                kernels_[i]=pipeline(plan_.nodeKernels[i]);
                if(!kernels_[i].valid())throw std::runtime_error("materialized node pipeline rejected");
            }
            pack_=pipeline(plan_.packKernel);
            if(!pack_.valid())throw std::runtime_error("materialized pack pipeline rejected");
            state_=State::Running;
        }catch(const std::exception& e){
            fail(e.what());releaseRetired(dev);
        }
    }
    // At most one compute or one range-read submission per tick. Every next
    // write, slot reuse and final publication waits for real GPU completion.
    void tick(gfx::Device& dev,const tg::Program* program){
        if(!dev.gpuHealthy()){
            if(state_==State::Running)fail("materialized GPU device unhealthy");
            forgetHandles();return;
        }
        if(fence_.busy()&&!fence_.retire(dev.gpuCompletedFence()))return;
        if(state_!=State::Running){releaseRetired(dev);return;}
        try{
            if(!program)throw std::logic_error("missing immutable materialized program");
            if(step_==plan_.slots.steps.size()){
                if(completed_!=results_.size())throw std::logic_error("materialized outputs incomplete");
                state_=State::Ready;reason_="all materialized GPU outputs passed CPU parity";
                releaseRetired(dev);return;
            }
            const auto& step=plan_.slots.steps[step_];
            if(phase_==Phase::Node){
                std::vector<gfx::BufferHandle> inputs;
                for(auto slot:step.inputSlots)inputs.push_back(slots_.at(slot));
                if(!encode(dev,kernels_.at(step_),slots_.at(step.outputSlot),inputs))
                    throw std::runtime_error("materialized node dispatch rejected");
                if(step.packOutputIndices.empty())++step_;
                else phase_=Phase::Pack;
                return;
            }
            if(phase_==Phase::Pack){
                pixels_.assign(plan_.slots.packedBytes,0);received_=0;
                if(!encode(dev,pack_,packed_,{slots_.at(step.outputSlot)}))
                    throw std::runtime_error("materialized pack dispatch rejected");
                phase_=Phase::Read;return;
            }
            if(read_){
                if(!read_->done)return;
                if(read_->bytes.size()!=expected_||expected_>pixels_.size()-received_)
                    throw std::runtime_error("materialized range readback mismatch");
                std::copy(read_->bytes.begin(),read_->bytes.end(),pixels_.begin()+received_);
                received_+=expected_;++blocks_;read_.reset();
                if(received_==pixels_.size()){acceptOutput(*program);return;}
            }
            if(!queueRead(dev))throw std::runtime_error("materialized range read rejected");
        }catch(const std::exception& e){fail(e.what());releaseRetired(dev);}
    }
    // Called only after the existing host's shutdown-idle contract.
    void shutdownAfterIdle(gfx::Device& dev){
        cancel();fence_.abandon();
        if(dev.gpuHealthy())releaseRetired(dev);else forgetHandles();
    }
};
} // namespace aiverse::editor
