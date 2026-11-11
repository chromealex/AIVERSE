#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
namespace aiverse::lib::systems {
struct ActionStage {std::int64_t action=0;std::uint64_t minimumTicks=0,timeoutTicks=0;};
enum class StageResult {Pending,Complete,Failed};
enum class SequencePhase {Ready,Running,Complete,Cancelled,Failed};
struct SequenceState {std::size_t stage=0;std::uint64_t started=0,lastTick=0;bool hasTick=false;SequencePhase phase=SequencePhase::Ready;};
// At most one stage transition per call. All callbacks run synchronously; persist State after the tick.
template<class Begin,class Poll,class Cleanup>
bool advanceSequence(std::span<const ActionStage> plan,SequenceState& state,std::uint64_t tick,Begin begin,Poll poll,Cleanup cleanup){
    for(const auto& stage:plan)if(stage.action<=0||(stage.timeoutTicks&&stage.timeoutTicks<stage.minimumTicks))return false;
    if(state.phase!=SequencePhase::Ready&&state.phase!=SequencePhase::Running)return false;
    if(state.stage>plan.size()||(state.hasTick&&tick<=state.lastTick))return false;
    if(state.phase==SequencePhase::Running&&(state.stage==plan.size()||tick<state.started))return false;
    state.lastTick=tick;state.hasTick=true;
    if(state.stage==plan.size()){state.phase=SequencePhase::Complete;return true;}
    const auto& stage=plan[state.stage];
    if(state.phase==SequencePhase::Ready){
        if(!begin(stage.action)){state.phase=SequencePhase::Failed;return true;}
        state.started=tick;state.phase=SequencePhase::Running;return true;
    }
    auto elapsed=tick-state.started;
    auto result=poll(stage.action);
    if(result==StageResult::Failed||(stage.timeoutTicks&&elapsed>=stage.timeoutTicks&&result!=StageResult::Complete)){
        cleanup(stage.action,false);state.phase=SequencePhase::Failed;return true;
    }
    if(result==StageResult::Complete&&elapsed>=stage.minimumTicks){
        cleanup(stage.action,true);++state.stage;state.phase=state.stage==plan.size()?SequencePhase::Complete:SequencePhase::Ready;
    }
    return true;
}
template<class Cleanup>
bool cancelSequence(std::span<const ActionStage> plan,SequenceState& state,Cleanup cleanup){
    if(state.phase!=SequencePhase::Ready&&state.phase!=SequencePhase::Running)return false;
    if(state.phase==SequencePhase::Running){if(state.stage>=plan.size())return false;cleanup(plan[state.stage].action,false);}
    state.phase=SequencePhase::Cancelled;return true;
}
}
