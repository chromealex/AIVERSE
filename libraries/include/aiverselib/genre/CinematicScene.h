#pragma once
// Cinematic scene flow (authoritative): scene timelines of timed cues on tracks, sync points that wait for
// host-reported facts (dialogue line finished, animation marker reached, participants in position) with timeouts,
// QTE stops with success/fail branches, interaction staging (marks, move-to-mark with timeout and teleport
// fallback, per-actor input lock) and checkpoint requests around scenes. Authoritative: timeline position, cue
// emission, wait/QTE outcomes, staging status, input locks and time-scale requests. Host/presentation: audio and
// animation playback progress, camera, navigation; they are inputs reported with authoritative ticks only and can
// never stall the simulation (every wait has a timeout). Positions are domain steps of the scene's time domain.
#include "CinematicTime.h"
#include "CinematicQte.h"
#include "engine/core/Fixed.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr uint32_t kSceneMaxLength=10000000,kSceneMaxTimeout=1000000;
inline constexpr std::size_t kSceneMaxCues=4096,kSceneMaxStops=256,kSceneMaxActors=64,kSceneMaxWaits=32,kSceneMaxScenes=4096,
    kSceneMaxQueue=8,kSceneMaxInput=4096,kSceneMaxPresets=256;
inline constexpr Fixed kSceneCoordLimit=Fixed::fromInt(100000);

// ---- Immutable definitions (see CinematicData.h for JSON). IDs are stable positive integers.
// Dialogue/Animation/Camera/Sound: presentation cues (ref = line / clip / shot / sound id); Event: gameplay event
// id; TimeScale: ref = time preset id (request tagged with the scene id); Move: actor walks to mark `ref`;
// InputLock/InputRelease: actor's input. Move/InputLock/InputRelease are always essential.
enum class CueKind:uint8_t {Dialogue,Animation,Camera,Sound,Event,TimeScale,Move,InputLock,InputRelease};
struct CueDef {int64_t id=0,track=1;uint32_t at=0;CueKind kind=CueKind::Event;int64_t ref=0,actor=0;uint32_t duration=0;bool essential=false;};
inline bool cueEssential(const CueDef& c){return c.essential||c.kind==CueKind::Move||c.kind==CueKind::InputLock||c.kind==CueKind::InputRelease;}
inline bool cueReportable(CueKind k){return k==CueKind::Dialogue||k==CueKind::Animation;}
enum class WaitKind:uint8_t {LineFinished,Marker,InPosition};
// LineFinished: dialogue/animation cue reported finished. Marker: animation cue reported marker (or finished).
// InPosition: listed participants (empty = all) reached their mark or were teleported.
struct WaitDef {WaitKind kind=WaitKind::LineFinished;int64_t cue=0,marker=0;std::vector<int64_t> participants;};
struct SyncDef {int64_t id=0;uint32_t at=0,timeout=300;bool checkpoint=false;std::vector<WaitDef> waits;};
struct MarkDef {int64_t id=0;Vec3 position{};Fixed tolerance=Fixed::fromRatio(1,2);};
enum class StagingFallback:uint8_t {Teleport,Proceed,Abort};
struct ParticipantDef {int64_t id=0,mark=0;bool lockInput=false;uint32_t timeout=180;StagingFallback fallback=StagingFallback::Teleport;};
// domain 0 = the timeline advances one step per authoritative tick. Cues sorted by (at, track, id); syncs and
// qtes by (at, id). trigger: narrative cue id (CueRequest::cueId) that starts this scene, 0 = none.
struct SceneDef {
    int64_t id=0,version=1,trigger=0,domain=kDomainCinematic;uint32_t length=0;bool skippable=true,checkpointBefore=false,checkpointAfter=false;
    std::map<int64_t,MarkDef> marks;std::map<int64_t,ParticipantDef> participants;std::vector<CueDef> cues;std::vector<SyncDef> syncs;std::vector<QteDef> qtes;
};
struct TimePreset {int64_t id=0;TimeRequest request;}; // request.tag is replaced by the scene id
struct SceneLibrary {TimeConfig time=defaultTimeConfig();std::map<int64_t,TimePreset> presets;std::map<int64_t,SceneDef> scenes;std::map<int64_t,int64_t> triggers;};
inline void indexTriggers(SceneLibrary& lib){lib.triggers.clear();for(const auto& [id,s]:lib.scenes)if(s.trigger)lib.triggers[s.trigger]=id;}

