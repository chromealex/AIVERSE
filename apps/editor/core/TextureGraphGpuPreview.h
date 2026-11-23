// Native editor owner: one queued preview/bake, bounded readback and idle plan cache.
#pragma once
#include "engine/render/graph/TextureGraphGpu.h"
#include "MaterialOps.h"
#include <memory>
#include <optional>

namespace aiverse::editor {
class TextureGraphGpuPreview {
    using Program=render::texture_graph::Program;
    using Source=render::texture_graph::gpu::Source;
    struct Read {bool done=false;size_t offset=0,expected=0;std::vector<uint8_t> bytes;};
    std::shared_ptr<Read> read_;
    gfx::BufferHandle buffer_{},scratch_{};gfx::ComputeHandle compute_{},secondCompute_{};
    std::string firstKey_,secondKey_;
    size_t capacity_=0,received_=0,blocks_=0,scratchBytes_=0;
    int passes_=0;bool reused_=false,inFlight_=false,bake_=false,published_=false,setBake_=false;
    Json sourceGraph_,overrides_;
    std::vector<std::pair<std::string,size_t>> setOutputs_;
    std::vector<render::Image> setImages_;
    std::vector<std::string> setBackends_;
    size_t reusedOutputs_=0;
    bool setIncomplete()const{return setBake_&&setImages_.size()<setOutputs_.size();}
    bool setPending()const{return setIncomplete()&&state_!="failed"&&state_!="cancelled";}
    uint64_t fence_=0,serial_=0;
    std::optional<Program> program_;
    size_t target_=0;int size_=0;
    std::string project_,state_="idle",reason_,backend_="none",submittedBackend_,output_;
    std::vector<uint8_t> pixels_;
    bool queued_=false,requireGpu_=false;
    bool ownsResources()const{return buffer_.valid()||scratch_.valid()||compute_.valid()||secondCompute_.valid();}
    void abandon(){buffer_={};scratch_={};compute_={};secondCompute_={};read_.reset();fence_=0;capacity_=0;firstKey_.clear();secondKey_.clear();inFlight_=false;}
    void release(gfx::Device& dev){if(compute_.valid())dev.destroyCompute(compute_);if(secondCompute_.valid())dev.destroyCompute(secondCompute_);if(buffer_.valid())dev.destroyBuffer(buffer_);if(scratch_.valid())dev.destroyBuffer(scratch_);abandon();}
    void fallback(const std::string& why){
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
        if(valid){state_="ready";backend_=submittedBackend_;reason_="complete native readback; CPU reference within one RGBA8 unit";}
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
        received_=blocks_=scratchBytes_=0;passes_=0;reused_=false;pixels_.clear();
        backend_="none";reason_.clear();queued_=true;state_="queued";
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
        r.set("busy",Json::fromBool(queued_||inFlight_||state_=="pending"||setPending()));
        r.set("encodedPasses",Json::number(int64_t(passes_)));r.set("plannedScratchBytes",Json::number(int64_t(scratchBytes_)));
        r.set("bake",Json::fromBool(bake_));r.set("setBake",Json::fromBool(setBake_));
        r.set("completedOutputs",Json::number(int64_t(setImages_.size())));r.set("totalOutputs",Json::number(int64_t(setOutputs_.size())));
        r.set("currentOutput",Json::string(output_));r.set("reusedOutputs",Json::number(int64_t(reusedOutputs_)));
        Json outputs=Json::array();for(size_t i=0;i<setBackends_.size();++i){Json row=Json::object();row.set("output",Json::string(setOutputs_[i].first));row.set("backend",Json::string(setBackends_[i]));outputs.push(row);}r.set("outputs",outputs);r.set("published",Json::fromBool(published_));
        r.set("receivedBytes",Json::number(int64_t(received_)));r.set("totalBytes",Json::number(int64_t(size_t(size_)*size_*4)));
        r.set("readbackBlocks",Json::number(int64_t(blocks_)));r.set("reusedPlan",Json::fromBool(reused_));
        r.set("retainedBufferBytes",Json::number(int64_t(capacity_*(scratch_.valid()?2:1))));
        if(state_=="ready"){
            r.set("width",Json::number(int64_t(size_)));r.set("height",Json::number(int64_t(size_)));
            // Full-size images never cross the JSON bridge.
            if(!bake_){std::string hex;hex.reserve(pixels_.size()*2);for(auto v:pixels_){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}r.set("rgbaHex",Json::string(hex));}
        }return r;
    }
    Json start(const Json& p,const std::string& project){
        if(queued_||inFlight_||state_=="pending"||setPending())throw std::runtime_error("previous GPU texture request still active");
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
        ++serial_;size_=std::min(program.size,requested);target_=target;program_=std::move(program);project_=project;output_=output;bake_=bake;published_=false;setBake_=setBake;
        sourceGraph_=setBake?p["graph"]:Json();overrides_=setBake?(p.has("overrides")?p["overrides"]:Json::object()):Json();
        setOutputs_.clear();setImages_.clear();setBackends_.clear();reusedOutputs_=0;
        if(setBake_){for(const auto& pair:program_->outputs)setOutputs_.push_back(pair);setImages_.reserve(setOutputs_.size());setBackends_.reserve(setOutputs_.size());}
        passes_=0;blocks_=received_=scratchBytes_=0;reused_=false;requireGpu_=p.getBool("requireGpu");pixels_.clear();reason_.clear();backend_="none";queued_=true;state_="queued";return status();
    }
    void checkProject(const std::string& project){
        if(!project_.empty()&&project_!=project){queued_=false;state_="cancelled";pixels_.clear();setImages_.clear();setBackends_.clear();project_.clear();}
    }
    Json publish(Workspace& ws,const Json& p){
        checkProject(ws.project.dir);
        if(!bake_||published_||state_!="ready"||p.getStr("request")!=std::to_string(serial_)||inFlight_||setIncomplete()||project_!=ws.project.dir)
            throw std::runtime_error("no matching completed unpublished texture bake");
        Json r;
        if(setBake_)r=publishTextureGraphSet(ws,p.getStr("textureName"),sourceGraph_,overrides_,setImages_,setBackends_);
        else{render::Image img;img.width=img.height=size_;img.rgba=pixels_;r=publishTextureGraphImage(ws,p.getStr("textureName"),output_,img);}
        published_=true;r.set("backend",Json::string(backend_));r.set("request",Json::string(std::to_string(serial_)));return r;
    }
    // Called at owner frame boundary, no active encoder. Read callback
    // owns only its bytes and is delivered on the owner thread.
    void tick(gfx::Device& dev,const std::string& project){
        checkProject(project);
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
        auto plan=render::texture_graph::gpu::plan(*program_,target_,size_);
        if(plan.first.msl.empty()){fallback(plan.first.reason);return;}
        scratchBytes_=plan.scratchBytes;submittedBackend_=metal?"metal":"vulkan";
        const auto first=metal?plan.first.msl:plan.first.glsl;
        const auto second=metal?plan.second.msl:plan.second.glsl;
        const size_t bytes=size_t(size_)*size_*4;
        // Only one exact last plan is retained, on this owner/device.
        // A completed readback is the prerequisite to reuse or replace it.
        reused_=capacity_==bytes&&first==firstKey_&&second==secondKey_&&buffer_.valid()&&compute_.valid()&&
            (!plan.twoPass||(scratch_.valid()&&secondCompute_.valid()));
        if(!reused_){
            release(dev);
            fence_=dev.gpuSubmissionFence();inFlight_=true;
            auto createPipeline=[&](const Source& source){return dev.createCompute({metal?source.msl.c_str():nullptr,vulkan?source.glsl.c_str():nullptr,"TextureGraph compute"});};
            compute_=createPipeline(plan.first);
            if(plan.twoPass)secondCompute_=createPipeline(plan.second);
            gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;desc.size=bytes;desc.label="TextureGraph output";
            buffer_=dev.createBuffer(desc);
            if(plan.twoPass){desc.label="TextureGraph height scratch";scratch_=dev.createBuffer(desc);}
            if(!compute_.valid()||!buffer_.valid()||(plan.twoPass&&(!secondCompute_.valid()||!scratch_.valid()))){if(!ownsResources())inFlight_=false;fallback("GPU plan allocation rejected");return;}
            capacity_=bytes;firstKey_=first;secondKey_=second;
        }
        fence_=dev.gpuSubmissionFence();inFlight_=true;
        auto dispatch=[&](gfx::ComputeHandle pipeline,gfx::BufferHandle out,gfx::BufferHandle in){
            if(!dev.beginCompute(pipeline))return false;
            dev.setComputeBuffer(0,out);if(in.valid())dev.setComputeBuffer(1,in);
            const bool encoded=dev.dispatchChecked(uint32_t(bytes/4));dev.endCompute();
            if(!encoded||(metal&&!dev.computeDispatchSucceeded()))return false;
            ++passes_;return true;
        };
        if(!dispatch(compute_,plan.twoPass?scratch_:buffer_,{})||(plan.twoPass&&!dispatch(secondCompute_,buffer_,scratch_))){fallback("GPU dispatch rejected");return;}
        pixels_.assign(bytes,0);state_="pending";
        if(!queueRange(dev))fallback("GPU readback request rejected");
    }
    void shutdownAfterIdle(gfx::Device& dev){queued_=false;state_="cancelled";pixels_.clear();setImages_.clear();setBackends_.clear();if(dev.gpuHealthy())release(dev);else abandon();}
};
} // namespace aiverse::editor
