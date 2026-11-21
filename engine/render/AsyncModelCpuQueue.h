// Genre-neutral persistent CPU worker pool for model imports.
// All public API calls occur on the owning host thread. FileReader callbacks
// may execute simultaneously; callers MUST supply thread-safe readers.
// No renderer, GPU device, ECS or MotionWeave operation runs on workers.
#pragma once
#include "Model.h"
#include "ModelTextureCache.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class ModelLoadPriority : uint8_t {
    Background=0, Visible=1, Interactive=2
};

class AsyncModelCpuQueue final {
public:
    struct Prepared {
        std::string source;
        uint64_t revision=0;
        std::unique_ptr<Model> model;
        std::vector<std::string> warnings;
    };
    struct Stats {
        size_t queued=0;
        bool workerActive=false;
        uint64_t finished=0,discarded=0;
        size_t queuedInteractive=0,queuedVisible=0;
        size_t activeWorkers=0,configuredWorkers=1;
        uint64_t dispatched=0,backgroundFairnessDispatches=0;
        size_t ready=0,liveThreads=0;
    };
    AsyncModelCpuQueue()=default;
    AsyncModelCpuQueue(const AsyncModelCpuQueue&)=delete;
    AsyncModelCpuQueue& operator=(const AsyncModelCpuQueue&)=delete;
    ~AsyncModelCpuQueue(){shutdown();}

    // Reconfiguration is rare: it joins in-flight CPU imports but does not
    // cancel them or drop already completed results. Do not reconfigure
    // repeatedly on the render hot path. The pool starts lazily.
    void setWorkerCount(size_t count) {
        count=std::clamp(count,size_t(1),size_t(8));
        std::unique_lock lock(mutex_);
        if(count==workerCount_)return;
        stopWorkersLocked(lock);
        workerCount_=count;
        if(!queued_.empty())startWorkersLocked();
        lock.unlock();
        cv_.notify_all();
    }
    size_t workerCount() const {
        std::lock_guard lock(mutex_);
        return workerCount_;
    }
    void setMaxQueued(size_t count) {
        std::lock_guard lock(mutex_);
        maxQueued_=count;
    }
    size_t maxQueued() const {
        std::lock_guard lock(mutex_);
        return maxQueued_;
    }
    void setMaxReady(size_t count) {
        std::lock_guard lock(mutex_);
        maxReady_=std::max(size_t(1),count);
        cv_.notify_all();
    }
    Stats stats() const {
        std::lock_guard lock(mutex_);
        Stats result;
        result.queued=queued_.size();
        result.workerActive=activeCount_!=0;
        result.finished=finished_;
        result.discarded=discarded_;
        result.activeWorkers=activeCount_;
        result.configuredWorkers=workerCount_;
        result.dispatched=dispatched_;
        result.backgroundFairnessDispatches=fairnessDispatches_;
        result.ready=ready_.size();
        result.liveThreads=threads_.size();
        for(const auto& r:queued_) {
            if(r.priority==ModelLoadPriority::Interactive)++result.queuedInteractive;
            if(r.priority==ModelLoadPriority::Visible)++result.queuedVisible;
        }
        return result;
    }
    bool contains(const std::string& source) const {
        std::lock_guard lock(mutex_);
        return containsLocked(source);
    }

    bool request(std::string source,FileReader reader,
                 std::shared_ptr<ModelTextureCache> textures,
                 ModelLoadPriority priority=ModelLoadPriority::Visible) {
        if(source.empty() || !reader || !textures)return false;
        std::unique_lock lock(mutex_);
        const uint64_t revision=currentRevisionLocked(source);
        const auto active=active_.find(source);
        if(active!=active_.end() && active->second==revision)return false;
        for(auto& item:queued_) {
            if(item.source!=source || item.revision!=revision)continue;
            if(item.priority>=priority)return false;
            item.priority=priority; // promotion never creates a duplicate
            lock.unlock();
            cv_.notify_all();
            return true;
        }
        for(const auto& item:ready_)
            if(item.source==source && item.revision==revision)return false;
        // Queue + completed-but-unpublished results are bounded. Worker
        // activity can temporarily contribute up to workerCount_ more.
        if(maxQueued_!=0 && queued_.size()+ready_.size()>=maxQueued_)
            return false;
        queued_.push_back({std::move(source),revision,
                           std::move(reader),std::move(textures),priority});
        startWorkersLocked();
        lock.unlock();
        cv_.notify_one();
        return true;
    }

    // Never wait for the active CPU task: invalidate its revision and let
    // it finish naturally, then discard its stale result at pollReady().
    void invalidate(const std::string& source) {
        std::lock_guard lock(mutex_);
        ++revisions_[source];
        for(auto it=queued_.begin();it!=queued_.end();) {
            if(it->source==source)it=queued_.erase(it);
            else ++it;
        }
        cv_.notify_all();
    }

