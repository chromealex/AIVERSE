// Read-only, bounded report refresh for World Processes. The owning
// simulation still advances every authoritative tick. Never use these
// potentially stale reports for collision, slot allocation or rumor spread.
#pragma once
#include "WorldProcessEntityBudget.h"
#include "WorldProcessesQueries.h"
#include <limits>
#include <utility>

namespace aiverse::lib::worldprocess {
template<class Sample> struct TimedEntityReport {
    Entity entity=kNullEntity;
    uint32_t requestedTick=0,sampleTick=0;
    Sample value{};
};
template<class Sample> struct EntityReportBatch {
    EntityBudgetBatch scheduling;
    world::SimulationLodDiagnostics lod{};
    std::vector<TimedEntityReport<Sample>> samples;
};
namespace report_detail {
// Position returns optional canonical cell (unknown fails hot). Critical
// and position callbacks are pure, synchronous, and read only the World.
// The entire plan is derived; cap bounds report visits, not membership scan.
template<class Component,class Sample,class Position,class Critical,class Read>
inline std::optional<EntityReportBatch<Sample>> prepare(
        const World& ecs,const world::SimulationLodConfig& cfg,
        std::span<const world::Cell> observers,uint32_t tick,
        const EntityBudgetState& state,EntityBudgetOptions options,
        size_t maxItems,Position position,Critical critical,Read read){
    if(ecs.countExisting<Component>()>maxItems)return std::nullopt;
    EntityWorkPlan plan;
    auto& d=plan.diagnostics;d.tick=tick;
    ecs.eachExistingConst<Component>([&](Entity e,const Component& value){
        if(!ecs.alive(e)||!value.enabled||!valid(value))return;
        const auto cell=position(e,value);
        const auto lod=cell?world::simulationLodDecision(cfg,*cell,observers,
            e.packed(),tick,critical(e)):world::SimulationLodDecision{};
        plan.entities.push_back({e,lod});
        switch(lod.tier){
            case world::SimulationLodTier::Hot:++d.hot;break;
            case world::SimulationLodTier::Warm:++d.warm;break;
            case world::SimulationLodTier::Cold:++d.cold;break;
        }
        if(lod.due)++d.executed;else ++d.deferred;
    });
    std::sort(plan.entities.begin(),plan.entities.end(),[](const auto& a,const auto& b){
        return a.entity.packed()<b.entity.packed();
    });
    auto scheduled=prepareEntityBudget(plan,state,options,maxItems);
    if(!scheduled)return std::nullopt;
    EntityReportBatch<Sample> out{std::move(*scheduled),d,{}};
    out.samples.reserve(out.scheduling.selected.size());
    for(const auto& item:out.scheduling.selected){
        if(!ecs.alive(item.entity))return std::nullopt;
        auto sample=read(ecs,item.entity);
        if(!sample)return std::nullopt;
        out.samples.push_back({item.entity,item.firstDueTick,tick,*sample});
    }
    return out;
}
// Crowd ECS coordinates are centimetres; signed floor, no float conversion.
inline int32_t crowdCell(int64_t cm,int32_t size) noexcept{
    return int32_t(cm/size-(cm%size<0?1:0));
}
} // namespace report_detail

// CROWD uses the actual Agent x/z, not an additional position component.
// Observer cells are in the same centimetre grid; project away height.
template<class Critical>
inline std::optional<EntityReportBatch<queries::CrowdSample>> prepareCrowdReports(
        const World& ecs,const world::SimulationLodConfig& cfg,
        std::span<const world::Cell> observers,int32_t cellSizeCm,uint32_t tick,
        const EntityBudgetState& state,EntityBudgetOptions options,Critical critical){
    std::vector<world::Cell> planar(observers.begin(),observers.end());
    for(auto& cell:planar)cell.y=0;
    return report_detail::prepare<crowd::Agent,queries::CrowdSample>(
        ecs,cfg,planar,tick,state,options,crowd::kMaxAgents,
        [cellSizeCm](Entity,const crowd::Agent& agent)->std::optional<world::Cell>{
            if(cellSizeCm<=0)return std::nullopt;
            return world::Cell{report_detail::crowdCell(agent.x,cellSizeCm),0,
                report_detail::crowdCell(agent.z,cellSizeCm)};
        },critical,queries::crowdPose);
}
inline std::optional<EntityReportBatch<queries::CrowdSample>> prepareCrowdReports(
        const World& ecs,const world::SimulationLodConfig& cfg,
        std::span<const world::Cell> observers,int32_t cellSizeCm,uint32_t tick,
        const EntityBudgetState& state,EntityBudgetOptions options){
    return prepareCrowdReports(ecs,cfg,observers,cellSizeCm,tick,state,options,
        [](Entity){return false;});
}
// SOCIETY Person deliberately has no position. Game owner provides a
// canonical Cell resolver (or nullopt); no mirrored social position store.
// Resolver and observer cells must use the same grid and coordinate frame.
template<class Position,class Critical>
inline std::optional<EntityReportBatch<queries::SocietySample>> prepareSocietyReports(
        const World& ecs,const world::SimulationLodConfig& cfg,
        std::span<const world::Cell> observers,uint32_t tick,
        const EntityBudgetState& state,EntityBudgetOptions options,
        Position position,Critical critical){
    return report_detail::prepare<society::Person,queries::SocietySample>(
        ecs,cfg,observers,tick,state,options,society::kMaxPeople,
        [&](Entity e,const society::Person&)->std::optional<world::Cell>{
            return position(e);
        },critical,queries::societyPerson);
}
template<class Position>
inline std::optional<EntityReportBatch<queries::SocietySample>> prepareSocietyReports(
        const World& ecs,const world::SimulationLodConfig& cfg,
        std::span<const world::Cell> observers,uint32_t tick,
        const EntityBudgetState& state,EntityBudgetOptions options,Position position){
    return prepareSocietyReports(ecs,cfg,observers,tick,state,options,
        position,[](Entity){return false;});
}
} // namespace aiverse::lib::worldprocess
