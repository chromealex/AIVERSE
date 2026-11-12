#pragma once
// Bridge narrative-graphs cue requests (NarrativeCues.h) into cinematic-gameplay scene commands (CinematicScene.h).
// Authoritative and deterministic: the owner calls queueNarrativeCues() with each tick's narrative report cues and
// releaseNarrativeCues() once per tick before stepScene(); due cues become TriggerCue commands ordered by
// (due tick, narrative sequence). Already-seen sequences are ignored, so replaying a report after a restore never
// triggers a scene twice. Cue IDs without a scene trigger are dropped by the scene runner (TriggerIgnored event).
// Persist NarrativeCueQueue with genre::save/load (record kind 76, inside cinematic-gameplay's reserved 72..79).
#include "CinematicScene.h"
#include "NarrativeCues.h"
#include "Persistence.h"
#include <algorithm>
#include <span>
#include <vector>
namespace aiverse::lib::genre {

inline constexpr std::size_t kNarrativeCueQueueMax=256;

struct PendingNarrativeCue {uint32_t due=0;uint64_t sequence=0;int64_t cueId=0,speaker=0;};
struct NarrativeCueQueue {
    std::vector<PendingNarrativeCue> pending; // sorted by (due, sequence)
    uint64_t lastSequence=0;                  // highest narrative sequence accepted
    uint32_t lastTick=0;bool initialized=false;
};

// Adds this tick's narrative cue requests. Returns the number accepted; requests with sequence <= lastSequence,
// non-positive cue IDs and overflow beyond kNarrativeCueQueueMax are skipped (never partially applied per request).
inline std::size_t queueNarrativeCues(NarrativeCueQueue& q,std::span<const CueRequest> cues){
    std::vector<CueRequest> sorted(cues.begin(),cues.end());
    std::sort(sorted.begin(),sorted.end(),[](const CueRequest& a,const CueRequest& b){return a.sequence<b.sequence;});
    std::size_t accepted=0;
    for(const auto& c:sorted){
        if(c.sequence<=q.lastSequence||c.cueId<=0)continue;
        q.lastSequence=c.sequence;
        if(q.pending.size()>=kNarrativeCueQueueMax)continue;
        const uint64_t due=uint64_t(c.tick)+c.delayTicks;
        q.pending.push_back({uint32_t(std::min<uint64_t>(due,UINT32_MAX)),c.sequence,c.cueId,c.speaker});++accepted;
    }
    std::stable_sort(q.pending.begin(),q.pending.end(),[](const auto& a,const auto& b){return a.due!=b.due?a.due<b.due:a.sequence<b.sequence;});
    return accepted;
}

// Emits TriggerCue commands for every cue due at or before `tick`, consuming command sequence numbers from
// `nextCommandSeq` (the owner's monotonic SceneCommand counter). bindings(cue) -> SceneBindings maps the cue's
// speaker/participants to entities. Returns nullopt (state untouched) on a repeated/backwards tick.
template<class BindingsFn>
std::optional<std::vector<SceneCommand>> releaseNarrativeCues(NarrativeCueQueue& q,uint32_t tick,uint64_t& nextCommandSeq,BindingsFn&& bindings){
    if(q.initialized&&tick<=q.lastTick)return std::nullopt;
    std::vector<SceneCommand> out;std::size_t n=0;
    while(n<q.pending.size()&&q.pending[n].due<=tick){
        const auto& p=q.pending[n];
        out.push_back(cueTrigger(nextCommandSeq+out.size(),p.cueId,bindings(p)));++n;
    }
    nextCommandSeq+=out.size();
    q.pending.erase(q.pending.begin(),q.pending.begin()+std::ptrdiff_t(n));
    q.lastTick=tick;q.initialized=true;
    return out;
}

inline void io(Archive& a,PendingNarrativeCue& p){a(p.due,p.sequence,p.cueId,p.speaker);}
inline void io(Archive& a,NarrativeCueQueue& q){a(q.pending,q.lastSequence,q.lastTick,q.initialized);}
inline bool valid(const NarrativeCueQueue& q){
    if(q.pending.size()>kNarrativeCueQueueMax)return false;
    for(std::size_t i=0;i<q.pending.size();++i){
        const auto& p=q.pending[i];
        if(p.cueId<=0||!p.sequence||p.sequence>q.lastSequence)return false;
        if(i&&(p.due<q.pending[i-1].due||(p.due==q.pending[i-1].due&&p.sequence<=q.pending[i-1].sequence)))return false;
    }
    return true;
}
inline uint16_t stateKind(const NarrativeCueQueue&){return 76;}

} // namespace aiverse::lib::genre
