// Genre-neutral GPU retirement batches. Each batch has its own ordered
// completion token: a newer eviction can never be released by an older fence.
// No polling operation blocks unless the backend lacks async tokens and the
// explicit synchronous fallback is used.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class RetirementDiagnosticKind : uint8_t {
    Enqueued, TokenSubmitted, Pending, Completed, Failed,
    IdleFallbackFailed, RecoveredAfterIdle
};
// Observer-side metadata only; never used to authorize resource destruction.
enum class RetirementResourceKind : uint8_t { Unknown, Model, Image, Target };
struct RetirementDiagnosticEvent {
    RetirementDiagnosticKind kind=RetirementDiagnosticKind::Pending;
    uint64_t token=0;
    size_t resources=0;
    RetirementResourceKind resource=RetirementResourceKind::Unknown;
};

template<class Resource>
class AsyncResourceRetirementQueue final {
public:
    using DiagnosticObserver=
        std::function<void(const RetirementDiagnosticEvent&)>;
    void setDiagnosticObserver(DiagnosticObserver observer) {
        observer_=std::move(observer);
    }
    struct Stats {
        size_t pendingResources=0;
        size_t pendingBatches=0;
        size_t unsubmittedResources=0;
        size_t highWatermark=0;
        uint64_t completedResources=0;
        uint64_t completedBatches=0;
        uint64_t pendingPolls=0;
        uint64_t tokenFailures=0;
        uint64_t idleFallbackFailures=0;
        uint64_t failedFenceBatches=0;
        size_t unresolvedFailedFences=0;
    };
    void enqueue(Resource resource) {
        enqueueFrom(resource);
    }
    // Preserve caller ownership if pending_ allocation throws. The resource
    // is moved only after vector storage is available. Safe for move-only
    // handles used by Model and texture staging transactions.
    void enqueueFrom(Resource& resource) {
        static_assert(std::is_nothrow_move_constructible_v<Resource>);
        pending_.push_back(std::move(resource));
        ++count_;
        emit({RetirementDiagnosticKind::Enqueued,0,count_});
        if(count_>highWatermark_)highWatermark_=count_;
    }
    size_t pending() const noexcept {return count_;}
    size_t pendingBatches() const noexcept {return batches_.size();}
    bool empty() const noexcept {return count_==0;}
    // Advisory backpressure for new allocations, never a reason to
    // destroy a model before its own GPU token completes.
    bool saturated(size_t limit) const noexcept {
        return limit!=0 && count_>=limit;
    }
    Stats stats() const noexcept {
        size_t failed=0;
        for(const auto& batch:batches_)if(batch.failed)++failed;
        return {count_,batches_.size(),pending_.size(),highWatermark_,
                completedResources_,completedBatches_,pendingPolls_,
                tokenFailures_,idleFallbackFailures_,failedFenceBatches_,failed};
    }
    // Read-only diagnostic traversal. The estimate callback receives a
    // const Resource&, may return approximate bytes, and must have no GPU
    // side effects. Both token-assigned and unsubmitted resources count.
    // Owner inspection, without changing GPU retirement or completion
    // semantics. Borrowed resources remain alive until their own token.
    template<class Visit>
    void forEachPending(Visit&& visit) const {
        for(const auto& resource:pending_)visit(resource);
        for(const auto& batch:batches_)
            for(const auto& resource:batch.resources)visit(resource);
    }
    template<class Estimate>
    uint64_t estimatedPendingBytes(Estimate&& estimate) const {
        uint64_t bytes=0;
        const auto accumulate=[&](const Resource& resource) {
            const auto amount=uint64_t(estimate(resource));
            if(amount>std::numeric_limits<uint64_t>::max()-bytes)
                bytes=std::numeric_limits<uint64_t>::max();
            else bytes+=amount;
        };
        for(const auto& resource:pending_)accumulate(resource);
        for(const auto& batch:batches_)
            for(const auto& resource:batch.resources)accumulate(resource);
        return bytes;
    }

