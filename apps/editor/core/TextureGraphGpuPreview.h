// Native editor owner only. One bounded async preview; no blocking GPU waits.
#pragma once
#include "engine/render/graph/TextureGraphGpu.h"
#include <memory>
#include <optional>

namespace aiverse::editor {
class TextureGraphGpuPreview {
    using Program=render::texture_graph::Program;
    struct Read {bool done=false;std::vector<uint8_t> bytes;};
    std::shared_ptr<Read> read_;
    gfx::BufferHandle buffer_{},scratch_{};gfx::ComputeHandle compute_{},secondCompute_{};
    int passes_=0;size_t scratchBytes_=0;
    bool ownsResources()const{return buffer_.valid()||scratch_.valid()||compute_.valid()||secondCompute_.valid();}
    void abandon(){buffer_={};scratch_={};compute_={};secondCompute_={};read_.reset();fence_=0;}
    uint64_t fence_=0,serial_=0;
    std::optional<Program> program_;
    size_t target_=0;int size_=0;
    std::string project_,state_="idle",reason_,backend_="none",submittedBackend_;
    std::vector<uint8_t> pixels_;
    bool queued_=false,requireGpu_=false;
    void fallback(const std::string& why){
        reason_=why;queued_=false;
        if(requireGpu_){state_="failed";return;}
        try{pixels_=render::texture_graph::evaluate(*program_,target_,size_).rgba;state_="ready";backend_="cpu";}
        catch(const std::exception& e){state_="failed";reason_=e.what();}
    }
    void release(gfx::Device& dev){if(compute_.valid())dev.destroyCompute(compute_);if(secondCompute_.valid())dev.destroyCompute(secondCompute_);if(buffer_.valid())dev.destroyBuffer(buffer_);if(scratch_.valid())dev.destroyBuffer(scratch_);abandon();}
public:
    Json status() const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string(state_));r.set("backend",Json::string(backend_));
        r.set("request",Json::string(std::to_string(serial_)));r.set("reason",Json::string(reason_));
        r.set("busy",Json::fromBool(queued_||ownsResources()));r.set("encodedPasses",Json::number(int64_t(passes_)));r.set("plannedScratchBytes",Json::number(int64_t(scratchBytes_)));
        if(state_=="ready"){
            std::string hex;hex.reserve(pixels_.size()*2);for(auto v:pixels_){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}
            r.set("width",Json::number(int64_t(size_)));r.set("height",Json::number(int64_t(size_)));r.set("rgbaHex",Json::string(hex));
        }return r;
    }
    Json start(const Json& p,const std::string& project){
        if(queued_||ownsResources())throw std::runtime_error("previous GPU preview still owns resources");
        if(project.empty())throw std::invalid_argument("open project first");
        auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object());
        size_t target=program.nodes.size();
        if(p.has("node")){for(size_t i=0;i<program.nodes.size();++i)if(program.nodes[i].id==p.getStr("node"))target=i;}
        else{auto it=program.outputs.find(p.getStr("output"));if(it!=program.outputs.end())target=it->second;}
        if(target>=program.nodes.size())throw std::invalid_argument("unknown preview target");
        if(serial_==~uint64_t{0})throw std::overflow_error("GPU preview identity exhausted");
        const int requested=render::texture_graph::integer(p["resolution"],32,8,render::texture_graph::gpu::maxPreviewSize);
        ++serial_;size_=std::min(program.size,requested);target_=target;program_=std::move(program);project_=project;
        passes_=0;scratchBytes_=0;requireGpu_=p.getBool("requireGpu");pixels_.clear();reason_.clear();backend_="none";queued_=true;state_="queued";return status();
    }
    void checkProject(const std::string& project){
        if(!project_.empty()&&project_!=project){queued_=false;state_="cancelled";pixels_.clear();project_.clear();}
    }
    // Called at native frame boundary outside encoders. Readback completion
    // is delivered on owner thread; never capture the editor or device in callback.
    void tick(gfx::Device& dev,const std::string& project){
        checkProject(project);
        if(ownsResources()&&!dev.gpuHealthy()){
            // Device-loss handles are abandoned to device teardown, never destroyed here.
            abandon();
            if(state_=="pending")fallback("GPU device unhealthy");
        }
        if(read_&&read_->done){
            if(state_=="pending"){
                auto& data=read_->bytes;bool valid=data.size()==size_t(size_)*size_*4;
                if(valid)for(size_t i=3;i<data.size();i+=4)if(data[i]!=255){valid=false;break;}
                if(valid){
                    // A small CPU reference is an explicit acceptance check, not a speed benchmark.
                    try{
                        const auto reference=render::texture_graph::evaluate(*program_,target_,size_);
                        for(size_t i=0;i<data.size();++i)if(std::abs(int(data[i])-int(reference.rgba[i]))>1){valid=false;break;}
                    }catch(...){valid=false;}
                }
                if(valid){pixels_=std::move(data);state_="ready";backend_=submittedBackend_;reason_="completed native readback; CPU reference within one RGBA8 unit";}
                else fallback("GPU readback or CPU parity check failed");
            }
            // Callback means GPU command completed (success or failure).
            if(dev.gpuHealthy())release(dev);else{abandon();}
        }else if(!read_&&fence_&&dev.gpuCompletedFence()>=fence_&&dev.gpuHealthy())release(dev);
        if(!queued_)return;
        queued_=false;
        const bool metal=dev.info().backend==gfx::Backend::Metal;
        const bool vulkan=dev.info().backend==gfx::Backend::Vulkan;
        if((!metal&&!vulkan)||!dev.supportsCompute()||!dev.gpuHealthy()){fallback("Metal/Vulkan compute unavailable");return;}
        auto plan=render::texture_graph::gpu::plan(*program_,target_,size_);
        if(plan.first.msl.empty()){fallback(plan.first.reason);return;}
        scratchBytes_=plan.scratchBytes;
        submittedBackend_=metal?"metal":"vulkan";
        fence_=dev.gpuSubmissionFence();
        auto createPipeline=[&](const render::texture_graph::gpu::Source& source){return dev.createCompute({metal?source.msl.c_str():nullptr,vulkan?source.glsl.c_str():nullptr,"TextureGraph preview"});};
        compute_=createPipeline(plan.first);
        if(!compute_.valid()){fallback("Native compute pipeline rejected");return;}
        std::vector<uint32_t> zero(size_t(size_)*size_,0);
        gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;desc.size=zero.size()*4;desc.data=zero.data();desc.label="TextureGraph preview RGBA8";
        buffer_=dev.createBuffer(desc);
        if(plan.twoPass){
            secondCompute_=createPipeline(plan.second);
            desc.label="TextureGraph float height scratch";scratch_=dev.createBuffer(desc);
            if(!scratch_.valid()||!secondCompute_.valid()){fallback("GPU neighborhood resources rejected");return;}
        }
        // Existing RHI orders writes between successive compute encoders.
        // Keep all resources until proven completion, even if pass 2 fails.
        auto dispatch=[&](gfx::ComputeHandle pipeline,gfx::BufferHandle output,gfx::BufferHandle input){
            if(!output.valid()||!dev.beginCompute(pipeline))return false;
            dev.setComputeBuffer(0,output);if(input.valid())dev.setComputeBuffer(1,input);
            const bool encoded=dev.dispatchChecked(uint32_t(zero.size()));dev.endCompute();
            if(!encoded||(metal&&!dev.computeDispatchSucceeded()))return false;
            ++passes_;return true;
        };
        if(!buffer_.valid()||!dispatch(compute_,plan.twoPass?scratch_:buffer_,{})||
           (plan.twoPass&&!dispatch(secondCompute_,buffer_,scratch_))){fallback("GPU dispatch rejected");return;}
        auto read=std::make_shared<Read>();
        auto callback=[read](std::vector<uint8_t> bytes){read->bytes=std::move(bytes);read->done=true;};
        const bool accepted=metal?dev.readBufferRangeAsync(buffer_,0,desc.size,callback):dev.readBufferAsync(buffer_,desc.size,callback);
        if(!accepted){fallback("GPU readback request rejected");return;}
        read_=std::move(read);state_="pending";
    }
    // Called only after the existing native shutdown GPU idle barrier.
    void shutdownAfterIdle(gfx::Device& dev){queued_=false;state_="cancelled";if(dev.gpuHealthy())release(dev);else{abandon();}}
};
} // namespace aiverse::editor
