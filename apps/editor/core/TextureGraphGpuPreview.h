// Native editor owner: one queued preview/bake, bounded readback and idle plan cache.
#pragma once
#include "engine/render/graph/TextureGraphGpu.h"
#include "engine/render/graph/TextureGraphPipelineCache.h"
#include "engine/render/graph/TextureGraphSharedScratch.h"
#include "engine/render/graph/TextureGraphPassSchedule.h"
#include <limits>
#include "engine/render/graph/TextureGraphMaterialBinding.h"
#include "engine/view/AppearanceAssetRegistry.h"
#include "MaterialOps.h"
#include "TextureGraphGpuExecution.h"
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
    using PipelineKey=render::texture_graph::gpu::PipelineCacheKey;
    using PipelineCache=render::texture_graph::gpu::IdlePipelineCache<gfx::ComputeHandle>;
    PipelineKey activeKey_;
    PipelineCache idlePipelines_;
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
    size_t capacity_=0,received_=0,blocks_=0,scratchBytes_=0;
    size_t plannedPasses_=0,plannedSlotBytes_=0;
    int passes_=0;bool reused_=false,reusedBuffers_=false,inFlight_=false,bake_=false,published_=false,setBake_=false;
    Json sourceGraph_,overrides_;
    std::vector<std::pair<std::string,size_t>> setOutputs_;
    std::vector<render::Image> setImages_;
    std::vector<std::string> setBackends_;
    size_t reusedOutputs_=0;
    TextureGraphGpuExecution dag_;
    bool dagMode_=false,dagRequested_=false,dagUsed_=false;
    std::string dagFallbackReason_;
    size_t dagPlannedBytes_=0;
    bool setIncomplete()const{return setBake_&&!published_&&setImages_.size()<setOutputs_.size();}
    bool setPending()const{return setIncomplete()&&state_!="failed"&&state_!="cancelled";}
    using Clock=std::chrono::steady_clock;
    Clock::time_point deadline_{};
    int timeoutMs_=60000;
    uint64_t fence_=0,serial_=0;
    bool active()const{return queued_||state_=="pending"||setPending();}
    // Discard results now; keep submitted resources until owner retirement.
    void discard(const char* state,const char* why){
        if(dagMode_)dag_.cancel();
        sharedScratch_.invalidate();reusedScratch_=false;
        queued_=false;state_=state;reason_=why;backend_="none";pixels_.clear();
        setImages_.clear();setBackends_.clear();program_.reset();sourceGraph_=Json();overrides_=Json();
    }
    std::optional<Program> program_;
    size_t target_=0;int size_=0;
    std::string project_,state_="idle",reason_,backend_="none",submittedBackend_,output_;
    std::vector<uint8_t> pixels_;
    bool queued_=false,requireGpu_=false;
    bool ownsActiveResources()const{return buffer_.valid()||scratch_.valid()||compute_.valid()||secondCompute_.valid();}
    bool ownsResources()const{return ownsActiveResources()||idlePipelines_.size()!=0;}
    void abandon(){sharedScratch_.invalidate();reusedScratch_=false;buffer_={};scratch_={};compute_={};secondCompute_={};read_.reset();fence_=0;capacity_=0;activeKey_={};activePlanAccepted_=false;idlePipelines_.abandon();inFlight_=false;}
    void release(gfx::Device& dev){clearIdle(dev);if(compute_.valid())dev.destroyCompute(compute_);if(secondCompute_.valid())dev.destroyCompute(secondCompute_);if(buffer_.valid())dev.destroyBuffer(buffer_);if(scratch_.valid())dev.destroyBuffer(scratch_);abandon();}
    void fallback(const std::string& why){
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
        if(valid){sharedScratch_.accept();if(reusedScratch_)++sharedScratchHits_;activePlanAccepted_=true;state_="ready";backend_=submittedBackend_;reason_="complete native readback; CPU reference within one RGBA8 unit";}
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
    // The existing owner serializes both execution paths. A materialized
    // request never runs alongside fused storage or publishes partial outputs.
    void tickDag(gfx::Device& dev){
        using DagState=TextureGraphGpuExecution::State;
        if(queued_){
            queued_=false;state_="pending";
            if(ownsResources()){if(dev.gpuHealthy())release(dev);else abandon();}
            try{
                std::vector<size_t> targets;
                for(const auto& row:setOutputs_)targets.push_back(row.second);
                auto plan=render::texture_graph::gpu::compileMaterializedDag(*program_,targets,size_);
                dagPlannedBytes_=plan.slots.requiredBytes;
                if(!plan.ready())dagFallbackReason_=plan.reason;
                else dag_.begin(dev,std::move(plan),size_,targets.size());
            }catch(const std::exception& e){dagFallbackReason_=e.what();}
        }else dag_.tick(dev,program_?&*program_:nullptr);
        if(dag_.busy())return;
        dagMode_=false;
        if(state_=="failed"||state_=="cancelled")return;
        if(dag_.state()==DagState::Ready){
            auto images=dag_.takeResults();
            if(images.size()!=setOutputs_.size()){
                dagFallbackReason_="materialized output count mismatch";
            }else{
                setImages_=std::move(images);setBackends_.assign(setImages_.size(),dag_.backend());
                reusedOutputs_=0;
                for(size_t i=0;i<setOutputs_.size();++i)
                    for(size_t j=0;j<i;++j)if(setOutputs_[i].second==setOutputs_[j].second){++reusedOutputs_;break;}
                dagUsed_=true;state_="ready";backend_=dag_.backend();reason_=dag_.reason();
                return;
            }
        }
        if(dagFallbackReason_.empty())dagFallbackReason_=dag_.reason();
        // No accepted materialized result survives a failed attempt.
        // Restart the original fused GPU route; its requireGpu guard still
        // controls whether CPU evaluation is ever permitted.
        setImages_.clear();setBackends_.clear();queued_=true;state_="queued";backend_="none";
        reason_="materialized attempt rejected; trying fused GPU";
    }
    // One range in flight, with exact offset and length. No graphics stalls.
    bool queueRange(gfx::Device& dev){
        if(received_>=pixels_.size())return false;
        auto read=std::make_shared<Read>();read->offset=received_;read->expected=std::min(render::texture_graph::gpu::readbackBlockBytes,pixels_.size()-received_);
        auto callback=[read](std::vector<uint8_t> bytes){read->bytes=std::move(bytes);read->done=true;};
        if(!dev.readBufferRangeAsync(buffer_,read->offset,read->expected,callback))return false;
        read_=std::move(read);fence_=dev.gpuSubmissionFence();inFlight_=true;return true;
    }
public:
    Json status() const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string(setPending()&&state_=="ready"?"pending":state_));r.set("backend",Json::string(backend_));
        r.set("request",Json::string(std::to_string(serial_)));r.set("reason",Json::string(reason_));
        r.set("busy",Json::fromBool(active()||inFlight_||dag_.busy()));
        r.set("draining",Json::fromBool((inFlight_||dag_.busy())&&(state_=="cancelled"||state_=="failed")));
        r.set("executionMode",Json::string(dagUsed_||dagMode_?"materialized":"fused"));
        r.set("materializedRequested",Json::fromBool(dagRequested_));
        r.set("materializedFallbackReason",Json::string(dagFallbackReason_));
        r.set("materializedCompletedOutputs",Json::number(int64_t(dag_.completed())));
        r.set("materializedEncodedPasses",Json::number(int64_t(dag_.passes())));
        r.set("materializedReadbackBlocks",Json::number(int64_t(dag_.blocks())));
        r.set("materializedPlannedBytes",Json::number(int64_t(dagPlannedBytes_)));
        r.set("materializedAllocatedBytes",Json::number(int64_t(dag_.allocatedBytes())));
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
        r.set("retainedBufferBytes",Json::number(int64_t(capacity_*(scratch_.valid()?2:1))));
        if(state_=="ready"){
            r.set("width",Json::number(int64_t(size_)));r.set("height",Json::number(int64_t(size_)));
            // Full-size images never cross the JSON bridge.
            if(!bake_){std::string hex;hex.reserve(pixels_.size()*2);for(auto v:pixels_){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}r.set("rgbaHex",Json::string(hex));}
        }return r;
    }
    Json start(const Json& p,const std::string& project){
        if(queued_||inFlight_||dag_.busy()||state_=="pending"||setPending())throw std::runtime_error("previous GPU texture request still active");
        if(project.empty())throw std::invalid_argument("open project first");
        auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object());
        size_t target=program.nodes.size();const bool setBake=p.getBool("bakeSet"),bake=setBake||p.getBool("bake");
        const bool dagRequested=p.getBool("materializedDag");
        if(dagRequested&&!setBake)throw std::invalid_argument("materializedDag requires bakeSet");
        auto output=p.getStr("output");
        if(setBake){output=program.outputs.begin()->first;target=program.outputs.begin()->second;}
        else if(!bake&&p.has("node")){for(size_t i=0;i<program.nodes.size();++i)if(program.nodes[i].id==p.getStr("node"))target=i;}
        else{auto it=program.outputs.find(output);if(it!=program.outputs.end())target=it->second;}
        if(target>=program.nodes.size())throw std::invalid_argument("unknown texture target");
        if(serial_==~uint64_t{0})throw std::overflow_error("GPU texture identity exhausted");
        const int requested=bake?program.size:render::texture_graph::integer(p["resolution"],32,8,render::texture_graph::gpu::maxPreviewSize);
        timeoutMs_=render::texture_graph::integer(p["timeoutMs"],60000,1000,300000);
        deadline_=Clock::now()+std::chrono::milliseconds(timeoutMs_);
        dag_.reset();dagMode_=dagRequested_=dagRequested;dagUsed_=false;dagFallbackReason_.clear();dagPlannedBytes_=0;
        sharedScratch_.invalidate();reusedScratch_=false;sharedScratchHits_=0;
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
        if(!setBake_||published_||state_!="ready"||active()||inFlight_||setIncomplete()||
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
        if(!setBake_||published_||state_!="ready"||active()||inFlight_||setIncomplete()||
           !program_||project.empty()||project_!=project||request!=std::to_string(serial_)){
            if(error)*error="no matching completed texture set";
            return false;
        }
        return registry.publishTextureGraphMaterial(key,expectedVersion,expectedModelPublication,
            materialIndex,model,device,lease,rollback,*program_,setImages_,generation,error);
    }
    Json publish(Workspace& ws,const Json& p){
        checkProject(ws.project.dir);
        if(!bake_||published_||state_!="ready"||p.getStr("request")!=std::to_string(serial_)||inFlight_||setIncomplete()||project_!=ws.project.dir)
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
        if(dagMode_){tickDag(dev);return;}
        if(ownsResources()&&!dev.gpuHealthy()){
            abandon();
            if(state_=="pending")fallback("GPU device unhealthy");
        }
        if(read_&&read_->done){
            auto read=std::move(read_);inFlight_=false;fence_=0;
            if(state_=="pending"){
                if(read->offset!=received_||read->bytes.size()!=read->expected||received_>pixels_.size()||read->expected>pixels_.size()-received_){fallback("invalid GPU readback block");release(dev);}
                else {
                    std::copy(read->bytes.begin(),read->bytes.end(),pixels_.begin()+received_);received_+=read->expected;++blocks_;
                    if(received_==pixels_.size())finish();
                    else if(!queueRange(dev)){fallback("GPU readback block rejected");release(dev);}
                }
            }else release(dev);
        }else if(inFlight_&&!read_&&fence_&&dev.gpuCompletedFence()>=fence_&&dev.gpuHealthy())release(dev);
        if(project_.empty()&&!inFlight_&&ownsResources())release(dev);
        if(setBake_&&state_=="failed"){setImages_.clear();setBackends_.clear();}
        if(setPending()&&state_=="ready"&&!inFlight_){advanceSet();return;}
        if(!queued_)return;
        queued_=false;
        const bool metal=dev.info().backend==gfx::Backend::Metal,vulkan=dev.info().backend==gfx::Backend::Vulkan;
        if((!metal&&!vulkan)||!dev.supportsCompute()||!dev.gpuHealthy()){fallback("Metal/Vulkan compute unavailable");return;}
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
    void shutdownAfterIdle(gfx::Device& dev){discard("cancelled","owner shutdown");dag_.shutdownAfterIdle(dev);dagMode_=false;if(dev.gpuHealthy())release(dev);else abandon();}
};
} // namespace aiverse::editor