struct SceneStop {uint32_t at=0;bool qte=false;std::size_t index=0;int64_t id=0;};
// Sync points and QTEs in timeline order: (at, sync before qte, id).
inline std::vector<SceneStop> sceneStops(const SceneDef& d){
    std::vector<SceneStop> out;for(std::size_t i=0;i<d.syncs.size();++i)out.push_back({d.syncs[i].at,false,i,d.syncs[i].id});
    for(std::size_t i=0;i<d.qtes.size();++i)out.push_back({d.qtes[i].at,true,i,d.qtes[i].id});
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return std::tuple(a.at,a.qte,a.id)<std::tuple(b.at,b.qte,b.id);});return out;
}
inline const CueDef* findCue(const SceneDef& d,int64_t id){for(const auto& c:d.cues)if(c.id==id)return &c;return nullptr;}
inline const SyncDef* findSync(const SceneDef& d,int64_t id){for(const auto& s:d.syncs)if(s.id==id)return &s;return nullptr;}
inline bool sceneBounded(Vec3 p){auto m=kSceneCoordLimit;return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m&&p.z>= -m&&p.z<=m;}

// Empty when valid; otherwise the first problem as "cinematic.<path>: reason".
inline std::string sceneDefError(const SceneLibrary& lib,const SceneDef& d){
    auto path="cinematic.scene "+std::to_string(d.id);
    if(d.id<=0||d.version<1||d.trigger<0)return path+": id must be positive, version >= 1, trigger >= 0";
    if(d.domain<0||(d.domain&&!lib.time.domains.contains(d.domain)))return path+".domain: unknown time domain "+std::to_string(d.domain);
    if(d.length>kSceneMaxLength)return path+".length: must be 0..10000000";
    if(d.cues.size()>kSceneMaxCues||d.syncs.size()+d.qtes.size()>kSceneMaxStops||d.marks.size()>kSceneMaxActors||d.participants.size()>kSceneMaxActors)
        return path+": at most 4096 cues, 256 syncs+qtes, 64 marks and 64 participants";
    for(const auto& [id,m]:d.marks)if(id<=0||m.id!=id||!sceneBounded(m.position)||m.tolerance<=Fixed{}||m.tolerance>Fixed::fromInt(1000))
        return path+".mark "+std::to_string(id)+": position within +-100000, tolerance in (0,1000]";
    for(const auto& [id,p]:d.participants){
        auto pp=path+".participant "+std::to_string(id);
        if(id<=0||p.id!=id)return pp+": id must be positive";
        if(p.mark&&!d.marks.contains(p.mark))return pp+".mark: unknown mark "+std::to_string(p.mark);
        if(p.timeout<1||p.timeout>kSceneMaxTimeout||uint8_t(p.fallback)>uint8_t(StagingFallback::Abort))return pp+".timeout: must be 1..1000000 ticks";
    }
    std::vector<int64_t> ids;
    for(std::size_t i=0;i<d.cues.size();++i){
        const auto& c=d.cues[i];auto cp=path+".cue "+std::to_string(c.id);
        if(c.id<=0||c.track<=0)return cp+": id and track must be positive";
        if(i&&std::tuple(d.cues[i-1].at,d.cues[i-1].track,d.cues[i-1].id)>=std::tuple(c.at,c.track,c.id))return cp+": cues must be sorted by (at, track, id) without duplicates";
        if(c.at>d.length||c.duration>kSceneMaxLength)return cp+".at: must be 0..length "+std::to_string(d.length);
        if(uint8_t(c.kind)>uint8_t(CueKind::InputRelease))return cp+".kind: unknown";
        bool actorNeeded=c.kind==CueKind::Move||c.kind==CueKind::InputLock||c.kind==CueKind::InputRelease;
        if((actorNeeded||c.actor)&&!d.participants.contains(c.actor))return cp+".actor: unknown participant "+std::to_string(c.actor);
        if(c.kind==CueKind::TimeScale&&!lib.presets.contains(c.ref))return cp+".ref: unknown time preset "+std::to_string(c.ref);
        if(c.kind==CueKind::Move&&!d.marks.contains(c.ref))return cp+".ref: unknown mark "+std::to_string(c.ref);
        if(c.kind<=CueKind::Event&&c.ref<=0)return cp+".ref: must be positive";
        ids.push_back(c.id);
    }
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return path+".cues: duplicate cue id";
    ids.clear();
    for(std::size_t i=0;i<d.syncs.size();++i){
        const auto& s=d.syncs[i];auto sp=path+".sync "+std::to_string(s.id);
        if(s.id<=0)return sp+": id must be positive";
        if(i&&std::pair(d.syncs[i-1].at,d.syncs[i-1].id)>=std::pair(s.at,s.id))return sp+": syncs must be sorted by (at, id) without duplicates";
        if(s.at>d.length)return sp+".at: must be 0..length "+std::to_string(d.length);
        if(s.timeout<1||s.timeout>kSceneMaxTimeout)return sp+".timeout: must be 1..1000000 ticks (a wait must never stall the simulation)";
        if(s.waits.size()>kSceneMaxWaits)return sp+".wait: at most 32 conditions";
        for(std::size_t w=0;w<s.waits.size();++w){
            const auto& c=s.waits[w];auto wp=sp+".wait["+std::to_string(w)+"]";
            if(uint8_t(c.kind)>uint8_t(WaitKind::InPosition))return wp+".kind: unknown";
            if(c.kind==WaitKind::InPosition){
                if(d.participants.empty())return wp+": scene has no participants";
                for(auto p:c.participants)if(!d.participants.contains(p))return wp+".participants: unknown participant "+std::to_string(p);
                continue;
            }
            auto cue=findCue(d,c.cue);
            if(!cue||!cueReportable(cue->kind)||(c.kind==WaitKind::Marker&&cue->kind!=CueKind::Animation))return wp+".cue: unknown "+(c.kind==WaitKind::Marker?"animation":"dialogue/animation")+" cue "+std::to_string(c.cue);
            if(cue->at>=s.at)return wp+".cue: cue "+std::to_string(c.cue)+" must start before the sync point";
            if(c.kind==WaitKind::Marker&&c.marker<=0)return wp+".marker: must be positive";
        }
        ids.push_back(s.id);
    }
    for(std::size_t i=0;i<d.qtes.size();++i){
        const auto& q=d.qtes[i];auto qp=path+".qte "+std::to_string(q.id);
        if(auto e=qteError(q);!e.empty())return qp+"."+e;
        if(i&&std::pair(d.qtes[i-1].at,d.qtes[i-1].id)>=std::pair(q.at,q.id))return qp+": qtes must be sorted by (at, id) without duplicates";
        if(q.at>d.length)return qp+".at: must be 0..length "+std::to_string(d.length);
        for(auto [name,target]:{std::pair<const char*,int64_t>{"onSuccess",q.onSuccess},{"onFail",q.onFail}})
            if(target&&!findSync(d,target))return qp+"."+name+": unknown sync point "+std::to_string(target);
        if(q.onSuccess&&findSync(d,q.onSuccess)->at<=q.at)return qp+".onSuccess: must branch forward (sync after the qte); only onFail may retry backwards";
    }
    return {};
}
inline std::string sceneLibraryError(const SceneLibrary& lib){
    if(auto e=timeConfigError(lib.time);!e.empty())return "cinematic.time."+e;
    if(lib.presets.size()>kSceneMaxPresets||lib.scenes.size()>kSceneMaxScenes)return "cinematic: at most 256 time presets and 4096 scenes";
    for(const auto& [id,p]:lib.presets)if(id<=0||p.id!=id||p.request.tag||!validRequest(lib.time,p.request))
        return "cinematic.timePreset "+std::to_string(id)+": known domain, ratio 0..16 (denominator dividing 720720), duration <= 10000000";
    std::map<int64_t,int64_t> triggers;
    for(const auto& [id,s]:lib.scenes){
        if(s.id!=id)return "cinematic.scene "+std::to_string(id)+": id must match its key";
        if(auto e=sceneDefError(lib,s);!e.empty())return e;
        if(s.trigger&&!triggers.emplace(s.trigger,id).second)return "cinematic.scene "+std::to_string(id)+".trigger: cue "+std::to_string(s.trigger)+" already starts another scene";
    }
    if(triggers!=lib.triggers)return "cinematic.triggers: out of date (call indexTriggers)";
    return {};
}
inline bool valid(const SceneLibrary& lib){return sceneLibraryError(lib).empty();}
// Bridge for narrative CueRequest{cueId,...}: the scene a cue id starts, 0 when none.
inline int64_t sceneForCue(const SceneLibrary& lib,int64_t cueId){auto it=lib.triggers.find(cueId);return it==lib.triggers.end()?0:it->second;}

