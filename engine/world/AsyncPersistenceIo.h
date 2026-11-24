#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <utility>

namespace aiverse::world {
// Single-flight bounded IO mailbox. One producer/consumer owner, one disk
// worker. Capacity includes the running request and its unconsumed receipt.
// The owner must keep every referenced IO object alive until stop() returns.
class AsyncPersistenceIo {
public:
    enum class Status { Idle, Queued, Writing, Durable, Failed };
    enum class Submit { Accepted, Busy, TooLarge, Stopped };
    struct Receipt {uint64_t ticket=0;Status status=Status::Idle;size_t bytes=0;};
    using Operation=std::function<bool()>;
private:
    mutable std::mutex mutex_;
    std::condition_variable wake_;
#if !defined(__EMSCRIPTEN__)
    std::thread worker_;
#endif
    Operation operation_;
    Receipt receipt_;
    size_t budget_=0;
    bool stopping_=false,started_=false,poisoned_=false;
    void run(){
        std::unique_lock lock(mutex_);
        for(;;){
            wake_.wait(lock,[&]{return stopping_||receipt_.status==Status::Queued;});
            if(receipt_.status!=Status::Queued)break;
            auto job=std::move(operation_);
            receipt_.status=Status::Writing;
            lock.unlock();
            bool ok=false;try{ok=job();}catch(...){}
            job={};
            lock.lock();
            receipt_.status=ok?Status::Durable:Status::Failed;
            if(!ok)poisoned_=true;
            if(stopping_)break;
        }
    }
public:
    AsyncPersistenceIo()=default;
    AsyncPersistenceIo(const AsyncPersistenceIo&)=delete;
    AsyncPersistenceIo& operator=(const AsyncPersistenceIo&)=delete;
    ~AsyncPersistenceIo(){stop();}
    bool start(size_t budget){
#if defined(__EMSCRIPTEN__)
        (void)budget;return false;
#else
        std::lock_guard lock(mutex_);
        if(started_||!budget)return false;
        budget_=budget;stopping_=false;poisoned_=false;receipt_={};
        try{worker_=std::thread([this]{run();});}catch(...){return false;}
        started_=true;return true;
#endif
    }
    Submit submit(uint64_t ticket,size_t bytes,Operation job){
        std::lock_guard lock(mutex_);
        if(!started_||stopping_||poisoned_||!ticket||!job)return Submit::Stopped;
        if(receipt_.status!=Status::Idle)return Submit::Busy;
        if(bytes>budget_)return Submit::TooLarge;
        operation_=std::move(job);receipt_={ticket,Status::Queued,bytes};
        wake_.notify_one();return Submit::Accepted;
    }
    Receipt status()const{std::lock_guard lock(mutex_);return receipt_;}
    // A durable receipt means the operation returned success AFTER flush,
    // not just enqueue or write. Failed workers cannot accept more jobs.
    bool take(Receipt& out){
        std::lock_guard lock(mutex_);
        if(receipt_.status!=Status::Durable&&receipt_.status!=Status::Failed)return false;
        out=receipt_;receipt_={};return true;
    }
    // Drain the one accepted request before releasing its IO dependencies.
    // The owner may discard the receipt; recovery will inspect disk again.
    void stop(){
        {
            std::lock_guard lock(mutex_);
            if(!started_)return;
            stopping_=true;wake_.notify_one();
        }
#if !defined(__EMSCRIPTEN__)
        if(worker_.joinable())worker_.join();
#endif
        std::lock_guard lock(mutex_);
        started_=false;operation_={};receipt_={};budget_=0;
    }
};
}
