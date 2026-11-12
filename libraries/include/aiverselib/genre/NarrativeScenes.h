#pragma once
// Narrative and social scenes (narrative-graphs 0.2.0): typed facts (int/bool/enum) with persistent world-state
// facts, per-character relationship meters (affinity/trust/fear) with bounds, per-character knowledge sets, one-shot
// flags, chapter/act state, AND/OR/NOT conditions, deferred consequences (N ticks or N scenes later), speakers and
// participants per line, cinematic cue requests, ambient barks (priority + cooldowns + conditions) and visual-novel
// helpers (backlog, read-line detection, skip-read, auto-advance in ticks). Integer-only, deterministic (ascending IDs),
// one stepNarrative per authoritative tick with strictly increasing ticks; commands are deduplicated by ID.
// All text is localization keys. The policy returns events, cue requests and bark lines; UI, audio, cameras,
// animation and rewards belong to their owners. Legacy Story/StoryState (Narrative.h) stay unchanged.
#include "NarrativeCues.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kSceneMaxNodes=10000,kSceneMaxChoices=128,kSceneMaxFacts=10000,kSceneMaxCharacters=1024,kSceneMaxList=256,
    kSceneMaxCues=32,kSceneMaxParticipants=32,kSceneMaxBarks=4096,kSceneMaxTerms=256,kSceneMaxDepth=16,kSceneMaxEnumValues=256,
    kSceneMaxScenes=1024,kSceneMaxChapters=1024,kSceneMaxFlags=10000,kSceneMaxConsequences=4096,kSceneMaxBacklog=10000,
    kSceneMaxPending=65536,kSceneMaxCommands=256,kSceneMaxBarkRequests=4096;
inline constexpr int64_t kSceneMeterLimit=1000000000,kScenePriorityLimit=1000000;
inline constexpr uint32_t kSceneMaxTicks=10000000,kSceneMaxSkipLines=64;

enum class SceneFactType:uint8_t {Int,Bool,Enum};
enum class SocialMeter:uint8_t {Affinity,Trust,Fear};
inline constexpr std::size_t kSocialMeterCount=3;
enum class SceneCompare:uint8_t {Eq,Ne,Lt,Le,Gt,Ge,Between};
// All/Any/Not compose `children`; leaves read one value. Chose: subject = node, value = choice taken earlier.
enum class SceneConditionKind:uint8_t {All,Any,Not,Fact,Meter,Knows,Flag,Visited,Chapter,Act,Chose};
struct SceneCondition {
    SceneConditionKind kind=SceneConditionKind::All;SceneCompare op=SceneCompare::Eq;
    int64_t subject=0,character=0,value=0,value2=0;SocialMeter meter=SocialMeter::Affinity;std::vector<SceneCondition> children;
};
// Schedule: subject = consequence, delay ticks (or scenes with byScene). Reveal/Forget: subject = fact, characters
// = knowers (0 = player). Meter effects clamp to the character's bounds; AddFact clamps to the int fact's bounds.
enum class SceneEffectKind:uint8_t {SetFact,AddFact,SetMeter,AddMeter,Reveal,Forget,SetFlag,SetChapter,Schedule,Cancel};
struct SceneEffect {
    SceneEffectKind kind=SceneEffectKind::SetFact;int64_t subject=0,character=0,value=0;SocialMeter meter=SocialMeter::Affinity;
    std::vector<int64_t> characters;uint32_t delay=0;bool byScene=false;
};
struct SceneCue {int64_t id=0;uint32_t delay=0;};

// ---- Immutable definitions (NarrativeSceneData.h reads them from JSON).
struct SceneFactDef {int64_t id=0;SceneFactType type=SceneFactType::Int;int64_t minimum=0,maximum=0,initial=0;bool persistent=false;std::vector<std::string> values;};
struct MeterRange {int64_t minimum=-100,maximum=100,initial=0;};
struct SceneCharacter {int64_t id=0;std::string nameKey;std::array<MeterRange,kSocialMeterCount> meters{};};
struct SceneFlagDef {int64_t id=0;bool persistent=true;};
struct SceneChapter {int64_t id=0,act=1;std::string titleKey;};
struct SceneDef {int64_t id=0,entry=0,chapter=0;std::string titleKey;};
struct SceneChoice {int64_t id=0,next=0;std::string textKey;bool once=false;std::vector<SceneCondition> conditions;std::vector<SceneEffect> effects;std::vector<SceneCue> cues;};
// linear: a line without choices that continues to `next` (0 ends the scene) on Advance, auto-advance or skip.
struct SceneNode {
    int64_t id=0,speaker=0,next=0;std::string textKey;bool linear=false;uint32_t autoTicks=0; // 0 = config default
    std::vector<int64_t> participants;std::vector<SceneCue> cues;std::vector<SceneEffect> onEnter;std::vector<SceneChoice> choices;
};
struct SceneConsequence {int64_t id=0;std::vector<SceneCondition> conditions;std::vector<SceneEffect> effects;std::vector<SceneCue> cues;};
struct SceneBark {int64_t id=0,trigger=0,speaker=0,priority=0,cooldown=-1;std::string textKey;bool once=false;std::vector<SceneCondition> conditions;std::vector<SceneCue> cues;}; // cooldown -1 = config default
struct NarrativeWorld {
    int64_t initialChapter=0;std::map<int64_t,SceneFactDef> facts;std::map<int64_t,SceneCharacter> characters;std::map<int64_t,SceneFlagDef> flags;
    std::map<int64_t,SceneChapter> chapters;std::map<int64_t,SceneDef> scenes;std::map<int64_t,SceneNode> nodes;
    std::map<int64_t,SceneConsequence> consequences;std::map<int64_t,SceneBark> barks;
};
struct NarrativeConfig {
    uint32_t autoAdvanceTicks=180,skipLinesPerTick=4,backlogLimit=200,barkCooldownTicks=600,speakerCooldownTicks=120,maxPending=1024,maxBarkRequests=64;
    MeterRange meter{}; // default bounds for characters that do not declare a meter
};

