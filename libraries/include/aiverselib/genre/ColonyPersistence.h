#pragma once
// settlement-production persistence: versioned genre::save/load records and checksum hashing.
// Record kinds 104..111 are reserved for settlement-production + survival-ecology: ColonyState 104,
// PowerGridState 105, SurvivalState 106, EcologyState 107, EcologyAnimalState 108, EcologyResourceState 109
// (SurvivalPersistence.h), bots::ColonyPlannerState 110 (bots/ColonyPlanner.h), 111 free.
// Store each record in its own NativeState key, feed colonyHash/powerHash into the simulation checksum and bind
// colonyDefsHash(defs) in the save envelope.
#include "ColonyRunner.h"
#include "ColonyPower.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
namespace detail_colony_io {
// Reservation books / tasks live in other packages' namespaces: encode them through plain containers here.
inline void book(Archive& a,systems::ReservationBook& b){
    std::map<int64_t,std::pair<int64_t,systems::Quantities>> t;
    if(a.writing())for(const auto& [id,r]:b.tickets)t[id]={r.owner,r.amounts};
    a(t);
    if(!a.writing()){b.tickets.clear();for(auto& [id,r]:t)b.tickets[id]=systems::Reservation{r.first,r.second};}
}
inline void task(Archive& a,Task& t){a(t.id,t.priority,t.worker,t.status);}
}
inline void io(Archive& a,ColonyColonist& c){a(c.id,c.priorities,c.skills);for(auto& b:c.schedule)a(b);a(c.needs,c.mood,c.band,c.activity,c.busyUntil,c.calmUntil,c.job,c.restoring,c.incapacitated);}
inline void io(Archive& a,ColonyStockpile& p){a(p.id,p.capacity,p.stock);detail_colony_io::book(a,p.reserved);}
inline void io(Archive& a,ColonyWorkstation& w){a(w.id,w.type,w.stockpile,w.recipe,w.target,w.job,w.needsPower);}
inline void io(Archive& a,ColonyJob& j){detail_colony_io::task(a,j.task);a(j.kind,j.workType,j.ref,j.site,j.from,j.to,j.node,j.amount,j.work,j.progress,j.alert,j.createdAt,j.blocked);}
inline void io(Archive& a,ColonyAlert& x){a(x.id,x.kind,x.ref,x.severity,x.raisedAt,x.worked);}
inline void io(Archive& a,ColonyState& s){a(s.colonists,s.stockpiles,s.workstations,s.jobs,s.alerts,s.nextJob,s.nextAlert,s.nextStructure,s.lastCommand,s.lastTick,s.initialized);}
inline void io(Archive& a,PowerNode& n){a(n.id,n.medium,n.kind,n.output,n.demand,n.priority,n.capacity,n.rate,n.charge,n.enabled,n.powered,n.reconnectAt);}
inline void io(Archive& a,PowerGridState& s){a(s.nodes,s.links,s.restoreDelayTicks,s.lastTick,s.initialized);}
inline uint16_t stateKind(const ColonyState&){return 104;}
inline uint16_t stateKind(const PowerGridState&){return 105;}
// Definitions are hashed through the same canonical encoding (never saved as state records).
inline void io(Archive& a,ColonyMoodStep& m){a(m.below,m.mood);}
inline void io(Archive& a,ColonyNeedDef& n){a(n.id,n.decayPerTick,n.initial,n.lowAt,n.criticalAt,n.recoveredAt,n.mood);}
inline void io(Archive& a,ColonyRestoreDef& r){a(r.activity,r.need,r.perTick);}
inline void io(Archive& a,ColonyMealDef& m){a(m.resource,m.need,m.amount,m.ticks);}
inline void io(Archive& a,ColonyWorkTypeDef& w){a(w.id,w.defaultPriority);}
inline void io(Archive& a,ColonyRecipeDef& r){a(r.id,r.workstationType,r.workType,r.work,r.inputs,r.outputs);}
inline void io(Archive& a,ColonyBlueprintDef& b){a(b.id,b.workType,b.work,b.workstationType,b.needsPower,b.cost);}
inline void io(Archive& a,ColonyGatherDef& g){a(g.id,g.workType,g.resource,g.yield,g.work);}
inline void io(Archive& a,ColonyHaulDef& h){a(h.id,h.from,h.to,h.resource,h.keep,h.batch,h.work,h.workType);}
inline void io(Archive& a,ColonyAlertDef& x){a(x.id,x.workType,x.priority,x.workPerSeverity,x.mood,x.preempt,x.ignoreSchedule);}
inline void io(Archive& a,ColonyConfig& c){a(c.ticksPerHour,c.moodBase,c.breakAt,c.lowAt,c.highAt,c.breakTicks,c.lowSpeedMilli,c.contentSpeedMilli,c.highSpeedMilli,c.maxResponseJobs);}
inline void io(Archive& a,ColonyDefs& d){a(d.config,d.needs,d.restores,d.meals,d.restNeed,d.workTypes,d.recipes,d.blueprints,d.gathers,d.hauls,d.alerts);}
inline void io(Archive& a,ColonyEvent& e){a(e.kind,e.colonist,e.job,e.id,e.value);}
inline void io(Archive& a,ColonyReport& r){a(r.events,r.working,r.idle,r.sleeping,r.broken);}
inline void io(Archive& a,PowerComponent& c){a(c.id,c.medium,c.supply,c.demand,c.served,c.shed,c.charged,c.discharged,c.stored,c.capacity);}
inline void io(Archive& a,PowerEvent& e){a(e.kind,e.node,e.component,e.value);}
inline void io(Archive& a,PowerReport& r){a(r.components,r.powered,r.unpowered,r.events);}
template<class T> void hashColony(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t colonyHash(const T& value){Hasher h;hashColony(h,value);return h.value();}
inline uint64_t colonyDefsHash(const ColonyDefs& d){return colonyHash(d);}
inline uint64_t powerHash(const PowerGridState& s){return colonyHash(s);}
}