// ---- Runner state (authoritative; persist with CinematicPersistence.h).
enum class ScenePhase:uint8_t {Idle,Pending,Staging,Playing,Waiting,Qte};
enum class SceneOutcome:uint8_t {None,Completed,Skipped,Aborted};
enum class StageStatus:uint8_t {Idle,Moving,InPosition,Teleported,Late};
struct ParticipantState {int64_t entity=0,mark=0;StageStatus status=StageStatus::Idle;uint32_t deadline=0;bool locked=false;};
struct CueRecord {uint32_t emittedAt=0;int64_t finishedAt=-1;bool skipped=false;std::vector<int64_t> markers;};
using SceneBindings=std::vector<std::pair<int64_t,int64_t>>; // (participant, entity)
struct QueuedScene {int64_t scene=0;SceneBindings bindings;};
struct SceneState {
    int64_t scene=0,version=0;ScenePhase phase=ScenePhase::Idle;bool paused=false,skippedScene=false;
    uint32_t position=0,cursor=0,nextStop=0,lastSync=0,triggeredAt=0,stopAt=0,deadline=0; // lastSync: stop index + 1 of the last sync reached
    std::vector<int64_t> essentialDone;std::map<int64_t,CueRecord> cues;std::map<int64_t,ParticipantState> participants;
    QteProgress qte;std::map<int64_t,QteOutcome> results;std::vector<QueuedScene> queue;QteOptions options;
    uint64_t lastCommand=0;uint32_t lastTick=0;bool initialized=false;int64_t lastScene=0;SceneOutcome lastOutcome=SceneOutcome::None;
};
// TriggerScene/TriggerCue: value = scene id / narrative cue id (queued while another scene runs). Skip: to the next
// sync point (or the end). SkipScene: to the end. Seek: value = position (forward requires skippable).
// ResumeCheckpoint: after loading a save, rewind to the last sync point reached and replay presentation cues.
enum class SceneCommandKind:uint8_t {TriggerScene,TriggerCue,Pause,Unpause,Skip,SkipScene,Seek,Abort,ResumeCheckpoint};
struct SceneCommand {uint64_t seq=0;SceneCommandKind kind=SceneCommandKind::TriggerScene;int64_t value=0;SceneBindings bindings;};
struct LineReport {int64_t cue=0;uint32_t tick=0;};              // dialogue line / animation clip finished at tick
struct MarkerReport {int64_t cue=0,marker=0;uint32_t tick=0;};   // animation marker reached at tick
struct ParticipantPose {int64_t participant=0;Vec3 position{};};
struct SceneInput {
    std::vector<SceneCommand> commands;std::vector<LineReport> finished;std::vector<MarkerReport> markers;
    std::vector<ParticipantPose> poses;std::vector<int64_t> arrived;std::vector<QteInput> qte;
    std::map<int64_t,uint64_t> domainSteps;std::optional<QteOptions> options;
};
enum class SceneEventKind:uint8_t {
    Triggered,Queued,TriggerIgnored,CommandIgnored,ReportIgnored,SnapshotRequested,Started,MoveToMark,InPosition,TeleportRequested,
    StagingLate,InputLocked,InputReleased,Cue,SyncReached,SyncReleased,QtePrompt,QteResolved,Branch,Paused,Unpaused,Skipped,Seeked,
    Resumed,Finished,Aborted
};
// SnapshotRequested value: 0 before the scene (state is Pending), 1 at a checkpoint sync point, 2 after the scene.
// Cue: id cue, ref cue ref, actor participant, value CueKind, flags 1 = emitted while skipping (essential cue).
// SyncReleased value 1 = timed out. QteResolved value = QteOutcome. Finished value = SceneOutcome.
struct SceneEvent {SceneEventKind kind=SceneEventKind::Cue;int64_t scene=0,id=0,ref=0,actor=0,entity=0,value=0;uint8_t flags=0;};
// timeAdd/timeCancelTags go into the next TimeInput (one tick latency, explicit and deterministic).
struct SceneReport {std::vector<SceneEvent> events;std::vector<TimeRequest> timeAdd;std::vector<int64_t> timeCancelTags;uint32_t cuesSkipped=0;};
inline std::map<int64_t,uint64_t> sceneDomainSteps(const TimeReport& r){std::map<int64_t,uint64_t> out;for(const auto& d:r.domains)out[d.domain]=d.steps;return out;}
inline SceneCommand cueTrigger(uint64_t seq,int64_t cueId,SceneBindings bindings={}){return {seq,SceneCommandKind::TriggerCue,cueId,std::move(bindings)};}
inline bool sceneActive(const SceneState& s){return s.phase!=ScenePhase::Idle;}
inline bool inputLocked(const SceneState& s,int64_t participant){auto it=s.participants.find(participant);return it!=s.participants.end()&&it->second.locked;}
inline std::vector<int64_t> lockedEntities(const SceneState& s){std::vector<int64_t> out;for(const auto& [id,p]:s.participants)if(p.locked)out.push_back(p.entity?p.entity:id);std::sort(out.begin(),out.end());return out;}
inline QteOutcome qteResult(const SceneState& s,int64_t qte){auto it=s.results.find(qte);return it==s.results.end()?QteOutcome::None:it->second;}

