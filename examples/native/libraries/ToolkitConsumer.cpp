#include "modules/rts/RtsNative.h"
#include <aiverselib/runtime/Bounded.h>
#include <aiverselib/runtime/HotReload.h>
#include <aiverselib/runtime/WorkBudget.h>
#include <aiverselib/runtime/StateDiff.h>
// Compilation example only: live UI settings are owned by the host, not a NativeModule.
bool libraryToolkitConsumerExample(){
    using namespace aiverse::lib::runtime;
    ScratchBuffer<int,4> scratch;scratch.push(1);
    SlotPool<int,4> pool;auto handle=pool.acquire(2);if(!handle||!pool.get(*handle))return false;
    HotReload<int> settings(1);settings.stage(2,[](int v){return std::optional<int>(v);},[](int v){return v>0;});
    if(!settings.commit(true,0))return false;
    WorkItem jobs[]={{1,2,0}};WorkCursor cursor;
    if(runBudget(jobs,cursor,1,[](auto,auto){return true;}).operations!=1)return false;
    aiverse::game::NativeState state;return !firstStateDifference(state,state);
}