    // Invoke only at a real host frame boundary after previous GPU work has
    // been submitted. Callbacks: begin()->token (0=unsupported), completed
    // (token)->bool, close(token), waitIdle()->bool, destroy(Resource&).
    template<class Begin,class Completed,class Close,class Wait,class Destroy>
    bool collect(Begin&& begin,Completed&& completed,Close&& close,
                 Wait&& waitIdle,Destroy&& destroy) {
        return collect(std::forward<Begin>(begin),
                       std::forward<Completed>(completed),
                       std::forward<Close>(close),
                       std::forward<Wait>(waitIdle),
                       std::forward<Destroy>(destroy),
                       [](uint64_t){return false;});
    }
    // Explicit failure probe is evaluated ONLY when a token has not
    // completed. A failed marker is retained until a successful full-GPU
    // idle recovery drain; it can never be freed by a later token.
    template<class Begin,class Completed,class Close,class Wait,
             class Destroy,class Failed>
    bool collect(Begin&& begin,Completed&& completed,Close&& close,
                 Wait&& waitIdle,Destroy&& destroy,Failed&& failed) {
        while(!batches_.empty()) {
            auto& batch=batches_.front();
            if(batch.failed || !completed(batch.token)) {
                if(!batch.failed && failed(batch.token)) {
                    batch.failed=true;
                    ++failedFenceBatches_;
                    emit({RetirementDiagnosticKind::Failed,batch.token,
                          batch.resources.size()});
                } else if(!batch.failed)
                    emit({RetirementDiagnosticKind::Pending,batch.token,
                          batch.resources.size()});
                ++pendingPolls_;
                break;
            }
            emit({RetirementDiagnosticKind::Completed,batch.token,
                  batch.resources.size()});
            close(batch.token);
            for(auto& resource:batch.resources) {
                destroy(resource);
                --count_;
                ++completedResources_;
            }
            ++completedBatches_;
            batches_.pop_front();
        }
        if(pending_.empty())return empty();
        const uint64_t token=begin();
        if(token!=0) {
            Batch batch;
            batch.token=token;
            batch.resources=std::move(pending_);
            pending_.clear();
            batches_.push_back(std::move(batch));
            emit({RetirementDiagnosticKind::TokenSubmitted,token,
                  batches_.back().resources.size()});
            return false; // new batch cannot be assumed complete
        }
        // Unsupported token backend: fall back to one explicit completion
        // barrier; never silently delete potentially GPU-visible resources.
        ++tokenFailures_;
        if(!waitIdle()) {
            ++idleFallbackFailures_;
            emit({RetirementDiagnosticKind::IdleFallbackFailed,0,pending_.size()});
            return false;
        }
        emit({RetirementDiagnosticKind::RecoveredAfterIdle,0,count_});
        drainKnownCompleted(close,destroy);
        return true;
    }

    // Only a successful full-device completion barrier may bypass in-flight
    // per-batch completion checks (e.g., application shutdown).
    template<class Wait,class Close,class Destroy>
    bool drain(Wait&& waitIdle,Close&& close,Destroy&& destroy) {
        if(empty())return true;
        if(!waitIdle()) {
            ++idleFallbackFailures_;
            emit({RetirementDiagnosticKind::IdleFallbackFailed,0,count_});
            return false;
        }
        emit({RetirementDiagnosticKind::RecoveredAfterIdle,0,count_});
        drainKnownCompleted(close,destroy);
        return true;
    }

private:
    struct Batch {
        uint64_t token=0;
        std::vector<Resource> resources;
        bool failed=false;
    };
    template<class Close,class Destroy>
    void drainKnownCompleted(Close&& close,Destroy&& destroy) {
        for(auto& batch:batches_) {
            close(batch.token); // the GPU has been proven idle
            for(auto& resource:batch.resources) {
                destroy(resource);
                --count_;
                ++completedResources_;
            }
            ++completedBatches_;
        }
        batches_.clear();
        for(auto& resource:pending_) {
            destroy(resource);
            --count_;
            ++completedResources_;
        }
        pending_.clear();
    }
    void emit(RetirementDiagnosticEvent event) noexcept {
        if(!observer_)return;
        // Diagnostics must NEVER jeopardize an otherwise safe GPU
        // retirement transaction (an observer may allocate or throw).
        try {observer_(event);}catch(...) {}
    }
    DiagnosticObserver observer_;
    size_t count_=0,highWatermark_=0;
    uint64_t completedResources_=0,completedBatches_=0;
    uint64_t pendingPolls_=0,tokenFailures_=0,idleFallbackFailures_=0;
    uint64_t failedFenceBatches_=0;
    std::vector<Resource> pending_;
    std::deque<Batch> batches_;
};

} // namespace aiverse::render
