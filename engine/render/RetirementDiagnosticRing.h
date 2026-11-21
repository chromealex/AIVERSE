// Bounded, owner-thread diagnostic ring for GPU resource retirement.
// Captures only metadata; never holds a Model, gfx handle or completion
// token lease and never determines whether GPU destruction is safe.
#pragma once
#include "AsyncResourceRetirementQueue.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace aiverse::render {

class RetirementDiagnosticRing final {
public:
    explicit RetirementDiagnosticRing(size_t capacity=128)
        :capacity_(capacity),ownerThread_(std::this_thread::get_id()){}
    size_t capacity() const noexcept{return capacity_;}
    size_t size() const noexcept{return events_.size();}
    uint64_t overflowCount() const noexcept{return overflows_;}
    uint64_t receivedCount() const noexcept{return received_;}
    uint64_t wrongThreadCount() const noexcept{
        return wrongThread_.load(std::memory_order_relaxed);
    }
    void record(RetirementDiagnosticEvent event) noexcept {
        if(std::this_thread::get_id()!=ownerThread_) {
            wrongThread_.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        ++received_;
        if(capacity_==0) {++overflows_;return;}
        try {
            if(events_.size()==capacity_) {
                events_.pop_front();
                ++overflows_;
            }
            events_.push_back(event);
        }catch(...) {
            ++overflows_; // diagnostic OOM must not affect GPU retirement
        }
    }
    std::vector<RetirementDiagnosticEvent> snapshot() const {
        return {events_.begin(),events_.end()};
    }
    void clear() noexcept {
        events_.clear();
        overflows_=0;
        received_=0;
        wrongThread_.store(0,std::memory_order_relaxed);
    }
    // Callback cannot extend the Ring's lifecycle; safe if a model
    // cache emits events after the owning editor window disappears.
    static std::function<void(const RetirementDiagnosticEvent&)>
    observe(const std::shared_ptr<RetirementDiagnosticRing>& ring) {
        std::weak_ptr<RetirementDiagnosticRing> weak=ring;
        return [weak](const RetirementDiagnosticEvent& event) {
            if(auto live=weak.lock())live->record(event);
        };
    }
private:
    size_t capacity_=0;
    std::thread::id ownerThread_;
    std::deque<RetirementDiagnosticEvent> events_;
    uint64_t received_=0,overflows_=0;
    std::atomic<uint64_t> wrongThread_{0};
};

} // namespace aiverse::render
