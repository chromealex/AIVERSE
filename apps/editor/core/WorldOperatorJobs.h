#pragma once
#include "engine/data/Json.h"
#include <functional>
#include <mutex>
#include <memory>
#include <thread>
#include <stdexcept>
#include <utility>
#include <cstdint>

namespace aiverse::editor {
// One outstanding job per Workspace, including the retained terminal result.
// Callers must explicitly release terminal results. No live Workspace access
// in the worker. Cancellation and the irreversible barrier share one mutex.
class WorldOperatorJobs {
public:
    struct Cancelled final:std::exception {
        const char* what()const noexcept override{return "world job cancelled";}
    };
    class Control {
        friend class WorldOperatorJobs;
        mutable std::mutex mutex_;
        std::string phase_="queued";
        Json progress_=Json::object();
        bool cancel_=false,cancellable_=true,terminal_=false;
        void finish(){std::lock_guard lock(mutex_);terminal_=true;cancellable_=false;}
    public:
        bool cancelled()const{std::lock_guard lock(mutex_);return cancel_;}
        void check()const{if(cancelled())throw Cancelled{};}
        void progress(std::string phase,Json data=Json::object()){
            std::lock_guard lock(mutex_);phase_=std::move(phase);progress_=std::move(data);
        }
        // Call before first possible mutation. Once entered, cancellation is
        // refused; never report a durable side effect as cancelled.
        void irreversible(std::string phase){
            std::lock_guard lock(mutex_);
            if(cancel_)throw Cancelled{};
            cancellable_=false;phase_=std::move(phase);progress_=Json::object();
        }
        bool requestCancel(){
            std::lock_guard lock(mutex_);
            if(!cancellable_||terminal_)return false;
            cancel_=true;return true;
        }
        Json snapshot()const{
            std::lock_guard lock(mutex_);
            Json j=Json::object();j.set("phase",Json::string(phase_));
            j.set("progress",progress_);j.set("cancelRequested",Json::fromBool(cancel_));
            j.set("cancelAllowed",Json::fromBool(cancellable_&&!terminal_&&!cancel_));
            return j;
        }
    };
    using Work=std::function<Json(Control&)>;
private:
    struct Job {
        uint64_t id=0;
        std::string project,operation,state="running",error;
        Control control;
        Json result;
        std::mutex mutex;
    };
    std::unique_ptr<Job> job_;
    std::thread worker_;
    uint64_t lastId_=0;
    bool terminal()const{
        if(!job_)return true;
        std::lock_guard lock(job_->mutex);return job_->state!="running";
    }
    Job& checked(uint64_t id){
        if(!job_||job_->id!=id)throw std::runtime_error("unknown world job");
        return *job_;
    }
public:
    WorldOperatorJobs()=default;
    WorldOperatorJobs(const WorldOperatorJobs&)=delete;
    WorldOperatorJobs& operator=(const WorldOperatorJobs&)=delete;
    ~WorldOperatorJobs(){if(job_)job_->control.requestCancel();if(worker_.joinable())worker_.join();}
    bool occupied()const{return bool(job_);}
    uint64_t start(std::string project,std::string op,Work work){
#if defined(__EMSCRIPTEN__)
        (void)project;(void)op;(void)work;throw std::runtime_error("native world jobs unavailable");
#else
        if(job_||!work||lastId_==UINT64_MAX)throw std::runtime_error("world job slot busy; release terminal result first");
        auto j=std::make_unique<Job>();j->id=++lastId_;j->project=std::move(project);j->operation=std::move(op);
        auto* raw=j.get();
        job_=std::move(j);
        try{
            worker_=std::thread([raw,work=std::move(work)](){
                Json result;std::string state="succeeded",error;
                try{raw->control.check();result=work(raw->control);raw->control.check();}
                catch(const Cancelled&){state="cancelled";}
                catch(const std::exception& e){state=raw->control.cancelled()?"cancelled":"failed";error=e.what();}
                catch(...){state=raw->control.cancelled()?"cancelled":"failed";error="unknown world job error";}
                // Synchronize terminal result and cancellation so a late
                // accepted read-only cancellation cannot appear successful.
                std::lock_guard lock(raw->mutex);
                raw->control.finish();
                if(raw->control.cancelled()&&state=="succeeded")state="cancelled";
                raw->state=std::move(state);raw->error=std::move(error);
                if(raw->state=="succeeded")raw->result=std::move(result);
            });
        }catch(...){job_.reset();throw;}
        return raw->id;
#endif
    }
    Json status(uint64_t id=0){
        if(!job_){Json j=Json::object();j.set("state",Json::string("idle"));return j;}
        auto& j=checked(id?id:job_->id);
        std::lock_guard lock(j.mutex);
        auto out=j.control.snapshot();out.set("id",Json::string(std::to_string(j.id)));
        out.set("project",Json::string(j.project));out.set("operation",Json::string(j.operation));
        out.set("state",Json::string(j.state));if(!j.error.empty())out.set("error",Json::string(j.error));
        if(j.state=="succeeded")out.set("result",j.result);
        return out;
    }
    bool cancel(uint64_t id){return checked(id).control.requestCancel();}
    bool release(uint64_t id){
        checked(id);if(!terminal())return false;
        if(worker_.joinable())worker_.join();job_.reset();return true;
    }
};
}
