// Optional owner-thread CPU scope instrumentation. No runtime or ECS authority.
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>
#include <cstddef>

namespace aiverse::graph_profile {
enum class Phase:uint8_t { Sync, Update, Evaluate };
struct Counter {
    uint64_t calls=0;
    double totalUs=0, maxUs=0;
};
struct Event {
    uint16_t node=0;
    Phase phase=Phase::Update;
    double startUs=0,durationUs=0;
};
class Profile {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr size_t maxNodes=128,maxEvents=512;
    using NodeCounters=std::array<Counter,3>;
    explicit Profile(size_t nodes):nodes_(nodes<=maxNodes?nodes:0){
        events_.reserve(maxEvents);
        beginTrace();
    }
    // Explicit deep copy for a selected prepared presentation owner.
    Profile(const Profile& other):nodes_(other.nodes_),events_(other.events_),origin_(other.origin_),overflow_(other.overflow_){events_.reserve(maxEvents);}
    Profile& operator=(const Profile&)=delete;
    Profile(Profile&&)=default;
    Profile& operator=(Profile&&)=default;
    void beginTrace() noexcept {
        events_.clear();overflow_=0;origin_=Clock::now();
    }
    void reset() noexcept {
        for(auto& row:nodes_)row={};
        beginTrace();
    }
    void record(size_t node,Phase phase,Clock::time_point start,Clock::time_point end) noexcept {
        if(node>=nodes_.size())return;
        const auto duration=std::chrono::duration<double,std::micro>(end-start).count();
        auto& counter=nodes_[node][size_t(phase)];
        if(counter.calls!=std::numeric_limits<uint64_t>::max())++counter.calls;
        counter.totalUs+=duration;
        if(duration>counter.maxUs)counter.maxUs=duration;
        // Reserved storage, no allocation from the scope destructor.
        if(events_.size()<maxEvents&&events_.size()<events_.capacity()){
            events_.push_back({uint16_t(node),phase,
                std::chrono::duration<double,std::micro>(start-origin_).count(),duration});
        }else if(overflow_!=std::numeric_limits<uint64_t>::max())++overflow_;
    }
    const std::vector<NodeCounters>& nodes() const noexcept{return nodes_;}
    const std::vector<Event>& events() const noexcept{return events_;}
    uint64_t droppedEvents() const noexcept{return overflow_;}
private:
    std::vector<NodeCounters> nodes_;
    std::vector<Event> events_;
    Clock::time_point origin_{};
    uint64_t overflow_=0;
};
// Disabled scopes perform no clock reads and no allocations.
class Scope {
    Profile* profile_;
    size_t node_;
    Phase phase_;
    Profile::Clock::time_point start_{};
public:
    Scope(Profile* profile,size_t node,Phase phase) noexcept:profile_(profile),node_(node),phase_(phase){
        if(profile_)start_=Profile::Clock::now();
    }
    ~Scope() noexcept {if(profile_)profile_->record(node_,phase_,start_,Profile::Clock::now());}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
} // namespace aiverse::graph_profile
