#pragma once
#include "engine/mechanics/Namespace.h"
// quest-journal: quests as data with stages, counted objectives (optional ones too), branches chosen by the player or
// by world facts, prerequisites, time limits, failure, abandon/restart rules, a tracked quest and a bounded journal of
// entries for the UI ("quest started", "stage reached", "objective 3/5", "completed", "failed"). The owner reports
// game events as (key, amount) pairs - kills of a species, items collected, places reached, dialogue lines - and the
// policy advances every active quest that listens to them in ascending quest id. Versioned definitions migrate saved
// progress: stages are matched by id, counters of removed objectives are dropped, a missing stage falls back to
// `fallbackStage`. Pair with narrative-graphs (dialogue, facts) and objectives (map markers). Record kind 207.
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct QuestObjective {int64_t id=0,eventKey=0,target=1;bool optional=false;};
struct QuestBranch {int64_t choice=0,nextStage=0;};          // nextStage 0 = complete the quest
struct QuestStage {int64_t id=0;std::vector<QuestObjective> objectives;std::vector<QuestBranch> branches;int64_t nextStage=0;   // used when there are no branches
    uint32_t timeLimitTicks=0;};                             // 0 = no limit; expiry fails the quest
struct QuestDef {int64_t id=0;uint32_t version=1;std::vector<QuestStage> stages;std::vector<int64_t> prerequisites;bool repeatable=false,canAbandon=true;int64_t fallbackStage=0;};
using QuestDefs=std::map<int64_t,QuestDef>;
enum class QuestStatus:uint8_t {Active,Completed,Failed,Abandoned};
struct QuestProgress {QuestStatus status=QuestStatus::Active;int64_t stage=0;uint32_t version=1,stageStart=0,completions=0;std::map<int64_t,int64_t> counters;};
enum class JournalKind:uint8_t {Started,StageReached,ObjectiveProgress,ObjectiveDone,Completed,Failed,Abandoned,Migrated};
struct JournalEntry {uint32_t tick=0;int64_t quest=0,stage=0,objective=0,value=0;JournalKind kind{};};
struct QuestLog {std::map<int64_t,QuestProgress> quests;std::vector<JournalEntry> journal;int64_t tracked=0;uint32_t lastTick=0;bool initialized=false;};
enum class QuestCommandKind:uint8_t {Start,Abandon,Fail,Choose,Track};
struct QuestCommand {QuestCommandKind kind{};int64_t quest=0,choice=0;};
struct QuestEvent {int64_t key=0,amount=1;};
inline constexpr std::size_t kJournalMax=512;

inline const QuestStage* findStage(const QuestDef& q,int64_t id){for(auto& s:q.stages)if(s.id==id)return &s;return nullptr;}
inline bool valid(const QuestDef& q){
    if(q.id<=0||q.version==0||q.stages.empty()||q.stages.size()>256)return false;std::set<int64_t> ids;
    for(auto& s:q.stages){if(s.id<=0||!ids.insert(s.id).second||s.objectives.size()>64)return false;std::set<int64_t> o;
        for(auto& x:s.objectives)if(x.id<=0||x.eventKey<=0||x.target<1||!o.insert(x.id).second)return false;}
    for(auto& s:q.stages){if(s.nextStage&&!ids.contains(s.nextStage))return false;for(auto& b:s.branches)if(b.choice<=0||(b.nextStage&&!ids.contains(b.nextStage)))return false;}
    return !q.fallbackStage||ids.contains(q.fallbackStage);
}
inline bool valid(const QuestDefs& d){for(auto& [id,q]:d){if(id!=q.id||!valid(q))return false;for(auto r:q.prerequisites)if(!d.contains(r)||r==id)return false;}return true;}
inline bool stageDone(const QuestStage& s,const QuestProgress& p){for(auto& o:s.objectives)if(!o.optional){auto it=p.counters.find(o.id);if(it==p.counters.end()||it->second<o.target)return false;}return true;}