// ---- Dynamic state (NarrativeScenePersistence.h saves it as record kind 64).
struct PendingConsequence {int64_t consequence=0;bool byScene=false;int64_t due=0;uint64_t seq=0;int64_t source=0;}; // due: tick or scene count
struct BacklogEntry {int64_t node=0,speaker=0,choice=0;uint32_t tick=0;int64_t scene=0;};
struct BarkMemory {std::map<int64_t,uint32_t> lastBark,lastSpeaker;std::map<int64_t,int64_t> counts;};
using SceneKey=std::pair<int64_t,int64_t>;
struct NarrativeState {
    int64_t scene=0,node=0,chapter=0,scenesStarted=0;
    std::map<int64_t,int64_t> facts;std::map<SceneKey,int64_t> meters;          // (character, meter) -> value
    std::map<SceneKey,uint32_t> knowledge;                                       // (knower, fact) -> tick learned
    std::map<int64_t,uint32_t> flags;std::map<SceneKey,uint32_t> taken;          // flag -> tick; (node, choice) -> tick
    std::map<int64_t,int64_t> visits,read;                                       // node -> times entered (read survives scenes)
    std::vector<PendingConsequence> pending;std::vector<BacklogEntry> backlog;BarkMemory barks;
    uint64_t nextSeq=1,nextCue=1,lastCommand=0;uint32_t lastTick=0,lineStart=0;
    bool initialized=false,inScene=false,lineWasRead=false,autoMode=false,skipMode=false;
};

enum class NarrativeCommandKind:uint8_t {StartScene,Advance,Choose,SetAuto,SetSkip};
struct NarrativeCommand {uint64_t id=0;NarrativeCommandKind kind=NarrativeCommandKind::Advance;int64_t value=0;}; // scene / choice / 0-1
struct BarkRequest {int64_t id=0,trigger=0;std::vector<int64_t> speakers;}; // empty speakers = any
struct NarrativeInput {std::vector<NarrativeCommand> commands;std::vector<BarkRequest> barks;};
enum class NarrativeEventKind:uint8_t {
    SceneStarted,LineShown,ChoiceTaken,SceneEnded,FactChanged,MeterChanged,Revealed,Forgotten,FlagSet,ChapterChanged,
    ConsequenceScheduled,ConsequenceCancelled,ConsequenceFired,ConsequenceSkipped,ConsequenceDropped,BarkSpoken,BarkNone,
    AutoAdvanced,SkipStopped,ModeChanged,CommandRejected
};
// LineShown: subject node, character speaker, value scene, extra 1 when read before. MeterChanged: subject character,
// character meter index, value new, extra old. FactChanged/ChapterChanged: value new, extra old. CommandRejected:
// subject command id, value kind, extra 1 for a duplicate/old id. AutoAdvanced: extra 1 when by skip.
struct NarrativeEvent {NarrativeEventKind kind=NarrativeEventKind::LineShown;int64_t subject=0,character=0,value=0,extra=0;};
struct BarkLine {int64_t request=0,bark=0,speaker=0;std::string textKey;};
struct NarrativeReport {std::vector<NarrativeEvent> events;std::vector<CueRequest> cues;std::vector<BarkLine> barks;};

