#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for narrative scenes.
// Record kinds 64..71 are reserved for narrative-graphs scenes (64 NarrativeState, 65 BarkMemory used).
// The record body starts with kNarrativeSceneFormat so later layouts can migrate. After load, call
// narrativeStateError(world, config, state) and bind narrativeWorldHash + narrativeConfigHash in the save envelope.
#include "NarrativeScenes.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline constexpr uint32_t kNarrativeSceneFormat=1;
inline void io(Archive& a,PendingConsequence& p){a(p.consequence,p.byScene,p.due,p.seq,p.source);}
inline void io(Archive& a,BacklogEntry& e){a(e.node,e.speaker,e.choice,e.tick,e.scene);}
inline void io(Archive& a,BarkMemory& m){a(m.lastBark,m.lastSpeaker,m.counts);}
inline void io(Archive& a,NarrativeState& s){
    uint32_t format=kNarrativeSceneFormat;a(format);if(format!=kNarrativeSceneFormat)a.fail();
    a(s.scene,s.node,s.chapter,s.scenesStarted,s.facts,s.meters,s.knowledge,s.flags,s.taken,s.visits,s.read,s.pending,s.backlog,s.barks,
      s.nextSeq,s.nextCue,s.lastCommand,s.lastTick,s.lineStart,s.initialized,s.inScene,s.lineWasRead,s.autoMode,s.skipMode);
}
inline uint16_t stateKind(const NarrativeState&){return 64;}
inline uint16_t stateKind(const BarkMemory&){return 65;}
inline bool valid(const BarkMemory& m){
    if(m.lastBark.size()>kSceneMaxBarks||m.counts.size()!=m.lastBark.size()||m.lastSpeaker.size()>kSceneMaxCharacters+1)return false;
    for(const auto& [id,t]:m.lastBark){(void)t;auto c=m.counts.find(id);if(id<=0||c==m.counts.end()||c->second<1)return false;}
    for(const auto& [id,t]:m.lastSpeaker){(void)t;if(id<0)return false;}return true;
}
// Structural limits only; narrativeStateError additionally checks the state against its definitions.
inline bool valid(const NarrativeState& s){
    if(s.facts.size()>kSceneMaxFacts||s.meters.size()>kSceneMaxCharacters*kSocialMeterCount||s.flags.size()>kSceneMaxFlags||s.visits.size()>kSceneMaxNodes
        ||s.read.size()>kSceneMaxNodes||s.pending.size()>kSceneMaxPending||s.backlog.size()>kSceneMaxBacklog||s.knowledge.size()>kSceneMaxFacts*(kSceneMaxCharacters+1)
        ||s.taken.size()>kSceneMaxNodes*kSceneMaxChoices||!valid(s.barks))return false;
    if(s.scene<0||s.node<0||s.chapter<0||s.scenesStarted<0||s.nextSeq<1||s.nextCue<1||s.lineStart>s.lastTick)return false;
    if(s.inScene?(s.node<=0||s.scene<=0||!s.scenesStarted):(s.node!=0||s.lineWasRead||s.skipMode))return false;
    if(!s.initialized&&(s.lastTick||s.lastCommand||!s.backlog.empty()||s.scenesStarted))return false;
    for(const auto& [id,v]:s.facts){(void)v;if(id<=0)return false;}
    for(const auto& [k,v]:s.meters){(void)v;if(k.first<=0||k.second<0||k.second>=int64_t(kSocialMeterCount))return false;}
    for(const auto& [k,t]:s.knowledge)if(k.first<0||k.second<=0||t>s.lastTick)return false;
    for(const auto& [id,t]:s.flags)if(id<=0||t>s.lastTick)return false;
    for(const auto& [k,t]:s.taken)if(k.first<=0||k.second<=0||t>s.lastTick)return false;
    for(const auto* m:{&s.visits,&s.read})for(const auto& [id,v]:*m)if(id<=0||v<1)return false;
    uint64_t seq=0;for(const auto& p:s.pending){if(p.consequence<=0||p.seq<=seq||p.seq>=s.nextSeq||p.due<0)return false;seq=p.seq;}
    uint32_t tick=0;for(const auto& b:s.backlog){if(b.node<=0||b.speaker<0||b.choice<0||b.scene<0||b.tick<tick||b.tick>s.lastTick)return false;tick=b.tick;}
    for(const auto& [id,t]:s.barks.lastBark){(void)id;if(t>s.lastTick)return false;}for(const auto& [id,t]:s.barks.lastSpeaker){(void)id;if(t>s.lastTick)return false;}
    return true;
}

