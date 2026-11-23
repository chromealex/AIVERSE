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
    gfx::BufferHandle buffer_{};gfx::ComputeHandle compute_{};
    uint64_t fence_=0,serial_=0;
    std::optional<Program> program_;
    size_t target_=0;int size_=0;
    std::string project_,state_="idle",reason_,backend_="none";
    std::vector<uint8_t> pixels_;
    bool queued_=false,requireGpu_=false;
    void fallback(const std::string& why){
        reason_=why;queued_=false;
        if(requireGpu_){state_="failed";return;}
        try{pixels_=render::texture_graph::evaluate(*program_,target_,size_).rgba;state_="ready";backend_="cpu";}
        catch(const std::exception& e){state_="failed";reason_=e.what();}
    }
    void release(gfx::Device& dev){if(compute_.valid())dev.destroyCompute(compute_);if(buffer_.valid())dev.destroyBuffer(buffer_);compute_={};buffer_={};fence_=0;read_.reset();}
public:
    Json status() const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string(state_));r.set("backend",Json::string(backend_));
        r.set("request",Json::string(std::to_string(serial_)));r.set("reason",Json::string(reason_));
        r.set("busy",Json::fromBool(queued_||buffer_.valid()||compute_.valid()));
        if(state_=="ready"){
            std::string hex;hex.reserve(pixels_.size()*2);for(auto v:pixels_){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}
            r.set("width",Json::number(int64_t(size_)));r.set("height",Json::number(int64_t(size_)));r.set("rgbaHex",Json::string(hex));
        }return r;
    }
    Json start(const Json& p,const std::string& project){
        if(queued_||buffer_.valid()||compute_.valid())throw std::runtime_error("previous GPU preview still owns resources");
        if(project.empty())throw std::invalid_argument("open project first");
        auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object());
        size_t target=program.nodes.size();
        if(p.has("node")){for(size_t i=0;i<program.nodes.size();++i)if(program.nodes[i].id==p.getStr("node"))target=i;}
        else{auto it=program.outputs.find(p.getStr("output"));if(it!=program.outputs.end())target=it->second;}
        if(target>=program.nodes.size())throw std::invalid_argument("unknown preview target");
        if(serial_==~uint64_t{0})throw std::overflow_error("GPU preview identity exhausted");
        ++serial_;size_=std::min(program.size,32);target_=target;program_=std::move(program);project_=project;
        requireGpu_=p.getBool("requireGpu");pixels_.clear();reason_.clear();backend_="none";queued_=true;state_="queued";return status();
    }
    void checkProject(const std::string& project){
        if(!project_.empty()&&project_!=project){queued_=false;state_="cancelled";pixels_.clear();project_.clear();}
    }
    // Called at native frame boundary outside encoders. Readback completion
    // is delivered on owner thread; never capture the editor or device in callback.
    void tick(gfx::Device& dev,const std::string& project){
        checkProject(project);
        if(read_&&read_->done){
            if(state_=="pending"){
                auto& data=read_->bytes;bool valid=data.size()==size_t(size_)*size_*4;
                if(valid)for(size_t i=3;i<data.size();i+=4)if(data[i]!=255){valid=false;break;}
                if(valid){
                    // A small CPU reference is an explicit acceptance check, not a speed benchmark.
                    const auto reference=render::texture_graph::evaluate(*program_,target_,size_);
                    for(size_t i=0;i<data.size();++i)if(std::abs(int(data[i])-int(reference.rgba[i]))>1){valid=false;break;}
                }
                if(valid){pixels_=std::move(data);state_="ready";backend_="metal";reason_="completed native readback; CPU reference within one RGBA8 unit";}
                else fallback("GPU readback or CPU parity check failed");
            }
            // Callback means GPU command completed (success or failure).
            if(dev.gpuHealthy())release(dev);else{buffer_={};compute_={};read_.reset();fence_=0;}
        }else if(!read_&&fence_&&dev.gpuCompletedFence()>=fence_&&dev.gpuHealthy())release(dev);
        if(!queued_)return;
        queued_=false;
        if(dev.info().backend!=gfx::Backend::Metal||!dev.supportsCompute()||!dev.gpuHealthy()){fallback("Metal compute unavailable");return;}
        auto source=render::texture_graph::gpu::compile(*program_,target_,size_);
        if(source.msl.empty()){fallback(source.reason);return;}
        compute_=dev.createCompute({source.msl.c_str(),nullptr,"TextureGraph preview"});
        if(!compute_.valid()){fallback("Metal pipeline rejected");return;}
        std::vector<uint32_t> zero(size_t(size_)*size_,0);
        gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;desc.size=zero.size()*4;desc.data=zero.data();desc.label="TextureGraph preview RGBA8";
        buffer_=dev.createBuffer(desc);
        // Keep allocations until a proven fence/idle even if encoding fails.
        fence_=dev.gpuSubmissionFence();
        if(!buffer_.valid()||!dev.beginCompute(compute_)){fallback("GPU buffer or encoder rejected");return;}
        dev.setComputeBuffer(0,buffer_);const bool encoded=dev.dispatchChecked(uint32_t(zero.size()));dev.endCompute();
        if(!encoded||!dev.computeDispatchSucceeded()){fallback("GPU dispatch rejected");return;}
        auto read=std::make_shared<Read>();
        if(!dev.readBufferRangeAsync(buffer_,0,desc.size,[read](std::vector<uint8_t> bytes){read->bytes=std::move(bytes);read->done=true;})){fallback("GPU readback request rejected");return;}
        read_=std::move(read);state_="pending";
    }
    // Called only after the existing native shutdown GPU idle barrier.
    void shutdownAfterIdle(gfx::Device& dev){queued_=false;state_="cancelled";if(dev.gpuHealthy())release(dev);else{buffer_={};compute_={};read_.reset();fence_=0;}}
};
} // namespace aiverse::editor
