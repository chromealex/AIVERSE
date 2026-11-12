#pragma once
// settlement-production: one authoritative colony tick (stepColony) plus setup helpers.
// Order inside a tick: commands (ascending seq) -> owner facts (incapacitation, alerts raised/cleared) ->
// need decay/restoration -> mood bands and breaks -> activity choice per colonist (ascending id: critical needs,
// alert response, schedule block) -> job generation (bills, hauls) -> assignment (preferences 1..4, effective
// priority = base + active alert boosts, then lowest job id; reservations taken on claim) -> work progress and
// completion. Strictly increasing ticks; malformed input returns nullopt with the state untouched.
#include "ColonyTypes.h"
#include <span>
namespace aiverse::lib::genre {
namespace detail_colony {
inline void emit(ColonyReport& r,ColonyEventKind k,int64_t colonist,int64_t job,int64_t id,int64_t value){r.events.push_back({k,colonist,job,id,value});}
inline ColonyMoodBand band(const ColonyConfig& c,int64_t mood){
    if(mood<c.breakAt)return ColonyMoodBand::Breaking;
    if(mood<c.lowAt)return ColonyMoodBand::Low;
    if(mood>=c.highAt)return ColonyMoodBand::High;
    return ColonyMoodBand::Content;
}
inline int64_t bandSpeed(const ColonyConfig& c,ColonyMoodBand b){
    switch(b){case ColonyMoodBand::Breaking:return 0;case ColonyMoodBand::Low:return c.lowSpeedMilli;case ColonyMoodBand::High:return c.highSpeedMilli;default:return c.contentSpeedMilli;}
}
inline systems::Quantities jobNeeds(const ColonyDefs& d,const ColonyJob& j){
    switch(j.kind){
    case ColonyJobKind::Recipe:return d.recipes.at(j.ref).inputs;
    case ColonyJobKind::Build:return d.blueprints.at(j.ref).cost;
    case ColonyJobKind::Haul:return {{d.hauls.at(j.ref).resource,j.amount}};
    default:return {};
    }
}
inline bool reservable(const ColonyStockpile& p,const systems::Quantities& q){for(auto [id,n]:q)if(colonyAvailable(p,id)<n)return false;return true;}
inline int64_t room(const ColonyStockpile& p){return p.capacity==0?kColonyAmountLimit:std::max<int64_t>(0,p.capacity-colonyTotal(p));}
inline void addStock(ColonyStockpile& p,int64_t resource,int64_t n){if(n>0)p.stock[resource]=std::min(kColonyAmountLimit,colonyStock(p,resource)+n);}
inline int64_t sum(const systems::Quantities& q){int64_t n=0;for(auto [id,v]:q){(void)id;n+=v;}return n;}
inline int64_t createJob(ColonyState& s,ColonyReport& r,ColonyJob j,int64_t tick){
    j.task.id=s.nextJob++;j.task.status=TaskStatus::Pending;j.task.worker=0;j.createdAt=tick;const auto id=j.task.id;
    emit(r,ColonyEventKind::JobCreated,0,id,j.ref,int64_t(j.kind));s.jobs.emplace(id,std::move(j));return id;
}
// Unassigns the worker and frees reserved material; progress stays with the job.
inline void release(ColonyState& s,ColonyJob& j,ColonyReport& r){
    if(j.task.status!=TaskStatus::Active)return;
    auto worker=j.task.worker;releaseWorker(std::span<Task>(&j.task,1),worker);
    if(j.from)if(auto p=s.stockpiles.find(j.from);p!=s.stockpiles.end())(void)systems::release(p->second.reserved,j.task.id,worker);
    if(auto c=s.colonists.find(worker);c!=s.colonists.end()&&c->second.job==j.task.id)c->second.job=0;
    emit(r,ColonyEventKind::JobReleased,worker,j.task.id,j.ref,int64_t(j.kind));
}
inline void releaseColonist(ColonyState& s,ColonyColonist& c,ColonyReport& r){if(c.job)if(auto it=s.jobs.find(c.job);it!=s.jobs.end())release(s,it->second,r);c.job=0;}
inline void cancel(ColonyState& s,int64_t id,ColonyReport& r){
    auto it=s.jobs.find(id);if(it==s.jobs.end())return;release(s,it->second,r);
    for(auto& [wid,w]:s.workstations){(void)wid;if(w.job==id)w.job=0;}
    s.jobs.erase(it);
}
inline bool applyCommand(const ColonyDefs& d,ColonyState& s,const ColonyCommand& c,ColonyReport& r,int64_t tick){
    if(!colonyCommandValid(c))return false;
    switch(c.kind){
    case ColonyCommandKind::SetPriority:{
        auto it=s.colonists.find(c.colonist);if(it==s.colonists.end()||!d.workTypes.contains(c.ref))return false;
        it->second.priorities[c.ref]=c.value;
        if(c.value==0&&it->second.job&&s.jobs.at(it->second.job).workType==c.ref)releaseColonist(s,it->second,r);
        return true;}
    case ColonyCommandKind::SetSchedule:{
        auto it=s.colonists.find(c.colonist);if(it==s.colonists.end())return false;it->second.schedule[std::size_t(c.target)]=ColonyBlock(c.value);return true;}
    case ColonyCommandKind::SetBill:{
        auto w=s.workstations.find(c.target);auto rec=d.recipes.find(c.ref);
        if(w==s.workstations.end()||rec==d.recipes.end()||rec->second.workstationType!=w->second.type)return false;
        if(w->second.job&&w->second.recipe!=c.ref)cancel(s,w->second.job,r);
        w->second.recipe=c.ref;w->second.target=c.value;return true;}
    case ColonyCommandKind::ClearBill:{
        auto w=s.workstations.find(c.target);if(w==s.workstations.end())return false;
        if(w->second.job)cancel(s,w->second.job,r);
        w->second.recipe=0;w->second.target=0;return true;}
    case ColonyCommandKind::OrderBuild:{
        auto b=d.blueprints.find(c.ref);if(b==d.blueprints.end()||!s.stockpiles.contains(c.stockpile)||s.jobs.size()>=kColonyMaxJobs)return false;
        ColonyJob j;j.kind=ColonyJobKind::Build;j.workType=b->second.workType;j.ref=c.ref;j.from=c.stockpile;j.work=b->second.work;createJob(s,r,j,tick);return true;}
    case ColonyCommandKind::OrderGather:{
        auto g=d.gathers.find(c.ref);if(g==d.gathers.end()||!s.stockpiles.contains(c.stockpile)||s.jobs.size()>=kColonyMaxJobs)return false;
        ColonyJob j;j.kind=ColonyJobKind::Gather;j.workType=g->second.workType;j.ref=c.ref;j.node=c.target;j.to=c.stockpile;j.amount=g->second.yield;j.work=g->second.work;createJob(s,r,j,tick);return true;}
    case ColonyCommandKind::CancelJob:{
        auto it=s.jobs.find(c.target);if(it==s.jobs.end()||(it->second.kind!=ColonyJobKind::Build&&it->second.kind!=ColonyJobKind::Gather))return false;
        cancel(s,c.target,r);return true;}
    }
    return false;
}
inline bool validInput(const ColonyDefs& d,const ColonyState& s,const ColonyInput& in){
    if(in.commands.size()>4096||in.raise.size()>1024||in.clear.size()>1024||in.unpowered.size()>65536||in.gatherAvailable.size()>65536)return false;
    for(const auto& a:in.raise)if(!d.alerts.contains(a.kind)||a.severity<1||a.severity>1000000||a.ref<0)return false;
    for(auto [c,m]:in.moodModifiers)if(!s.colonists.contains(c)||m< -kColonyMoodLimit||m>kColonyMoodLimit)return false;
    for(auto [c,v]:in.incapacitated){(void)v;if(!s.colonists.contains(c))return false;}
    for(auto [n,v]:in.gatherAvailable)if(n<=0||v<0||v>kColonyAmountLimit)return false;
    return std::is_sorted(in.unpowered.begin(),in.unpowered.end());
}
inline int64_t alertBoost(const ColonyDefs& d,const ColonyState& s,int64_t workType){
    int64_t b=0;for(const auto& [id,a]:s.alerts){(void)id;const auto& def=d.alerts.at(a.kind);if(def.workType==workType)b+=def.priority;}return b;
}
inline bool responding(const ColonyDefs& d,const ColonyState& s,const ColonyColonist& c,bool scheduleOverride){
    for(const auto& [id,a]:s.alerts){(void)id;const auto& def=d.alerts.at(a.kind);
        if((!scheduleOverride||def.ignoreSchedule)&&colonyPriority(d,c,def.workType)>0)return true;}
    return false;
}
// Stockpile with food for a meal (lowest id with an unreserved unit).
inline ColonyStockpile* foodFor(ColonyState& s,int64_t resource){
    for(auto& [id,p]:s.stockpiles){(void)id;if(colonyAvailable(p,resource)>=1)return &p;}return nullptr;
}
}

// ---- Setup helpers (owner, before or between ticks).
// Hours [sleepFrom, sleepTo) sleep and [leisureFrom, leisureTo) leisure (wrapping past midnight, 24 = 0, -1 = none), others work.
inline std::array<ColonyBlock,kColonyHours> colonySchedule(int sleepFrom,int sleepTo,int leisureFrom,int leisureTo){
    std::array<ColonyBlock,kColonyHours> out{};out.fill(ColonyBlock::Work);
    auto mark=[&](int from,int to,ColonyBlock b){if(from<0||to<0)return;from%=kColonyHours;to%=kColonyHours;for(int h=from;h!=to;h=(h+1)%kColonyHours)out[std::size_t(h)]=b;};
    mark(leisureFrom,leisureTo,ColonyBlock::Leisure);mark(sleepFrom,sleepTo,ColonyBlock::Sleep);return out;
}
inline bool colonyAddColonist(const ColonyDefs& d,ColonyState& s,int64_t id,const std::array<ColonyBlock,kColonyHours>& schedule){
    if(id<=0||s.colonists.contains(id)||s.colonists.size()>=kColonyMaxColonists)return false;
    ColonyColonist c;c.id=id;c.schedule=schedule;for(const auto& [nid,n]:d.needs)c.needs[nid]=n.initial;c.mood=d.config.moodBase;c.band=detail_colony::band(d.config,c.mood);
    s.colonists.emplace(id,std::move(c));return true;
}
inline bool colonyAddStockpile(ColonyState& s,int64_t id,int64_t capacity,systems::Quantities stock={}){
    if(id<=0||s.stockpiles.contains(id)||capacity<0||capacity>kColonyAmountLimit||!colonyQuantitiesValid(stock,true))return false;
    s.stockpiles.emplace(id,ColonyStockpile{id,capacity,std::move(stock),{}});return true;
}
inline bool colonyAddWorkstation(ColonyState& s,int64_t id,int64_t type,int64_t stockpile,bool needsPower){
    if(id<=0||type<=0||s.workstations.contains(id)||!s.stockpiles.contains(stockpile))return false;
    s.workstations.emplace(id,ColonyWorkstation{id,type,stockpile,0,0,0,needsPower});return true;
}
// Owner removed a colonist (death, departure): frees its job and reservations.
inline void colonyRemoveColonist(ColonyState& s,int64_t id){
    auto it=s.colonists.find(id);if(it==s.colonists.end())return;ColonyReport ignored;detail_colony::releaseColonist(s,it->second,ignored);s.colonists.erase(it);
}

inline std::optional<ColonyReport> stepColony(const ColonyDefs& d,ColonyState& state,int64_t tick,const ColonyInput& in){
    using namespace detail_colony;
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(d)||!valid(state)||!validInput(d,state,in))return std::nullopt;
    for(const auto& [id,c]:state.colonists)for(auto [n,v]:c.needs){(void)v;if(!d.needs.contains(n))return std::nullopt;(void)id;}
    auto s=state;ColonyReport r;const auto& cfg=d.config;
    // 1. Commands: one validation path for players and bots.
    for(const auto& c:in.commands){
        if(c.seq<=s.lastCommand||!applyCommand(d,s,c,r,tick)){emit(r,ColonyEventKind::CommandIgnored,c.colonist,0,int64_t(c.kind),c.seq);if(c.seq>s.lastCommand)s.lastCommand=c.seq;continue;}
        s.lastCommand=c.seq;
    }
    // 2. Owner facts.
    for(auto [id,v]:in.incapacitated)s.colonists.at(id).incapacitated=v;
    for(auto id:in.clear){
        auto it=s.alerts.find(id);if(it==s.alerts.end())continue;
        std::vector<int64_t> drop;for(const auto& [jid,j]:s.jobs)if(j.alert==id)drop.push_back(jid);for(auto j:drop)cancel(s,j,r);
        emit(r,ColonyEventKind::AlertResolved,0,0,id,it->second.ref);s.alerts.erase(it);
    }
    for(const auto& a:in.raise){
        if(s.alerts.size()>=kColonyMaxDefs)break;
        const auto& def=d.alerts.at(a.kind);const auto id=s.nextAlert++;s.alerts.emplace(id,ColonyAlert{id,a.kind,a.ref,a.severity,tick,0});
        emit(r,ColonyEventKind::AlertRaised,0,0,id,a.ref);
        for(int64_t i=0;i<std::min(a.severity,cfg.maxResponseJobs)&&s.jobs.size()<kColonyMaxJobs;++i){
            ColonyJob j;j.kind=ColonyJobKind::Response;j.workType=def.workType;j.ref=a.kind;j.alert=id;j.node=a.ref;j.work=def.workPerSeverity;createJob(s,r,j,tick);
        }
    }
    int64_t alertMood=0;for(const auto& [id,a]:s.alerts){(void)id;alertMood+=d.alerts.at(a.kind).mood;}
    // 3. Needs and mood.
    for(auto& [id,c]:s.colonists){
        for(auto& [nid,v]:c.needs){
            const auto& n=d.needs.at(nid);const auto old=v;int64_t gain=0;
            for(const auto& rs:d.restores)if(rs.need==nid&&rs.activity==c.activity)gain+=rs.perTick;
            v=std::clamp<int64_t>(v-n.decayPerTick+gain,0,kColonyNeedMax);
            if(old>n.lowAt&&v<=n.lowAt)emit(r,ColonyEventKind::NeedLow,id,0,nid,v);
            if(old>n.criticalAt&&v<=n.criticalAt)emit(r,ColonyEventKind::NeedCritical,id,0,nid,v);
            if(old<=n.lowAt&&v>n.lowAt)emit(r,ColonyEventKind::NeedRecovered,id,0,nid,v);
        }
        int64_t mood=cfg.moodBase+alertMood;
        for(const auto& [nid,v]:c.needs)for(const auto& st:d.needs.at(nid).mood)if(v<st.below){mood+=st.mood;break;}
        if(auto m=in.moodModifiers.find(id);m!=in.moodModifiers.end())mood+=m->second;
        c.mood=std::clamp(mood,-kColonyMoodLimit,kColonyMoodLimit);
        auto b=band(cfg,c.mood);if(b!=c.band){c.band=b;emit(r,ColonyEventKind::MoodBand,id,0,0,int64_t(b));}
    }
    // 4. Activity choice.
    std::vector<int64_t> wantWork;
    for(auto& [id,c]:s.colonists){
        if(c.incapacitated){releaseColonist(s,c,r);c.activity=ColonyActivity::Incapacitated;continue;}
        if(c.activity==ColonyActivity::Incapacitated)c.activity=ColonyActivity::Idle;
        // Critical needs first (also interrupt breaks and work).
        bool ate=false;
        for(const auto& m:d.meals){
            auto v=c.needs.at(m.need);const auto& n=d.needs.at(m.need);
            if(v>n.lowAt||(c.busyUntil>tick&&!(v<=n.criticalAt&&c.activity==ColonyActivity::Broken)))continue;
            if(c.activity==ColonyActivity::Working&&v>n.criticalAt&&responding(d,s,c,false))continue; // responders eat when critical only
            auto* food=foodFor(s,m.resource);
            if(!food){if(v<=n.criticalAt&&tick%cfg.ticksPerHour==0)emit(r,ColonyEventKind::Starving,id,0,m.need,v);continue;}
            if(--food->stock[m.resource]==0)food->stock.erase(m.resource);
            c.needs[m.need]=std::min(kColonyNeedMax,v+m.amount);releaseColonist(s,c,r);
            c.activity=ColonyActivity::Eating;c.busyUntil=tick+m.ticks;emit(r,ColonyEventKind::Ate,id,0,m.resource,c.needs[m.need]);ate=true;break;
        }
        if(ate||c.busyUntil>tick)continue;
        if(c.band==ColonyMoodBand::Breaking&&tick>=c.calmUntil){
            releaseColonist(s,c,r);c.activity=ColonyActivity::Broken;c.busyUntil=tick+cfg.breakTicks;c.calmUntil=c.busyUntil+cfg.breakTicks;
            emit(r,ColonyEventKind::MentalBreak,id,0,0,c.mood);continue;
        }
        const auto rest=d.restNeed?c.needs.at(d.restNeed):kColonyNeedMax;const ColonyNeedDef* restDef=d.restNeed?&d.needs.at(d.restNeed):nullptr;
        const auto block=c.schedule[std::size_t(colonyHour(cfg,tick))];
        const bool respond=responding(d,s,c,true);
        auto sleep=[&]{if(c.activity!=ColonyActivity::Sleeping)releaseColonist(s,c,r);c.activity=ColonyActivity::Sleeping;c.restoring=d.restNeed;};
        if(restDef&&rest<=restDef->criticalAt){sleep();continue;}
        if(c.activity==ColonyActivity::Sleeping&&restDef&&rest<restDef->recoveredAt&&!respond&&block!=ColonyBlock::Work){sleep();continue;}
        if(block==ColonyBlock::Sleep&&!respond){sleep();continue;}
        if(block==ColonyBlock::Leisure&&!respond){releaseColonist(s,c,r);c.activity=ColonyActivity::Leisure;continue;}
        wantWork.push_back(id);
    }
    // 5. Job generation: workstation bills and haul rules (one open job each).
    for(auto& [wid,w]:s.workstations){
        if(!w.recipe||w.job||s.jobs.size()>=kColonyMaxJobs)continue;
        const auto& rec=d.recipes.at(w.recipe);
        if(w.target>0&&colonyStock(s.stockpiles.at(w.stockpile),rec.outputs.begin()->first)>=w.target)continue;
        ColonyJob j;j.kind=ColonyJobKind::Recipe;j.workType=rec.workType;j.ref=rec.id;j.site=wid;j.from=w.stockpile;j.to=w.stockpile;j.work=rec.work;
        w.job=createJob(s,r,j,tick);
    }
    for(const auto& [hid,h]:d.hauls){
        auto from=s.stockpiles.find(h.from),to=s.stockpiles.find(h.to);if(from==s.stockpiles.end()||to==s.stockpiles.end()||s.jobs.size()>=kColonyMaxJobs)continue;
        bool open=false;for(const auto& [jid,j]:s.jobs){(void)jid;if(j.kind==ColonyJobKind::Haul&&j.ref==hid)open=true;}if(open)continue;
        auto amount=std::min({h.batch,h.keep-colonyStock(to->second,h.resource),colonyAvailable(from->second,h.resource),room(to->second)});
        if(amount<=0)continue;
        ColonyJob j;j.kind=ColonyJobKind::Haul;j.workType=h.workType;j.ref=hid;j.from=h.from;j.to=h.to;j.amount=amount;j.work=h.work;createJob(s,r,j,tick);
    }
    // 6. Assignment through lib::claim, preference tier by tier.
    // Haul jobs shrink to what is still available when claimed (stock may have been used since creation).
    auto eligible=[&](const ColonyJob& j){
        if(j.kind==ColonyJobKind::Haul)return colonyAvailable(s.stockpiles.at(j.from),d.hauls.at(j.ref).resource)>0;
        auto n=jobNeeds(d,j);return n.empty()||reservable(s.stockpiles.at(j.from),n);
    };
    auto claimFor=[&](ColonyColonist& c,bool responseOnly)->ColonyJob*{
        std::vector<Task> tasks;
        for(const auto& [jid,j]:s.jobs){(void)jid;if(j.task.status!=TaskStatus::Pending||(responseOnly&&j.kind!=ColonyJobKind::Response))continue;
            auto t=j.task;t.priority=j.task.priority+alertBoost(d,s,j.workType);tasks.push_back(t);}
        std::optional<std::size_t> got;
        for(int64_t tier=1;tier<=kColonyLowestPriority&&!got;++tier)
            got=claim(std::span<Task>(tasks),c.id,[&](const Task& t){const auto& j=s.jobs.at(t.id);return colonyPriority(d,c,j.workType)==tier&&colonySkill(c,j.workType)>0&&eligible(j);});
        return got?&s.jobs.at(tasks[*got].id):nullptr;
    };
    for(auto id:wantWork){
        auto& c=s.colonists.at(id);
        ColonyJob* next=nullptr;
        if(c.job){
            const auto& cur=s.jobs.at(c.job);
            // Preemption: an alert with preempt=true pulls eligible colonists off ordinary jobs.
            bool preempt=false;
            if(cur.kind!=ColonyJobKind::Response)for(const auto& [aid,a]:s.alerts){(void)aid;if(d.alerts.at(a.kind).preempt)preempt=true;}
            if(preempt)next=claimFor(c,true);
            if(!next){c.activity=ColonyActivity::Working;continue;}
            releaseColonist(s,c,r);
        }
        if(!next)next=claimFor(c,false);
        if(!next){
            releaseColonist(s,c,r);
            // Anything/Work blocks without a job fall back to leisure while a leisure-restored need is low.
            bool leisure=false;for(const auto& rs:d.restores)if(rs.activity==ColonyActivity::Leisure&&c.needs.at(rs.need)<=d.needs.at(rs.need).lowAt)leisure=true;
            c.activity=leisure?ColonyActivity::Leisure:ColonyActivity::Idle;continue;
        }
        auto& j=*next;j.task.status=TaskStatus::Active;j.task.worker=id;
        if(j.kind==ColonyJobKind::Haul)j.amount=std::min(j.amount,colonyAvailable(s.stockpiles.at(j.from),d.hauls.at(j.ref).resource));
        auto needs=jobNeeds(d,j);
        if(!needs.empty()&&systems::reserve(s.stockpiles.at(j.from).reserved,s.stockpiles.at(j.from).stock,j.task.id,id,needs)!=systems::ReserveResult::Ok){
            j.task.status=TaskStatus::Pending;j.task.worker=0;c.activity=ColonyActivity::Idle;continue;
        }
        c.job=j.task.id;c.activity=ColonyActivity::Working;emit(r,ColonyEventKind::JobAssigned,id,j.task.id,j.ref,int64_t(j.kind));
    }
    // 7. Work.
    std::map<int64_t,int64_t> gathered;
    for(auto& [id,c]:s.colonists){
        if(c.activity!=ColonyActivity::Working||!c.job)continue;
        auto& j=s.jobs.at(c.job);
        bool blocked=false;
        if(j.kind==ColonyJobKind::Recipe&&s.workstations.at(j.site).needsPower&&std::binary_search(in.unpowered.begin(),in.unpowered.end(),j.site))blocked=true;
        if(!blocked)j.progress=std::min(j.work*1000,j.progress+colonySkill(c,j.workType)*bandSpeed(cfg,c.band)/1000);
        if(!blocked&&j.progress==j.work*1000&&(j.kind==ColonyJobKind::Recipe||j.kind==ColonyJobKind::Haul)){
            auto& to=s.stockpiles.at(j.to);
            if(j.kind==ColonyJobKind::Recipe){const auto& rec=d.recipes.at(j.ref);if(to.capacity&&colonyTotal(to)-sum(rec.inputs)+sum(rec.outputs)>to.capacity)blocked=true;}
            if(j.kind==ColonyJobKind::Haul&&room(to)<j.amount)blocked=true;
        }
        if(blocked!=j.blocked){j.blocked=blocked;if(blocked)emit(r,ColonyEventKind::JobBlocked,id,j.task.id,j.ref,int64_t(j.kind));}
        if(blocked||j.progress<j.work*1000)continue;
        const auto jid=j.task.id;auto needs=jobNeeds(d,j);
        if(!needs.empty()&&systems::commit(s.stockpiles.at(j.from).reserved,s.stockpiles.at(j.from).stock,jid,id)!=systems::ReserveResult::Ok)continue;
        for(auto& [rid,p]:s.stockpiles){(void)rid;for(auto it=p.stock.begin();it!=p.stock.end();)it=it->second==0?p.stock.erase(it):std::next(it);}
        switch(j.kind){
        case ColonyJobKind::Recipe:{auto& p=s.stockpiles.at(j.to);for(auto [res,n]:d.recipes.at(j.ref).outputs)addStock(p,res,n);
            s.workstations.at(j.site).job=0;emit(r,ColonyEventKind::Produced,id,jid,j.ref,sum(d.recipes.at(j.ref).outputs));break;}
        case ColonyJobKind::Haul:addStock(s.stockpiles.at(j.to),d.hauls.at(j.ref).resource,j.amount);emit(r,ColonyEventKind::Hauled,id,jid,j.ref,j.amount);break;
        case ColonyJobKind::Build:{
            const auto& b=d.blueprints.at(j.ref);const auto sid=s.nextStructure++;
            if(b.workstationType)s.workstations.emplace(sid,ColonyWorkstation{sid,b.workstationType,j.from,0,0,0,b.needsPower});
            emit(r,ColonyEventKind::Built,id,jid,j.ref,sid);break;}
        case ColonyJobKind::Gather:{
            auto av=in.gatherAvailable.find(j.node);int64_t left=(av==in.gatherAvailable.end()?0:av->second)-gathered[j.node];
            auto& p=s.stockpiles.at(j.to);auto n=std::max<int64_t>(0,std::min({j.amount,left,room(p)}));
            addStock(p,d.gathers.at(j.ref).resource,n);gathered[j.node]+=n;emit(r,ColonyEventKind::Gathered,id,jid,j.node,n);break;}
        case ColonyJobKind::Response:{
            auto a=s.alerts.find(j.alert);
            if(a!=s.alerts.end()){++a->second.worked;emit(r,ColonyEventKind::AlertWorked,id,jid,a->first,a->second.ref);}
            break;}
        }
        const auto alert=j.alert;
        emit(r,ColonyEventKind::JobCompleted,id,jid,j.ref,int64_t(j.kind));c.job=0;s.jobs.erase(jid);
        if(alert){bool left=false;for(const auto& [k,o]:s.jobs){(void)k;if(o.alert==alert)left=true;}
            if(!left)if(auto a=s.alerts.find(alert);a!=s.alerts.end()){emit(r,ColonyEventKind::AlertResolved,0,0,alert,a->second.ref);s.alerts.erase(a);}}
    }
    for(const auto& [id,c]:s.colonists){(void)id;
        switch(c.activity){case ColonyActivity::Working:++r.working;break;case ColonyActivity::Sleeping:++r.sleeping;break;case ColonyActivity::Broken:++r.broken;break;case ColonyActivity::Idle:++r.idle;break;default:break;}}
    s.lastTick=tick;s.initialized=true;state=std::move(s);return r;
}
}
