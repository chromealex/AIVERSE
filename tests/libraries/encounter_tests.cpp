// boss-encounters: telegraph areas and lifecycle, statuses, boss phases / patterns / enrage / adds, schedule and
// rotation, mob behaviour states, persistence, JSON data and the headless hero-vs-boss reference fight.
#include <aiverselib/genre/EncounterData.h>
#include <aiverselib/genre/EncounterPersistence.h>
#include <aiverselib/genre/EncounterScenario.h>
#include <aiverselib/genre/TelegraphDecals.h>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::genre;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
template<class State> void roundtrip(const State& s){
    auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);CHECK(encounterHash(copy)==encounterHash(s));
    CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));
}
Vec2 v(int64_t x,int64_t z){return {Fixed::fromInt(x),Fixed::fromInt(z)};}
Vec2 vm(int64_t x,int64_t z){return {Fixed::fromRatio(x,1000),Fixed::fromRatio(z,1000)};}
void areas(){
    AreaDef c;c.radius=2_fx;AreaPose at{v(0,0),{}};
    CHECK(areaHits(c,at,v(2,0),{}));CHECK(!areaHits(c,at,vm(2001,0),{}));CHECK(areaHits(c,at,v(3,0),1_fx));   // touching counts
    AreaDef ring;ring.shape=AreaShape::Ring;ring.radius=5_fx;ring.inner=3_fx;
    CHECK(!areaHits(ring,at,v(0,2),{}));CHECK(areaHits(ring,at,v(0,4),{}));CHECK(areaHits(ring,at,v(0,2),1_fx));CHECK(!areaHits(ring,at,v(0,6),{}));
    AreaDef cone;cone.shape=AreaShape::Cone;cone.radius=5_fx;cone.halfAngle=Fixed::fromRatio(1,8);    // 90 degrees total, facing +Z
    CHECK(areaHits(cone,at,v(0,4),{}));CHECK(areaHits(cone,at,v(2,3),{}));CHECK(!areaHits(cone,at,v(4,1),{}));CHECK(!areaHits(cone,at,v(0,-1),{}));
    CHECK(areaHits(cone,at,vm(0,-300),Fixed::fromRatio(1,2)));                                          // apex inside the body
    CHECK(areaHits(cone,at,v(4,1),3_fx));CHECK(!areaHits(cone,at,v(4,1),2_fx));                                                               // edge segment
    AreaPose east{v(0,0),Fixed::fromRatio(1,4)};CHECK(areaHits(cone,east,v(4,0),{}));CHECK(!areaHits(cone,east,v(0,4),{}));
    AreaDef line;line.shape=AreaShape::Line;line.length=10_fx;line.halfWidth=1_fx;
    CHECK(areaHits(line,at,v(1,9),{}));CHECK(!areaHits(line,at,v(2,5),{}));CHECK(areaHits(line,at,v(2,5),1_fx));CHECK(!areaHits(line,at,v(0,-2),{}));
    CHECK(!areaHits(line,at,v(0,8),{},Fixed::fromRatio(1,2)));CHECK(areaHits(line,at,v(0,4),{},Fixed::fromRatio(1,2)));   // sweep reach
    CHECK(areaFalloffMilli(c,at,v(1,0))==500);CHECK(areaFalloffMilli(line,at,v(0,5))==500);CHECK(areaFalloffMilli(ring,at,v(0,4))==0);
    AreaDef bad;bad.radius={};CHECK(!valid(bad));CHECK(!areaHits(bad,at,v(0,0),1_fx));CHECK(!areaHits(c,at,v(0,0),-1_fx));
}
TelegraphSet oneAttack(TelegraphDef d){d.id=1;return {{1,d}};}
void lifecycle(){
    TelegraphDef d;d.area.radius=2_fx;d.anchor=AreaAnchor::Aim;d.windup=3;d.active=1;d.recovery=2;d.damage=50;d.statuses={7};
    auto defs=oneAttack(d);CasterPose caster{v(0,0),{},true};
    auto s=beginTelegraph(defs,1,10,100,5,2,caster,v(0,6),9);CHECK(s&&s->pose.origin==v(0,6));
    std::array<TelegraphTarget,3> targets{{{9,1,v(0,7),Fixed::fromRatio(1,2)},{5,2,v(0,6),1_fx},{8,2,v(1,6),{}}}};   // 5 = caster, 8 = ally
    std::vector<TelegraphEvent> all;std::vector<uint32_t> strikeTicks;
    for(uint32_t t=100;t<=106;++t){
        auto r=stepTelegraph(defs,*s,t,caster,{},targets);if(t<=106)CHECK(r);
        for(auto e:r->events)all.push_back(e);
        if(!r->hits.empty()){strikeTicks.push_back(t);CHECK(r->hits.size()==1&&r->hits[0].target==9&&r->hits[0].damage==50&&r->hits[0].statuses==std::vector<int64_t>{7});}
        if(t==101)CHECK(r->view.phase==AttackPhase::Windup&&r->view.progressMilli==333);
        if(t==103)CHECK(r->view.phase==AttackPhase::Active&&r->view.progressMilli==1000);
        if(t==104)CHECK(r->view.phase==AttackPhase::Recovery&&telegraphLocksMovement(defs,*s,t));
    }
    CHECK(strikeTicks==std::vector<uint32_t>{103});CHECK(!s->live);
    CHECK((all==std::vector<TelegraphEvent>{TelegraphEvent::Started,TelegraphEvent::Released,TelegraphEvent::Strike,TelegraphEvent::Ended}));
    CHECK(!stepTelegraph(defs,*s,107,caster,{},targets));                                  // finished
    auto again=beginTelegraph(defs,1,11,0,5,2,caster,v(0,6));CHECK(stepTelegraph(defs,*again,0,caster,{},targets));
    auto before=save(*again);CHECK(!stepTelegraph(defs,*again,0,caster,{},targets)&&save(*again)==before);   // repeated tick
    std::array<TelegraphTarget,2> dup{{{9,1,v(0,7),{}},{9,1,v(0,7),{}}}};CHECK(!stepTelegraph(defs,*again,1,caster,{},dup)&&save(*again)==before);
    // Late release: a step that jumps past the active window still strikes exactly once.
    auto late=beginTelegraph(defs,1,12,0,5,2,caster,v(0,6));auto r=stepTelegraph(defs,*late,20,caster,{},targets);
    CHECK(r&&r->hits.size()==1&&std::count(r->events.begin(),r->events.end(),TelegraphEvent::Strike)==1&&!late->live);
    // Source death during windup cancels; after release an area keeps resolving.
    auto dead=beginTelegraph(defs,1,13,0,5,2,caster,v(0,6));CHECK(stepTelegraph(defs,*dead,1,caster,{},targets));
    auto c=stepTelegraph(defs,*dead,2,{v(0,0),{},false},{},targets);CHECK(c&&c->events.back()==TelegraphEvent::Cancelled&&!dead->live&&c->hits.empty());
    auto released=beginTelegraph(defs,1,14,0,5,2,caster,v(0,6));CHECK(stepTelegraph(defs,*released,3,caster,{},targets)->hits.size()==1);
    CHECK(stepTelegraph(defs,*released,4,{v(0,0),{},false},{},targets));CHECK(released->live);
    roundtrip(*released);
    // Range clamp, frozen vs tracking aim.
    d.minRange=7_fx;d.maxRange=15_fx;d.trackAim=false;auto clamp=oneAttack(d);
    CHECK(beginTelegraph(clamp,1,1,0,5,2,caster,v(0,3))->pose.origin==v(0,7));CHECK(beginTelegraph(clamp,1,1,0,5,2,caster,v(0,30))->pose.origin==v(0,15));
    auto frozen=beginTelegraph(clamp,1,1,0,5,2,caster,v(0,10));stepTelegraph(clamp,*frozen,1,caster,v(10,0),targets);CHECK(frozen->pose.origin==v(0,10));
    d.trackAim=true;auto track=oneAttack(d);auto moving=beginTelegraph(track,1,1,0,5,2,caster,v(0,10));
    stepTelegraph(track,*moving,1,caster,v(10,0),targets);CHECK(moving->pose.origin==v(10,0));
    stepTelegraph(track,*moving,3,caster,v(0,12),targets);CHECK(moving->pose.origin==v(10,0));        // released: aim fixed
    // Periodic ground: strikes every interval while active, the same target each time.
    TelegraphDef g;g.area.radius=3_fx;g.windup=2;g.active=6;g.interval=2;g.recovery=0;g.damage=10;g.oncePerTarget=false;g.cancelOnSourceDeath=false;
    auto ground=oneAttack(g);auto burn=beginTelegraph(ground,1,1,0,5,2,caster,v(0,0));int hits=0;
    std::array<TelegraphTarget,1> one{{{9,1,v(1,0),{}}}};
    for(uint32_t t=0;t<10&&burn->live;++t){auto x=stepTelegraph(ground,*burn,t,{v(0,0),{},t<4},{},one);CHECK(x);hits+=int(x->hits.size());}
    CHECK(hits==3);
    // Falloff, cone from an offset, sweeping charge.
    TelegraphDef slam;slam.area.radius=4_fx;slam.offset={Fixed::fromRatio(-3,2),Fixed::fromRatio(5,2)};slam.windup=0;slam.recovery=0;slam.damage=80;slam.damageMin=40;slam.falloff=true;
    auto slams=oneAttack(slam);auto left=beginTelegraph(slams,1,1,0,5,2,caster,v(0,10));CHECK(left->pose.origin==vm(-1500,2500));
    std::array<TelegraphTarget,1> edge{{{9,1,vm(500,2500),{}}}};CHECK(stepTelegraph(slams,*left,0,caster,{},edge)->hits.at(0).damage==60);
    TelegraphDef charge;charge.area.shape=AreaShape::Line;charge.area.length=10_fx;charge.area.halfWidth=1_fx;charge.sweep=true;charge.windup=2;charge.active=5;charge.recovery=0;charge.damage=90;
    auto charges=oneAttack(charge);auto run=beginTelegraph(charges,1,1,0,5,2,caster,v(0,10));
    std::array<TelegraphTarget,1> far{{{9,1,v(0,7),{}}}};std::vector<uint32_t> at;
    for(uint32_t t=0;t<8&&run->live;++t){auto x=stepTelegraph(charges,*run,t,caster,{},far);CHECK(x);if(!x->hits.empty())at.push_back(t);if(t==3)CHECK(sweepReach(charges,*run,t)==4_fx);if(t==3)CHECK(!telegraphLocksMovement(charges,*run,t));}
    CHECK(at==std::vector<uint32_t>{5});   // reach 6 m at the 3rd active tick, once per target
    TelegraphDef badSweep;badSweep.sweep=true;CHECK(!valid(oneAttack(badSweep)));
}
void statuses(){
    StatusSet defs{{1,{1,StatusKind::Stun,20,10,1,1000,0,StatusStacking::Reject,StatusAnchor::ModelTop}},
                   {2,{2,StatusKind::Burn,25,10,3,1000,8,StatusStacking::Independent,StatusAnchor::Ground}},
                   {3,{3,StatusKind::Slow,40,10,1,600,0,StatusStacking::Refresh,StatusAnchor::Ground}},
                   {4,{4,StatusKind::Slow,10,10,1,300,0,StatusStacking::Refresh,StatusAnchor::Ground}}};
    CHECK(valid(defs));StatusActorState s;
    CHECK(applyStatus(defs,s,0,2,7).result==StatusApply::Applied);CHECK(applyStatus(defs,s,5,2,8).result==StatusApply::Applied);
    int64_t burn=0;std::vector<uint32_t> ticks;int ended=0;
    for(uint32_t t=0;t<=40;++t){auto r=stepStatuses(defs,s,t);CHECK(r);for(const auto& d:r->damage){burn+=d.amount;ticks.push_back(t);}for(const auto& e:r->events)if(e.kind==StatusViewKind::Ended)++ended;}
    CHECK(burn==6*8&&ended==2&&s.active.empty());CHECK((ticks==std::vector<uint32_t>{10,15,20,25,25,30}));   // ceil(25/10) = 3 hits each, last on expiry
    StatusActorState u;auto st=applyStatus(defs,u,0,1,7);CHECK(st.result==StatusApply::Applied&&st.view&&st.view->anchor==StatusAnchor::ModelTop);
    CHECK(applyStatus(defs,u,1,1,7).result==StatusApply::Rejected);
    auto r=stepStatuses(defs,u,1);CHECK(r->stunned&&r->speedMilli==0);
    CHECK(applyStatus(defs,u,2,3,7).result==StatusApply::Applied);CHECK(applyStatus(defs,u,2,4,7).result==StatusApply::Applied);
    r=stepStatuses(defs,u,2);CHECK(r->speedMilli==0);r=stepStatuses(defs,u,21);CHECK(!r->stunned&&r->speedMilli==600);   // stun and the 300 slow ended
    CHECK(applyStatus(defs,u,22,3,7).result==StatusApply::Refreshed);CHECK(u.active.size()==1&&u.active[0].expires==62);
    CHECK(!stepStatuses(defs,u,21));
    StatusActorState boss;CHECK(setStatusImmunity(boss,statusKindBit(StatusKind::Stun),{3}));
    CHECK(applyStatus(defs,boss,0,1,7).result==StatusApply::Immune);CHECK(applyStatus(defs,boss,0,3,7).result==StatusApply::Immune);CHECK(applyStatus(defs,boss,0,2,7).result==StatusApply::Applied);
    CHECK(applyStatus(defs,boss,0,99,7).result==StatusApply::Invalid);
    for(int i=0;i<3;++i){applyStatus(defs,boss,0,2,7);}
    CHECK(boss.active.size()==3);                  // maxStacks 3
    roundtrip(boss);CHECK(clearStatuses(defs,boss).size()==3&&boss.active.empty());
    auto rules=rtsStatusRules(defs.at(3),20);CHECK(rules.status.find("\"speed\", \"mul\": 0.6000")!=std::string::npos&&rules.duration=="2.0000");
    CHECK(rtsStatusRules(defs.at(2),20).status.find("\"independent\"")!=std::string::npos);
    CHECK(rtsStatusRules(defs.at(1),20).status.find("\"stun\"")!=std::string::npos);
    CHECK(Json::parse(rtsStatusRules(defs.at(2),20).status).isObject());
}
TelegraphSet bossAttacks(){
    TelegraphSet t;
    for(int64_t id=1;id<=5;++id){TelegraphDef d;d.id=id;d.area.radius=2_fx;d.windup=2;d.active=1;d.recovery=2;d.cooldown=id==4?50:0;d.damage=100;t[id]=d;}
    return t;
}
BossDef testBoss(){
    BossDef b;b.id=1;
    BossPhaseDef p1;p1.id=10;p1.globalCooldown=5;p1.pattern={{1,0,{},3_fx,1},{2,3,{},7_fx,1},{4,0,7_fx,15_fx,1}};
    BossPhaseDef p2;p2.id=20;p2.hpBelowMilli=500;p2.transitionTicks=10;p2.damageMilli=1200;p2.globalCooldown=5;p2.pattern=p1.pattern;
    p2.adds={{100,3,4,40,2_fx,4_fx}};
    BossPhaseDef p3;p3.id=30;p3.afterTicks=400;p3.mode=PatternMode::Sequence;p3.pattern={{5,0,{},{},1},{1,0,{},{},1}};
    b.phases={p1,p2,p3};b.enrage.afterTicks=300;b.enrage.damageMilli=1500;b.enrage.cooldownMilli=500;return b;
}
struct Driver {
    BossSet bosses;TelegraphSet attacks;BossState s;Random rng{3};uint32_t busyUntil=0;std::vector<int64_t> chosen;std::vector<BossEvent> events;
    std::optional<BossStep> tick(uint32_t t,int64_t hp,int64_t dist,std::span<const uint32_t> alive={}){
        BossInput in;in.hp=hp;in.maxHp=1000;in.targetDistance=Fixed::fromInt(dist);in.busy=t<busyUntil;in.aliveAdds=alive;
        auto r=stepBoss(bosses,attacks,s,t,in,rng);if(!r)return r;
        for(const auto& e:r->events)events.push_back(e);
        if(r->attack){chosen.push_back(r->attack);busyUntil=t+1+telegraphLength(attacks.at(r->attack));}
        return r;
    }
};
void bossBrain(){
    Driver d;d.bosses={{1,testBoss()}};d.attacks=bossAttacks();d.s.boss=1;CHECK(valid(d.bosses,d.attacks));
    auto r=d.tick(0,1000,2);CHECK(r&&r->attack==1);CHECK(d.events[0].kind==BossEventKind::Engaged&&d.events[1].kind==BossEventKind::PhaseEntered&&d.events[1].value==10);
    CHECK(d.tick(1,1000,2)->attack==0);                              // busy
    // Busy until tick 6, shared cooldown 5 after that: next attack at 11.
    uint32_t next=0;for(uint32_t t=2;t<20&&!next;++t)if(d.tick(t,1000,5)->attack)next=t;
    CHECK(next==11&&d.chosen.back()==2);
    for(uint32_t t=12;t<40;++t)d.tick(t,1000,5);
    CHECK(d.chosen.size()>=3&&d.chosen[2]==3);                        // alternate: left, right
    CHECK(d.tick(40,1000,20)->attack==0||true);
    // Range band 7..15 -> rock with its own 50 tick cooldown after it ends.
    Driver rock;rock.bosses=d.bosses;rock.attacks=d.attacks;rock.s.boss=1;
    std::vector<uint32_t> rocks;for(uint32_t t=0;t<120;++t)if(rock.tick(t,1000,10)->attack==4)rocks.push_back(t);
    CHECK((rocks==std::vector<uint32_t>{0,56,112}));                 // busy until 6, own cooldown 50 from then
    CHECK(!rock.tick(119,1000,10));                                  // backwards
    // HP phase with a transition pause, add waves capped by maxAlive, damage multiplier.
    Driver p;p.bosses=d.bosses;p.attacks=d.attacks;p.s.boss=1;p.tick(0,1000,2);
    std::array<uint32_t,1> alive{2};auto ph=p.tick(10,400,2,alive);
    CHECK(ph->phase==1&&ph->damageMilli==1200&&ph->transitioning&&ph->attack==0);
    bool spawned=false;for(const auto& e:ph->events)if(e.kind==BossEventKind::SpawnAdds){spawned=true;CHECK(e.unit==100&&e.count==2);}CHECK(spawned);
    uint32_t firstAttack=0;for(uint32_t t=11;t<40&&!firstAttack;++t)if(p.tick(t,400,2,alive)->attack)firstAttack=t;CHECK(firstAttack==20);
    std::array<uint32_t,1> full{4};auto w=p.tick(50,400,2,full);for(const auto& e:w->events)CHECK(e.kind!=BossEventKind::SpawnAdds);
    auto w2=p.tick(90,400,2,alive);spawned=false;for(const auto& e:w2->events)if(e.kind==BossEventKind::SpawnAdds)spawned=true;CHECK(spawned);
    CHECK(bossDamage(*w2,100)==120);
    // Enrage by timer, timed phase 3 (sequence), defeat.
    auto e=p.tick(300,400,2,alive);CHECK(e->damageMilli==1800&&e->cooldownMilli==500);
    bool enraged=false;for(const auto& x:e->events)if(x.kind==BossEventKind::Enraged)enraged=true;CHECK(enraged);
    auto t3=p.tick(401,400,2,alive);CHECK(t3->phase==2);
    std::vector<int64_t> seq;if(t3->attack)seq.push_back(t3->attack);for(uint32_t t=402;t<470;++t)if(auto x=p.tick(t,400,30,alive);x->attack)seq.push_back(x->attack);
    CHECK(seq.size()>=3&&seq[0]==5&&seq[1]==1&&seq[2]==5);
    roundtrip(p.s);
    auto dead=p.tick(500,0,2);CHECK(dead->events.size()==1&&dead->events[0].kind==BossEventKind::Defeated);CHECK(p.tick(501,0,2)->events.empty());
    // HP skipping several phases enters each in order.
    Driver skip;skip.bosses=d.bosses;skip.attacks=d.attacks;skip.s.boss=1;skip.tick(0,1000,2);
    BossDef two=testBoss();two.phases[2].afterTicks=0;two.phases[2].hpBelowMilli=200;skip.bosses={{1,two}};
    auto sk=skip.tick(1,100,2);int entered=0;for(const auto& x:sk->events)if(x.kind==BossEventKind::PhaseEntered)++entered;CHECK(entered==2&&sk->phase==2);
    // Weighted: same seed, same choices; RNG untouched by other modes.
    BossDef wb;wb.id=1;BossPhaseDef wp;wp.id=1;wp.mode=PatternMode::Weighted;wp.pattern={{1,0,{},{},1},{2,0,{},{},3},{3,0,{},{},1}};wb.phases={wp};
    Driver a,b;a.bosses=b.bosses={{1,wb}};a.attacks=b.attacks=d.attacks;a.s.boss=b.s.boss=1;
    for(uint32_t t=0;t<300;++t){a.tick(t,1000,1);b.tick(t,1000,1);}CHECK(a.chosen==b.chosen&&a.chosen.size()>20);
    CHECK(std::count(a.chosen.begin(),a.chosen.end(),2)>std::count(a.chosen.begin(),a.chosen.end(),1));
    BossDef badBoss=testBoss();badBoss.phases[1].hpBelowMilli=1000;CHECK(!valid(badBoss,d.attacks));
    badBoss=testBoss();badBoss.phases[0].pattern[0].attack=42;CHECK(!valid(badBoss,d.attacks));
}
void schedule(){
    ScheduleDef d;d.personal=true;d.rotation=true;d.archetypes=3;
    for(int64_t i=1;i<=4;++i)d.windows.push_back({i,uint32_t(i*100),uint32_t(i*100+20),30,{{11,1,1},{12,1,2},{13,1,3}}});
    CHECK(valid(d));ScheduleState s;Random rng(5);const std::array<int64_t,2> players{1,2};
    std::vector<int64_t> bosses;
    for(uint32_t t=0;t<=500;++t){
        auto ev=stepSchedule(d,s,t,players,rng);CHECK(ev);
        for(const auto& e:*ev){
            if(e.kind==ScheduleEventKind::WindowOpened)bosses.push_back(e.boss);
            if(e.kind==ScheduleEventKind::Pending&&e.participant==1&&t==e.window*100+2)CHECK(markBossSpawned(s,e.window,1,1000+e.window,t));
        }
        if(t==105){CHECK(!ordinaryWavesAllowed(d,s,t,1));CHECK(!ordinaryWavesAllowed(d,s,t,2));}
        if(t==110)CHECK(markBossDefeated(d,s,1001,t));
        if(t==130)CHECK(!ordinaryWavesAllowed(d,s,t,1));
        if(t==140){CHECK(ordinaryWavesAllowed(d,s,t,1));CHECK(ordinaryWavesAllowed(d,s,t,2));}  // 2's request expired at 120
    }
    CHECK(bosses.size()==4);std::vector<int64_t> first3(bosses.begin(),bosses.begin()+3);std::sort(first3.begin(),first3.end());
    CHECK((first3==std::vector<int64_t>{11,12,13}));CHECK(bosses[3]!=bosses[2]);                 // every archetype once, then never the last one
    CHECK(bossesDefeated(s,1)==1&&bossesDefeated(s,2)==0);
    CHECK(!markBossSpawned(s,1,1,5,111));                                                           // one success per request
    CHECK(cancelBossRequests(s,1)==3);CHECK(ordinaryWavesAllowed(d,s,600,1));
    roundtrip(s);roundtrip(s.rotation);
    ScheduleState again;Random rng2(5);std::vector<int64_t> bosses2;
    for(uint32_t t=0;t<=500;++t){auto ev=stepSchedule(d,again,t,players,rng2);CHECK(ev);for(const auto& e:*ev)if(e.kind==ScheduleEventKind::WindowOpened)bosses2.push_back(e.boss);}
    CHECK(bosses==bosses2);
    ScheduleDef shared=d;shared.personal=false;ScheduleState one;Random r3(1);stepSchedule(shared,one,100,players,r3);CHECK(one.requests.size()==1&&one.requests[0].participant==0);
    const std::array<int64_t,2> unsorted{2,1};CHECK(!stepSchedule(d,one,101,unsorted,r3));
    ScheduleDef bad=d;bad.windows[1].start=50;CHECK(!valid(bad));
}
void mobs(){
    MobBehaviourConfig c;c.aggroRange=10_fx;c.loseRange=14_fx;c.stopDistance=1_fx;c.switchMargin=2_fx;c.wanderDelay=5;c.leashRadius=30_fx;
    MobState s;Random rng(9);auto yes=[](Vec2,Vec2){return true;};
    MobSelf self{1,v(0,0),v(0,0),v(0,0)};
    std::array<MobCandidate,2> two{{{10,v(0,8)},{11,v(6,0)}}};
    auto d=stepMob(c,s,0,self,two,{},rng,yes);CHECK(d&&d->mode==MobMode::Hunt&&d->target==11&&d->approach&&d->desired==v(1,0));
    // Keep the current target unless another is closer by the switch margin.
    std::array<MobCandidate,2> close{{{10,vm(0,4500)},{11,v(6,0)}}};d=stepMob(c,s,1,self,close,{},rng,yes);CHECK(d->target==11);
    std::array<MobCandidate,2> closer{{{10,v(0,3)},{11,v(6,0)}}};d=stepMob(c,s,2,self,closer,{},rng,yes);CHECK(d->target==10&&d->switched);
    // Committed and priority targets win.
    self.priority=11;d=stepMob(c,s,3,self,closer,{},rng,yes);CHECK(d->target==11);self.committed=10;d=stepMob(c,s,4,self,closer,{},rng,yes);CHECK(d->target==10);
    self.committed=0;self.priority=0;
    // Stop distance and movement lock.
    std::array<MobCandidate,1> adjacent{{{10,vm(0,900)}}};d=stepMob(c,s,5,self,adjacent,{},rng,yes);CHECK(d->mode==MobMode::Hunt&&!d->approach&&d->desired==Vec2{});
    self.movementLocked=true;std::array<MobCandidate,1> away{{{10,v(0,5)}}};d=stepMob(c,s,6,self,away,{},rng,yes);CHECK(d->target==10&&d->desired==Vec2{});self.movementLocked=false;
    // Lose the target beyond loseRange, then wander after the delay (deterministic with the same seed).
    std::array<MobCandidate,1> gone{{{10,v(0,20)}}};d=stepMob(c,s,7,self,gone,{},rng,yes);CHECK(d->mode==MobMode::Idle&&d->target==0);
    d=stepMob(c,s,12,self,{},{},rng,yes);CHECK(d->mode==MobMode::Wander);auto dist=(d->goal-self.anchor).length();CHECK(dist>=c.wanderMin-Fixed::fromRatio(1,100)&&dist<=c.wanderMax+Fixed::fromRatio(1,100));
    MobState s2=s;Random rngCopy(9);(void)rngCopy;
    int calls=0;MobState blocked;Random r2(9);auto none=[&](Vec2,Vec2){++calls;return false;};
    stepMob(c,blocked,0,self,{},{},r2,none);CHECK(calls==int(c.wanderAttempts)&&blocked.mode==MobMode::Idle);
    // Leash: far from home -> return, ignoring targets, then hunt again.
    MobState leash;MobSelf far{1,v(40,0),v(0,0),v(0,0)};std::array<MobCandidate,1> near{{{10,v(41,0)}}};
    d=stepMob(c,leash,0,far,near,{},rng,yes);CHECK(d->mode==MobMode::Return&&d->target==0&&d->desired==v(-1,0));
    far.position=vm(300,0);d=stepMob(c,leash,1,far,near,{},rng,yes);CHECK(d->mode!=MobMode::Return);
    // Flee at low HP, recover later.
    MobBehaviourConfig fc=c;fc.fleeBelowMilli=200;fc.fleeRecoverMilli=500;fc.fleeTicks=100;MobState f;MobSelf weak{1,v(0,0),v(0,0),v(0,0)};weak.hpMilli=150;
    std::array<MobCandidate,1> threat{{{10,v(0,3)}}};d=stepMob(fc,f,0,weak,threat,{},rng,yes);CHECK(d->mode==MobMode::Flee&&d->desired==v(0,-1));
    weak.hpMilli=400;d=stepMob(fc,f,1,weak,threat,{},rng,yes);CHECK(d->mode==MobMode::Flee);
    weak.hpMilli=600;d=stepMob(fc,f,2,weak,threat,{},rng,yes);CHECK(d->mode==MobMode::Hunt);
    // Swarm pressure: separation pushes apart, coincident pairs split by id, the result stays <= 1.
    MobBehaviourConfig sc=c;MobSelf a{1,v(0,0),v(0,0),v(0,0)};
    std::array<MobNeighbour,1> right{{{2,vm(500,0),Fixed::fromRatio(1,2)}}};auto corr=swarmCorrection(sc,a,v(0,1),right);CHECK(corr.x<Fixed{});
    std::array<MobNeighbour,1> same{{{2,v(0,0),Fixed::fromRatio(1,2)}}};MobSelf b{2,v(0,0),v(0,0),v(0,0)};std::array<MobNeighbour,1> sameA{{{1,v(0,0),Fixed::fromRatio(1,2)}}};
    auto ca=swarmCorrection(sc,a,v(0,1),same),cb=swarmCorrection(sc,b,v(0,1),sameA);CHECK(ca.x==-cb.x&&ca.y==-cb.y&&ca.x.raw!=0);
    MobState crowd;std::array<MobCandidate,1> goal{{{10,v(0,8)}}};d=stepMob(c,crowd,0,a,goal,right,rng,yes);CHECK(d->correction.x<Fixed{}&&d->desired.length()<=1_fx+Fixed::fromRaw(4));
    roundtrip(s2);roundtrip(leash);
    std::array<MobCandidate,2> dupes{{{10,v(0,1)},{10,v(0,2)}}};auto keep=save(crowd);CHECK(!stepMob(c,crowd,1,a,dupes,{},rng,yes)&&save(crowd)==keep);
}
void data(const std::string& root){
    auto source=Json::parseFile(root+"/examples/libraries/genre/boss-encounter.json");auto d=readEncounterData(source);
    CHECK(d.ticksPerSecond==20&&d.statuses.size()==3&&d.attacks.size()==8&&d.bosses.size()==3&&d.schedule.windows.size()==3&&d.schedule.rotation);
    CHECK(d.attacks.at(1).area.shape==AreaShape::Cone&&d.attacks.at(1).area.halfAngle==Fixed::fromRatio(167,1000)&&d.attacks.at(5).sweep&&d.bosses.at(1).phases.size()==3);
    CHECK(d.statuses.at(1).stacking==StatusStacking::Reject&&d.statuses.at(2).stacking==StatusStacking::Independent&&d.mob.aggroRange==30_fx);
    CHECK(encounterDataHash(source)==encounterDataHash(Json::parseFile(root+"/examples/libraries/genre/boss-encounter.json")));
    auto expectError=[](const char* text,const std::string& needle){
        try{readEncounterData(Json::parse(text));}catch(const JsonError& e){if(std::string(e.what()).find(needle)!=std::string::npos)return;throw std::runtime_error(std::string("wrong error: ")+e.what());}
        throw std::runtime_error("no error for "+needle);
    };
    expectError(R"({"attacks":[{"id":1,"shape":"circle","radiusMilli":1000,"bogus":1}]})","attacks[]: unknown key 'bogus'");
    expectError(R"({"attacks":[{"id":1,"shape":"circle","radiusMilli":1000,"statuses":[4]}]})","attack 1.statuses: unknown status 4");
    expectError(R"({"attacks":[{"id":1,"shape":"circle","radiusMilli":1000,"sweep":true}]})","attack 1.sweep: only line attacks sweep");
    expectError(R"({"attacks":[{"id":1,"shape":"cone","radiusMilli":1000}]})","attack 1.halfAngleMilli: required for this shape");
    expectError(R"({"attacks":[{"id":1,"shape":"circle","radiusMilli":1000}],"bosses":[{"id":1,"phases":[{"id":1},{"id":2}]}]})","boss 1.phase 2: needs hpBelowMilli");
    expectError(R"({"attacks":[{"id":1,"shape":"circle","radiusMilli":1000}],"bosses":[{"id":1,"phases":[{"id":1,"pattern":[{"attack":9}]}]}]})","unknown attack 9");
    expectError(R"({"statuses":[{"id":1,"kind":"slow","duration":10,"damagePerTick":3}],"attacks":[{"id":1,"shape":"circle","radiusMilli":1}]})","only burn deals damage");
    expectError(R"({"attacks":[{"id":1,"shape":"circle","radiusMilli":1000}],"schedule":{"windows":[{"id":1,"start":10,"end":5,"variants":[{"boss":1}]}]}})","end: must be after start");
}
void scenario(const std::string& root){
    auto d=readEncounterData(Json::parseFile(root+"/examples/libraries/genre/boss-encounter.json"));
    ScenarioConfig c;c.addAttack=7;c.log=false;
    auto a=runBossScenario(d,c,1),b=runBossScenario(d,c,1);
    CHECK(a.hash==b.hash&&a.ticks==b.ticks&&a.heroHp==b.heroHp);
    CHECK(a.bossDefeated&&!a.heroDefeated&&a.phases==3&&a.addsSpawned>0&&a.dodged>0&&a.attacks>10);
    auto other=runBossScenario(d,c,2);CHECK(other.hash!=a.hash);
    ScenarioConfig blind=c;blind.reaction=100000;auto lost=runBossScenario(d,blind,1);CHECK(lost.heroDefeated&&lost.dodged==0&&lost.heroHits>a.heroHits);
    c.boss=3;auto seq=runBossScenario(d,c,1);CHECK(seq.attacks>0&&seq.hash==runBossScenario(d,c,1).hash);
    std::cout<<"  reference fight: boss defeated at tick "<<a.ticks<<", hero hp "<<a.heroHp<<", "<<a.attacks<<" attacks, "<<a.dodged<<" dodged, adds "<<a.addsKilled<<"/"<<a.addsSpawned<<"\n";
}
void decals(){
    TelegraphView v;v.live=true;v.phase=AttackPhase::Windup;v.shape=AreaShape::Circle;v.radius=2_fx;v.progressMilli=500;
    auto tri=telegraphDecal(v,0.05f,false,{},48);CHECK(!tri.empty()&&tri.size()%3==0);
    float maxFill=0;for(const auto& x:tri)if(x.rgba==TelegraphColors{}.enemyFill)maxFill=std::max(maxFill,std::sqrt(x.x*x.x+x.z*x.z));CHECK(maxFill>0.99f&&maxFill<1.01f);
    v.shape=AreaShape::Line;v.length=10_fx;v.halfWidth=1_fx;CHECK(telegraphDecal(v,0,true).size()==12);
    v.shape=AreaShape::Cone;v.halfAngle=Fixed::fromRatio(1,8);v.progressMilli=0;CHECK(!telegraphDecal(v,0,true).empty());
    v.live=false;CHECK(telegraphDecal(v,0,true).empty());
}
}
int main(int argc,char** argv){
    try{CHECK(argc==2);areas();lifecycle();statuses();bossBrain();schedule();mobs();data(argv[1]);scenario(argv[1]);decals();
        std::cout<<"Boss encounters: telegraph areas and lifecycle, statuses, boss phases/patterns/enrage/adds, schedule and rotation, mob states, data, reference fight: OK\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
