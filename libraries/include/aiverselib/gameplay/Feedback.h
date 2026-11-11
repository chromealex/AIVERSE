#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <vector>
namespace aiverse::lib::gameplay {
enum class FeedbackKind { Sound,Particle,Animation,Notification,CameraShake };
struct FeedbackAction { FeedbackKind kind=FeedbackKind::Sound;std::int64_t asset=0,magnitude=1000; };
struct FeedbackRule { std::int64_t id=0,event=0,cooldownTicks=0;bool ownOnly=false;std::vector<FeedbackAction> actions; };
struct FeedbackEvent { std::int64_t type=0,subject=0,tick=0;bool own=true; };
struct FeedbackState {std::map<std::int64_t,std::int64_t> lastTick;};
// Presentation state only: clear after rollback/seek. Dispatch only after simulation events are accepted.
template<class Emit>
void dispatchFeedback(std::span<const FeedbackRule> rules,FeedbackState& state,const FeedbackEvent& event,Emit emit) {
    if(event.tick<0)return;
    for(const auto& r:rules)if(r.event==event.type&&(!r.ownOnly||event.own)&&r.cooldownTicks>=0){
        auto it=state.lastTick.find(r.id);
        if(it!=state.lastTick.end()&&(event.tick<it->second||event.tick-it->second<r.cooldownTicks))continue;
        state.lastTick[r.id]=event.tick;for(const auto& a:r.actions)emit(a,event.subject);
    }
}
}
