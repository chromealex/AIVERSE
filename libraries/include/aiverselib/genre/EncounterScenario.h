#pragma once
// Headless reference fight (boss-encounters): one scripted hero against one boss of an EncounterData file,
// wiring every policy the way an owner would: stepBoss chooses attacks and add waves, beginTelegraph/stepTelegraph
// resolve them on exact ticks, StatusEffects slow/stun/burn the hero, adds hunt with MobBehaviour + swarm
// pressure. The hero kites, steps out of windups it can see after `reaction` ticks and shoots. Deterministic:
// same data + config + seed -> same report and checksum. Used by tests and `aiverse_encounter simulate`.
#include "EncounterData.h"
#include "EncounterPersistence.h"
#include "engine/core/Hash.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
struct ScenarioConfig {
    int64_t boss=0;                       // 0: the first boss of the data
    uint32_t maxTicks=6000,reaction=4,heroAttackEvery=10;
    int64_t heroHp=1000,heroDamage=40,bossMaxHp=6000,addHp=60,addAttack=0;   // addAttack 0: adds deal no damage
    Fixed heroSpeed=Fixed::fromRatio(1,4),heroRadius=Fixed::fromRatio(1,2),heroRange=9_fx,heroIdeal=6_fx;  // per tick / metres
    Fixed bossSpeed=Fixed::fromRatio(1,10),bossRadius=Fixed::fromRatio(3,2),bossReach=Fixed::fromRatio(5,2),addSpeed=Fixed::fromRatio(3,20),addRadius=Fixed::fromRatio(2,5);
    uint32_t bossStatusImmunity=statusKindBit(StatusKind::Stun)|statusKindBit(StatusKind::Root);
    bool log=true;
};
struct ScenarioReport {
    bool bossDefeated=false,heroDefeated=false,timedOut=false,enraged=false;
    uint32_t ticks=0,phases=0,attacks=0,strikes=0,heroHits=0,dodged=0,addsSpawned=0,addsKilled=0,statusesApplied=0,burnTicks=0;
    int64_t heroHp=0,bossHp=0;uint64_t hash=0;std::vector<std::string> log;
};
namespace detail {
struct ScenarioAdd {int64_t id=0,hp=0;uint32_t wave=0;Vec2 position{};MobState mob;bool alive=true;};
struct ScenarioCast {TelegraphInstance t;bool boss=true,threatened=false;int64_t damageMilli=1000;bool hitHero=false;};
inline Vec2 moveBy(Vec2 p,Vec2 dir,Fixed step){return p+dir*step;}
}
inline ScenarioReport runBossScenario(const EncounterData& data,const ScenarioConfig& cfg,uint64_t seed){
    using namespace detail;ScenarioReport rep;
    auto bossIt=cfg.boss?data.bosses.find(cfg.boss):data.bosses.begin();
    if(bossIt==data.bosses.end()||cfg.heroHp<=0||cfg.bossMaxHp<=0||(cfg.addAttack&&!data.attacks.contains(cfg.addAttack))){rep.log.push_back("invalid scenario");return rep;}
    const int64_t heroId=1,bossId=2;int64_t nextId=100,nextCast=1;Random rng(seed,0x5bd1e995u);
    Vec2 hero{{},{}},boss{{},Fixed::fromInt(14)};int64_t heroHp=cfg.heroHp,bossHp=cfg.bossMaxHp;
    StatusActorState heroStatus,bossStatus;setStatusImmunity(bossStatus,cfg.bossStatusImmunity,{});
    BossState brain;brain.boss=bossIt->first;std::vector<ScenarioAdd> adds;std::vector<ScenarioCast> casts;
    uint32_t heroCooldown=0;
    auto say=[&](uint32_t t,const std::string& s){if(cfg.log)rep.log.push_back("t="+std::to_string(t)+" "+s);};
    auto applyHit=[&](uint32_t t,const TelegraphHit& h,int64_t damageMilli){
        if(h.target==heroId){
            heroHp-=h.damage*damageMilli/1000;++rep.heroHits;
            for(auto st:h.statuses){auto r=applyStatus(data.statuses,heroStatus,t,st,bossId);if(r.result==StatusApply::Applied||r.result==StatusApply::Refreshed)++rep.statusesApplied;}
        }
    };
    uint32_t t=0;
    for(;t<cfg.maxTicks;++t){
        // 1. Hero statuses: burn damage, slow / stun.
        auto hs=stepStatuses(data.statuses,heroStatus,t);if(!hs)break;
        for(const auto& d:hs->damage){heroHp-=d.amount;++rep.burnTicks;}
        stepStatuses(data.statuses,bossStatus,t);
        // 2. Boss brain.
        bool busy=false;for(const auto& c:casts)if(c.boss&&c.t.live)busy=true;
        std::vector<uint32_t> alive;if(const auto* p=bossPhase(data.bosses,brain)){alive.assign(p->adds.size(),0);for(const auto& a:adds)if(a.alive&&a.wave<alive.size())++alive[a.wave];}
        BossInput in;in.hp=std::max<int64_t>(bossHp,0);in.maxHp=cfg.bossMaxHp;in.targetDistance=(hero-boss).length();in.busy=busy;in.aliveAdds=alive;
        auto step=stepBoss(data.bosses,data.attacks,brain,t,in,rng);if(!step)break;
        for(const auto& e:step->events){
            if(e.kind==BossEventKind::PhaseEntered){++rep.phases;say(t,"phase "+std::to_string(e.value));}
            else if(e.kind==BossEventKind::Enraged){rep.enraged=true;say(t,"enraged");}
            else if(e.kind==BossEventKind::Defeated){rep.bossDefeated=true;say(t,"boss defeated");}
            else if(e.kind==BossEventKind::SpawnAdds){
                for(uint32_t i=0;i<e.count;++i){
                    auto turn=rng.unit();auto r=e.minRadius+(e.maxRadius-e.minRadius)*rng.unit();
                    ScenarioAdd a;a.id=nextId++;a.hp=cfg.addHp;a.wave=uint32_t(e.value);a.position=boss+areaForward(turn)*r;adds.push_back(a);++rep.addsSpawned;
                }
                say(t,"adds +"+std::to_string(e.count));
            }else if(e.kind==BossEventKind::Attack){
                CasterPose cp{boss,yawTowards(hero-boss,{}),true};
                auto inst=beginTelegraph(data.attacks,e.value,nextCast++,t,bossId,2,cp,hero,heroId);
                if(inst){ScenarioCast c{*inst,true,areaHits(data.attacks.at(e.value).area,inst->pose,hero,cfg.heroRadius),step->damageMilli};casts.push_back(c);++rep.attacks;say(t,"attack "+std::to_string(e.value));}
            }
        }
        if(rep.bossDefeated)break;
        // 3. Adds: hunt the hero with swarm pressure, bite when close.
        std::vector<MobNeighbour> crowd;for(const auto& a:adds)if(a.alive)crowd.push_back({a.id,a.position,cfg.addRadius});
        const MobCandidate heroCandidate{heroId,hero,true,true};
        for(auto& a:adds){
            if(!a.alive)continue;
            bool locked=false;
            for(const auto& c:casts)if(!c.boss&&c.t.source==a.id&&telegraphLocksMovement(data.attacks,c.t,t))locked=true;
            MobSelf self{a.id,a.position,a.position,boss,cfg.addRadius,a.hp*1000/std::max<int64_t>(cfg.addHp,1),0,0,locked};
            auto d=stepMob(data.mob,a.mob,t,self,std::span<const MobCandidate>(&heroCandidate,1),crowd,rng,[](Vec2,Vec2){return true;});
            if(!d)continue;
            if(!locked)a.position=moveBy(a.position,d->desired,cfg.addSpeed);
            bool casting=false;for(const auto& c:casts)if(!c.boss&&c.t.source==a.id&&c.t.live)casting=true;
            if(cfg.addAttack&&d->mode==MobMode::Hunt&&!d->approach&&!casting){
                auto inst=beginTelegraph(data.attacks,cfg.addAttack,nextCast++,t,a.id,2,{a.position,yawTowards(hero-a.position,{}),true},hero,heroId);
                if(inst)casts.push_back({*inst,false,true,1000});
            }
        }
        // 4. Telegraphs resolve against the hero.
        TelegraphTarget heroTarget{heroId,1,hero,cfg.heroRadius,true};
        for(auto& c:casts){
            if(!c.t.live)continue;
            Vec2 casterPos=boss;bool casterAlive=bossHp>0;
            if(!c.boss){casterAlive=false;for(const auto& a:adds)if(a.id==c.t.source&&a.alive){casterPos=a.position;casterAlive=true;}}
            auto r=stepTelegraph(data.attacks,c.t,t,{casterPos,c.t.pose.yaw,casterAlive},hero,std::span<const TelegraphTarget>(&heroTarget,1));
            if(!r)continue;
            for(const auto& h:r->hits){applyHit(t,h,c.damageMilli);c.hitHero=true;}
            for(auto ev:r->events){if(ev==TelegraphEvent::Strike)++rep.strikes;if(ev==TelegraphEvent::Ended&&c.boss&&c.threatened&&!c.hitHero)++rep.dodged;}
            if(c.boss&&data.attacks.at(c.t.attack).sweep&&c.t.released&&c.t.live){auto reach=sweepReach(data.attacks,c.t,t);boss=c.t.caster+areaForward(c.t.pose.yaw)*reach;}
        }
        casts.erase(std::remove_if(casts.begin(),casts.end(),[](const ScenarioCast& c){return !c.t.live;}),casts.end());
        if(heroHp<=0){rep.heroDefeated=true;say(t,"hero defeated");break;}
        // 5. Hero: dodge visible windups, otherwise keep the ideal distance; shoot adds close by, else the boss.
        Fixed speed=cfg.heroSpeed*Fixed::fromRatio(hs->speedMilli,1000);
        Vec2 move{};bool dodging=false;
        for(const auto& c:casts){
            auto view=telegraphView(data.attacks,c.t,t);
            if(view.phase!=AttackPhase::Windup||t-c.t.started<cfg.reaction)continue;
            const auto& def=data.attacks.at(c.t.attack);if(!areaHits(def.area,c.t.pose,hero,cfg.heroRadius))continue;
            dodging=true;
            if(def.area.shape==AreaShape::Line){auto right=areaRight(c.t.pose.yaw);auto side=(hero-c.t.pose.origin).dot(right)<Fixed{}?-1_fx:1_fx;move+=right*side;}
            else if(def.area.shape==AreaShape::Ring){auto out=hero-c.t.pose.origin;auto d=out.length();move+=d.raw?(d<(def.area.radius+def.area.inner)/2?out/d*-1_fx:out/d):Vec2{1_fx,{}};}
            else{auto out=hero-c.t.pose.origin;auto d=out.length();move+=d.raw?out/d:areaRight(c.t.pose.yaw);}
        }
        if(!dodging){
            auto to=boss-hero;auto d=to.length();
            if(d.raw&&d>cfg.heroIdeal+1_fx)move=to/d;else if(d.raw&&d<cfg.heroIdeal-1_fx)move=to/d*-1_fx;
        }
        if(auto l=move.length();l.raw>0&&!hs->stunned)hero=hero+move/l*speed;
        if(heroCooldown>0)--heroCooldown;
        if(!hs->stunned&&heroCooldown==0){
            ScenarioAdd* pick=nullptr;for(auto& a:adds)if(a.alive&&(a.position-hero).lengthSq()<=16_fx&&(!pick||(a.position-hero).lengthSq()<(pick->position-hero).lengthSq()))pick=&a;
            if(pick){pick->hp-=cfg.heroDamage;heroCooldown=cfg.heroAttackEvery;if(pick->hp<=0){pick->alive=false;++rep.addsKilled;}}
            else if((boss-hero).length()<=cfg.heroRange){bossHp-=cfg.heroDamage;heroCooldown=cfg.heroAttackEvery;}
        }
        // 6. Boss walks to the hero unless an attack holds its pose.
        bool locked=false;for(const auto& c:casts)if(c.boss&&telegraphLocksMovement(data.attacks,c.t,t))locked=true;
        if(!locked){auto to=hero-boss;auto d=to.length();if(d>cfg.bossReach)boss=boss+to/d*(cfg.bossSpeed*Fixed::fromRatio(step->speedMilli,1000));}
        // 7. Checksum of everything that decides the outcome.
        Hasher h;h.u64(rep.hash);h.u32(t);h.fx(hero.x);h.fx(hero.y);h.fx(boss.x);h.fx(boss.y);h.i64(heroHp);h.i64(bossHp);
        hashEncounterState(h,brain);hashEncounterState(h,heroStatus);for(const auto& c:casts)hashEncounterState(h,c.t);
        for(const auto& a:adds){h.i64(a.id);h.i64(a.hp);h.fx(a.position.x);h.fx(a.position.y);hashEncounterState(h,a.mob);}
        h.u64(rng.state());rep.hash=h.value();
    }
    rep.ticks=t;rep.timedOut=!rep.bossDefeated&&!rep.heroDefeated;rep.heroHp=heroHp;rep.bossHp=bossHp;
    return rep;
}
}