    // Called only by the host/render owner thread. Discards as many stale
    // completions as necessary while returning at most one valid result.
    bool pollReady(Prepared& out) {
        std::unique_lock lock(mutex_);
        while(!ready_.empty()) {
            Prepared next=std::move(ready_.front());
            ready_.pop_front();
            if(next.revision!=currentRevisionLocked(next.source)) {
                ++discarded_;
                cv_.notify_all();
                continue;
            }
            ++finished_;
            out=std::move(next);
            lock.unlock();
            cv_.notify_all();
            return true;
        }
        return false;
    }

    // Joins every worker and invalidates its pending CPU result before the
    // host frees the FileReader's captured project data or texture cache.
    void shutdown() {
        std::unique_lock lock(mutex_);
        queued_.clear();
        stopWorkersLocked(lock);
        ready_.clear();
        active_.clear();
        revisions_.clear();
        priorityRun_=0;
    }

private:
    struct Request {
        std::string source;
        uint64_t revision=0;
        FileReader reader;
        std::shared_ptr<ModelTextureCache> textures;
        ModelLoadPriority priority=ModelLoadPriority::Visible;
    };

    uint64_t currentRevisionLocked(const std::string& source) const {
        const auto it=revisions_.find(source);
        return it==revisions_.end()?0:it->second;
    }
    bool containsLocked(const std::string& source) const {
        const uint64_t revision=currentRevisionLocked(source);
        const auto active=active_.find(source);
        if(active!=active_.end() && active->second==revision)return true;
        for(const auto& r:queued_)
            if(r.source==source && r.revision==revision)return true;
        for(const auto& r:ready_)
            if(r.source==source && r.revision==revision)return true;
        return false;
    }
    // If high-priority requests repeatedly bypass an older background
    // item, the eighth such dispatch admits the oldest request instead.
    size_t chooseNextLocked() {
        auto best=std::max_element(queued_.begin(),queued_.end(),
            [](const Request& a,const Request& b) {
                return a.priority<b.priority;
            });
        const size_t preferred=size_t(std::distance(queued_.begin(),best));
        if(preferred!=0 && priorityRun_>=7 && queued_.size()>1) {
            priorityRun_=0;
            ++fairnessDispatches_;
            return 0;
        }
        if(preferred==0)priorityRun_=0;
        else ++priorityRun_;
        return preferred;
    }
    void runWorker() {
        for(;;) {
            Request request;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock,[&] {
                    return stop_ ||
                        (!queued_.empty() && ready_.size()<maxReady_);
                });
                if(stop_)return;
                const size_t choice=chooseNextLocked();
                request=std::move(queued_[choice]);
                queued_.erase(queued_.begin()+
                    static_cast<std::ptrdiff_t>(choice));
                active_[request.source]=request.revision;
                ++activeCount_;
                ++dispatched_;
            }
            Prepared result;
            result.source=request.source;
            result.revision=request.revision;
            try {
                result.model=loadModel(request.source,request.reader,
                                       &result.warnings,request.textures);
                if(result.model)prepareModelUpload(*result.model);
            }catch(const std::exception& e) {
                result.warnings.push_back(request.source+": "+e.what());
                result.model.reset();
            }
            {
                std::lock_guard lock(mutex_);
                const auto active=active_.find(request.source);
                if(active!=active_.end() &&
                   active->second==request.revision)active_.erase(active);
                --activeCount_;
                // Pool resizing waits for active tasks but must preserve
                // valid results. Only explicit shutdown clears the ready
                // queue after joining, while invalidation rejects old ids.
                if(result.revision==currentRevisionLocked(result.source))
                    ready_.push_back(std::move(result));
                else ++discarded_;
            }
            cv_.notify_all();
        }
    }
    void startWorkersLocked() {
        if(!threads_.empty())return;
        stop_=false;
        for(size_t i=0;i<workerCount_;++i)
            threads_.emplace_back([this]{runWorker();});
    }
    // Lock may be temporarily released only to join running tasks.
    void stopWorkersLocked(std::unique_lock<std::mutex>& lock) {
        if(threads_.empty())return;
        stop_=true;
        cv_.notify_all();
        lock.unlock();
        for(auto& worker:threads_)if(worker.joinable())worker.join();
        lock.lock();
        threads_.clear();
        active_.clear();
        activeCount_=0;
        stop_=false;
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_=false;
    size_t workerCount_=1,maxQueued_=16,maxReady_=16;
    size_t activeCount_=0;
    std::vector<std::thread> threads_;
    std::map<std::string,uint64_t> active_;
    std::deque<Request> queued_;
    std::deque<Prepared> ready_;
    std::map<std::string,uint64_t> revisions_;
    uint64_t finished_=0,discarded_=0,dispatched_=0;
    uint64_t fairnessDispatches_=0;
    size_t priorityRun_=0;
};

} // namespace aiverse::render
