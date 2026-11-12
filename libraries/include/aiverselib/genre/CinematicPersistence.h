#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for cinematic-gameplay state.
// Record kinds 72..79 are reserved for this package (72..75 used): TimeState 72, SceneState 73, QteProgress 74,
// ParticipantState 75. Store each state in its own NativeState key, feed cinematicHash(state) into the simulation
// checksum and bind sceneLibraryHash(library) (which includes every scene version) in the save envelope.
#include "CinematicRunner.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,TimeRatio& r){a(r.num,r.den);}
inline void io(Archive& a,TimeRequest& r){a(r.tag,r.domain,r.scale,r.priority,r.duration,r.blend);}
inline void io(Archive& a,ActiveTimeRequest& r){a(r.id,r.request,r.start);}
inline void io(Archive& a,DomainClock& c){a(c.accumulator,c.units,c.local);}
inline void io(Archive& a,TimeState& s){a(s.clocks,s.requests,s.nextRequest,s.lastTick,s.initialized);}
inline void io(Archive& a,QteOptions& o){a(o.windowScaleMilli,o.accessibleWindowMilli,o.mashScaleMilli,o.holdScaleMilli,o.autoSucceed);}
inline void io(Archive& a,QteProgress& p){a(p.qte,p.start,p.base,p.open,p.close,p.perfectOpen,p.perfectClose,p.count,p.hold,p.step,p.pressed,p.heldAt,p.windowMilli,p.holding);}
inline void io(Archive& a,ParticipantState& p){a(p.entity,p.mark,p.status,p.deadline,p.locked);}
inline void io(Archive& a,CueRecord& r){a(r.emittedAt,r.finishedAt,r.skipped,r.markers);}
inline void io(Archive& a,QueuedScene& q){a(q.scene,q.bindings);}
inline void io(Archive& a,SceneState& s){
    a(s.scene,s.version,s.phase,s.paused,s.skippedScene,s.position,s.cursor,s.nextStop,s.lastSync,s.triggeredAt,s.stopAt,s.deadline);
    a(s.essentialDone,s.cues,s.participants,s.qte,s.results,s.queue,s.options,s.lastCommand,s.lastTick,s.initialized,s.lastScene,s.lastOutcome);
}
inline uint16_t stateKind(const TimeState&){return 72;}
inline uint16_t stateKind(const SceneState&){return 73;}
inline uint16_t stateKind(const QteProgress&){return 74;}
inline uint16_t stateKind(const ParticipantState&){return 75;}
// Definitions are hashed through the same canonical encoding (never saved as state records).
inline void io(Archive& a,TimeDomainDef& d){a(d.id,d.parent,d.base,d.name);}
inline void io(Archive& a,TimeConfig& c){a(c.domains);}
inline void io(Archive& a,TimePreset& p){a(p.id,p.request);}
inline void io(Archive& a,CueDef& c){a(c.id,c.track,c.at,c.kind,c.ref,c.actor,c.duration,c.essential);}
inline void io(Archive& a,WaitDef& w){a(w.kind,w.cue,w.marker,w.participants);}
inline void io(Archive& a,SyncDef& s){a(s.id,s.at,s.timeout,s.checkpoint,s.waits);}
inline void io(Archive& a,MarkDef& m){a(m.id,m.position,m.tolerance);}
inline void io(Archive& a,ParticipantDef& p){a(p.id,p.mark,p.lockInput,p.timeout,p.fallback);}
inline void io(Archive& a,QteStep& s){a(s.button,s.window);}
inline void io(Archive& a,QteDef& q){a(q.id,q.at,q.kind,q.button,q.open,q.close,q.perfectOpen,q.perfectClose,q.count,q.hold,q.steps,q.strict,q.onSuccess,q.onFail,q.failAborts);}
inline void io(Archive& a,SceneDef& d){a(d.id,d.version,d.trigger,d.domain,d.length,d.skippable,d.checkpointBefore,d.checkpointAfter,d.marks,d.participants,d.cues,d.syncs,d.qtes);}
inline void io(Archive& a,SceneLibrary& l){a(l.time,l.presets,l.scenes,l.triggers);}
inline void io(Archive& a,DomainTick& d){a(d.domain,d.steps,d.units,d.local);}
inline void io(Archive& a,TimeEvent& e){a(e.kind,e.request,e.tag,e.domain);}
inline void io(Archive& a,TimeReport& r){a(r.domains,r.events);}
inline void io(Archive& a,SceneEvent& e){a(e.kind,e.scene,e.id,e.ref,e.actor,e.entity,e.value,e.flags);}
inline void io(Archive& a,SceneReport& r){a(r.events,r.timeAdd,r.timeCancelTags,r.cuesSkipped);}
// Canonical Archive bytes folded into a checksum; works for states, per-tick reports and definitions.
template<class T> void hashCinematic(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t cinematicHash(const T& value){Hasher h;hashCinematic(h,value);return h.value();}
inline uint64_t sceneLibraryHash(const SceneLibrary& lib){return cinematicHash(lib);}
}