inline bool sortedIds(const std::vector<int64_t>& v){return std::is_sorted(v.begin(),v.end())&&std::adjacent_find(v.begin(),v.end())==v.end()&&(v.empty()||v.front()>0);}
inline bool validBindings(const SceneBindings& b){
    if(b.size()>kSceneMaxActors)return false;
    std::vector<int64_t> ids;for(auto [p,e]:b){if(p<=0||e<=0)return false;ids.push_back(p);}
    std::sort(ids.begin(),ids.end());return std::adjacent_find(ids.begin(),ids.end())==ids.end();
}
inline bool valid(const ParticipantState& p){return p.entity>=0&&p.mark>=0&&uint8_t(p.status)<=uint8_t(StageStatus::Late);}
inline bool valid(const CueRecord& r){return r.finishedAt>=-1&&r.markers.size()<=kSceneMaxInput&&sortedIds(r.markers);}
inline bool valid(const SceneState& s){
    if(uint8_t(s.phase)>uint8_t(ScenePhase::Qte)||uint8_t(s.lastOutcome)>uint8_t(SceneOutcome::Aborted)||s.lastScene<0||!valid(s.options)||!valid(s.qte))return false;
    if(s.phase==ScenePhase::Idle?(s.scene||s.paused||!s.participants.empty()||!s.cues.empty()||!s.essentialDone.empty()):(s.scene<=0||s.version<1))return false;
    if(!sortedIds(s.essentialDone)||s.essentialDone.size()>kSceneMaxCues||s.cues.size()>kSceneMaxCues||s.participants.size()>kSceneMaxActors||s.queue.size()>kSceneMaxQueue||s.results.size()>kSceneMaxStops)return false;
    for(const auto& [id,r]:s.cues)if(id<=0||!valid(r))return false;
    for(const auto& [id,p]:s.participants)if(id<=0||!valid(p))return false;
    for(const auto& [id,o]:s.results)if(id<=0||o==QteOutcome::None||uint8_t(o)>uint8_t(QteOutcome::Skipped))return false;
    for(const auto& q:s.queue)if(q.scene<=0||!validBindings(q.bindings))return false;
    return (s.phase==ScenePhase::Qte)==(s.qte.qte!=0);
}
inline bool compatible(const SceneLibrary& lib,const SceneState& s){
    if(!valid(lib)||!valid(s))return false;
    for(const auto& q:s.queue)if(!lib.scenes.contains(q.scene))return false;
    if(!s.scene)return true;
    auto it=lib.scenes.find(s.scene);if(it==lib.scenes.end())return false;const auto& d=it->second;auto stops=sceneStops(d);
    if(s.version!=d.version||s.position>d.length||s.cursor>d.cues.size()||s.nextStop>stops.size()||s.lastSync>stops.size()||s.participants.size()!=d.participants.size())return false;
    for(const auto& [id,p]:d.participants){(void)p;if(!s.participants.contains(id))return false;}
    for(const auto& [id,r]:s.cues){(void)r;auto c=findCue(d,id);if(!c||!cueReportable(c->kind))return false;}
    for(auto id:s.essentialDone)if(!findCue(d,id))return false;
    for(const auto& [id,o]:s.results){(void)o;if(std::none_of(d.qtes.begin(),d.qtes.end(),[&](const auto& q){return q.id==id;}))return false;}
    if(s.lastSync&&stops[s.lastSync-1].qte)return false;
    if(s.phase==ScenePhase::Waiting&&(s.nextStop>=stops.size()||stops[s.nextStop].qte))return false;
    if(s.phase==ScenePhase::Qte&&(s.nextStop>=stops.size()||!stops[s.nextStop].qte||stops[s.nextStop].id!=s.qte.qte))return false;
    return true;
}
}