// ---- Validation. Empty string when valid, otherwise "path: reason" with definition IDs as [id=N].
inline bool meterRangeOk(const MeterRange& m){return m.minimum>= -kSceneMeterLimit&&m.minimum<=m.initial&&m.initial<=m.maximum&&m.maximum<=kSceneMeterLimit;}
inline std::string narrativeConfigError(const NarrativeConfig& c){
    if(c.autoAdvanceTicks<1||c.autoAdvanceTicks>kSceneMaxTicks)return "autoAdvanceTicks: must be 1..10000000";
    if(c.skipLinesPerTick<1||c.skipLinesPerTick>kSceneMaxSkipLines)return "skipLinesPerTick: must be 1..64";
    if(c.backlogLimit<1||c.backlogLimit>kSceneMaxBacklog)return "backlogLimit: must be 1..10000";
    if(c.barkCooldownTicks>kSceneMaxTicks||c.speakerCooldownTicks>kSceneMaxTicks)return "barkCooldownTicks: cooldowns must be 0..10000000";
    if(c.maxPending<1||c.maxPending>kSceneMaxPending)return "maxPending: must be 1..65536";
    if(c.maxBarkRequests<1||c.maxBarkRequests>kSceneMaxBarkRequests)return "maxBarkRequests: must be 1..4096";
    if(!meterRangeOk(c.meter))return "meter: requires -1000000000 <= minimum <= initial <= maximum <= 1000000000";
    return {};
}
inline bool valid(const NarrativeConfig& c){return narrativeConfigError(c).empty();}
namespace scene_detail {
inline std::string at(const std::string& path,const char* list,int64_t id){return path+list+"[id="+std::to_string(id)+"]";}
inline std::string idx(const std::string& path,std::size_t i){return path+"["+std::to_string(i)+"]";}
inline bool inDomain(const SceneFactDef& f,int64_t v){return v>=f.minimum&&v<=f.maximum;}
inline bool knower(const NarrativeWorld& w,int64_t c){return c==0||w.characters.contains(c);}
inline std::string cues(std::span<const SceneCue> list,const std::string& path){
    if(list.size()>kSceneMaxCues)return path+": at most 32 cues";
    for(std::size_t i=0;i<list.size();++i){if(list[i].id<=0)return idx(path,i)+".id: must be positive";if(list[i].delay>kSceneMaxTicks)return idx(path,i)+".delay: must be 0..10000000 ticks";}
    return {};
}
inline std::string condition(const NarrativeWorld& w,const SceneCondition& c,const std::string& path,std::size_t depth,std::size_t& terms){
    using K=SceneConditionKind;
    if(depth>kSceneMaxDepth)return path+": nested deeper than 16";if(++terms>kSceneMaxTerms)return path+": more than 256 condition terms";
    if(uint8_t(c.op)>uint8_t(SceneCompare::Between))return path+".op: unknown operator";
    bool group=c.kind==K::All||c.kind==K::Any||c.kind==K::Not;
    if(!group&&!c.children.empty())return path+": a leaf condition has no terms";
    if(!group&&c.op==SceneCompare::Between&&c.value>c.value2)return path+": min exceeds max";
    switch(c.kind){
    case K::All:case K::Any:if(c.children.empty())return path+": expected at least one term";break;
    case K::Not:if(c.children.size()!=1)return path+".not: expected exactly one term";break;
    case K::Fact:{
        auto f=w.facts.find(c.subject);if(f==w.facts.end())return path+".fact: unknown fact "+std::to_string(c.subject);
        if(f->second.type!=SceneFactType::Int){
            if(c.op!=SceneCompare::Eq&&c.op!=SceneCompare::Ne)return path+".op: bool and enum facts allow eq/ne only";
            if(!inDomain(f->second,c.value))return path+".value: outside the fact's values";
        }break;}
    case K::Meter:if(c.character<=0||!w.characters.contains(c.character))return path+".character: unknown character "+std::to_string(c.character);
        if(uint8_t(c.meter)>=kSocialMeterCount)return path+".meter: unknown meter";break;
    case K::Knows:if(!w.facts.contains(c.subject))return path+".knows: unknown fact "+std::to_string(c.subject);
        if(!knower(w,c.character))return path+".character: unknown character "+std::to_string(c.character);break;
    case K::Flag:if(!w.flags.contains(c.subject))return path+".flag: unknown flag "+std::to_string(c.subject);break;
    case K::Visited:if(!w.nodes.contains(c.subject))return path+".visited: unknown node "+std::to_string(c.subject);
        if(c.value<0)return path+".value: visit counts are nonnegative";break;
    case K::Chapter:case K::Act:break;
    case K::Chose:{
        auto n=w.nodes.find(c.subject);if(n==w.nodes.end())return path+".node: unknown node "+std::to_string(c.subject);
        if(std::none_of(n->second.choices.begin(),n->second.choices.end(),[&](const SceneChoice& x){return x.id==c.value;}))return path+".chose: node "+std::to_string(c.subject)+" has no choice "+std::to_string(c.value);
        break;}
    default:return path+": unknown condition kind";
    }
    for(std::size_t i=0;i<c.children.size();++i)if(auto e=condition(w,c.children[i],idx(path+".terms",i),depth+1,terms);!e.empty())return e;
    return {};
}
inline std::string conditions(const NarrativeWorld& w,std::span<const SceneCondition> list,const std::string& path){
    if(list.size()>kSceneMaxTerms)return path+": at most 256 conditions";std::size_t terms=0;
    for(std::size_t i=0;i<list.size();++i)if(auto e=condition(w,list[i],idx(path,i),1,terms);!e.empty())return e;return {};
}
inline std::string effects(const NarrativeWorld& w,std::span<const SceneEffect> list,const std::string& path){
    using K=SceneEffectKind;if(list.size()>kSceneMaxList)return path+": at most 256 effects";
    for(std::size_t i=0;i<list.size();++i){
        const auto& e=list[i];auto p=idx(path,i);
        switch(e.kind){
        case K::SetFact:case K::AddFact:{
            auto f=w.facts.find(e.subject);if(f==w.facts.end())return p+".fact: unknown fact "+std::to_string(e.subject);
            if(e.kind==K::SetFact&&!inDomain(f->second,e.value))return p+".value: outside the fact's domain";
            if(e.kind==K::AddFact&&f->second.type!=SceneFactType::Int)return p+".add: only int facts can be added to";break;}
        case K::SetMeter:case K::AddMeter:{
            auto c=w.characters.find(e.character);if(e.character<=0||c==w.characters.end())return p+".character: unknown character "+std::to_string(e.character);
            if(uint8_t(e.meter)>=kSocialMeterCount)return p+".meter: unknown meter";const auto& r=c->second.meters[uint8_t(e.meter)];
            if(e.kind==K::SetMeter&&(e.value<r.minimum||e.value>r.maximum))return p+".value: outside the meter bounds";
            if(e.kind==K::AddMeter&&(e.value< -2*kSceneMeterLimit||e.value>2*kSceneMeterLimit))return p+".value: change out of range";break;}
        case K::Reveal:case K::Forget:{
            if(!w.facts.contains(e.subject))return p+".fact: unknown fact "+std::to_string(e.subject);
            if(e.characters.empty()||e.characters.size()>kSceneMaxParticipants)return p+".characters: expected 1..32 characters";
            for(std::size_t k=0;k<e.characters.size();++k){
                if(!knower(w,e.characters[k]))return idx(p+".characters",k)+": unknown character "+std::to_string(e.characters[k]);
                if(std::count(e.characters.begin(),e.characters.end(),e.characters[k])>1)return idx(p+".characters",k)+": duplicate character";
            }break;}
        case K::SetFlag:if(!w.flags.contains(e.subject))return p+".flag: unknown flag "+std::to_string(e.subject);break;
        case K::SetChapter:if(!w.chapters.contains(e.subject))return p+".chapter: unknown chapter "+std::to_string(e.subject);break;
        case K::Schedule:if(!w.consequences.contains(e.subject))return p+".schedule: unknown consequence "+std::to_string(e.subject);
            if(e.delay<1||e.delay>kSceneMaxTicks)return p+".delay: must be 1..10000000";break;
        case K::Cancel:if(!w.consequences.contains(e.subject))return p+".cancel: unknown consequence "+std::to_string(e.subject);break;
        default:return p+": unknown effect kind";
        }
    }return {};
}
inline std::string key(const std::string& text,const std::string& path){return text.empty()?path+": expected a nonempty localization key":std::string{};}
}
inline std::string narrativeWorldError(const NarrativeWorld& w){
    namespace d=scene_detail;
    if(w.nodes.empty()&&w.barks.empty())return "nodes: expected at least one node or bark";
    if(w.facts.size()>kSceneMaxFacts||w.characters.size()>kSceneMaxCharacters||w.flags.size()>kSceneMaxFlags||w.chapters.size()>kSceneMaxChapters||w.scenes.size()>kSceneMaxScenes
        ||w.nodes.size()>kSceneMaxNodes||w.consequences.size()>kSceneMaxConsequences||w.barks.size()>kSceneMaxBarks)return "definitions: too many facts, characters, flags, chapters, scenes, nodes, consequences or barks";
    for(const auto& [id,f]:w.facts){
        auto p=d::at("","facts",id);if(id<=0||f.id!=id)return p+".id: must be positive and match its key";
        if(f.type==SceneFactType::Bool&&(f.minimum!=0||f.maximum!=1))return p+": bool facts range 0..1";
        if(f.type==SceneFactType::Enum){
            if(f.values.empty()||f.values.size()>kSceneMaxEnumValues||f.minimum!=0||f.maximum!=int64_t(f.values.size())-1)return p+".values: expected 1..256 values";
            for(std::size_t i=0;i<f.values.size();++i)if(f.values[i].empty()||std::count(f.values.begin(),f.values.end(),f.values[i])>1)return d::idx(p+".values",i)+": empty or duplicate value";
        }else if(!f.values.empty())return p+".values: only enum facts have values";
        if(uint8_t(f.type)>uint8_t(SceneFactType::Enum)||f.minimum>f.maximum)return p+": min exceeds max";
        if(!d::inDomain(f,f.initial))return p+".initial: outside the fact's domain";
    }
    for(const auto& [id,c]:w.characters){
        auto p=d::at("","characters",id);if(id<=0||c.id!=id)return p+".id: must be positive and match its key";
        if(auto e=d::key(c.nameKey,p+".nameKey");!e.empty())return e;
        for(const auto& m:c.meters)if(!meterRangeOk(m))return p+".meters: requires minimum <= initial <= maximum within +-1000000000";
    }
    for(const auto& [id,f]:w.flags)if(id<=0||f.id!=id)return d::at("","flags",id)+".id: must be positive and match its key";
    for(const auto& [id,c]:w.chapters){
        auto p=d::at("","chapters",id);if(id<=0||c.id!=id)return p+".id: must be positive and match its key";
        if(c.act<1||c.act>1000000)return p+".act: must be 1..1000000";if(auto e=d::key(c.titleKey,p+".titleKey");!e.empty())return e;
    }
    if(w.initialChapter&&!w.chapters.contains(w.initialChapter))return "initialChapter: unknown chapter "+std::to_string(w.initialChapter);
    for(const auto& [id,s]:w.scenes){
        auto p=d::at("","scenes",id);if(id<=0||s.id!=id)return p+".id: must be positive and match its key";
        if(!w.nodes.contains(s.entry))return p+".entry: unknown node "+std::to_string(s.entry);
        if(s.chapter&&!w.chapters.contains(s.chapter))return p+".chapter: unknown chapter "+std::to_string(s.chapter);
    }
    for(const auto& [id,n]:w.nodes){
        auto p=d::at("","nodes",id);if(id<=0||n.id!=id)return p+".id: must be positive and match its key";
        if(auto e=d::key(n.textKey,p+".textKey");!e.empty())return e;
        if(!d::knower(w,n.speaker))return p+".speaker: unknown character "+std::to_string(n.speaker);
        if(n.participants.size()>kSceneMaxParticipants)return p+".participants: at most 32";
        for(std::size_t i=0;i<n.participants.size();++i){
            if(n.participants[i]<=0||!w.characters.contains(n.participants[i]))return d::idx(p+".participants",i)+": unknown character "+std::to_string(n.participants[i]);
            if(std::count(n.participants.begin(),n.participants.end(),n.participants[i])>1)return d::idx(p+".participants",i)+": duplicate character";
        }
        if(n.autoTicks>kSceneMaxTicks)return p+".autoTicks: must be 0..10000000";
        if(auto e=d::cues(n.cues,p+".cues");!e.empty())return e;if(auto e=d::effects(w,n.onEnter,p+".onEnter");!e.empty())return e;
        if(n.linear){
            if(!n.choices.empty())return p+": a linear line has no choices";
            if(n.next&&!w.nodes.contains(n.next))return p+".next: unknown node "+std::to_string(n.next);
            continue;
        }
        if(n.next)return p+".next: a choice node continues through its choices";
        if(n.choices.empty()||n.choices.size()>kSceneMaxChoices)return p+".choices: expected 1..128 choices";
        for(const auto& c:n.choices){
            auto q=d::at(p+".","choices",c.id);if(c.id<=0)return q+".id: must be positive";
            if(std::count_if(n.choices.begin(),n.choices.end(),[&](const SceneChoice& x){return x.id==c.id;})>1)return q+".id: duplicate choice";
            if(auto e=d::key(c.textKey,q+".textKey");!e.empty())return e;
            if(c.next&&!w.nodes.contains(c.next))return q+".next: unknown node "+std::to_string(c.next);
            if(auto e=d::conditions(w,c.conditions,q+".conditions");!e.empty())return e;
            if(auto e=d::effects(w,c.effects,q+".effects");!e.empty())return e;
            if(auto e=d::cues(c.cues,q+".cues");!e.empty())return e;
        }
    }
    for(const auto& [id,c]:w.consequences){
        auto p=d::at("","consequences",id);if(id<=0||c.id!=id)return p+".id: must be positive and match its key";
        if(auto e=d::conditions(w,c.conditions,p+".conditions");!e.empty())return e;
        if(auto e=d::effects(w,c.effects,p+".effects");!e.empty())return e;if(auto e=d::cues(c.cues,p+".cues");!e.empty())return e;
    }
    for(const auto& [id,b]:w.barks){
        auto p=d::at("","barks",id);if(id<=0||b.id!=id)return p+".id: must be positive and match its key";
        if(b.trigger<=0)return p+".trigger: must be positive";if(!d::knower(w,b.speaker))return p+".speaker: unknown character "+std::to_string(b.speaker);
        if(auto e=d::key(b.textKey,p+".textKey");!e.empty())return e;
        if(b.priority< -kScenePriorityLimit||b.priority>kScenePriorityLimit)return p+".priority: must be -1000000..1000000";
        if(b.cooldown< -1||b.cooldown>int64_t(kSceneMaxTicks))return p+".cooldown: must be 0..10000000 ticks";
        if(auto e=d::conditions(w,b.conditions,p+".conditions");!e.empty())return e;if(auto e=d::cues(b.cues,p+".cues");!e.empty())return e;
    }
    // Every line must be able to reach an ending (graph-wise, ignoring conditions): rejects cycles without exits.
    std::map<int64_t,std::vector<int64_t>> into;std::vector<int64_t> open;std::map<int64_t,bool> exits;
    auto edge=[&](int64_t from,int64_t to){if(to==0){if(!exits[from]){exits[from]=true;open.push_back(from);}}else into[to].push_back(from);};
    for(const auto& [id,n]:w.nodes){if(n.linear)edge(id,n.next);else for(const auto& c:n.choices)edge(id,c.next);}
    while(!open.empty()){auto n=open.back();open.pop_back();for(auto from:into[n])if(!exits[from]){exits[from]=true;open.push_back(from);}}
    for(const auto& [id,n]:w.nodes){(void)n;if(!exits[id])return d::at("","nodes",id)+": cannot reach an ending (cycle without exit)";}
    return {};
}
inline bool valid(const NarrativeWorld& w){return narrativeWorldError(w).empty();}

