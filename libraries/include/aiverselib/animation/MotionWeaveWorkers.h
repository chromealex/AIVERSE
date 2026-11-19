// MotionWeave reusable bounded work-stealing worker pool.
// Workers persist across animation frames; one caller thread participates.
// This scheduler is owned by a presentation host, never the simulation World.
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace aiverse::lib::animation {
class MotionWeaveWorkerPool final {
public:
    static size_t recommendedWorkers() noexcept {
#if defined(__EMSCRIPTEN__)
        return 0;
#else
        const unsigned hardware=std::thread::hardware_concurrency();
        return hardware>1 ? std::min<size_t>(hardware-1,7) : 0;
#endif
    }
    explicit MotionWeaveWorkerPool(size_t workers=recommendedWorkers()) {
#if defined(__EMSCRIPTEN__)
        (void)workers;
#else
        const size_t count=std::min<size_t>(workers,7);
        try {
            for(size_t i=0;i<count;++i)
                workers_.emplace_back([this]{runWorker();});
        } catch(...) {
            stopWorkers();
            throw;
        }
#endif
    }
    MotionWeaveWorkerPool(const MotionWeaveWorkerPool&)=delete;
    MotionWeaveWorkerPool& operator=(const MotionWeaveWorkerPool&)=delete;
    MotionWeaveWorkerPool(MotionWeaveWorkerPool&&)=delete;
    MotionWeaveWorkerPool& operator=(MotionWeaveWorkerPool&&)=delete;
    ~MotionWeaveWorkerPool() {stopWorkers();}

    size_t workerCount() const noexcept {return workers_.size();}
    // Executes each independent task once. Call only from a host-owned
    // frame thread. Concurrent callers are serialized. Waiting for return
    // is a barrier: no worker retains task references afterwards.
    // A worker exception is rethrown on the caller after ALL lanes join.
    template<class F> void parallelFor(size_t tasks,F&& action) {
        std::unique_lock submission(submission_);
        if(tasks==0)return;
        if(workers_.empty() || tasks==1) {
            for(size_t i=0;i<tasks;++i)action(i);
            return;
        }
        // A reference to this stack-owned callable is valid for the entire
        // blocking dispatch. Workers never allocate/copy std::function.
        std::function<void(size_t)> fn=[&action](size_t i){action(i);};
        {
            std::lock_guard lock(mutex_);
            firstError_=nullptr;
            next_.store(0,std::memory_order_relaxed);
            count_=tasks;
            job_=&fn;
            outstanding_=workers_.size();
            ++epoch_;
        }
        wake_.notify_all();
        consume(fn,tasks);
        std::exception_ptr failure;
        {
            std::unique_lock lock(mutex_);
            done_.wait(lock,[&]{return outstanding_==0;});
            failure=firstError_;
            job_=nullptr;
        }
        if(failure)std::rethrow_exception(failure);
    }
private:
    void consume(const std::function<void(size_t)>& job,size_t count) noexcept {
        for(;;) {
            const size_t i=next_.fetch_add(1,std::memory_order_relaxed);
            if(i>=count)break;
            try {job(i);}
            catch(...) {
                std::lock_guard lock(mutex_);
                if(!firstError_)firstError_=std::current_exception();
            }
        }
    }
    void runWorker() noexcept {
        uint64_t seen=0;
        for(;;) {
            const std::function<void(size_t)>* fn=nullptr;
            size_t count=0;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock,[&]{return stopping_ || epoch_!=seen;});
                if(stopping_)return;
                seen=epoch_;
                fn=job_;
                count=count_;
            }
            // The caller cannot clear *fn until outstanding_ reaches zero.
            consume(*fn,count);
            {
                std::lock_guard lock(mutex_);
                if(--outstanding_==0)done_.notify_one();
            }
        }
    }
    void stopWorkers() noexcept {
        std::unique_lock submission(submission_);
        {
            std::lock_guard lock(mutex_);
            stopping_=true;
        }
        wake_.notify_all();
        for(auto& worker:workers_)if(worker.joinable())worker.join();
        workers_.clear();
    }
    std::mutex submission_;
    std::mutex mutex_;
    std::condition_variable wake_,done_;
    std::vector<std::thread> workers_;
    const std::function<void(size_t)>* job_=nullptr;
    std::atomic<size_t> next_{0};
    size_t count_=0,outstanding_=0;
    uint64_t epoch_=0;
    std::exception_ptr firstError_;
    bool stopping_=false;
};
} // namespace aiverse::lib::animation
