#include "AsyncChunkStream.h"
#include <atomic>
#include <deque>
#include <mutex>
#ifndef __EMSCRIPTEN__
#include <condition_variable>
#include <thread>
#endif

namespace aiverse::world {
struct AsyncChunkStream::Impl {
    struct Job { Cell cell;size_t bytes;std::atomic<bool> canceled=false;bool done=false;Payload payload;std::string error; };
    ChunkStreamer selector;
    Prepare prepare;
    size_t stagingLimit,maxJobs,staging=0;
    std::map<Cell,size_t> costs;
    std::map<Cell,std::shared_ptr<Job>> active;
    std::set<Cell> published;
    std::deque<std::shared_ptr<Job>> jobs;
    std::deque<std::shared_ptr<Job>> completed;
    std::mutex mutex;
    bool stopping=false;
    Status status;
#ifndef __EMSCRIPTEN__
    std::condition_variable wake;
    std::thread worker;
#endif
    Impl(StreamLimits limits,size_t bytes,size_t count,Prepare p):selector(limits),prepare(std::move(p)),stagingLimit(bytes),maxJobs(count) {
#ifndef __EMSCRIPTEN__
        worker=std::thread([this]{for(;;){std::shared_ptr<Job> job;{
            std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||!jobs.empty();});
            if(stopping)return;
            job=jobs.front();jobs.pop_front();
        }run(job);}});
#endif
    }
    void run(const std::shared_ptr<Job>& job) {
        if(!job->canceled){try{job->payload=prepare(job->cell);if(!job->payload)job->error="chunk preparation failed";}
            catch(const std::exception& e){job->error=e.what();}catch(...){job->error="chunk preparation failed";}}
        std::lock_guard lock(mutex);job->done=true;completed.push_back(job);
    }
    void stop() {
        {std::lock_guard lock(mutex);stopping=true;for(auto& job:jobs)job->canceled=true;}
#ifndef __EMSCRIPTEN__
        wake.notify_all();if(worker.joinable())worker.join();
#endif
    }
    ~Impl(){stop();}
};
AsyncChunkStream::AsyncChunkStream(StreamLimits limits,size_t bytes,size_t jobs,Prepare prepare):impl_(std::make_unique<Impl>(limits,bytes,jobs,std::move(prepare))){}
AsyncChunkStream::~AsyncChunkStream()=default;
bool AsyncChunkStream::add(Cell cell,size_t bytes){if(bytes>impl_->stagingLimit)return false;if(!impl_->selector.add(cell,bytes))return false;impl_->costs[cell]=bytes;return true;}
bool AsyncChunkStream::ready(Cell cell)const{return impl_->published.contains(cell);}
const AsyncChunkStream::Status& AsyncChunkStream::status()const{return impl_->status;}
AsyncChunkStream::Status AsyncChunkStream::update(std::span<const StreamObserver> observers,std::span<const Cell> pins,size_t requests,size_t publications,const Publish& publish,const Streamer::Unload& unload,size_t publicationBytes) {
    auto& s=*impl_;if(s.stopping)return s.status;
    // Results of canceled jobs still own their staging reservation until the worker releases them.
    std::deque<std::shared_ptr<Impl::Job>> completed;
    {std::lock_guard lock(s.mutex);completed.swap(s.completed);}
    for(auto& job:completed) {
        s.staging-=job->bytes;
        auto it=s.active.find(job->cell);
        if(job->canceled||it==s.active.end()||it->second!=job){++s.status.discarded;job->payload.reset();continue;}
        if(!job->payload){s.status.error=job->error;s.selector.failed(job->cell);s.active.erase(it);}
        // Successful CPU data stays under its residency reservation until publish or eviction.
    }
    auto retire=[&](Cell cell){
        auto it=s.active.find(cell);if(it!=s.active.end()){it->second->canceled=true;s.active.erase(it);}
        if(s.published.erase(cell))unload(cell);
    };
    s.status.selection=s.selector.update(observers,pins,requests,[&](Cell cell){
        auto bytes=s.costs.at(cell);
        if(bytes>s.stagingLimit-s.staging)return false;
        auto job=std::make_shared<Impl::Job>();job->cell=cell;job->bytes=bytes;
        {std::lock_guard lock(s.mutex);if(s.jobs.size()>=s.maxJobs)return false;s.jobs.push_back(job);}
        s.active[cell]=job;s.staging+=bytes;
#ifndef __EMSCRIPTEN__
        s.wake.notify_one();
#endif
        return true;
    },retire);
#ifdef __EMSCRIPTEN__
    // No pthread dependency in the default browser build. One bounded preparation per update.
    std::shared_ptr<Impl::Job> job;
    {std::lock_guard lock(s.mutex);if(!s.jobs.empty()){job=s.jobs.front();s.jobs.pop_front();}}
    if(job)s.run(job);
#endif
    for(auto it=s.active.begin();it!=s.active.end()&&publications;) {
        auto job=it->second;bool done;
        {std::lock_guard lock(s.mutex);done=job->done;}
        // Only drained completions are publishable: staging accounting must precede publication.
        bool drained=done;
        {std::lock_guard lock(s.mutex);for(const auto& c:s.completed)if(c==job)drained=false;}
        if(!drained||!job->payload||job->bytes>publicationBytes){++it;continue;}
        --publications;publicationBytes-=job->bytes;bool ok=false;std::string error;
        try{ok=publish(it->first,job->payload);}catch(const std::exception& e){error=e.what();}catch(...){error="chunk publication failed";}
        if(ok){s.published.insert(it->first);s.status.error.clear();}
        else {s.selector.failed(it->first);s.status.error=error.empty()?"chunk publication failed":error;}
        it=s.active.erase(it);
    }
    s.status.ready=s.published.size();s.status.preparing=s.active.size();s.status.stagingBytes=s.staging;
    // A failed publication invalidated the selector after its update.
    s.status.selection.resident=s.selector.loaded().size();
    s.status.selection.reservedBytes=0;for(auto cell:s.selector.loaded())s.status.selection.reservedBytes+=s.costs.at(cell);
    s.status.selection.pending=s.status.selection.wanted-s.status.selection.budgetBlocked-s.status.selection.resident;
    return s.status;
}
void AsyncChunkStream::shutdown(const Streamer::Unload& unload) {
    auto& s=*impl_;s.stop();for(auto cell:s.published)unload(cell);
    s.published.clear();s.active.clear();s.jobs.clear();s.completed.clear();s.staging=0;s.status={};
}
}