// ---- Queries.
inline bool sceneCompare(SceneCompare op,int64_t v,int64_t a,int64_t b){
    switch(op){case SceneCompare::Eq:return v==a;case SceneCompare::Ne:return v!=a;case SceneCompare::Lt:return v<a;case SceneCompare::Le:return v<=a;
    case SceneCompare::Gt:return v>a;case SceneCompare::Ge:return v>=a;case SceneCompare::Between:return v>=a&&v<=b;}return false;
}
inline int64_t socialMeter(const NarrativeState& s,int64_t character,SocialMeter m){auto it=s.meters.find({character,int64_t(m)});return it==s.meters.end()?0:it->second;}
inline bool knows(const NarrativeState& s,int64_t character,int64_t fact){return s.knowledge.contains({character,fact});}
inline bool sceneCondition(const NarrativeWorld& w,const NarrativeState& s,const SceneCondition& c){
    using K=SceneConditionKind;
    switch(c.kind){
    case K::All:return std::all_of(c.children.begin(),c.children.end(),[&](const SceneCondition& x){return sceneCondition(w,s,x);});
    case K::Any:return std::any_of(c.children.begin(),c.children.end(),[&](const SceneCondition& x){return sceneCondition(w,s,x);});
    case K::Not:return c.children.size()==1&&!sceneCondition(w,s,c.children[0]);
    case K::Fact:{auto it=s.facts.find(c.subject);return it!=s.facts.end()&&sceneCompare(c.op,it->second,c.value,c.value2);}
    case K::Meter:{auto it=s.meters.find({c.character,int64_t(c.meter)});return it!=s.meters.end()&&sceneCompare(c.op,it->second,c.value,c.value2);}
    case K::Knows:return knows(s,c.character,c.subject);
    case K::Flag:return s.flags.contains(c.subject);
    case K::Visited:{auto it=s.visits.find(c.subject);return sceneCompare(c.op,it==s.visits.end()?0:it->second,c.value,c.value2);}
    case K::Chapter:return sceneCompare(c.op,s.chapter,c.value,c.value2);
    case K::Act:{auto it=w.chapters.find(s.chapter);return sceneCompare(c.op,it==w.chapters.end()?0:it->second.act,c.value,c.value2);}
    case K::Chose:return s.taken.contains({c.subject,c.value});
    }return false;
}
inline bool sceneConditions(const NarrativeWorld& w,const NarrativeState& s,std::span<const SceneCondition> list){
    return std::all_of(list.begin(),list.end(),[&](const SceneCondition& c){return sceneCondition(w,s,c);});
}
inline bool sceneChoiceAllowed(const NarrativeWorld& w,const NarrativeState& s,const SceneChoice& c){
    return !(c.once&&s.taken.contains({s.node,c.id}))&&sceneConditions(w,s,c.conditions);
}
// Available choice IDs of the current line, ascending; empty for a linear line or outside a scene.
inline std::vector<int64_t> sceneChoices(const NarrativeWorld& w,const NarrativeState& s){
    std::vector<int64_t> out;auto n=w.nodes.find(s.node);if(!s.inScene||n==w.nodes.end())return out;
    for(const auto& c:n->second.choices)if(sceneChoiceAllowed(w,s,c))out.push_back(c.id);std::sort(out.begin(),out.end());return out;
}
inline bool lineRead(const NarrativeState& s,int64_t node){auto it=s.read.find(node);return it!=s.read.end()&&it->second>0;}
// Skip-read: the current line is linear and was already read before this visit.
inline bool canSkipLine(const NarrativeWorld& w,const NarrativeState& s){auto n=w.nodes.find(s.node);return s.inScene&&n!=w.nodes.end()&&n->second.linear&&s.lineWasRead;}
// Tick at which auto mode advances the current line (nullopt for choices / outside scenes).
inline std::optional<uint32_t> autoAdvanceTick(const NarrativeWorld& w,const NarrativeConfig& c,const NarrativeState& s){
    auto n=w.nodes.find(s.node);if(!s.inScene||n==w.nodes.end()||!n->second.linear)return {};
    uint64_t at=uint64_t(s.lineStart)+(n->second.autoTicks?n->second.autoTicks:c.autoAdvanceTicks);
    return at>std::numeric_limits<uint32_t>::max()?std::optional<uint32_t>{}:std::optional<uint32_t>(uint32_t(at));
}

