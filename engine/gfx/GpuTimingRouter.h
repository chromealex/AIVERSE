// AIVERSE RHI: one owner-thread GPU timing collector per gfx::Device.
// Multiple unrelated clients submit tagged timing requests and independently
// consume completed samples without draining another client's results.
// Backend implementations remain unchanged. No GPU stalls/readbacks.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

namespace aiverse::gfx {

class GpuTimingRouter {
public:
    using Subscriber=uint32_t;
    struct Sample {
        uint64_t tag=0; // original client-provided tag
        double milliseconds=0.0;
        bool valid=false;
    };
    // The router is owned by the Device and used on that Device's render
    // owner thread only. Subscriber zero is invalid. One device, max 32
    // independent subscribers and at most 64 pending GPU requests.
    Subscriber subscribe() {
        if(mailboxes_.size()>=32u)return 0;
        for(size_t tries=0;tries<33u;++tries) {
            ++nextSubscriber_;
            if(nextSubscriber_==0u)++nextSubscriber_;
            if(mailboxes_.emplace(nextSubscriber_,Mailbox{}).second)
                return nextSubscriber_;
        }
        return 0;
    }
    void unsubscribe(Subscriber subscriber) {
        mailboxes_.erase(subscriber);
        for(auto it=pending_.begin();it!=pending_.end();) {
            if(it->second.subscriber==subscriber)it=pending_.erase(it);
            else ++it;
        }
    }
    // A subscriber may give up on a delayed query without affecting other
    // subscribers' pending GPU results. If it completes later the raw tag
    // is ignored; it can never be misdelivered to another mailbox.
    void cancel(Subscriber subscriber,uint64_t clientTag) {
        for(auto it=pending_.begin();it!=pending_.end();) {
            if(it->second.subscriber==subscriber &&
               it->second.clientTag==clientTag)
                it=pending_.erase(it);
            else ++it;
        }
    }
    // Does not start/end any GPU frame. Existing Metal, GL, Vulkan and
    // other backends may reject concurrent or unsupported timer requests;
    // false means the caller must use its own CPU-only fallback.
    template<class DeviceLike>
    bool request(DeviceLike& device,Subscriber subscriber,uint64_t clientTag) {
        if(!subscriber || !mailboxes_.contains(subscriber) ||
           pending_.size()>=64u)
            return false;
        const uint64_t raw=nextRawTag();
        if(!device.timeGpuFrame(raw))return false;
        pending_.emplace(raw,Pending{subscriber,clientTag});
        return true;
    }
    // THE ONLY destructive backend takeGpuTimings() consumer when sharing a
    // Device. Unknown/legacy raw tags cannot be delivered: all cooperating
    // app/tools must register through this router to avoid stolen samples.
    template<class DeviceLike>
    void poll(DeviceLike& device) {
        for(const auto& result:device.takeGpuTimings()) {
            const auto it=pending_.find(result.tag);
            if(it==pending_.end()) {
                ++unownedResults_;
                continue;
            }
            const Pending pending=it->second;
            pending_.erase(it);
            auto receiver=mailboxes_.find(pending.subscriber);
            if(receiver==mailboxes_.end())continue;
            auto& mailbox=receiver->second;
            if(mailbox.results.size()>=64u) {
                mailbox.results.pop_front();
                ++droppedResults_;
            }
            mailbox.results.push_back({
                pending.clientTag,result.milliseconds,result.valid});
        }
    }
    // Non-destructive to other subscribers. Delivers one subscriber's
    // completed samples in backend completion order, preserving client tags.
    std::vector<Sample> take(Subscriber subscriber) {
        const auto it=mailboxes_.find(subscriber);
        if(it==mailboxes_.end())return {};
        auto& queue=it->second.results;
        std::vector<Sample> results(queue.begin(),queue.end());
        queue.clear();
        return results;
    }
    size_t subscribers() const {return mailboxes_.size();}
    size_t pendingRequests() const {return pending_.size();}
    size_t unownedResults() const {return unownedResults_;}
    size_t droppedResults() const {return droppedResults_;}

private:
    struct Pending {
        Subscriber subscriber=0;
        uint64_t clientTag=0;
    };
    struct Mailbox {
        std::deque<Sample> results;
    };
    static constexpr uint64_t kPrefix=0xD17A000000000000ull;
    static constexpr uint64_t kMask=0x0000ffffffffffffull;
    uint64_t nextRawTag() {
        // Monotonically increasing per-device namespace. When a 48-bit raw
        // sequence wraps, avoid colliding with outstanding GPU requests.
        for(size_t i=0;i<65u;++i) {
            const uint64_t raw=kPrefix|(++sequence_ & kMask);
            if(!pending_.contains(raw))return raw;
        }
        // request() never enters here with <64 pending requests.
        return 0;
    }
    std::map<uint64_t,Pending> pending_;
    std::map<Subscriber,Mailbox> mailboxes_;
    uint64_t sequence_=0;
    Subscriber nextSubscriber_=0;
    size_t unownedResults_=0,droppedResults_=0;
};

} // namespace aiverse::gfx
