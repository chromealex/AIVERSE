// Native editor owner: one queued preview/bake, bounded readback and idle plan cache.
#pragma once
#include "engine/render/graph/TextureGraphGpu.h"
#include "engine/render/graph/TextureGraphPipelineCache.h"
#include "engine/render/graph/TextureGraphSharedScratch.h"
#include "engine/render/graph/TextureGraphPassSchedule.h"
#include "engine/render/graph/TextureGraphMaterializedDispatch.h"
#include "engine/render/graph/TextureGraphGpuResourceCache.h"
#include <array>
#include <limits>
#include "engine/render/graph/TextureGraphMaterialBinding.h"
#include "engine/view/AppearanceAssetRegistry.h"
#include "MaterialOps.h"
#include <chrono>
#include <memory>
#include <optional>

namespace aiverse::editor {
class TextureGraphGpuPreview {
    using Program=render::texture_graph::Program;
    using Source=render::texture_graph::gpu::Source;
    struct Read {bool done=false;size_t offset=0,expected=0;std::vector<uint8_t> bytes;};
    std::shared_ptr<Read> read_;
    gfx::BufferHandle buffer_{},scratch_{};gfx::ComputeHandle compute_{},secondCompute_{};
    // Materialized resources belong to this SAME frame-boundary owner.
    // Only successful, fence-retired requests enter the materialized idle cache.
    std::optional<render::texture_graph::gpu::MaterializedSources> matSources_;
    std::vector<gfx::BufferHandle> matSlots_;
    std::vector<gfx::ComputeHandle> matNodes_,matPacks_;
    uint64_t matSerial_=0;
    size_t matBytes_=0,matNodePasses_=0,matPackPasses_=0;
    bool matRequested_=false,matFailed_=false,matEncoded_=false;
    using PipelineKey=render::texture_graph::gpu::PipelineCacheKey;
    using PipelineCache=render::texture_graph::gpu::IdlePipelineCache<gfx::ComputeHandle>;
    PipelineKey activeKey_;
    PipelineCache idlePipelines_;
    using MatPipelineCache=render::texture_graph::gpu::IdlePipelineCache<gfx::ComputeHandle,16>;
    MatPipelineCache matIdlePipelines_;
    std::vector<MatPipelineCache::Entry> matActivePipelines_;
    render::texture_graph::gpu::RetiredBufferSet<gfx::BufferHandle> matIdleBuffers_;
    std::vector<size_t> matLayout_;
    gfx::Device* matCacheDevice_=nullptr;
    std::string matCacheProject_;
    uint64_t matLastFence_=0;
    size_t matPipelineHits_=0;
    bool matBufferHit_=false;
    bool activePlanAccepted_=false,cacheHit_=false;
    uint64_t cacheHits_=0;
    render::texture_graph::gpu::SharedScalarScratch sharedScratch_;
    bool reusedScratch_=false;
    size_t sharedScratchHits_=0;
    void destroyPair(gfx::Device& dev,gfx::ComputeHandle first,gfx::ComputeHandle second){
        if(first.valid())dev.destroyCompute(first);
        if(second.valid())dev.destroyCompute(second);
    }
    void clearIdle(gfx::Device& dev){
        idlePipelines_.clear([&](auto& e){destroyPair(dev,e.first,e.second);});
    }
    void destroyMatBuffers(gfx::Device& dev,auto& entry){
        for(auto b:entry.handles)if(b.valid())dev.destroyBuffer(b);
    }
    void clearMatIdle(gfx::Device& dev){
        if(matCacheDevice_==&dev&&dev.gpuHealthy()){
            matIdleBuffers_.clear([&](auto& e){destroyMatBuffers(dev,e);});
            matIdlePipelines_.clear([&](auto& e){destroyPair(dev,e.first,e.second);});
        }else{matIdleBuffers_.abandon();matIdlePipelines_.abandon();}
        matCacheDevice_=nullptr;matCacheProject_.clear();
    }
    bool matRetirementPending()const{return matSources_&&!inFlight_&&
        (matSerial_!=serial_||!matRequested_||matFailed_||!active());}
    size_t capacity_=0,received_=0,blocks_=0,scratchBytes_=0;
    size_t plannedPasses_=0,plannedSlotBytes_=0;
    int passes_=0;bool reused_=false,reusedBuffers_=false,inFlight_=false,bake_=false,published_=false,setBake_=false;
    Json sourceGraph_,overrides_;
    std::vector<std::pair<std::string,size_t>> setOutputs_;
    std::vector<render::Image> setImages_;
    std::vector<std::string> setBackends_;
    size_t reusedOutputs_=0;
    bool setIncomplete()const{return setBake_&&!published_&&setImages_.size()<setOutputs_.size();}
    bool setPending()const{return setIncomplete()&&state_!="failed"&&state_!="cancelled";}
    using Clock=std::chrono::steady_clock;
    Clock::time_point deadline_{};
    int timeoutMs_=60000;
    uint64_t fence_=0,serial_=0;
    bool active()const{return queued_||state_=="pending"||setPending();}
    // Discard results now; keep submitted resources until owner retirement.
    void discard(const char* state,const char* why){
        sharedScratch_.invalidate();reusedScratch_=false;
        queued_=false;state_=state;reason_=why;backend_="none";pixels_.clear();
        setImages_.clear();setBackends_.clear();program_.reset();sourceGraph_=Json();overrides_=Json();
    }
    std::optional<Program> program_;
    size_t target_=0;int size_=0;
    std::string project_,state_="idle",reason_,backend_="none",submittedBackend_,output_;
    std::vector<uint8_t> pixels_;
    bool queued_=false,requireGpu_=false;
    bool ownsActiveResources()const{return buffer_.valid()||scratch_.valid()||compute_.valid()||secondCompute_.valid()||matSources_.has_value();}
    bool ownsResources()const{return ownsActiveResources()||idlePipelines_.size()!=0||matIdleBuffers_.bytes()!=0||matIdlePipelines_.size()!=0;}
    void abandonActive(){matActivePipelines_.clear();matLayout_.clear();matLastFence_=0;matSources_.reset();matSlots_.clear();matNodes_.clear();matPacks_.clear();matSerial_=0;matBytes_=0;matEncoded_=false;sharedScratch_.invalidate();reusedScratch_=false;buffer_={};scratch_={};compute_={};secondCompute_={};read_.reset();fence_=0;capacity_=0;activeKey_={};activePlanAccepted_=false;idlePipelines_.abandon();inFlight_=false;}
    void abandon(){abandonActive();matIdleBuffers_.abandon();matIdlePipelines_.abandon();matCacheDevice_=nullptr;matCacheProject_.clear();}
    void release(gfx::Device& dev){
        for(auto& e:matActivePipelines_)destroyPair(dev,e.first,e.second);
        for(auto h:matSlots_)if(h.valid())dev.destroyBuffer(h);
        clearMatIdle(dev);clearIdle(dev);
        if(compute_.valid())dev.destroyCompute(compute_);
        if(secondCompute_.valid())dev.destroyCompute(secondCompute_);
        if(buffer_.valid())dev.destroyBuffer(buffer_);
        if(scratch_.valid())dev.destroyBuffer(scratch_);
        abandon();
    }
    // Even after the last readback callback, cache transfer needs a
    // completed token covering the whole last submitted frame.
    bool retireMaterialized(gfx::Device& dev){
        if(!matRetirementPending())return false;
        if(!dev.gpuHealthy()){abandon();return true;}
        if(matLastFence_&&dev.gpuCompletedFence()<matLastFence_)return false;
        const bool keep=matLastFence_&&matSerial_==serial_&&matRequested_&&!matFailed_&&
            !active()&&state_=="ready"&&project_==matCacheProject_&&matCacheDevice_==&dev&&
            (backend_=="metal"||backend_=="vulkan");
        if(!keep){release(dev);return true;}
        matSlots_.push_back(buffer_);buffer_={};
        matIdleBuffers_.put({std::move(matLayout_),std::move(matSlots_)},
            render::texture_graph::gpu::materializedBudgetBytes,[&](auto& e){destroyMatBuffers(dev,e);});
        for(auto& e:matActivePipelines_)
            matIdlePipelines_.put(std::move(e),[&](auto& old){destroyPair(dev,old.first,old.second);});
        abandonActive();return true;
    }
    void fallback(const std::string& why){
        if(matRequested_)matFailed_=true;
        sharedScratch_.invalidate();reusedScratch_=false;
        reason_=why;queued_=false;backend_="none";
        if(requireGpu_){state_="failed";pixels_.clear();return;}
        try{pixels_=render::texture_graph::evaluate(*program_,target_,size_).rgba;state_="ready";backend_="cpu";}
        catch(const std::exception& e){state_="failed";reason_=e.what();pixels_.clear();}
    }
    void finish(){
        bool valid=received_==size_t(size_)*size_*4&&pixels_.size()==received_;
        if(valid)for(size_t i=3;i<pixels_.size();i+=4)if(pixels_[i]!=255){valid=false;break;}
        if(valid)try{
            const auto ref=render::texture_graph::evaluate(*program_,target_,size_);
            if(ref.rgba.size()!=pixels_.size())valid=false;
            else for(size_t i=0;i<pixels_.size();++i)if(std::abs(int(pixels_[i])-int(ref.rgba[i]))>1){valid=false;break;}
        }catch(...){valid=false;}
        if(valid){sharedScratch_.accept();if(reusedScratch_)++sharedScratchHits_;activePlanAccepted_=!matRequested_;state_="ready";backend_=submittedBackend_;reason_="complete native readback; CPU reference within one RGBA8 unit";}
        else fallback("GPU readback or CPU parity check failed");
    }
    // Collect only a completed output. Multiple named outputs pointing to
    // the same compiled node reuse its validated bytes and actual backend.
    void advanceSet(){
        if(!setPending()||state_!="ready"||inFlight_)return;
        render::Image image;image.width=image.height=size_;image.rgba=std::move(pixels_);
        setImages_.push_back(std::move(image));setBackends_.push_back(backend_);
        while(setIncomplete()){
            const auto next=setImages_.size();
            size_t previous=next;
            for(size_t i=0;i<next;++i)if(setOutputs_[i].second==setOutputs_[next].second){previous=i;break;}
            if(previous==next)break;
            setImages_.push_back(setImages_[previous]);setBackends_.push_back(setBackends_[previous]);++reusedOutputs_;
        }
        if(!setIncomplete()){
            backend_=setBackends_.front();
            for(const auto& b:setBackends_)if(b!=backend_){backend_="mixed";break;}
            reason_="all declared outputs completed";return;
        }
        const auto& next=setOutputs_[setImages_.size()];output_=next.first;target_=next.second;
        received_=blocks_=scratchBytes_=0;plannedPasses_=plannedSlotBytes_=0;passes_=0;reused_=reusedBuffers_=cacheHit_=reusedScratch_=false;pixels_.clear();
        backend_="none";reason_.clear();queued_=true;state_="queued";
    }
    // One range in flight, with exact offset and length. No graphics stalls.
    bool queueRange(gfx::Device& dev){
        if(received_>=pixels_.size())return false;
        auto read=std::make_shared<Read>();read->offset=received_;read->expected=std::min(render::texture_graph::gpu::readbackBlockBytes,pixels_.size()-received_);
        auto callback=[read](std::vector<uint8_t> bytes){read->bytes=std::move(bytes);read->done=true;};
        if(matSources_){
            const auto token=dev.gpuSubmissionFence();
            if(!token||token<=dev.gpuCompletedFence())return false;
            matLastFence_=token;
        }
        if(!dev.readBufferRangeAsync(buffer_,read->offset,read->expected,callback))return false;
        read_=std::move(read);fence_=dev.gpuSubmissionFence();inFlight_=true;return true;
    }
    // Called only when no previous readback/submission is outstanding.
    // RHI barriers order the node dispatches within the frame. All materialized
    // slots remain pinned until the last packed output is accepted or retired.
    void submitMaterialized(gfx::Device& dev,bool metal){
        using namespace render::texture_graph::gpu;
        try{
            const auto token=dev.gpuSubmissionFence();
            if(!token||token<=dev.gpuCompletedFence()){fallback("materialized DAG requires a current native submission fence");return;}
            const bool first=!matSources_;
            if(first){
                // Drop idle fused allocations before admitting the 32 MiB DAG.
                if(ownsActiveResources()||idlePipelines_.size()!=0)release(dev);
                if(matCacheDevice_!=&dev||matCacheProject_!=project_)clearMatIdle(dev);
                matCacheDevice_=&dev;matCacheProject_=project_;
                std::vector<size_t> targets;
                if(setBake_){for(const auto& pair:setOutputs_)targets.push_back(pair.second);}
                else targets.push_back(target_);
                matSources_=compileMaterializedDag(*program_,targets,size_);
                matSerial_=serial_;
                auto& sources=*matSources_;
                matLayout_=sources.plan.schedule.slotBytes;
                matLayout_.push_back(sources.resultBytes);
                if(!RetiredBufferSet<gfx::BufferHandle>::admissible(matLayout_,materializedBudgetBytes))
                    throw std::runtime_error("materialized layout exceeds cache budget");
                matNodes_.resize(sources.nodes.size());matPacks_.resize(sources.packs.size());
                matActivePipelines_.reserve(sources.plan.schedule.passes.size()+sources.packs.size());
                // Any partial creation or submission belongs to this fence.
                fence_=matLastFence_=token;inFlight_=true;
                auto create=[&](const Source& source){
                    const auto& code=metal?source.msl:source.glsl;
                    PipelineKey key{metal?"metal":"vulkan",code,{}};
                    for(const auto& e:matActivePipelines_)if(e.key==key)return e.first;
                    if(auto hit=matIdlePipelines_.take(key)){
                        ++matPipelineHits_;matActivePipelines_.push_back(std::move(*hit));
                        return matActivePipelines_.back().first;
                    }
                    auto h=dev.createCompute({metal?code.c_str():nullptr,metal?nullptr:code.c_str(),"TextureGraph materialized"});
                    if(!h.valid())throw std::runtime_error("materialized pipeline rejected");
                    matActivePipelines_.push_back({std::move(key),h,{}});return h;
                };
                for(const auto& pass:sources.plan.schedule.passes)
                    matNodes_.at(pass.pipeline)=create(sources.nodes.at(pass.pipeline));
                for(size_t i=0;i<matPacks_.size();++i)matPacks_[i]=create(sources.packs[i]);
                if(auto cached=matIdleBuffers_.take(matLayout_)){
                    matSlots_=std::move(cached->handles);buffer_=matSlots_.back();matSlots_.pop_back();
                    matBytes_=sources.plan.allocatedBytes;capacity_=sources.resultBytes;matBufferHit_=true;
                }else{
                    matIdleBuffers_.clear([&](auto& e){destroyMatBuffers(dev,e);});
                    matSlots_.resize(sources.plan.schedule.slotBytes.size());
                    matSlots_.reserve(matSlots_.size()+1);
                    gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;desc.label="TextureGraph materialized slot";
                    for(size_t i=0;i<matSlots_.size();++i){
                        desc.size=sources.plan.schedule.slotBytes[i];matSlots_[i]=dev.createBuffer(desc);
                        if(!matSlots_[i].valid())throw std::runtime_error("materialized slot allocation rejected");
                        matBytes_+=desc.size;
                    }
                    desc.size=sources.resultBytes;desc.label="TextureGraph packed result";buffer_=dev.createBuffer(desc);
                    if(!buffer_.valid())throw std::runtime_error("materialized result allocation rejected");
                    capacity_=desc.size;
                }
            }
            auto& sources=*matSources_;
            if(matSerial_!=serial_||(!first&&!matEncoded_))throw std::logic_error("stale materialized execution");
            const size_t index=setBake_?setImages_.size():0;
            if(index>=sources.packs.size())throw std::logic_error("invalid materialized output ordinal");
            const auto pixels=uint32_t(sources.resultBytes/4);
            // Allocate CPU output before encoding work; failures still retain
            // any newly created handles through the owner retirement path.
            pixels_.assign(sources.resultBytes,0);
            submittedBackend_=metal?"metal":"vulkan";
            plannedPasses_=(first?sources.plan.schedule.passes.size():0)+1;
            plannedSlotBytes_=sources.totalBytes;scratchBytes_=sources.plan.allocatedBytes;
            fence_=matLastFence_=token;inFlight_=true;
            if(first){
                const auto result=encodePassSchedule(sources.plan.schedule,materializedBudgetBytes-sources.resultBytes,[&](const ScheduledPass& pass){
                    std::array<gfx::BufferHandle,3> inputs{};
                    if(pass.inputs.size()>inputs.size())return false;
                    for(size_t i=0;i<pass.inputs.size();++i)inputs[i]=matSlots_.at(pass.inputs[i]);
                    if(!dispatchMaterializedPass(dev,matNodes_.at(pass.pipeline),matSlots_.at(pass.output),
                            std::span<const gfx::BufferHandle>(inputs.data(),pass.inputs.size()),pixels,metal))return false;
                    ++passes_;++matNodePasses_;return true;
                });
                if(!result.complete)throw std::runtime_error("materialized node dispatch rejected");
                matEncoded_=true;
            }
            const std::array<gfx::BufferHandle,1> input{matSlots_.at(sources.plan.outputSlots.at(index))};
            if(!dispatchMaterializedPass(dev,matPacks_.at(index),buffer_,
                    std::span<const gfx::BufferHandle>(input),pixels,metal))
                throw std::runtime_error("materialized output packing rejected");
            ++passes_;++matPackPasses_;
            state_="pending";
            if(!queueRange(dev))fallback("materialized readback request rejected");
        }catch(const std::exception& e){fallback(e.what());}
    }
public:
    Json status() const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string(setPending()&&state_=="ready"?"pending":state_));r.set("backend",Json::string(backend_));
        r.set("request",Json::string(std::to_string(serial_)));r.set("reason",Json::string(reason_));
        r.set("busy",Json::fromBool(active()||inFlight_||matRetirementPending()));
        r.set("draining",Json::fromBool(matRetirementPending()||(inFlight_&&(state_=="cancelled"||state_=="failed"))));
        r.set("timeoutMs",Json::number(int64_t(timeoutMs_)));
        r.set("plannedPasses",Json::number(int64_t(plannedPasses_)));
        r.set("plannedSlotBytes",Json::number(int64_t(plannedSlotBytes_)));
        r.set("encodedPasses",Json::number(int64_t(passes_)));r.set("plannedScratchBytes",Json::number(int64_t(scratchBytes_)));
        r.set("bake",Json::fromBool(bake_));r.set("setBake",Json::fromBool(setBake_));
        r.set("completedOutputs",Json::number(int64_t(published_&&setBake_?setOutputs_.size():setImages_.size())));r.set("totalOutputs",Json::number(int64_t(setOutputs_.size())));
        r.set("currentOutput",Json::string(output_));r.set("reusedOutputs",Json::number(int64_t(reusedOutputs_)));
        Json outputs=Json::array();for(size_t i=0;i<setBackends_.size();++i){Json row=Json::object();row.set("output",Json::string(setOutputs_[i].first));row.set("backend",Json::string(setBackends_[i]));outputs.push(row);}r.set("outputs",outputs);r.set("published",Json::fromBool(published_));
        r.set("receivedBytes",Json::number(int64_t(received_)));r.set("totalBytes",Json::number(int64_t(size_t(size_)*size_*4)));
        r.set("readbackBlocks",Json::number(int64_t(blocks_)));r.set("reusedPlan",Json::fromBool(reused_));r.set("reusedBuffers",Json::fromBool(reusedBuffers_));
        r.set("pipelineCacheHit",Json::fromBool(cacheHit_));
        r.set("pipelineCacheHits",Json::number(int64_t(std::min<uint64_t>(cacheHits_,uint64_t(std::numeric_limits<int64_t>::max())))));
        r.set("idlePipelinePlans",Json::number(int64_t(idlePipelines_.size())));
        r.set("reusedScratch",Json::fromBool(reusedScratch_));
        r.set("sharedScratchHits",Json::number(int64_t(sharedScratchHits_)));
        r.set("materializedRequested",Json::fromBool(matRequested_));
        r.set("materializedFailed",Json::fromBool(matFailed_));
        r.set("materializedNodePasses",Json::number(int64_t(matNodePasses_)));
        r.set("materializedPackPasses",Json::number(int64_t(matPackPasses_)));
        r.set("materializedBufferCacheHit",Json::fromBool(matBufferHit_));
        r.set("materializedPipelineCacheHits",Json::number(int64_t(matPipelineHits_)));
        r.set("materializedIdleBytes",Json::number(int64_t(matIdleBuffers_.bytes())));
        r.set("materializedIdlePipelines",Json::number(int64_t(matIdlePipelines_.size())));
        r.set("retainedBufferBytes",Json::number(int64_t(matIdleBuffers_.bytes()+matBytes_+capacity_*(scratch_.valid()?2:1))));
        if(state_=="ready"){
            r.set("width",Json::number(int64_t(size_)));r.set("height",Json::number(int64_t(size_)));
            // Full-size images never cross the JSON bridge.
            if(!bake_){std::string hex;hex.reserve(pixels_.size()*2);for(auto v:pixels_){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}r.set("rgbaHex",Json::string(hex));}
        }return r;
    }
    Json start(const Json& p,const std::string& project){
        if(queued_||inFlight_||matRetirementPending()||state_=="pending"||setPending())throw std::runtime_error("previous GPU texture request still active");
        if(project.empty())throw std::invalid_argument("open project first");
        auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object());
        size_t target=program.nodes.size();const bool setBake=p.getBool("bakeSet"),bake=setBake||p.getBool("bake");
        auto output=p.getStr("output");
        if(setBake){output=program.outputs.begin()->first;target=program.outputs.begin()->second;}
        else if(!bake&&p.has("node")){for(size_t i=0;i<program.nodes.size();++i)if(program.nodes[i].id==p.getStr("node"))target=i;}
        else{auto it=program.outputs.find(output);if(it!=program.outputs.end())target=it->second;}
        if(target>=program.nodes.size())throw std::invalid_argument("unknown texture target");
        if(serial_==~uint64_t{0})throw std::overflow_error("GPU texture identity exhausted");
        const int requested=bake?program.size:render::texture_graph::integer(p["resolution"],32,8,render::texture_graph::gpu::maxPreviewSize);
        timeoutMs_=render::texture_graph::integer(p["timeoutMs"],60000,1000,300000);
        deadline_=Clock::now()+std::chrono::milliseconds(timeoutMs_);
        sharedScratch_.invalidate();reusedScratch_=false;sharedScratchHits_=0;
        matRequested_=p.getBool("materialized")||p.getBool("materializedDag");matFailed_=false;matNodePasses_=matPackPasses_=matPipelineHits_=0;matBufferHit_=false;
        ++serial_;size_=std::min(program.size,requested);target_=target;program_=std::move(program);project_=project;output_=output;bake_=bake;published_=false;setBake_=setBake;
        sourceGraph_=setBake?p["graph"]:Json();overrides_=setBake?(p.has("overrides")?p["overrides"]:Json::object()):Json();
        setOutputs_.clear();setImages_.clear();setBackends_.clear();reusedOutputs_=0;
        if(setBake_){for(const auto& pair:program_->outputs)setOutputs_.push_back(pair);setImages_.reserve(setOutputs_.size());setBackends_.reserve(setOutputs_.size());}
        plannedPasses_=plannedSlotBytes_=0;passes_=0;blocks_=received_=scratchBytes_=0;reused_=reusedBuffers_=cacheHit_=false;requireGpu_=p.getBool("requireGpu");pixels_.clear();reason_.clear();backend_="none";queued_=true;state_="queued";return status();
    }
    void checkProject(const std::string& project){
        if(!project_.empty()&&project_!=project){discard("cancelled","project changed");project_.clear();}
    }
    Json cancel(const Json& p,const std::string& project){
        checkProject(project);
        if(project_.empty()||p.getStr("request")!=std::to_string(serial_)||published_||
            (!active()&&state_!="ready"))throw std::runtime_error("no matching cancellable GPU texture request");
        discard("cancelled","cancelled by caller");return status();
    }
    // Native host integration point; only the owning appearance transaction
    // may commit the returned candidate. No project-file mutation here.
    render::Material stageCompletedMaterial(const std::string& project,const std::string& request,
            std::string_view generation,render::ModelImageUploadTransaction& transaction,
            render::Model& model,const render::Material& base) const {
        if(!setBake_||published_||state_!="ready"||active()||inFlight_||matRetirementPending()||setIncomplete()||
           !program_||project.empty()||project_!=project||request!=std::to_string(serial_))
            throw std::runtime_error("no matching completed texture set for material staging");
        auto plan=render::texture_graph::prepareMaterialImages(*program_,setImages_,generation);
        return render::texture_graph::stageMaterialImages(transaction,model,base,std::move(plan));
    }
    // Native appearance owner entry point; the host supplies its actual
    // registry, Model, graphics epoch and rollback pool. No raw-handle RPC.
    bool publishCompletedMaterial(const std::string& project,const std::string& request,
            std::string_view generation,view::AppearanceAssetRegistry& registry,
            const std::string& key,uint64_t expectedVersion,uint64_t expectedModelPublication,
            size_t materialIndex,render::Model& model,gfx::Device& device,
            const render::RenderDeviceLifetime::Lease& lease,
            render::ModelImageRollbackPool& rollback,std::string* error=nullptr) const {
        if(!setBake_||published_||state_!="ready"||active()||inFlight_||matRetirementPending()||setIncomplete()||
           !program_||project.empty()||project_!=project||request!=std::to_string(serial_)){
            if(error)*error="no matching completed texture set";
            return false;
        }
        return registry.publishTextureGraphMaterial(key,expectedVersion,expectedModelPublication,
            materialIndex,model,device,lease,rollback,*program_,setImages_,generation,error);
    }
    Json publish(Workspace& ws,const Json& p){
        checkProject(ws.project.dir);
        if(!bake_||published_||state_!="ready"||p.getStr("request")!=std::to_string(serial_)||inFlight_||matRetirementPending()||setIncomplete()||project_!=ws.project.dir)
            throw std::runtime_error("no matching completed unpublished texture bake");
        Json r;
        if(setBake_)r=publishTextureGraphSet(ws,p.getStr("textureName"),sourceGraph_,overrides_,setImages_,setBackends_);
        else{render::Image img;img.width=img.height=size_;img.rgba=pixels_;r=publishTextureGraphImage(ws,p.getStr("textureName"),output_,img);}
        published_=true;pixels_.clear();setImages_.clear();r.set("backend",Json::string(backend_));r.set("request",Json::string(std::to_string(serial_)));return r;
    }
    // Called at owner frame boundary, no active encoder. Read callback
    // owns only its bytes and is delivered on the owner thread.
    void tick(gfx::Device& dev,const std::string& project){
        checkProject(project);
        // Request-wide monotonic deadline, evaluated at owner frame boundaries.
        if(active()&&Clock::now()>=deadline_)discard("failed","GPU texture request timed out");
        if(matCacheDevice_&&matCacheDevice_!=&dev){
            abandon();
            if(state_=="pending")fallback("materialized device replaced");
        }
        if(ownsResources()&&!dev.gpuHealthy()){
            abandon();
            if(state_=="pending")fallback("GPU device unhealthy");
        }
        if(read_&&read_->done){
            auto read=std::move(read_);inFlight_=false;fence_=0;
            if(state_=="pending"){
                if(read->offset!=received_||read->bytes.size()!=read->expected||received_>pixels_.size()||read->expected>pixels_.size()-received_){fallback("invalid GPU readback block");if(!matSources_)release(dev);}
                else {
                    std::copy(read->bytes.begin(),read->bytes.end(),pixels_.begin()+received_);received_+=read->expected;++blocks_;
                    if(received_==pixels_.size())finish();
                    else if(!queueRange(dev)){fallback("GPU readback block rejected");if(!matSources_)release(dev);}
                }
            }else if(!matSources_)release(dev);
        }else if(inFlight_&&!read_&&fence_&&dev.gpuCompletedFence()>=fence_&&dev.gpuHealthy())release(dev);
        if(matRetirementPending()&&!retireMaterialized(dev))return;
        if(!matSources_&&!inFlight_&&(project_.empty()||state_=="cancelled"||state_=="failed"||
            (matCacheDevice_&&matCacheProject_!=project_)||matFailed_||(queued_&&!matRequested_)))clearMatIdle(dev);
        if(project_.empty()&&!inFlight_&&ownsResources())release(dev);
        if(setBake_&&state_=="failed"){setImages_.clear();setBackends_.clear();}
        if(setPending()&&state_=="ready"&&!inFlight_){advanceSet();if(!setPending()&&matSources_)retireMaterialized(dev);return;}
        if(!queued_)return;
        queued_=false;
        const bool metal=dev.info().backend==gfx::Backend::Metal,vulkan=dev.info().backend==gfx::Backend::Vulkan;
        if((!metal&&!vulkan)||!dev.supportsCompute()||!dev.gpuHealthy()){fallback("Metal/Vulkan compute unavailable");return;}
        if(matRequested_){
            if(matFailed_)fallback("materialized request previously failed; CPU fallback");
            else submitMaterialized(dev,metal);
            return;
        }
        auto plan=render::texture_graph::gpu::plan(*program_,target_,size_,setBake_);
        if(plan.first.msl.empty()){fallback(plan.first.reason);return;}
        scratchBytes_=plan.scratchBytes;submittedBackend_=metal?"metal":"vulkan";
        const auto first=metal?plan.first.msl:plan.first.glsl;
        const auto second=metal?plan.second.msl:plan.second.glsl;
        const size_t bytes=size_t(size_)*size_*4;
        // Cache only completed, parity-accepted plans; buffers stay single-owner.
        PipelineKey key{submittedBackend_,first,second};
        reused_=activePlanAccepted_&&capacity_==bytes&&key==activeKey_&&buffer_.valid()&&compute_.valid()&&
            (!plan.twoPass||(scratch_.valid()&&secondCompute_.valid()));
        if(!reused_){
            // On a completed plan boundary, same-sized storage is reusable
            // even when the next output requires different shader source.
            const bool keepBuffers=capacity_==bytes&&buffer_.valid();
            auto cached=idlePipelines_.take(key);
            if(activePlanAccepted_&&compute_.valid())
                idlePipelines_.put({std::move(activeKey_),compute_,secondCompute_},
                    [&](auto& e){destroyPair(dev,e.first,e.second);});
            else destroyPair(dev,compute_,secondCompute_);
            compute_={};secondCompute_={};activeKey_={};activePlanAccepted_=false;
            if(!keepBuffers){sharedScratch_.invalidate();if(buffer_.valid())dev.destroyBuffer(buffer_);buffer_={};if(scratch_.valid())dev.destroyBuffer(scratch_);scratch_={};capacity_=0;}
            reusedBuffers_=keepBuffers;
            fence_=dev.gpuSubmissionFence();inFlight_=true;
            auto createPipeline=[&](const Source& source){return dev.createCompute({metal?source.msl.c_str():nullptr,vulkan?source.glsl.c_str():nullptr,"TextureGraph compute"});};
            if(cached){
                compute_=cached->first;secondCompute_=cached->second;cacheHit_=true;
                if(cacheHits_!=~uint64_t{0})++cacheHits_;
            }else{
                compute_=createPipeline(plan.first);
                if(plan.twoPass)secondCompute_=createPipeline(plan.second);
            }
            gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;desc.size=bytes;desc.label="TextureGraph output";
            if(!buffer_.valid())buffer_=dev.createBuffer(desc);
            if(plan.twoPass&&!scratch_.valid()){sharedScratch_.invalidate();desc.label="TextureGraph height scratch";scratch_=dev.createBuffer(desc);}
            if(!compute_.valid()||!buffer_.valid()||(plan.twoPass&&(!secondCompute_.valid()||!scratch_.valid()))){if(!ownsActiveResources())inFlight_=false;fallback("GPU plan allocation rejected");return;}
            capacity_=bytes;activeKey_=std::move(key);
        }
        activePlanAccepted_=false;
        fence_=dev.gpuSubmissionFence();inFlight_=true;
        auto dispatch=[&](gfx::ComputeHandle pipeline,gfx::BufferHandle out,gfx::BufferHandle in){
            if(!dev.beginCompute(pipeline))return false;
            dev.setComputeBuffer(0,out);if(in.valid())dev.setComputeBuffer(1,in);
            const bool encoded=dev.dispatchChecked(uint32_t(bytes/4));dev.endCompute();
            if(!encoded||(metal&&!dev.computeDispatchSucceeded()))return false;
            ++passes_;return true;
        };
        // Scratch is request-local and may survive intervening pointwise outputs.
        // Only an accepted producer with the same scalar node, size and backend
        // can skip the first pass. Every consumer still receives CPU parity.
        reusedScratch_=false;
        if(setBake_&&plan.twoPass){
            const auto& node=program_->nodes[target_];
            const auto scalarTarget=std::string(node.info->id)=="normal"?node.inputs[0]:target_;
            reusedScratch_=sharedScratch_.begin({serial_,scalarTarget,size_,vulkan});
        }else if(plan.twoPass)sharedScratch_.invalidate();
        using namespace render::texture_graph::gpu;
        PassSchedule schedule;
        schedule.slotBytes={bytes};
        if(plan.twoPass||scratch_.valid())schedule.slotBytes.push_back(bytes);
        if(plan.twoPass){
            if(reusedScratch_)schedule.externalReady={1};
            else schedule.passes.push_back({0,1,{}});
            schedule.passes.push_back({1,0,{1}});
        }else schedule.passes.push_back({0,0,{}});
        plannedPasses_=schedule.passes.size();
        plannedSlotBytes_=bytes*schedule.slotBytes.size();
        const auto encoded=encodePassSchedule(schedule,8u*1024u*1024u,[&](const ScheduledPass& pass){
            const auto pipeline=pass.pipeline==0?compute_:secondCompute_;
            const auto output=pass.output==0?buffer_:scratch_;
            const auto input=pass.inputs.empty()?gfx::BufferHandle{}:
                (pass.inputs.front()==0?buffer_:scratch_);
            return dispatch(pipeline,output,input);
        });
        if(!encoded.complete){fallback("GPU pass schedule or dispatch rejected");return;}
        pixels_.assign(bytes,0);state_="pending";
        if(!queueRange(dev))fallback("GPU readback request rejected");
    }
    void shutdownAfterIdle(gfx::Device& dev){discard("cancelled","owner shutdown");if(dev.gpuHealthy())release(dev);else abandon();}
};
} // namespace aiverse::editor