// Fresh state: every fact at its initial value (overrides must be in domain), every meter at its initial value.
inline std::optional<NarrativeState> beginNarrative(const NarrativeWorld& w,const NarrativeConfig& c,const std::map<int64_t,int64_t>& overrides={}){
    if(!valid(w)||!valid(c))return {};NarrativeState s;
    for(const auto& [id,f]:w.facts)s.facts[id]=f.initial;
    for(auto [id,v]:overrides){auto f=w.facts.find(id);if(f==w.facts.end()||!scene_detail::inDomain(f->second,v))return {};s.facts[id]=v;}
    for(const auto& [id,ch]:w.characters)for(std::size_t m=0;m<kSocialMeterCount;++m)s.meters[{id,int64_t(m)}]=ch.meters[m].initial;
    s.chapter=w.initialChapter;return s;
}

// ---- Stepping.
namespace scene_detail {
struct Step {const NarrativeWorld& w;const NarrativeConfig& c;NarrativeState& s;NarrativeReport& r;uint32_t tick;};
inline void event(Step& st,NarrativeEventKind k,int64_t subject,int64_t character=0,int64_t value=0,int64_t extra=0){st.r.events.push_back({k,subject,character,value,extra});}
inline int64_t clampAdd(int64_t v,int64_t d,int64_t lo,int64_t hi){
    v=std::clamp(v,lo,hi);
    if(d>=0){uint64_t room=uint64_t(hi)-uint64_t(v);return uint64_t(d)>room?hi:int64_t(uint64_t(v)+uint64_t(d));}
    uint64_t room=uint64_t(v)-uint64_t(lo),mag=uint64_t(0)-uint64_t(d);return mag>room?lo:int64_t(uint64_t(v)-mag);
}
inline void cues(Step& st,std::span<const SceneCue> list,CueSource source,int64_t node,int64_t choice,int64_t speaker,int64_t bark=0,int64_t consequence=0){
    for(const auto& c:list)st.r.cues.push_back({st.s.nextCue++,c.id,node,choice,st.s.inScene?st.s.scene:0,speaker,bark,consequence,st.tick,c.delay,source});
}
inline bool apply(Step& st,std::span<const SceneEffect> list,int64_t source){
    using K=SceneEffectKind;auto& s=st.s;
    for(const auto& e:list){
        switch(e.kind){
        case K::SetFact:case K::AddFact:{
            auto f=st.w.facts.find(e.subject);auto v=s.facts.find(e.subject);if(f==st.w.facts.end()||v==s.facts.end())return false;
            int64_t old=v->second,now=e.kind==K::SetFact?e.value:clampAdd(old,e.value,f->second.minimum,f->second.maximum);
            if(!inDomain(f->second,now))return false;if(now!=old){v->second=now;event(st,NarrativeEventKind::FactChanged,e.subject,0,now,old);}break;}
        case K::SetMeter:case K::AddMeter:{
            auto c=st.w.characters.find(e.character);auto v=s.meters.find({e.character,int64_t(e.meter)});if(c==st.w.characters.end()||v==s.meters.end())return false;
            const auto& r=c->second.meters[uint8_t(e.meter)];int64_t old=v->second,now=e.kind==K::SetMeter?std::clamp(e.value,r.minimum,r.maximum):clampAdd(old,e.value,r.minimum,r.maximum);
            if(now!=old){v->second=now;event(st,NarrativeEventKind::MeterChanged,e.character,int64_t(e.meter),now,old);}break;}
        case K::Reveal:for(auto who:e.characters)if(s.knowledge.emplace(SceneKey{who,e.subject},st.tick).second)event(st,NarrativeEventKind::Revealed,e.subject,who);break;
        case K::Forget:for(auto who:e.characters)if(s.knowledge.erase({who,e.subject}))event(st,NarrativeEventKind::Forgotten,e.subject,who);break;
        case K::SetFlag:if(s.flags.emplace(e.subject,st.tick).second)event(st,NarrativeEventKind::FlagSet,e.subject);break;
        case K::SetChapter:if(s.chapter!=e.subject){event(st,NarrativeEventKind::ChapterChanged,e.subject,0,e.subject,s.chapter);s.chapter=e.subject;}break;
        case K::Schedule:{
            if(s.pending.size()>=st.c.maxPending){event(st,NarrativeEventKind::ConsequenceDropped,e.subject,0,0,1);break;}
            int64_t due=e.byScene?s.scenesStarted+int64_t(e.delay):int64_t(st.tick)+int64_t(e.delay);
            s.pending.push_back({e.subject,e.byScene,due,s.nextSeq++,source});event(st,NarrativeEventKind::ConsequenceScheduled,e.subject,0,due,e.byScene);break;}
        case K::Cancel:{
            auto before=s.pending.size();std::erase_if(s.pending,[&](const PendingConsequence& p){return p.consequence==e.subject;});
            if(before!=s.pending.size())event(st,NarrativeEventKind::ConsequenceCancelled,e.subject,0,int64_t(before-s.pending.size()));break;}
        default:return false;
        }
    }return true;
}
inline bool fire(Step& st,bool byScene,int64_t now){
    std::vector<PendingConsequence> due;
    std::erase_if(st.s.pending,[&](const PendingConsequence& p){if(p.byScene!=byScene||p.due>now)return false;due.push_back(p);return true;});
    std::sort(due.begin(),due.end(),[](const PendingConsequence& a,const PendingConsequence& b){return std::pair(a.due,a.seq)<std::pair(b.due,b.seq);});
    for(const auto& p:due){
        auto def=st.w.consequences.find(p.consequence);if(def==st.w.consequences.end()){event(st,NarrativeEventKind::ConsequenceDropped,p.consequence);continue;}
        if(!sceneConditions(st.w,st.s,def->second.conditions)){event(st,NarrativeEventKind::ConsequenceSkipped,p.consequence,0,p.due);continue;}
        event(st,NarrativeEventKind::ConsequenceFired,p.consequence,0,p.due,int64_t(p.seq));cues(st,def->second.cues,CueSource::Consequence,0,0,0,0,p.consequence);
        if(!apply(st,def->second.effects,p.source))return false;
    }return true;
}
inline bool enter(Step& st,int64_t id){
    auto it=st.w.nodes.find(id);if(it==st.w.nodes.end())return false;const auto& n=it->second;auto& s=st.s;
    s.node=id;auto& v=s.visits[id];if(v<std::numeric_limits<int64_t>::max())++v;auto& rd=s.read[id];s.lineWasRead=rd>0;if(rd<std::numeric_limits<int64_t>::max())++rd;
    s.lineStart=st.tick;s.backlog.push_back({id,n.speaker,0,st.tick,s.scene});
    if(s.backlog.size()>st.c.backlogLimit)s.backlog.erase(s.backlog.begin(),s.backlog.begin()+std::ptrdiff_t(s.backlog.size()-st.c.backlogLimit));
    event(st,NarrativeEventKind::LineShown,id,n.speaker,s.scene,s.lineWasRead);cues(st,n.cues,CueSource::NodeEnter,id,0,n.speaker);
    return apply(st,n.onEnter,id);
}
inline bool go(Step& st,int64_t next){
    if(next)return enter(st,next);
    event(st,NarrativeEventKind::SceneEnded,st.s.scene,0,st.s.node);st.s.inScene=false;st.s.node=0;st.s.lineWasRead=false;st.s.skipMode=false;return true;
}
inline bool command(Step& st,const NarrativeCommand& cmd){
    auto& s=st.s;using C=NarrativeCommandKind;
    switch(cmd.kind){
    case C::StartScene:{
        auto sc=st.w.scenes.find(cmd.value);if(s.inScene||sc==st.w.scenes.end()||s.scenesStarted==std::numeric_limits<int64_t>::max())return false;
        ++s.scenesStarted;s.scene=cmd.value;s.inScene=true;
        for(const auto& [id,f]:st.w.facts)if(!f.persistent){auto& v=s.facts[id];if(v!=f.initial){event(st,NarrativeEventKind::FactChanged,id,0,f.initial,v);v=f.initial;}}
        for(const auto& [id,f]:st.w.flags)if(!f.persistent)s.flags.erase(id);
        if(sc->second.chapter&&sc->second.chapter!=s.chapter){event(st,NarrativeEventKind::ChapterChanged,sc->second.chapter,0,sc->second.chapter,s.chapter);s.chapter=sc->second.chapter;}
        event(st,NarrativeEventKind::SceneStarted,cmd.value,0,s.scenesStarted);
        return fire(st,true,s.scenesStarted)&&enter(st,sc->second.entry);}
    case C::Advance:{auto n=st.w.nodes.find(s.node);if(!s.inScene||n==st.w.nodes.end()||!n->second.linear)return false;return go(st,n->second.next);}
    case C::Choose:{
        auto n=st.w.nodes.find(s.node);if(!s.inScene||n==st.w.nodes.end()||n->second.linear)return false;
        auto c=std::find_if(n->second.choices.begin(),n->second.choices.end(),[&](const SceneChoice& x){return x.id==cmd.value;});
        if(c==n->second.choices.end()||!sceneChoiceAllowed(st.w,s,*c))return false;
        s.taken[{s.node,c->id}]=st.tick;if(!s.backlog.empty()&&s.backlog.back().node==s.node)s.backlog.back().choice=c->id;
        event(st,NarrativeEventKind::ChoiceTaken,c->id,n->second.speaker,s.node);cues(st,c->cues,CueSource::Choice,s.node,c->id,n->second.speaker);
        return apply(st,c->effects,s.node)&&go(st,c->next);}
    case C::SetAuto:case C::SetSkip:{
        if(cmd.value!=0&&cmd.value!=1)return false;bool& mode=cmd.kind==C::SetAuto?s.autoMode:s.skipMode;
        if(mode!=(cmd.value==1)){mode=cmd.value==1;event(st,NarrativeEventKind::ModeChanged,cmd.kind==C::SetAuto?0:1,0,cmd.value);}return true;}
    }return false;
}
inline bool better(const SceneBark& a,const BarkMemory& m,const SceneBark* b){
    if(!b)return true;if(a.priority!=b->priority)return a.priority>b->priority;
    auto la=m.lastBark.find(a.id),lb=m.lastBark.find(b->id);
    if((la==m.lastBark.end())!=(lb==m.lastBark.end()))return la==m.lastBark.end();         // never spoken first
    if(la!=m.lastBark.end()&&la->second!=lb->second)return la->second<lb->second;         // then least recently spoken
    return a.id<b->id;
}
inline void barks(Step& st,std::vector<BarkRequest> requests){
    auto& s=st.s;std::sort(requests.begin(),requests.end(),[](const BarkRequest& a,const BarkRequest& b){return a.id<b.id;});std::vector<int64_t> busy;
    for(const auto& q:requests){
        const SceneBark* best=nullptr;
        for(const auto& [id,b]:st.w.barks){
            if(b.trigger!=q.trigger||(!q.speakers.empty()&&std::find(q.speakers.begin(),q.speakers.end(),b.speaker)==q.speakers.end())
                ||std::find(busy.begin(),busy.end(),b.speaker)!=busy.end()||(b.once&&s.barks.counts.contains(id)))continue;
            auto last=s.barks.lastBark.find(id);uint32_t cooldown=b.cooldown<0?st.c.barkCooldownTicks:uint32_t(b.cooldown);
            if(last!=s.barks.lastBark.end()&&st.tick-last->second<cooldown)continue;
            auto spoke=s.barks.lastSpeaker.find(b.speaker);if(spoke!=s.barks.lastSpeaker.end()&&st.tick-spoke->second<st.c.speakerCooldownTicks)continue;
            if(!sceneConditions(st.w,s,b.conditions)||!better(b,s.barks,best))continue;best=&b;
        }
        if(!best){event(st,NarrativeEventKind::BarkNone,q.id,0,q.trigger);continue;}
        s.barks.lastBark[best->id]=st.tick;s.barks.lastSpeaker[best->speaker]=st.tick;auto& count=s.barks.counts[best->id];if(count<std::numeric_limits<int64_t>::max())++count;
        busy.push_back(best->speaker);st.r.barks.push_back({q.id,best->id,best->speaker,best->textKey});
        event(st,NarrativeEventKind::BarkSpoken,best->id,best->speaker,q.id,q.trigger);cues(st,best->cues,CueSource::Bark,0,0,best->speaker,best->id);
    }
}
}
// One authoritative tick: due tick consequences, commands by ascending ID (each atomic; old/duplicate IDs and
// invalid commands are rejected with CommandRejected and change nothing), skip-read / auto-advance, then barks.
// Returns nullopt and leaves `state` untouched for a repeated/backwards tick, invalid config/input or a state
// that does not belong to `world` (validate the world once with narrativeWorldError / readNarrativeWorld).
inline std::optional<NarrativeReport> stepNarrative(const NarrativeWorld& w,const NarrativeConfig& c,NarrativeState& state,uint32_t tick,const NarrativeInput& input={}){
    namespace d=scene_detail;
    if(!valid(c)||(state.initialized&&tick<=state.lastTick)||input.commands.size()>kSceneMaxCommands||input.barks.size()>c.maxBarkRequests)return {};
    if(state.inScene&&!w.nodes.contains(state.node))return {};
    auto commands=input.commands;std::sort(commands.begin(),commands.end(),[](const NarrativeCommand& a,const NarrativeCommand& b){return a.id<b.id;});
    for(std::size_t i=0;i<commands.size();++i)if(commands[i].id==0||uint8_t(commands[i].kind)>uint8_t(NarrativeCommandKind::SetSkip)||(i&&commands[i].id==commands[i-1].id))return {};
    for(std::size_t i=0;i<input.barks.size();++i){
        const auto& q=input.barks[i];if(q.id<=0||q.trigger<=0||q.speakers.size()>kSceneMaxParticipants)return {};
        for(std::size_t k=0;k<i;++k)if(input.barks[k].id==q.id)return {};
    }
    NarrativeState s=state;NarrativeReport r;d::Step st{w,c,s,r,tick};s.initialized=true;s.lastTick=tick;
    if(!d::fire(st,false,tick))return {};
    for(const auto& cmd:commands){
        if(cmd.id<=s.lastCommand){d::event(st,NarrativeEventKind::CommandRejected,int64_t(cmd.id),0,int64_t(cmd.kind),1);continue;}
        NarrativeState trial=s;NarrativeReport tr;d::Step ts{w,c,trial,tr,tick};
        if(!d::command(ts,cmd)){d::event(st,NarrativeEventKind::CommandRejected,int64_t(cmd.id),0,int64_t(cmd.kind),0);continue;}
        trial.lastCommand=cmd.id;s=std::move(trial);
        r.events.insert(r.events.end(),tr.events.begin(),tr.events.end());r.cues.insert(r.cues.end(),tr.cues.begin(),tr.cues.end());
    }
    if(s.inScene&&s.skipMode){
        for(uint32_t i=0;i<c.skipLinesPerTick&&s.inScene;++i){
            auto n=w.nodes.find(s.node);if(n==w.nodes.end())return {};
            if(!n->second.linear||!s.lineWasRead){d::event(st,NarrativeEventKind::SkipStopped,s.node,0,0,n->second.linear?0:1);s.skipMode=false;break;}
            d::event(st,NarrativeEventKind::AutoAdvanced,s.node,0,0,1);if(!d::go(st,n->second.next))return {};
        }
    }else if(s.inScene&&s.autoMode){
        auto at=autoAdvanceTick(w,c,s);
        if(at&&tick>=*at){d::event(st,NarrativeEventKind::AutoAdvanced,s.node);if(!d::go(st,w.nodes.at(s.node).next))return {};}
    }
    d::barks(st,input.barks);
    state=std::move(s);return r;
}

