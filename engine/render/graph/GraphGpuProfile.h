// Scoped GPU diagnostics: accepts only actual completed RHI timing queries.
// Host remains sole consumer of Device::takeGpuScopeTimings().
#pragma once
#include "engine/gfx/Gfx.h"
#include "engine/data/Json.h"
#include <cmath>
#include <deque>
#include <map>
#include <limits>

namespace aiverse::graph_gpu_profile {
inline const char* backend(gfx::Backend b){
    switch(b){case gfx::Backend::Metal:return "Metal";case gfx::Backend::GL33:return "GL33";case gfx::Backend::GLES3:return "GLES3";case gfx::Backend::Vulkan:return "Vulkan";}
    return "unknown";
}
class Capture {
    struct Pending {std::string scope;uint64_t frame=0,view=0;bool adaptive=false;};
    std::map<uint64_t,Pending> pending_;
    std::deque<Json> samples_;
    uint64_t serial_=0,epoch_=0,accepted_=0,rejected_=0,invalid_=0;
    bool enabled_=false;
    std::string backend_,device_,project_;
public:
    static constexpr uint64_t prefix=0x4750000000000000ULL,mask=0x0000ffffffffffffULL;
    static constexpr size_t maxPending=32,maxSamples=120;
    bool enabled() const{return enabled_;}
    void clear(){enabled_=false;pending_.clear();samples_.clear();accepted_=rejected_=invalid_=0;project_.clear();}
    void start(const gfx::DeviceInfo& info,const std::string& project){
        clear();if(epoch_==std::numeric_limits<uint64_t>::max())throw std::overflow_error("GPU profile epoch exhausted");
        ++epoch_;backend_=backend(info.backend);device_=info.name;project_=project;enabled_=true;
    }
    void checkProject(const std::string& project){if(project!=project_&&enabled_)clear();}
    void stop(){enabled_=false;pending_.clear();}
    template<class Device> uint64_t begin(Device& dev,const char* scope,uint64_t view,bool adaptive){
        if(!enabled_)return 0;
        if(pending_.size()>=maxPending||serial_>=mask){++rejected_;return 0;}
        const auto tag=prefix|++serial_;
        // Reserve metadata before starting backend scope. No failure after begin.
        pending_.emplace(tag,Pending{scope,dev.frameSerial(),view,adaptive});
        if(!dev.beginGpuScope(tag)){pending_.erase(tag);++rejected_;return 0;}
        ++accepted_;return tag;
    }
    // Returns the legacy adaptive view ID only for completed valid requested
    // viewport measurements. Unknown/cancelled tags can never be relabeled.
    uint64_t complete(const gfx::Device::GpuTiming& timing,uint64_t completionFrame){
        auto it=pending_.find(timing.tag);if(it==pending_.end())return 0;
        const auto meta=std::move(it->second);pending_.erase(it);
        if(!timing.valid||!std::isfinite(timing.milliseconds)||timing.milliseconds<=0){++invalid_;return 0;}
        Json row=Json::object();row.set("scope",Json::string(meta.scope));row.set("tag",Json::string(std::to_string(timing.tag)));
        row.set("submittedFrame",Json::string(std::to_string(meta.frame)));row.set("completionFrame",Json::string(std::to_string(completionFrame)));
        row.set("view",Json::string(std::to_string(meta.view)));row.set("milliseconds",Json::number(timing.milliseconds,6));
        samples_.push_back(std::move(row));if(samples_.size()>maxSamples)samples_.pop_front();
        return meta.adaptive?meta.view:0;
    }
    void cancel(uint64_t tag){pending_.erase(tag);}
    Json report() const{
        Json r=Json::object();r.set("available",Json::fromBool(!samples_.empty()));r.set("enabled",Json::fromBool(enabled_));
        r.set("backend",Json::string(backend_));r.set("device",Json::string(device_));r.set("epoch",Json::string(std::to_string(epoch_)));
        r.set("accepted",Json::string(std::to_string(accepted_)));r.set("rejected",Json::string(std::to_string(rejected_)));
        r.set("invalid",Json::string(std::to_string(invalid_)));r.set("pending",Json::number(int64_t(pending_.size())));
        Json rows=Json::array();for(const auto& s:samples_)rows.push(s);r.set("samples",rows);
        r.set("note",Json::string("Completed native scope timestamps only. Viewport scope includes render, FX draw and post resolve; not per-node, per-material or per-pass timing. FX step scope includes encoded preview step work. Rejected queries may mean unsupported backend, encoder conflict or capacity. No CPU fallback labeled GPU."));
        return r;
    }
};
// End the scope on exception as well; discard failed host work.
class Scope {
    gfx::Device* dev_=nullptr;Capture* capture_=nullptr;uint64_t tag_=0;
public:
    Scope(gfx::Device& dev,Capture& capture,const char* name,uint64_t view=0,bool adaptive=false):capture_(&capture){
        tag_=capture.begin(dev,name,view,adaptive);if(tag_)dev_=&dev;
    }
    ~Scope(){if(dev_){dev_->endGpuScope();capture_->cancel(tag_);}}
    bool active()const{return dev_!=nullptr;}
    void finish(){if(dev_){dev_->endGpuScope();dev_=nullptr;}}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
} // namespace aiverse::graph_gpu_profile
