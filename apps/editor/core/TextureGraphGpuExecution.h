// Request-local execution under TextureGraphGpuPreview; caches own only
// completed handles on the same device/project. No independent thread/device.
#pragma once
#include "engine/render/graph/TextureGraphGpu.h"
#include "engine/render/graph/TextureGraphGpuStepState.h"
#include "engine/render/graph/TextureGraphGpuResourceCache.h"
#include "engine/render/graph/TextureGraphPipelineCache.h"
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
    tgg::MaterializedSources plan_;
    std::vector<size_t> targets_,layout_;
    std::vector<gfx::BufferHandle> buffers_; // last buffer is RGBA8 result
    std::vector<gfx::ComputeHandle> kernels_,packs_;
    using Cache=tgg::IdlePipelineCache<gfx::ComputeHandle,16>;
    Cache idlePipelines_;
    std::vector<Cache::Entry> activePipelines_; // unique exact-source handles
    tgg::RetiredBufferSet<gfx::BufferHandle> idleBuffers_;
    gfx::Device* cacheDevice_=nullptr;
    std::string cacheProject_;
    std::shared_ptr<Range> read_;
    size_t expected_=0,received_=0,step_=0,output_=0,completed_=0,passes_=0,blocks_=0,allocated_=0;
    size_t pipelineHits_=0;bool bufferHit_=false;
    int size_=0;
    std::vector<uint8_t> pixels_;
    std::vector<render::Image> results_;
    std::string reason_,backend_;
    void fail(std::string reason){
        state_=State::Failed;reason_=std::move(reason);
        results_.clear();pixels_.clear();completed_=0;
    }
    void abandonIdle(){
        idleBuffers_.abandon();idlePipelines_.abandon();
        cacheDevice_=nullptr;cacheProject_.clear();
    }
    void forgetActive(){
        buffers_.clear();layout_.clear();kernels_.clear();packs_.clear();activePipelines_.clear();read_.reset();
        fence_.abandon();allocated_=0;plan_={};targets_.clear();
    }
    void destroyBuffers(gfx::Device& dev,auto& entry){
        for(auto b:entry.handles)if(b.valid())dev.destroyBuffer(b);
    }
    void destroyPipeline(gfx::Device& dev,Cache::Entry& entry){
        if(entry.first.valid())dev.destroyCompute(entry.first);
        if(entry.second.valid())dev.destroyCompute(entry.second);
    }
    void releaseRetired(gfx::Device& dev,bool keep=false){
        if(fence_.busy())return;
        if(keep){
            idleBuffers_.put({std::move(layout_),std::move(buffers_)},tgg::materializedBudgetBytes,
                [&](auto& e){destroyBuffers(dev,e);});
            for(auto& e:activePipelines_)idlePipelines_.put(std::move(e),[&](auto& old){destroyPipeline(dev,old);});
        }else{
            for(auto b:buffers_)if(b.valid())dev.destroyBuffer(b);
            for(auto& e:activePipelines_)destroyPipeline(dev,e);
        }
        forgetActive();
    }
    bool reserveSubmission(gfx::Device& dev){
        const auto token=dev.gpuSubmissionFence();
        return token>dev.gpuCompletedFence()&&fence_.submit(token);
    }
    gfx::ComputeHandle pipeline(gfx::Device& dev,const tgg::Source& source){
        const bool metal=backend_=="metal";
        const auto& code=metal?source.msl:source.glsl;
        if(code.empty())throw std::runtime_error("missing materialized kernel source");
        tgg::PipelineCacheKey key{backend_,code,{}};
        for(const auto& e:activePipelines_)if(e.key==key)return e.first;
        if(auto old=idlePipelines_.take(key)){
            ++pipelineHits_;activePipelines_.push_back(std::move(*old));return activePipelines_.back().first;
        }
        auto handle=dev.createCompute({metal?code.c_str():nullptr,metal?nullptr:code.c_str(),"TextureGraph materialized"});
        if(!handle.valid())throw std::runtime_error("materialized pipeline rejected");
        activePipelines_.push_back({std::move(key),handle,{}});
        return handle;
    }
    bool encode(gfx::Device& dev,gfx::ComputeHandle kernel,gfx::BufferHandle output,
            const std::vector<gfx::BufferHandle>& inputs){
        if(!kernel.valid()||!output.valid()||inputs.size()>3)return false;
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
        if(!dev.readBufferRangeAsync(buffers_.back(),received_,expected_,[range](auto bytes){
            range->bytes=std::move(bytes);range->done=true;
        }))return false;
        read_=std::move(range);return true;
    }
    void acceptOutput(const tg::Program& program){
        const auto reference=tg::evaluate(program,targets_.at(output_),size_);
        if(reference.rgba.size()!=pixels_.size())throw std::runtime_error("materialized output size mismatch");
        for(size_t i=0;i<pixels_.size();++i)
            if((i%4==3&&pixels_[i]!=255)||std::abs(int(pixels_[i])-int(reference.rgba[i]))>1)
                throw std::runtime_error("materialized GPU CPU-reference mismatch");
        auto& image=results_.at(output_);
        image.width=image.height=size_;image.rgba=std::move(pixels_);++completed_;
        ++output_;phase_=Phase::Pack;
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
    size_t idleBytes()const{return idleBuffers_.bytes();}
    size_t idlePipelineCount()const{return idlePipelines_.size();}
    size_t pipelineHits()const{return pipelineHits_;}
    bool bufferHit()const{return bufferHit_;}
    std::vector<render::Image> takeResults(){
        if(state_!=State::Ready||busy())throw std::logic_error("materialized results not ready");
        return std::move(results_);
    }
    // Owner calls this before the fused path, project teardown, or device loss.
    void clearIdle(gfx::Device& dev){
        if(cacheDevice_!=&dev||!dev.gpuHealthy()){abandonIdle();return;}
        idleBuffers_.clear([&](auto& e){destroyBuffers(dev,e);});
        idlePipelines_.clear([&](auto& e){destroyPipeline(dev,e);});
        cacheDevice_=nullptr;cacheProject_.clear();
    }
    void reset(){
        if(busy()||!buffers_.empty()||!activePipelines_.empty())
            throw std::logic_error("reset of live materialized GPU request");
        state_=State::Idle;phase_=Phase::Node;step_=output_=completed_=passes_=blocks_=received_=expected_=0;
        pipelineHits_=0;bufferHit_=false;results_.clear();pixels_.clear();reason_.clear();backend_.clear();size_=0;plan_={};
    }
    void cancel(){
        if(state_==State::Idle)return;
        state_=State::Cancelled;reason_="materialized request cancelled";
        results_.clear();pixels_.clear();completed_=0;
    }
    void begin(gfx::Device& dev,tgg::MaterializedSources plan,int n,std::vector<size_t> targets,
            const std::string& project){
        reset();size_=n;plan_=std::move(plan);targets_=std::move(targets);
        const bool metal=dev.info().backend==gfx::Backend::Metal,vulkan=dev.info().backend==gfx::Backend::Vulkan;
        if(cacheDevice_!=&dev||cacheProject_!=project||!dev.gpuHealthy())clearIdle(dev);
        if(targets_.empty()||targets_.size()>9||(!metal&&!vulkan)||!dev.supportsCompute()||!dev.gpuHealthy()){
            fail("materialized plan or backend unavailable");forgetActive();return;
        }
        backend_=metal?"metal":"vulkan";cacheDevice_=&dev;cacheProject_=project;
        try{
            const auto& schedule=plan_.plan.schedule;
            if(plan_.resultBytes!=size_t(n)*size_t(n)*4||plan_.totalBytes>tgg::materializedBudgetBytes||
               plan_.plan.outputSlots.size()!=targets_.size()||plan_.packs.size()!=targets_.size()||
               !tgg::validPassSchedule(schedule,tgg::materializedBudgetBytes))
                throw std::logic_error("invalid canonical materialized plan");
            std::vector<bool> initialized(schedule.slotBytes.size());
            for(const auto& pass:schedule.passes){
                if(pass.pipeline>=plan_.nodes.size()||pass.inputs.size()>3)
                    throw std::logic_error("invalid canonical materialized pass");
                initialized[pass.output]=true;
            }
            for(auto slot:plan_.plan.outputSlots)
                if(slot>=initialized.size()||!initialized[slot])throw std::logic_error("unwritten materialized output");
            layout_=schedule.slotBytes;layout_.push_back(plan_.resultBytes);
            if(!tgg::RetiredBufferSet<gfx::BufferHandle>::admissible(layout_,tgg::materializedBudgetBytes))
                throw std::logic_error("materialized pool budget exceeded");
            size_t total=0;for(auto bytes:layout_)total+=bytes;
            if(total!=plan_.totalBytes)throw std::logic_error("materialized pool layout mismatch");
            results_.resize(targets_.size());kernels_.resize(plan_.nodes.size());packs_.resize(targets_.size());
            activePipelines_.reserve(schedule.passes.size()+targets_.size());
            if(!reserveSubmission(dev))throw std::runtime_error("GPU completion timeline unavailable");
            if(auto cached=idleBuffers_.take(layout_)){
                buffers_=std::move(cached->handles);allocated_=total;bufferHit_=true;
            }else{
                // Do not retain both old and new layouts at once.
                idleBuffers_.clear([&](auto& e){destroyBuffers(dev,e);});
                buffers_.resize(layout_.size());
                for(size_t i=0;i<layout_.size();++i){
                    gfx::BufferDesc d;d.type=gfx::BufferType::Storage;d.usage=gfx::Usage::Immutable;d.size=layout_[i];d.label="TextureGraph materialized";
                    buffers_[i]=dev.createBuffer(d);
                    if(!buffers_[i].valid())throw std::runtime_error("materialized slot allocation failed");
                    allocated_+=layout_[i];
                }
            }
            for(const auto& pass:schedule.passes)kernels_[pass.pipeline]=pipeline(dev,plan_.nodes.at(pass.pipeline));
            for(size_t i=0;i<packs_.size();++i)packs_[i]=pipeline(dev,plan_.packs[i]);
            state_=State::Running;
        }catch(const std::exception& e){fail(e.what());releaseRetired(dev);}
    }
    // At most one submission per tick; every reuse/pack/read waits for the
    // preceding real completion token, not a CPU frame counter.
    void tick(gfx::Device& dev,const tg::Program* program){
        if(!dev.gpuHealthy()||(cacheDevice_&&cacheDevice_!=&dev)){
            if(state_==State::Running)fail("materialized GPU device lost or replaced");
            forgetActive();abandonIdle();return;
        }
        if(fence_.busy()&&!fence_.retire(dev.gpuCompletedFence()))return;
        if(state_!=State::Running){releaseRetired(dev);return;}
        try{
            if(!program)throw std::logic_error("missing immutable materialized program");
            const auto& schedule=plan_.plan.schedule;
            if(phase_==Phase::Node){
                if(step_==schedule.passes.size()){phase_=Phase::Pack;return;}
                const auto& pass=schedule.passes[step_];
                std::vector<gfx::BufferHandle> inputs;
                for(auto slot:pass.inputs)inputs.push_back(buffers_.at(slot));
                if(!encode(dev,kernels_.at(pass.pipeline),buffers_.at(pass.output),inputs))
                    throw std::runtime_error("materialized node dispatch rejected");
                ++step_;return;
            }
            if(phase_==Phase::Pack){
                if(output_==targets_.size()){
                    if(completed_!=results_.size())throw std::logic_error("materialized outputs incomplete");
                    state_=State::Ready;reason_="all materialized GPU outputs passed CPU parity";
                    releaseRetired(dev,true);return;
                }
                // Pinned target slot stays valid; duplicate target bytes
                // reuse only an already accepted output from this request.
                for(size_t i=0;i<output_;++i)if(targets_[i]==targets_[output_]){
                    results_[output_]=results_[i];++completed_;++output_;return;
                }
                pixels_.assign(plan_.resultBytes,0);received_=0;
                if(!encode(dev,packs_.at(output_),buffers_.back(),{buffers_.at(plan_.plan.outputSlots.at(output_))}))
                    throw std::runtime_error("materialized pack dispatch rejected");
                phase_=Phase::Read;return;
            }
            if(read_){
                if(!read_->done)return;
                if(read_->bytes.size()!=expected_||received_>pixels_.size()||expected_>pixels_.size()-received_)
                    throw std::runtime_error("materialized range readback mismatch");
                std::copy(read_->bytes.begin(),read_->bytes.end(),pixels_.begin()+received_);
                received_+=expected_;++blocks_;read_.reset();
                if(received_==pixels_.size()){acceptOutput(*program);return;}
            }
            if(!queueRead(dev))throw std::runtime_error("materialized range read rejected");
        }catch(const std::exception& e){fail(e.what());releaseRetired(dev);}
    }
    void shutdownAfterIdle(gfx::Device& dev){
        cancel();fence_.abandon();
        if(dev.gpuHealthy()&&cacheDevice_==&dev)releaseRetired(dev);else forgetActive();
        clearIdle(dev);
    }
};
} // namespace aiverse::editor