// Structural + definition binding check for a restored state (call after genre::load). Empty when it fits.
inline std::string narrativeStateError(const NarrativeWorld& w,const NarrativeConfig& c,const NarrativeState& s){
    if(s.facts.size()!=w.facts.size())return "facts: state does not match the fact definitions";
    for(const auto& [id,v]:s.facts){auto f=w.facts.find(id);if(f==w.facts.end()||!scene_detail::inDomain(f->second,v))return "facts["+std::to_string(id)+"]: unknown or out of domain";}
    if(s.meters.size()!=w.characters.size()*kSocialMeterCount)return "meters: state does not match the characters";
    for(const auto& [k,v]:s.meters){auto ch=w.characters.find(k.first);if(ch==w.characters.end()||k.second<0||k.second>=int64_t(kSocialMeterCount))return "meters: unknown character "+std::to_string(k.first);
        const auto& r=ch->second.meters[std::size_t(k.second)];if(v<r.minimum||v>r.maximum)return "meters["+std::to_string(k.first)+"]: out of bounds";}
    for(const auto& [k,t]:s.knowledge)if(!scene_detail::knower(w,k.first)||!w.facts.contains(k.second)||t>s.lastTick)return "knowledge: unknown knower or fact";
    for(const auto& [id,t]:s.flags)if(!w.flags.contains(id)||t>s.lastTick)return "flags["+std::to_string(id)+"]: unknown flag";
    for(const auto& [k,t]:s.taken)if(!w.nodes.contains(k.first)||t>s.lastTick)return "taken: unknown node "+std::to_string(k.first);
    for(const auto* m:{&s.visits,&s.read})for(const auto& [id,v]:*m)if(!w.nodes.contains(id)||v<1)return "visits: unknown node "+std::to_string(id);
    for(const auto& p:s.pending)if(!w.consequences.contains(p.consequence))return "pending: unknown consequence "+std::to_string(p.consequence);
    if(s.pending.size()>c.maxPending||s.backlog.size()>c.backlogLimit)return "pending: exceeds configured limits";
    for(const auto& b:s.backlog)if(!w.nodes.contains(b.node))return "backlog: unknown node "+std::to_string(b.node);
    for(const auto& [id,t]:s.barks.lastBark){(void)t;if(!w.barks.contains(id))return "barks: unknown bark "+std::to_string(id);}
    if(s.chapter&&!w.chapters.contains(s.chapter))return "chapter: unknown chapter "+std::to_string(s.chapter);
    if(s.inScene&&(!w.scenes.contains(s.scene)||!w.nodes.contains(s.node)))return "scene: unknown scene or node";
    if(s.scene&&!w.scenes.contains(s.scene))return "scene: unknown scene "+std::to_string(s.scene);
    return {};
}
}
