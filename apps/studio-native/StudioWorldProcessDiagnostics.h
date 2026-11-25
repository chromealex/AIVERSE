// Studio-owned advisory report sampling, never an authoritative scheduler.
#pragma once
#include "aiverselib/world/WorldProcessReportJobs.h"
#include "engine/sim/CommonComponents.h"
#include "engine/data/Json.h"
#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::studio {
class StudioWorldProcessDiagnostics {
    struct States {
        evolve::RegionBudgetState evolve;
        lib::worldprocess::EntityBudgetState crowd,society;
    };
    States states_{};
    std::array<lib::worldprocess::EntityBudgetState,3> spatialStates_{};
    bool spatialMode_=false;
    Json cached_;
    std::string epoch_,focus_;
    int cap_=0;
    std::optional<uint32_t> tick_;
    static constexpr int kCellCm=3200;
    static std::optional<world::Cell> position(const World& ecs,Entity e){
        if(!ecs.alive(e))return {};
        const auto* t=ecs.tryGetConst<Transform>(e);
        if(!t)return {};
        constexpr int64_t step=32*Fixed::kOne;
        auto cell=[](int64_t raw)->std::optional<int32_t>{
            const auto q=raw/step-(raw%step<0?1:0);
            if(q<INT32_MIN||q>INT32_MAX)return {};
            return int32_t(q);
        };
        const auto x=cell(t->pos.x.raw),z=cell(t->pos.z.raw);
        if(!x||!z)return {};
        return world::Cell{*x,0,*z};
    }
    static std::optional<Entity> parseFocus(const std::string& ref){
        if(ref.empty())return {};
        uint64_t id=0;const auto p=std::from_chars(ref.data(),ref.data()+ref.size(),id);
        if(p.ec!=std::errc{}||p.ptr!=ref.data()+ref.size()||!Entity::fromPacked(id).valid())
            throw std::invalid_argument("Invalid diagnostic focus entity reference");
        return Entity::fromPacked(id);
    }
    template<class Schedule,class Values,class Key>
    static Json lane(const Schedule& s,const world::SimulationLodDiagnostics& d,
                     const Values& values,bool parallel,int cap,Key key){
        auto out=world::simulationLodDiagnosticsJson(&d);
        auto put=[&](const char* k,size_t v){out.set(k,Json::number(int64_t(v)));};
        put("budget",size_t(cap));put("selected",values.size());put("pending",s.remaining);
        put("urgent",s.urgentRemaining);put("urgentSelected",s.urgentSelected);
        put("queued",s.queued);put("removed",s.removed);
        uint32_t oldest=0;
        for(const auto& item:s.next.pending)
            oldest=std::max(oldest,d.tick-item.firstDueTick);
        put("oldestPendingTicks",oldest);
        out.set("mode",Json::string(parallel?"parallel":"serial"));
        Json rows=Json::array();
        for(const auto& value:values){
            if(rows.size()>=32)break;
            Json row=Json::object();
            row.set("id",Json::string(std::to_string(value.entity.packed())));
            row.set("requestedTick",Json::number(int64_t(value.requestedTick)));
            row.set("sampleTick",Json::number(int64_t(value.sampleTick)));
            rows.push(std::move(row));
        }
        Json waiting=Json::array();
        for(const auto& item:s.next.pending){
            if(waiting.size()>=32)break;
            Json row=Json::object();
            row.set("id",Json::string(std::to_string(key(item).packed())));
            row.set("requestedTick",Json::number(int64_t(item.firstDueTick)));
            row.set("urgent",Json::fromBool(item.urgent));waiting.push(std::move(row));
        }
        out.set("samples",std::move(rows));out.set("waiting",std::move(waiting));
        return out;
    }
    static Json unavailable(const char* error){
        Json out=Json::object();
        out.set("available",Json::fromBool(false));out.set("error",Json::string(error));
        return out;
    }
public:
    void reset(){states_={};spatialStates_={};spatialMode_=false;cached_=Json();epoch_.clear();focus_.clear();cap_=0;tick_.reset();}
    static std::optional<Entity> focusEntity(const std::string& focus){return parseFocus(focus);}
    // Spatial owner supplies a pure distance decision, never a flattened
    // legacy position. Keep the same advisory queue and visit cap.
    template<class Decide>
    Json sampleSpatial(const World& ecs,std::string epoch,uint32_t tick,int cap,
                       const std::string& focus,bool resolved,Json focusInfo,
                       const Decide& decide){
        if(cap<0||cap>512||focus.size()>20)throw std::invalid_argument("Diagnostic budget/focus out of bounds");
        (void)parseFocus(focus);
        if(!spatialMode_||epoch_!=epoch||focus_!=focus||cap_!=cap||(tick_&&tick<*tick_))reset();
        if(tick_&&tick==*tick_)return cached_;
        namespace wp=lib::worldprocess;
        const world::SimulationLodConfig cfg{true,4,16,4,16};
        auto next=spatialStates_;
        Json lanes=Json::object();
        struct SampleRow{Entity entity;uint32_t requestedTick,sampleTick;};
        auto prepare=[&]<class Component>(const char* name,size_t slot,size_t limit){
            if(ecs.countExisting<Component>()>limit){lanes.set(name,unavailable("Entity limit exceeded"));return;}
            wp::EntityWorkPlan plan;
            auto& d=plan.diagnostics;d.tick=tick;
            ecs.eachExistingConst<Component>([&](Entity e,const Component& value){
                if(!ecs.alive(e)||!value.enabled||!valid(value))return;
                const auto lod=resolved?decide(e,value,cfg):world::SimulationLodDecision{};
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
            auto batch=wp::prepareEntityBudget(plan,spatialStates_[slot],{size_t(cap),{}},limit);
            if(!batch){lanes.set(name,unavailable("Invalid spatial report plan"));return;}
            std::vector<SampleRow> rows;
            rows.reserve(batch->selected.size());
            for(const auto& p:batch->selected)rows.push_back({p.entity,p.firstDueTick,tick});
            lanes.set(name,lane(*batch,d,rows,false,cap,[](const auto& p){return p.entity;}));
            next[slot]=std::move(batch->next);
        };
        prepare.template operator()<evolve::Region>("evolve",0,evolve::kMaxRegions);
        prepare.template operator()<crowd::Agent>("crowd",1,crowd::kMaxAgents);
        prepare.template operator()<society::Person>("society",2,society::kMaxPeople);
        Json out=Json::object();out.set("active",Json::fromBool(true));
        out.set("epoch",Json::string(epoch));out.set("tick",Json::number(int64_t(tick)));
        out.set("observerAvailable",Json::fromBool(resolved));
        out.set("cellSizeCm",Json::number(int64_t(kCellCm)));
        out.set("config",world::simulationLodConfigJson(cfg));out.set("lanes",std::move(lanes));
        out.set("topology",Json::string("planet"));out.set("focusSurface",std::move(focusInfo));
        cached_=out;spatialStates_=std::move(next);spatialMode_=true;
        epoch_=std::move(epoch);focus_=focus;cap_=cap;tick_=tick;
        return out;
    }
    Json sample(const World* ecs,std::string epoch,uint32_t tick,int cap,const std::string& focus){
        if(cap<0||cap>512||focus.size()>20)throw std::invalid_argument("Diagnostic budget/focus out of bounds");
        const auto entity=parseFocus(focus);
        if(!ecs){reset();Json out=Json::object();out.set("active",Json::fromBool(false));return out;}
        if(spatialMode_||epoch_!=epoch||focus_!=focus||cap_!=cap||(tick_&&tick<*tick_))reset();
        if(tick_&&tick==*tick_)return cached_;
        namespace wp=lib::worldprocess;
        const world::SimulationLodConfig cfg{true,4,16,4,16};
        std::vector<world::Cell> observers;
        if(entity)if(auto at=position(*ecs,*entity))observers.push_back(*at);
        // Explicit diagnostic focus from a real ECS entity; no camera, no
        // guessed player set, no influence on game rule/authority decisions.
        const std::span<const world::Cell> cells(observers);
        auto ev=evolve::prepareRegionReports(*ecs,cfg,cells,kCellCm,tick,
            states_.evolve,size_t(cap));
        auto cr=wp::prepareCrowdReports(*ecs,cfg,cells,kCellCm,tick,states_.crowd,{size_t(cap),{}});
        auto so=wp::prepareSocietyReports(*ecs,cfg,cells,tick,states_.society,{size_t(cap),{}},
            [&](Entity e){return position(*ecs,e);});
        // No Studio-owned worker pool; these cheap analyses use serial fallback.
        // All reads and report staging are completed before updating owner state.
        Json lanes=Json::object();States next=states_;
        if(ev){const auto a=wp::analyzeEvolveReports(*ev);
            lanes.set("evolve",lane(ev->scheduling,ev->lod,a.values,a.parallel,cap,
                [](const auto& p){return p.region;}));next.evolve=std::move(ev->scheduling.next);
        }else lanes.set("evolve",unavailable("Region limit or invalid diagnostic plan"));
        if(cr){const auto a=wp::analyzeCrowdReports(*cr);
            lanes.set("crowd",lane(cr->scheduling,cr->lod,a.values,a.parallel,cap,
                [](const auto& p){return p.entity;}));next.crowd=std::move(cr->scheduling.next);
        }else lanes.set("crowd",unavailable("Agent limit or invalid diagnostic plan"));
        if(so){const auto a=wp::analyzeSocietyReports(*so);
            lanes.set("society",lane(so->scheduling,so->lod,a.values,a.parallel,cap,
                [](const auto& p){return p.entity;}));next.society=std::move(so->scheduling.next);
        }else lanes.set("society",unavailable("Person limit or invalid diagnostic plan"));
        Json out=Json::object();out.set("active",Json::fromBool(true));
        out.set("epoch",Json::string(epoch));out.set("tick",Json::number(int64_t(tick)));
        out.set("observerAvailable",Json::fromBool(!observers.empty()));
        out.set("cellSizeCm",Json::number(int64_t(kCellCm)));
        out.set("config",world::simulationLodConfigJson(cfg));
        out.set("lanes",std::move(lanes));
        cached_=out;states_=std::move(next);epoch_=std::move(epoch);focus_=focus;cap_=cap;tick_=tick;
        return out;
    }
};
} // namespace aiverse::studio
