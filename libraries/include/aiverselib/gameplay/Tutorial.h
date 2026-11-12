#pragma once
#include "Diagnostics.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>
namespace aiverse::lib::gameplay {
struct TutorialStep { std::int64_t id=0,event=0,target=1,next=-1,highlight=0;bool allowSkip=true;std::vector<std::int64_t> blockedActions; };
struct TutorialState { std::int64_t step=0,progress=0;bool complete=false; };
inline const TutorialStep* findStep(std::span<const TutorialStep> steps,std::int64_t id){for(const auto& s:steps)if(s.id==id)return &s;return nullptr;}
inline Decision validateTutorial(std::span<const TutorialStep> steps) {
    for(std::size_t i=0;i<steps.size();++i){const auto& s=steps[i];if(s.id<0||s.target<1||(s.next!=-1&&!findStep(steps,s.next)))return {Reason::Invalid,s.id};
        for(std::size_t j=0;j<i;++j)if(steps[j].id==s.id)return {Reason::Invalid,s.id};}
    return {};
}
inline Decision tutorialEvent(std::span<const TutorialStep> steps,TutorialState& state,std::int64_t event,bool skip=false) {
    if(state.complete)return {};
    auto s=findStep(steps,state.step);
    if(!s||s->target<1||state.progress<0||(s->next!=-1&&!findStep(steps,s->next)))return {Reason::Invalid,state.step};
    if(skip&&!s->allowSkip)return {Reason::Blocked,s->id};
    if(!skip&&s->event!=event)return {};
    auto next=state;if(skip)next.progress=s->target;else if(next.progress<s->target)++next.progress;
    if(next.progress>=s->target){next.progress=0;if(s->next==-1)next.complete=true;else next.step=s->next;}
    state=next;return {};
}
inline bool tutorialAllows(std::span<const TutorialStep> steps,const TutorialState& state,std::int64_t action,bool hard) {
    if(!hard||state.complete)return true;
    auto s=findStep(steps,state.step);
    if(!s)return false;
    return std::find(s->blockedActions.begin(),s->blockedActions.end(),action)==s->blockedActions.end();
}
}