inline std::optional<std::vector<JournalEntry>> stepQuests(const QuestDefs& defs,QuestLog& log,uint32_t tick,std::span<const QuestCommand> commands,std::span<const QuestEvent> events){
    if(!valid(defs)||!policyTickOk(log.initialized,log.lastTick,tick)||commands.size()>kPolicyMaxCommands||events.size()>kPolicyMaxCommands)return std::nullopt;
    auto s=log;s.initialized=true;s.lastTick=tick;std::vector<JournalEntry> out;auto note=[&](JournalEntry e){e.tick=tick;out.push_back(e);};
    auto enter=[&](int64_t q,QuestProgress& p,int64_t stage){p.stage=stage;p.stageStart=tick;p.counters.clear();note({0,q,stage,0,0,JournalKind::StageReached});};
    auto finish=[&](int64_t q,QuestProgress& p,QuestStatus st){p.status=st;++p.completions;note({0,q,p.stage,0,0,st==QuestStatus::Completed?JournalKind::Completed:st==QuestStatus::Failed?JournalKind::Failed:JournalKind::Abandoned});
        if(s.tracked==q)s.tracked=0;};
    for(auto& c:commands){auto d=defs.find(c.quest);if(d==defs.end())return std::nullopt;auto it=s.quests.find(c.quest);
        switch(c.kind){
        case QuestCommandKind::Start:{
            bool can=it==s.quests.end()||(it->second.status!=QuestStatus::Active&&(d->second.repeatable||it->second.status==QuestStatus::Abandoned));
            for(auto r:d->second.prerequisites){auto x=s.quests.find(r);if(x==s.quests.end()||x->second.status!=QuestStatus::Completed)can=false;}
            if(!can)continue;QuestProgress p;p.version=d->second.version;if(it!=s.quests.end())p.completions=it->second.completions;
            note({0,c.quest,0,0,0,JournalKind::Started});enter(c.quest,p,d->second.stages.front().id);s.quests[c.quest]=p;if(!s.tracked)s.tracked=c.quest;break;}
        case QuestCommandKind::Abandon:if(it!=s.quests.end()&&it->second.status==QuestStatus::Active&&d->second.canAbandon)finish(c.quest,it->second,QuestStatus::Abandoned);break;
        case QuestCommandKind::Fail:if(it!=s.quests.end()&&it->second.status==QuestStatus::Active)finish(c.quest,it->second,QuestStatus::Failed);break;
        case QuestCommandKind::Track:if(it!=s.quests.end()&&it->second.status==QuestStatus::Active)s.tracked=c.quest;break;
        case QuestCommandKind::Choose:{
            if(it==s.quests.end()||it->second.status!=QuestStatus::Active)continue;auto& p=it->second;const auto* st=findStage(d->second,p.stage);
            if(!st||!stageDone(*st,p))continue;for(auto& b:st->branches)if(b.choice==c.choice){if(b.nextStage)enter(c.quest,p,b.nextStage);else finish(c.quest,p,QuestStatus::Completed);break;}
            break;}
        default:return std::nullopt;}
    }
    for(auto& [q,p]:s.quests){if(p.status!=QuestStatus::Active)continue;const auto& def=defs.at(q);const auto* st=findStage(def,p.stage);if(!st)return std::nullopt;
        for(auto& e:events)for(auto& o:st->objectives)if(o.eventKey==e.key&&e.amount>0){auto& v=p.counters[o.id];if(v>=o.target)continue;v=std::min(o.target,v+e.amount);
            note({0,q,p.stage,o.id,v,v>=o.target?JournalKind::ObjectiveDone:JournalKind::ObjectiveProgress});}
        if(st->timeLimitTicks&&tick-p.stageStart>=st->timeLimitTicks&&!stageDone(*st,p)){finish(q,p,QuestStatus::Failed);continue;}
        if(stageDone(*st,p)&&st->branches.empty()){if(st->nextStage)enter(q,p,st->nextStage);else finish(q,p,QuestStatus::Completed);}
    }
    for(auto& e:out)s.journal.push_back(e);if(s.journal.size()>kJournalMax)s.journal.erase(s.journal.begin(),s.journal.begin()+std::ptrdiff_t(s.journal.size()-kJournalMax));
    log=std::move(s);return out;
}
// Bring saved progress to the current definitions (call after loading, before the first step).
inline bool migrateQuests(const QuestDefs& defs,QuestLog& log,uint32_t tick){
    if(!valid(defs))return false;auto s=log;
    for(auto& [q,p]:s.quests){auto d=defs.find(q);if(d==defs.end())return false;if(p.version==d->second.version)continue;
        const auto* st=findStage(d->second,p.stage);
        if(!st){if(p.status==QuestStatus::Active){p.stage=d->second.fallbackStage?d->second.fallbackStage:d->second.stages.front().id;p.counters.clear();p.stageStart=tick;}}
        else{std::map<int64_t,int64_t> keep;for(auto& o:st->objectives)if(auto it=p.counters.find(o.id);it!=p.counters.end())keep[o.id]=std::min(it->second,o.target);p.counters=std::move(keep);}
        p.version=d->second.version;s.journal.push_back({tick,q,p.stage,0,int64_t(p.version),JournalKind::Migrated});}
    if(s.journal.size()>kJournalMax)s.journal.erase(s.journal.begin(),s.journal.begin()+std::ptrdiff_t(s.journal.size()-kJournalMax));
    log=std::move(s);return true;
}
inline void io(Archive& a,QuestProgress& p){a(p.status,p.stage,p.version,p.stageStart,p.completions,p.counters);}
inline void io(Archive& a,JournalEntry& e){a(e.tick,e.quest,e.stage,e.objective,e.value,e.kind);}
inline void io(Archive& a,QuestLog& l){a(l.quests,l.journal,l.tracked,l.lastTick,l.initialized);}
inline uint16_t stateKind(const QuestLog&){return 207;}
inline bool valid(const QuestLog& l){if(!policyPositiveKeys(l.quests)||l.journal.size()>kJournalMax||(l.tracked&&!l.quests.contains(l.tracked)))return false;
    for(auto& [q,p]:l.quests)if(uint8_t(p.status)>3||p.stage<=0||p.version==0)return false;return true;}
}
