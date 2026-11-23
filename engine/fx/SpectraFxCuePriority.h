// SpectraFX — significance-aware, bounded presentation cue scheduling.
// A genre-independent ordering policy. It never discards gameplay events,
// mutates ECS, or issues RHI commands. Existing FIFO drain remains the default.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aiverse::fx {

// Each nonzero instance key is a FIFO group: a Stop/Kill can NEVER overtake
// that instance's Play/Move, even if another effect has higher significance.
// Key zero denotes independent one-shot Play events.
template<class Cue, class Score>
std::vector<size_t> rankPresentationCues(const std::deque<Cue>& queued,
                                         size_t limit, Score&& significance) {
    std::vector<size_t> result;
    if(limit==0 || queued.empty()) return result;
    struct Group {
        std::vector<size_t> indices;
        size_t next=0;
        bool needsLifecycle=false;
    };
    std::vector<Group> groups;
    groups.reserve(queued.size());
    std::unordered_map<uint64_t,size_t> keyed;
    keyed.reserve(queued.size());
    for(size_t index=0;index<queued.size();++index) {
        const auto& cue=queued[index];
        size_t group=groups.size();
        if(cue.key!=0) {
            const auto [it,inserted]=keyed.emplace(cue.key,group);
            if(!inserted) group=it->second;
            else groups.emplace_back();
        } else groups.emplace_back();
        groups[group].indices.push_back(index);
        if(cue.action==Cue::Action::Kill || cue.action==Cue::Action::Stop)
            groups[group].needsLifecycle=true;
    }
    struct Candidate {double score;size_t index;size_t group;};
    struct Less {
        bool operator()(const Candidate& a,const Candidate& b) const {
            if(a.score!=b.score)return a.score<b.score;
            return a.index>b.index; // stable producer order for ties
        }
    };
    std::priority_queue<Candidate,std::vector<Candidate>,Less> ready;
    const auto candidate=[&](size_t g) {
        const auto& group=groups[g];
        const size_t idx=group.indices[group.next];
        const auto& cue=queued[idx];
        const double raw=static_cast<double>(significance(cue));
        const double score=std::isfinite(raw)?std::clamp(raw,0.0,1000000.0):0.0;
        // Preserve lifecycle under saturation; priority inheritance ensures
        // any Play/Move that a queued Stop/Kill depends on is also drained.
        const bool lifecycle=cue.action==Cue::Action::Stop ||
                             cue.action==Cue::Action::Kill;
        return Candidate{score+(lifecycle?4000000.0:
                    group.needsLifecycle?2000000.0:0.0),idx,g};
    };
    for(size_t g=0;g<groups.size();++g)ready.push(candidate(g));
    result.reserve(std::min(limit,queued.size()));
    while(!ready.empty() && result.size()<limit) {
        const Candidate current=ready.top();
        ready.pop();
        result.push_back(current.index);
        auto& group=groups[current.group];
        if(++group.next<group.indices.size())
            ready.push(candidate(current.group));
    }
    return result;
}

} // namespace aiverse::fx