// Definitions and reports as canonical bytes for hashing (not a save format).
inline void io(Archive& a,SceneCondition& c){a(c.kind,c.op,c.subject,c.character,c.value,c.value2,c.meter,c.children);}
inline void io(Archive& a,SceneEffect& e){a(e.kind,e.subject,e.character,e.value,e.meter,e.characters,e.delay,e.byScene);}
inline void io(Archive& a,SceneCue& c){a(c.id,c.delay);}
inline void io(Archive& a,SceneFactDef& f){a(f.id,f.type,f.minimum,f.maximum,f.initial,f.persistent,f.values);}
inline void io(Archive& a,MeterRange& m){a(m.minimum,m.maximum,m.initial);}
inline void io(Archive& a,SceneCharacter& c){a(c.id,c.nameKey);for(auto& m:c.meters)io(a,m);}
inline void io(Archive& a,SceneFlagDef& f){a(f.id,f.persistent);}
inline void io(Archive& a,SceneChapter& c){a(c.id,c.act,c.titleKey);}
inline void io(Archive& a,NarrativeSceneDef& s){a(s.id,s.entry,s.chapter,s.titleKey);}
inline void io(Archive& a,SceneChoice& c){a(c.id,c.next,c.textKey,c.once,c.conditions,c.effects,c.cues);}
inline void io(Archive& a,SceneNode& n){a(n.id,n.speaker,n.next,n.textKey,n.linear,n.autoTicks,n.participants,n.cues,n.onEnter,n.choices);}
inline void io(Archive& a,SceneConsequence& c){a(c.id,c.conditions,c.effects,c.cues);}
inline void io(Archive& a,SceneBark& b){a(b.id,b.trigger,b.speaker,b.priority,b.cooldown,b.textKey,b.once,b.conditions,b.cues);}
inline void io(Archive& a,NarrativeWorld& w){a(w.initialChapter,w.facts,w.characters,w.flags,w.chapters,w.scenes,w.nodes,w.consequences,w.barks);}
inline void io(Archive& a,CueRequest& c){a(c.sequence,c.cueId,c.nodeId,c.choiceId,c.sceneId,c.speaker,c.barkId,c.consequenceId,c.tick,c.delayTicks,c.source);}
inline void io(Archive& a,NarrativeEvent& e){a(e.kind,e.subject,e.character,e.value,e.extra);}
inline void io(Archive& a,BarkLine& b){a(b.request,b.bark,b.speaker,b.textKey);}
inline void io(Archive& a,NarrativeReport& r){a(r.events,r.cues,r.barks);}
template<class T> void hashNarrative(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t narrativeHash(const T& value){Hasher h;hashNarrative(h,value);return h.value();}
inline uint64_t narrativeWorldHash(const NarrativeWorld& w){return narrativeHash(w);}
inline uint64_t narrativeConfigHash(const NarrativeConfig& c){
    Hasher h;for(auto v:{c.autoAdvanceTicks,c.skipLinesPerTick,c.backlogLimit,c.barkCooldownTicks,c.speakerCooldownTicks,c.maxPending,c.maxBarkRequests})h.u32(v);
    for(auto v:{c.meter.minimum,c.meter.maximum,c.meter.initial})h.i64(v);
    return h.value();
}
}
