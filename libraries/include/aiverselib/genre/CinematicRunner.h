#pragma once
// stepScene: one authoritative tick of the cinematic scene runner (see CinematicScene.h for the model).
// Order inside a tick: options, commands (by seq; seq <= lastCommand ignored), host reports (line finished /
// markers, validated against cue emission and the current tick), then unless paused: start a pending scene
// (triggered on an earlier tick), staging (poses/arrivals, timeouts, fallbacks), QTE judging, sync waits,
// timeline advance by the scene domain's steps. Paused ticks shift every tick-anchored deadline (gap ticks keep
// the previous pause state, the current tick takes the new one).
#include "CinematicScene.h"
namespace aiverse::lib::genre {
namespace detail_scene {
struct Run {
    const SceneLibrary& lib;SceneState& s;SceneReport& r;uint32_t tick;const SceneDef* def=nullptr;std::vector<SceneStop> stops;bool startedNow=false;
    void bind(){def=s.scene?&lib.scenes.at(s.scene):nullptr;stops=def?sceneStops(*def):std::vector<SceneStop>{};}
    int64_t entity(int64_t p)const{auto it=s.participants.find(p);return it==s.participants.end()?0:it->second.entity;}
    void event(SceneEventKind k,int64_t id=0,int64_t ref=0,int64_t actor=0,int64_t value=0,uint8_t flags=0){r.events.push_back({k,s.scene,id,ref,actor,entity(actor),value,flags});}
    void cancelTime(){if(s.scene&&std::find(r.timeCancelTags.begin(),r.timeCancelTags.end(),s.scene)==r.timeCancelTags.end())r.timeCancelTags.push_back(s.scene);}
    bool playing()const{return s.phase==ScenePhase::Playing||s.phase==ScenePhase::Waiting||s.phase==ScenePhase::Qte;}
    void lock(int64_t p,bool on){auto& ps=s.participants.at(p);if(ps.locked==on)return;ps.locked=on;event(on?SceneEventKind::InputLocked:SceneEventKind::InputReleased,p,0,p);}
    void teleport(int64_t p){auto& ps=s.participants.at(p);ps.status=StageStatus::Teleported;event(SceneEventKind::TeleportRequested,p,ps.mark,p);}
    void moveTo(int64_t p,int64_t mark,bool skipped){
        auto& ps=s.participants.at(p);ps.mark=mark;
        if(skipped){teleport(p);return;}
        ps.status=StageStatus::Moving;ps.deadline=tick+def->participants.at(p).timeout;event(SceneEventKind::MoveToMark,p,mark,p);
    }
    void emitCue(const CueDef& c,bool skipped){
        if(cueReportable(c.kind))s.cues[c.id]={tick,-1,skipped,{}};
        event(SceneEventKind::Cue,c.id,c.ref,c.actor,int64_t(c.kind),skipped?1:0);
        switch(c.kind){
        case CueKind::TimeScale:if(!skipped){auto q=lib.presets.at(c.ref).request;q.tag=s.scene;r.timeAdd.push_back(q);}break;
        case CueKind::Move:moveTo(c.actor,c.ref,skipped);break;
        case CueKind::InputLock:case CueKind::InputRelease:lock(c.actor,c.kind==CueKind::InputLock);break;
        default:break;
        }
    }
    // Process cues with at < limit in (at, track, id) order. Essential cues fire exactly once per scene run (also
    // while skipping, flagged); presentation cues fire once per pass and are recorded as skipped when skipped.
    void processBelow(uint64_t limit,bool skipping){
        while(s.cursor<def->cues.size()&&def->cues[s.cursor].at<limit){
            const auto& c=def->cues[s.cursor++];
            if(cueEssential(c)){
                auto at=std::lower_bound(s.essentialDone.begin(),s.essentialDone.end(),c.id);if(at!=s.essentialDone.end()&&*at==c.id)continue;
                s.essentialDone.insert(at,c.id);emitCue(c,skipping);
            }else if(skipping){if(cueReportable(c.kind))s.cues[c.id]={tick,-1,true,{}};++r.cuesSkipped;}
            else emitCue(c,false);
        }
    }
    bool waitMet(const WaitDef& w)const{
        if(w.kind==WaitKind::InPosition){
            auto ok=[&](int64_t p){auto st=s.participants.at(p).status;return st==StageStatus::InPosition||st==StageStatus::Teleported;};
            if(w.participants.empty()){for(const auto& [id,p]:s.participants){(void)p;if(!ok(id))return false;}return true;}
            return std::all_of(w.participants.begin(),w.participants.end(),ok);
        }
        auto it=s.cues.find(w.cue);if(it==s.cues.end())return false;const auto& rec=it->second;
        return rec.skipped||rec.finishedAt>=0||(w.kind==WaitKind::Marker&&std::binary_search(rec.markers.begin(),rec.markers.end(),w.marker));
    }
    void checkSync(){
        const auto& sync=def->syncs[stops[s.nextStop].index];
        bool met=std::all_of(sync.waits.begin(),sync.waits.end(),[&](const auto& w){return waitMet(w);});
        if(!met&&tick<s.deadline)return;
        event(SceneEventKind::SyncReleased,sync.id,0,0,met?0:1);s.phase=ScenePhase::Playing;++s.nextStop;
    }
    void activate(std::size_t i){
        const auto& st=stops[i];s.stopAt=tick;
        if(!st.qte){
            const auto& sync=def->syncs[st.index];s.phase=ScenePhase::Waiting;s.deadline=tick+sync.timeout;s.lastSync=uint32_t(i+1);
            event(SceneEventKind::SyncReached,sync.id,0,0,sync.at);if(sync.checkpoint)event(SceneEventKind::SnapshotRequested,sync.id,0,0,1);
            checkSync();return;
        }
        const auto& q=def->qtes[st.index];s.phase=ScenePhase::Qte;s.qte=startQte(q,s.options,tick);
        event(SceneEventKind::QtePrompt,q.id,q.button,0,int64_t(q.kind));if(s.options.autoSucceed)resolveQte(QteOutcome::AutoSuccess);
    }
    void finish(SceneOutcome outcome){
        for(const auto& [id,p]:def->participants){(void)p;lock(id,false);}cancelTime();
        if(outcome!=SceneOutcome::Aborted&&def->checkpointAfter)event(SceneEventKind::SnapshotRequested,0,0,0,2);
        event(outcome==SceneOutcome::Aborted?SceneEventKind::Aborted:SceneEventKind::Finished,s.scene,0,0,int64_t(outcome));
        SceneState next;next.options=s.options;next.lastCommand=s.lastCommand;next.lastTick=s.lastTick;next.initialized=s.initialized;
        next.queue=std::move(s.queue);next.results=std::move(s.results);next.lastScene=s.scene;next.lastOutcome=outcome;s=std::move(next);bind();
        if(!s.queue.empty()){auto q=s.queue.front();s.queue.erase(s.queue.begin());setPending(q.scene,q.bindings);}
    }
    void setPending(int64_t scene,const SceneBindings& bindings){
        const auto& d=lib.scenes.at(scene);s.scene=scene;s.version=d.version;s.phase=ScenePhase::Pending;s.triggeredAt=tick;s.results.clear();
        for(const auto& [id,p]:d.participants){(void)p;s.participants[id]={};}for(auto [p,e]:bindings)s.participants[p].entity=e;
        bind();event(SceneEventKind::Triggered,scene);if(d.checkpointBefore)event(SceneEventKind::SnapshotRequested,0,0,0,0);
    }
    void trigger(int64_t scene,const SceneBindings& bindings,int64_t cue){
        auto it=lib.scenes.find(scene);
        bool ok=it!=lib.scenes.end()&&validBindings(bindings)&&std::all_of(bindings.begin(),bindings.end(),[&](auto b){return it->second.participants.contains(b.first);});
        if(!ok){r.events.push_back({SceneEventKind::TriggerIgnored,scene,scene,cue});return;}
        if(s.phase==ScenePhase::Idle){setPending(scene,bindings);return;}
        if(s.queue.size()>=kSceneMaxQueue){r.events.push_back({SceneEventKind::TriggerIgnored,scene,scene,cue});return;}
        s.queue.push_back({scene,bindings});r.events.push_back({SceneEventKind::Queued,scene,scene,cue});
    }
    void play(){s.phase=ScenePhase::Playing;s.position=0;startedNow=true;settle();}
    void start(){
        event(SceneEventKind::Started,s.scene);s.phase=ScenePhase::Staging;startedNow=true;
        for(const auto& [id,p]:def->participants){if(p.lockInput)lock(id,true);if(p.mark)moveTo(id,p.mark,false);}
    }
    void stage(const SceneInput& in){
        for(const auto& [id,p]:def->participants){
            auto& ps=s.participants.at(id);if(ps.status!=StageStatus::Moving)continue;
            bool there=std::find(in.arrived.begin(),in.arrived.end(),id)!=in.arrived.end();
            for(const auto& pose:in.poses)if(pose.participant==id){
                const auto& mark=def->marks.at(ps.mark);auto d=pose.position-mark.position;if(d.x*d.x+d.y*d.y+d.z*d.z<=mark.tolerance*mark.tolerance)there=true;
            }
            if(there){ps.status=StageStatus::InPosition;event(SceneEventKind::InPosition,id,ps.mark,id);continue;}
            if(tick<ps.deadline)continue;
            if(p.fallback==StagingFallback::Teleport)teleport(id);
            else if(p.fallback==StagingFallback::Proceed){ps.status=StageStatus::Late;event(SceneEventKind::StagingLate,id,ps.mark,id);}
            else{finish(SceneOutcome::Aborted);return;}
        }
        if(s.phase==ScenePhase::Staging&&std::none_of(s.participants.begin(),s.participants.end(),[](const auto& p){return p.second.status==StageStatus::Moving;}))play();
    }
    void teleportAll(){for(auto& [id,p]:s.participants)if(p.status==StageStatus::Moving)teleport(id);}
    void rewind(std::size_t stop,uint32_t pos){
        s.position=pos;s.nextStop=uint32_t(stop);s.cursor=0;while(s.cursor<def->cues.size()&&def->cues[s.cursor].at<pos)++s.cursor;
        for(std::size_t i=s.cursor;i<def->cues.size();++i){const auto& c=def->cues[i];if(!cueEssential(c))s.cues.erase(c.id);}
        s.lastSync=0;for(std::size_t i=0;i<stop;++i)if(!stops[i].qte)s.lastSync=uint32_t(i+1);
        s.phase=ScenePhase::Playing;s.qte={};cancelTime();
    }
    void resolveQte(QteOutcome outcome){
        const auto& q=def->qtes[stops[s.nextStop].index];s.results[q.id]=outcome;s.qte={};s.phase=ScenePhase::Playing;
        event(SceneEventKind::QteResolved,q.id,q.button,0,int64_t(outcome));
        int64_t target=q.onSuccess;
        if(!qteSucceeded(outcome)){if(q.failAborts){finish(SceneOutcome::Aborted);return;}target=q.onFail;}
        if(!target){++s.nextStop;return;}
        for(std::size_t i=0;i<stops.size();++i)if(!stops[i].qte&&stops[i].id==target){
            event(SceneEventKind::Branch,target,q.id,0,int64_t(outcome));
            if(i>s.nextStop){++s.nextStop;skipForward(i,stops[i].at,false);}else rewind(i,stops[i].at); // forward jump skips, backward retries
            return;
        }
    }
    // Jump forward to stop `target` at `pos`: QTEs in between resolve as Skipped (no branch), essential cues fire
    // flagged, presentation cues are recorded as skipped (waits on them are satisfied), time requests cancelled.
    void skipForward(std::size_t target,uint32_t pos,bool inclusive){
        for(std::size_t i=s.nextStop;i<target;++i)if(stops[i].qte){auto id=stops[i].id;s.results[id]=QteOutcome::Skipped;event(SceneEventKind::QteResolved,id,0,0,int64_t(QteOutcome::Skipped));}
        s.qte={};s.phase=ScenePhase::Playing;teleportAll();processBelow(uint64_t(pos)+(inclusive?1:0),true);s.position=pos;s.nextStop=uint32_t(target);cancelTime();settle();
    }
    void settle(){
        while(s.phase==ScenePhase::Playing){
            if(s.nextStop<stops.size()&&stops[s.nextStop].at<=s.position){processBelow(stops[s.nextStop].at,false);activate(s.nextStop);continue;}
            processBelow(uint64_t(s.position)+1,false);
            if(s.position>=def->length&&s.nextStop>=stops.size())finish(s.skippedScene?SceneOutcome::Skipped:SceneOutcome::Completed);
            return;
        }
    }
    void advance(uint64_t steps){
        while(steps>0&&s.phase==ScenePhase::Playing){
            uint32_t limit=s.nextStop<stops.size()?std::min(stops[s.nextStop].at,def->length):def->length;
            uint64_t move=std::min<uint64_t>(steps,limit-s.position);s.position+=uint32_t(move);steps-=move;settle();if(!move)break;
        }
    }
    std::optional<std::size_t> nextSync(std::size_t from)const{for(auto i=from;i<stops.size();++i)if(!stops[i].qte)return i;return {};}
    void command(const SceneCommand& c){
        auto ignore=[&]{r.events.push_back({SceneEventKind::CommandIgnored,s.scene,int64_t(c.seq),c.value,0,0,int64_t(c.kind)});};
        bool active=s.phase!=ScenePhase::Idle;
        switch(c.kind){
        case SceneCommandKind::TriggerScene:trigger(c.value,c.bindings,0);return;
        case SceneCommandKind::TriggerCue:{auto scene=sceneForCue(lib,c.value);if(!scene){r.events.push_back({SceneEventKind::TriggerIgnored,0,0,c.value});return;}trigger(scene,c.bindings,c.value);return;}
        case SceneCommandKind::Pause:if(!active||s.paused)return ignore();s.paused=true;event(SceneEventKind::Paused);return;
        case SceneCommandKind::Unpause:if(!s.paused)return ignore();s.paused=false;event(SceneEventKind::Unpaused);return;
        case SceneCommandKind::Abort:if(!active)return ignore();finish(SceneOutcome::Aborted);return;
        default:break;
        }
        if(!active||s.paused||s.phase==ScenePhase::Pending)return ignore();
        if(c.kind==SceneCommandKind::ResumeCheckpoint){
            if(s.phase==ScenePhase::Staging){for(auto& [id,p]:s.participants)if(p.status==StageStatus::Moving)moveTo(id,p.mark,false);event(SceneEventKind::Resumed);return;}
            int64_t sync=s.lastSync?stops[s.lastSync-1].id:0;
            if(s.lastSync)rewind(s.lastSync-1,stops[s.lastSync-1].at);else rewind(0,0);
            event(SceneEventKind::Resumed,sync,0,0,s.position);settle();return;
        }
        if((c.kind==SceneCommandKind::Skip||c.kind==SceneCommandKind::SkipScene)&&!def->skippable)return ignore();
        if(c.kind==SceneCommandKind::Skip||c.kind==SceneCommandKind::SkipScene){
            if(s.phase==ScenePhase::Staging){
                teleportAll();
                if(c.kind==SceneCommandKind::Skip){event(SceneEventKind::Skipped);play();return;}
                s.phase=ScenePhase::Playing;s.position=0;
            }
            std::optional<std::size_t> to;
            if(c.kind==SceneCommandKind::Skip)to=nextSync(s.phase==ScenePhase::Playing?s.nextStop:s.nextStop+1);
            event(SceneEventKind::Skipped,to?stops[*to].id:0);
            if(to)skipForward(*to,stops[*to].at,false);else{s.skippedScene=true;skipForward(stops.size(),def->length,true);}
            return;
        }
        if(c.kind==SceneCommandKind::Seek){
            if(s.phase==ScenePhase::Staging||c.value<0||c.value>int64_t(def->length))return ignore();
            auto pos=uint32_t(c.value);bool forward=pos>s.position||(pos==s.position&&s.phase==ScenePhase::Playing);
            if(forward&&!def->skippable)return ignore();
            std::size_t j=forward?s.nextStop:0;while(j<stops.size()&&stops[j].at<pos)++j;
            event(SceneEventKind::Seeked,0,0,0,pos);
            if(forward)skipForward(j,pos,false);else{rewind(j,pos);settle();}
        }
    }
    void report(const SceneInput& in){
        auto check=[&](int64_t cue,uint32_t at,bool marker)->CueRecord*{
            auto c=def?findCue(*def,cue):nullptr;auto it=s.cues.find(cue);
            if(!c||(marker&&c->kind!=CueKind::Animation)||it==s.cues.end()||it->second.skipped||at<it->second.emittedAt||at>tick){
                r.events.push_back({SceneEventKind::ReportIgnored,s.scene,cue,0,0,0,marker?1:0});return nullptr;
            }
            return &it->second;
        };
        for(const auto& f:in.finished)if(auto rec=check(f.cue,f.tick,false)){if(rec->finishedAt<0)rec->finishedAt=f.tick;}
        for(const auto& m:in.markers)if(auto rec=check(m.cue,m.tick,true)){auto at=std::lower_bound(rec->markers.begin(),rec->markers.end(),m.marker);if(at==rec->markers.end()||*at!=m.marker)rec->markers.insert(at,m.marker);}
    }
    void pausedShift(uint32_t delta){
        if(!delta)return;
        if(s.phase==ScenePhase::Waiting)s.deadline+=delta;
        if(s.phase==ScenePhase::Qte)shiftQte(s.qte,delta);
        for(auto& [id,p]:s.participants){(void)id;if(p.status==StageStatus::Moving)p.deadline+=delta;}
    }
};
inline bool validInput(const SceneState& s,uint32_t tick,const SceneInput& in){
    if(in.commands.size()>kSceneMaxInput||in.finished.size()>kSceneMaxInput||in.markers.size()>kSceneMaxInput||in.poses.size()>kSceneMaxActors||
       in.arrived.size()>kSceneMaxActors||in.qte.size()>kSceneMaxInput||in.domainSteps.size()>kTimeMaxDomains||(in.options&&!valid(*in.options)))return false;
    std::vector<uint64_t> seqs;
    for(const auto& c:in.commands){if(!c.seq||uint8_t(c.kind)>uint8_t(SceneCommandKind::ResumeCheckpoint)||!validBindings(c.bindings))return false;seqs.push_back(c.seq);}
    std::sort(seqs.begin(),seqs.end());if(std::adjacent_find(seqs.begin(),seqs.end())!=seqs.end())return false;
    for(const auto& f:in.finished)if(f.cue<=0)return false;
    for(const auto& m:in.markers)if(m.cue<=0||m.marker<=0)return false;
    std::vector<int64_t> ids;for(const auto& p:in.poses){if(p.participant<=0||!sceneBounded(p.position))return false;ids.push_back(p.participant);}
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return false;
    for(auto a:in.arrived)if(a<=0)return false;
    uint32_t previous=s.initialized?s.lastTick+1:0;
    for(const auto& q:in.qte){if(q.button<=0||q.tick<previous||q.tick>tick)return false;previous=q.tick;}
    for(auto [d,n]:in.domainSteps)if(d<=0||n>uint64_t(kTimeMaxScale)*UINT32_MAX)return false;
    return true;
}
}
inline std::optional<SceneReport> stepScene(const SceneLibrary& lib,SceneState& state,uint32_t tick,const SceneInput& in){
    if((state.initialized&&tick<=state.lastTick)||!detail_scene::validInput(state,tick,in)||!compatible(lib,state))return {};
    auto s=state;SceneReport report;detail_scene::Run run{lib,s,report,tick,nullptr,{},false};run.bind();
    uint32_t delta=s.initialized?tick-s.lastTick:1;s.lastTick=tick;s.initialized=true;bool wasPaused=s.paused;
    if(in.options)s.options=*in.options;
    auto commands=in.commands;std::sort(commands.begin(),commands.end(),[](const auto& a,const auto& b){return a.seq<b.seq;});
    for(const auto& c:commands){
        if(c.seq<=s.lastCommand){report.events.push_back({SceneEventKind::CommandIgnored,s.scene,int64_t(c.seq),c.value,0,0,int64_t(c.kind)});continue;}
        s.lastCommand=c.seq;run.command(c);
    }
    run.report(in);
    // Gap ticks keep the previous pause state; this tick takes the new one.
    run.pausedShift((wasPaused?delta-1:0)+(s.paused?1:0));
    if(!s.paused){
        if(s.phase==ScenePhase::Pending&&tick>s.triggeredAt)run.start();
        if(s.phase==ScenePhase::Staging||run.playing())run.stage(in);
        if(s.phase==ScenePhase::Qte){
            const auto& q=run.def->qtes[run.stops[s.nextStop].index];auto outcome=judgeQte(q,s.qte,std::span<const QteInput>(in.qte),tick);
            if(outcome!=QteOutcome::None)run.resolveQte(outcome);
        }
        if(s.phase==ScenePhase::Waiting)run.checkSync();
        if(s.phase==ScenePhase::Playing)run.settle();
        if(s.phase==ScenePhase::Playing&&!run.startedNow){
            uint64_t steps=run.def->domain?0:delta;
            if(run.def->domain){auto it=in.domainSteps.find(run.def->domain);if(it!=in.domainSteps.end())steps=it->second;}
            run.advance(steps);
        }
    }
    state=std::move(s);return report;
}
}
