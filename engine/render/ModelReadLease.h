// Short-lived render-owner-thread reader guard for ModelResourceCache.
//
// This is NOT shared Model ownership and NOT a GPU completion fence.
// A live lease blocks cache eviction/replacement/shutdown; host must
// release it after joined animation evaluation and synchronous draw.
// The host's separate ModelResourceCache GPU queue still retires RHI handles.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace aiverse::render {

struct Model;
class ModelResourceCache;

struct ModelReadLeaseState final {
    std::atomic<size_t> readers{0};
    std::atomic<size_t> peakReaders{0};
    std::atomic<uint64_t> blockedMutations{0};
    std::atomic<bool> alive{true};
    std::thread::id ownerThread;

    void enter() noexcept {
        const size_t active=readers.fetch_add(1,std::memory_order_acq_rel)+1;
        size_t previous=peakReaders.load(std::memory_order_relaxed);
        while(active>previous &&
              !peakReaders.compare_exchange_weak(previous,active,
                  std::memory_order_relaxed,std::memory_order_relaxed)) {}
    }
};

class ModelReadLease final {
public:
    ModelReadLease()=default;
    ModelReadLease(const ModelReadLease&)=delete;
    ModelReadLease& operator=(const ModelReadLease&)=delete;
    ModelReadLease(ModelReadLease&& other) noexcept
        :state_(std::move(other.state_)),
         model_(std::exchange(other.model_,nullptr)) {}
    ModelReadLease& operator=(ModelReadLease&& other) noexcept {
        if(this!=&other) {
            reset();
            state_=std::move(other.state_);
            model_=std::exchange(other.model_,nullptr);
        }
        return *this;
    }
    ~ModelReadLease(){reset();}

    const Model* get() const noexcept {
        return state_ && model_ && state_->alive.load(std::memory_order_acquire) &&
               state_->ownerThread==std::this_thread::get_id()
               ?model_:nullptr;
    }
    bool valid() const noexcept{return get()!=nullptr;}
    explicit operator bool() const noexcept{return valid();}
    void reset() noexcept {
        if(state_) {
            // Releasing on a worker thread would permit cache mutation
            // before its borrowed Model read completed. This is a strict
            // owner-thread token, not an atomic cross-thread reader lock.
            if(state_->ownerThread!=std::this_thread::get_id()) {
                std::fputs("ModelReadLease released on foreign thread\n",stderr);
                std::terminate();
            }
            state_->readers.fetch_sub(1,std::memory_order_acq_rel);
            state_.reset();
        }
        model_=nullptr;
    }
private:
    friend class ModelResourceCache;
    ModelReadLease(std::shared_ptr<ModelReadLeaseState> state,
                   const Model* model) noexcept
        :state_(std::move(state)),model_(model) {
        state_->enter();
    }
    std::shared_ptr<ModelReadLeaseState> state_;
    const Model* model_=nullptr; // borrowed; cache mutations blocked while live
};


// Optional coarse read guard for a whole native View::draw transaction.
// Useful for legacy adapters that reference many cache-owned Models by
// raw pointer during a frame. Acquired once after loading assets and
// released after all synchronous draws are submitted.
class ModelCacheReadScope final {
public:
    ModelCacheReadScope()=default;
    ModelCacheReadScope(const ModelCacheReadScope&)=delete;
    ModelCacheReadScope& operator=(const ModelCacheReadScope&)=delete;
    ModelCacheReadScope(ModelCacheReadScope&& other) noexcept
        :state_(std::move(other.state_)) {}
    ModelCacheReadScope& operator=(ModelCacheReadScope&& other) noexcept {
        if(this!=&other){reset();state_=std::move(other.state_);}
        return *this;
    }
    ~ModelCacheReadScope(){reset();}
    bool valid() const noexcept {
        return state_ && state_->alive.load(std::memory_order_acquire) &&
               state_->ownerThread==std::this_thread::get_id();
    }
    explicit operator bool() const noexcept{return valid();}
    void reset() noexcept {
        if(state_) {
            if(state_->ownerThread!=std::this_thread::get_id()) {
                std::fputs("ModelCacheReadScope released on foreign thread\n",stderr);
                std::terminate();
            }
            state_->readers.fetch_sub(1,std::memory_order_acq_rel);
            state_.reset();
        }
    }
private:
    friend class ModelResourceCache;
    explicit ModelCacheReadScope(std::shared_ptr<ModelReadLeaseState> state) noexcept
        :state_(std::move(state)) {
        state_->enter();
    }
    std::shared_ptr<ModelReadLeaseState> state_;
};

} // namespace aiverse::render
