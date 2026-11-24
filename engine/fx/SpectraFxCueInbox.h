// SpectraFX: bounded, genre-independent presentation cue inbox.
// Game modules may translate authoritative events into transient cues, but
// this queue never reads ECS state, changes simulation, or touches gfx::Device.
// All calls are on the presentation owner thread. Merge worker/agent streams
// in a caller-defined order BEFORE calling submit(); this is not an MPSC queue.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include "SpectraFxCueBridge.h"
#include "SpectraFxCuePriority.h"
#include <vector>

namespace aiverse::fx {

class SpectraFxCueInbox {
public:
    static constexpr size_t kDefaultCapacity=1024;
    struct Stats {
        uint64_t submitted=0,accepted=0,coalescedMoves=0;
        uint64_t dropped=0,hidden=0,invalid=0,stale=0,evicted=0;
        uint64_t drained=0,dispatchFailed=0,resets=0;
        uint64_t coalescedStops=0,reservedLifecycle=0;
        uint64_t priorityDrained=0;
    };

    explicit SpectraFxCueInbox(size_t capacity=kDefaultCapacity)
        : capacity_(std::clamp<size_t>(capacity,1,65536)) {}

    uint64_t epoch() const {return epoch_;}
    size_t pending() const {return pending_.size();}
    size_t capacity() const {return capacity_;}
    const Stats& stats() const {return stats_;}
    void resetStats() {stats_={};}

    // expectedEpoch should be captured by a producer when a scene/match
    // begins. Stale callbacks from a previous world are then rejected.
    // A zero epoch is never valid.
    bool submit(SpectraFxCue cue,bool visibleToViewer,
                uint64_t expectedEpoch) {
        ++stats_.submitted;
        if(!expectedEpoch || expectedEpoch!=epoch_) {
            ++stats_.stale;return false;
        }
        // Visibility is supplied explicitly by the caller. Neither this
        // queue nor the generic renderer can infer fog-of-war restrictions.
        if(!visibleToViewer) {++stats_.hidden;return false;}
        if(!valid(cue)) {++stats_.invalid;return false;}

        // Multiple moves to the same key can be coalesced until a Play,
        // Stop or Kill for that key; preserve the order of those barriers.
        if(cue.action==SpectraFxCue::Action::Move) {
            for(auto it=pending_.rbegin();it!=pending_.rend();++it) {
                if(it->key!=cue.key) continue;
                if(it->action!=SpectraFxCue::Action::Move) break;
                it->position=cue.position;
                it->direction=cue.direction;
                ++stats_.coalescedMoves;
                ++stats_.accepted;
                return true;
            }
        }
        if(pending_.size()>=capacity_) {
            const bool critical=cue.action==SpectraFxCue::Action::Stop ||
                                cue.action==SpectraFxCue::Action::Kill;
            if(critical) {
                // Avoid losing a clean-up command merely because cosmetic
                // Play/Move requests saturated the frame inbox.
                auto victim=std::find_if(pending_.begin(),pending_.end(),
                    [](const SpectraFxCue& item) {
                        return item.action==SpectraFxCue::Action::Play ||
                               item.action==SpectraFxCue::Action::Move;
                    });
                if(victim!=pending_.end()) {
                    pending_.erase(victim);
                    ++stats_.evicted;
                } else if(coalesceLifecycle(cue)) {
                    // Queue holds only Stop/Kill: the key's latest pending
                    // cue already stops it (Kill supersedes Stop).
                    ++stats_.coalescedStops;
                    ++stats_.accepted;
                    return true;
                } else if(pending_.size()<capacity_*2) {
                    // Lifecycle reserve: a looping effect must never stay
                    // alive because other keys' Stop/Kill filled the inbox.
                    ++stats_.reservedLifecycle;
                } else {
                    ++stats_.dropped;return false;
                }
            } else {
                ++stats_.dropped;return false;
            }
        }
        pending_.push_back(std::move(cue));
        ++stats_.accepted;
        return true;
    }

    // Convenience source adapter: translation happens on the caller's
    // thread. Translator must return false for events not meant to be seen.
    // It is responsible for mapping game-specific IDs and coordinate spaces.
    template<class Events,class Translator>
    void translate(const Events& events,Translator&& translator,
                   uint64_t expectedEpoch) {
        for(const auto& event:events) {
            SpectraFxCue cue;
            bool visible=false;
            if(translator(event,cue,visible))
                submit(std::move(cue),visible,expectedEpoch);
        }
    }

    // Drain a snapshot of the queue at the device-safe frame boundary.
    // Callback-created cues remain pending for the following frame.
    template<class Dispatch>
    void drain(Dispatch&& dispatch,size_t limit=256) {
        const size_t n=std::min(limit,pending_.size());
        for(size_t i=0;i<n;++i) {
            auto cue=std::move(pending_.front());
            pending_.pop_front();
            ++stats_.drained;
            if(!dispatch(cue)) ++stats_.dispatchFailed;
        }
    }

    // Opt-in importance ordering. Keeps per-key FIFO, including Play before
    // Move/Stop/Kill; promotes entire groups containing pending lifecycle
    // commands so clean-up cannot starve behind cosmetic one-shots.
    // Snapshot indices are selected BEFORE callbacks, so callback-enqueued
    // cues are considered only on the following presentation frame.
    // No cue is discarded or transformed. An invalid score ranks as zero.
    template<class Score,class Dispatch>
    void drainPrioritized(Score&& score,Dispatch&& dispatch,size_t limit=256) {
        const auto chosen=rankPresentationCues(
            pending_,limit,std::forward<Score>(score));
        if(chosen.empty())return;
        std::vector<unsigned char> marked(pending_.size(),0);
        std::vector<SpectraFxCue> dispatched;
        dispatched.reserve(chosen.size());
        for(const size_t i:chosen) {
            marked[i]=1;
            dispatched.emplace_back(std::move(pending_[i]));
        }
        std::deque<SpectraFxCue> remaining;
        for(size_t i=0;i<pending_.size();++i)
            if(!marked[i])remaining.push_back(std::move(pending_[i]));
        pending_.swap(remaining);
        for(const auto& cue:dispatched) {
            ++stats_.drained;
            ++stats_.priorityDrained;
            if(!dispatch(cue))++stats_.dispatchFailed;
        }
    }

    // Discard all pending cues on rollback, level switch, origin shift,
    // despawn-generation change or project shutdown.
    void reset() {
        pending_.clear();
        ++epoch_;
        if(epoch_==0) ++epoch_;
        ++stats_.resets;
    }

private:
    bool coalesceLifecycle(const SpectraFxCue& cue) {
        for(auto it=pending_.rbegin();it!=pending_.rend();++it) {
            if(it->key!=cue.key) continue;
            if(it->action!=SpectraFxCue::Action::Stop &&
               it->action!=SpectraFxCue::Action::Kill) return false;
            if(cue.action==SpectraFxCue::Action::Kill) *it=cue;
            return true;
        }
        return false;
    }
    static bool finite(float3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static bool finite(float4 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&
               std::isfinite(p.z)&&std::isfinite(p.w);
    }
    static bool valid(const SpectraFxCue& cue) {
        if(!finite(cue.position)||!finite(cue.direction)||
           !finite(cue.tint)||!std::isfinite(cue.scale)||
           cue.scale<=0||cue.scale>10000) return false;
        if(cue.action!=SpectraFxCue::Action::Play && cue.key==0)
            return false;
        return true;
    }

    size_t capacity_=kDefaultCapacity;
    std::deque<SpectraFxCue> pending_;
    uint64_t epoch_=1;
    Stats stats_{};
};

} // namespace aiverse::fx
