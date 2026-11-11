#pragma once
#include "modules/rts/RtsModule.h"
#include "Loot.h"
#include <optional>
#include <span>
namespace aiverse::lib::systems {
inline std::optional<std::int64_t> rollSimLoot(Simulation& sim,std::span<const LootRow> rows,LootConfig config,LootState& state,const std::set<std::int64_t>& excluded={}){
    std::uint64_t sum=0;for(const auto& row:rows){if(row.weight<0||static_cast<std::uint64_t>(row.weight)>UINT32_MAX-sum)return {};sum+=static_cast<std::uint64_t>(row.weight);}
    return rollLoot(rows,config,state,excluded,[&](std::int64_t bound){return static_cast<std::int64_t>(sim.rng().below(static_cast<std::uint32_t>(bound)))+1;});
}
enum class BuiltinInteraction {Smart,PickItem,UseItem,Load,ToggleLock};
inline std::optional<Command> interactionCommand(BuiltinInteraction action,std::uint8_t player,Entity actor,Entity target=kNullEntity,Vec3 point={},std::int64_t slot=0){
    if(!player||!actor.valid()||slot<0)return {};
    Command c;c.player=player;c.units={actor};c.target=target;c.point=point;
    switch(action){
        case BuiltinInteraction::Smart:c.type=game::kCmdSmart;break;
        case BuiltinInteraction::PickItem:c.type=game::kCmdPickItem;break;
        case BuiltinInteraction::UseItem:c.type=game::kCmdUseItem;c.param=slot;break;
        case BuiltinInteraction::Load:c.type=game::kCmdLoad;break;
        case BuiltinInteraction::ToggleLock:c.type=game::kCmdLock;break;
        default:return {};
    }
    return c;
}
struct StatValue {game::Stat stat;Fixed value;};
// Read effective values from the existing authoritative modifier pipeline, never a second evaluator.
inline std::optional<std::vector<StatValue>> effectiveStats(game::RtsModule& module,Simulation& sim,Entity entity,std::span<const game::Stat> requested){
    if(!sim.world().has<game::Unit>(entity))return {};
    for(auto stat:requested)if(stat>=game::Stat::Count)return {};
    std::vector<StatValue> values;for(auto stat:requested)values.push_back({stat,module.stat(sim,entity,stat)});return values;
}
struct StatusSnapshot {std::uint32_t flags=0;std::vector<game::StatusInst> active;};
inline std::optional<StatusSnapshot> statuses(game::RtsModule& module,Simulation& sim,Entity entity){
    if(!sim.world().has<game::Unit>(entity))return {};
    StatusSnapshot snapshot;snapshot.flags=module.statusFlags(sim,entity);
    if(auto* status=sim.world().tryGet<game::Statuses>(entity))snapshot.active=status->list;
    return snapshot;
}
// Simulation command/system hook only, after game-specific authority/target validation.
inline bool applyStatus(game::RtsModule& module,Simulation& sim,Entity target,int definition,Fixed duration,const game::EffectCtx& context){
    if(!sim.world().has<game::Unit>(target)||definition<0||static_cast<std::size_t>(definition)>=module.rules().statuses.size()||duration<Fixed{})return false;
    module.addStatus(sim,target,definition,duration,context);return true;
}
}
