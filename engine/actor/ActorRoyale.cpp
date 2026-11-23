// Battle-royale wiring of the FPS module over the battle-royale package (libraries/.../genre/royale): air
// insertion, phased safe zone, bombardment, supply drops and last-team-standing standings. The package
// decides and reports; this file owns health, positions, loot and the winner. SimEvents:
// 5 = kill feed (player = actor, value = victim | kind << 8), 6 = zone (value = phase | kind << 8),
// 7 = supply drop (value = id | kind << 16, pos = point), 8 = shell impact (pos = centre, value = damage);
// 9 = gear piece broke (player = wearer, value = gear slot; ActorSystems damage).
#include "ActorModule.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
namespace aiverse::actor {
using namespace lib::mechanics;
namespace {
uint64_t seedFrom(Simulation& sim,uint32_t salt){Random g=sim.rng();uint64_t a=g.next(),b=g.next();return (a<<32|b)^(uint64_t(salt)*0x9e3779b97f4a7c15ull);}
}
void ActorHost::setupRoyale(Simulation& sim,const Json& map) {
    auto& s=state(sim);const size_t count=characterCount(sim);
    if(map.has("royale"))config_.royale=readRoyale(map["royale"],"map.royale");
    if(!config_.royale)return;
    if(config_.mode!="battleRoyale")throw JsonError("actor.royale: requires fps.mode \"battleRoyale\"");
    auto& r=*config_.royale;auto& run=s.royale;run.enabled=true;
    run.zone=beginSafeZone(seedFrom(sim,1));
    run.bombardment=beginBombardment(r.bombardment.value_or(BombardmentDef{}),seedFrom(sim,2));
    run.drops=beginSupplyDrops(seedFrom(sim,3));
    if(r.flight){
        auto f=planInsertionFlight(*r.flight,r.bounds,seedFrom(sim,4),0);if(!f)throw JsonError("actor.royale.flight: path does not fit");
        run.flight=*f;run.divers.assign(count,SkydiverState{});
        forEachCharacter(sim,[&](CharacterRef a){a.pos=insertionPosition(run.flight,0);});
    }
    forEachCharacter(sim,[&](CharacterRef a){addRoyaleCompetitor(run.standings,a.slot,a.team);});
    if(!startRoyaleStandings(run.standings,0))throw JsonError("actor.royale: invalid standings");
    run.lastAttacker.assign(count,0);run.lastWeapon.assign(count,0);
}
Fixed ActorHost::groundBelow(Vec3 p) const {
    // Short segments keep the hit fraction precise (Q16 fractions of a 100 km ray would be metres off).
    constexpr auto step=256_fx;auto top=p+Vec3{{},1_fx,{}};
    for(int i=0;i<1024&&top.y> -config_.worldLimit;++i,top.y-=step){auto hit=collision_.cast(top,{{},-step,{}});if(hit.hit)return top.y-step*hit.fraction;}
    return Fixed{};
}
// Moves an actor that has not landed yet. Returns true while it is still in the air (skip the ground motor).
bool ActorHost::skydive(Simulation& sim,CharacterRef a) {
    auto& run=state(sim).royale;
    if(!run.enabled||run.divers.empty()||!config_.royale->flight)return false;
    auto& d=run.divers[a.slot-1];if(d.phase==SkydivePhase::Landed)return false;
    const auto& r=*config_.royale;SkydiveInput in;
    Input input=a.input;if(sim.tick()-a.lastInputTick>config_.inputTimeout){input.forward=input.right=0;input.buttons=0;}
    Fixed f=Fixed::fromRatio(input.forward,1000),rt=Fixed::fromRatio(input.right,1000);
    Vec2 forward{fxSinTurns(input.yaw),fxCosTurns(input.yaw)},right{forward.y,-forward.x};
    in.move=forward*f+right*rt;in.move={fxClamp(in.move.x,-1_fx,1_fx),fxClamp(in.move.y,-1_fx,1_fx)};
    in.jump=in.deploy=(input.buttons&Jump)!=0;in.dive=(input.buttons&Sprint)!=0;
    if(a.health<=0)in={};   // a body keeps falling to the ground
    else if(bots_[a.slot-1]&&a.lastSequence==0){
        // Fallback bots: seeded jump spread along the path, then steer to their spawn and let the chute open itself.
        auto span=run.flight.durationTicks>r.flight->doorOpenTicks?run.flight.durationTicks-r.flight->doorOpenTicks:1u;
        auto jumpAt=run.flight.startTick+r.flight->doorOpenTicks+uint32_t(seedFrom(sim,100+a.slot)%span);
        in={};in.jump=sim.tick()>=jumpAt;
        if(d.phase!=SkydivePhase::Aboard){auto to=spawns_[a.slot-1].xz()-d.position.xz();if(to.lengthSq()>1_fx){auto n=fxNormalize(Vec3{to.x,{},to.y});in.move={n.x,n.z};}}
    }
    auto ground=d.phase==SkydivePhase::Aboard?Fixed{}:groundBelow(d.position);
    auto events=stepSkydiver(r.drop,*r.flight,run.flight,d,in,ground,sim.tick());
    if(!events)return true;
    a.pos=d.position;a.velocity=d.velocity*Fixed::fromInt(config_.tickRate);a.yaw=input.yaw;a.pitch=input.pitch;
    if(!reportRegionMoveIfCrossed(sim,a.entity))
        throw std::logic_error("regionOwnership: skydiver move dirty report failed");
    a.grounded=d.phase==SkydivePhase::Landed;
    a.input.buttons&=~uint32_t(Jump);
    return d.phase!=SkydivePhase::Landed;
}
void ActorHost::steerRoyaleBot(Simulation& sim,CharacterRef a) {
    auto& run=state(sim).royale;if(!run.enabled||a.input.buttons||a.input.forward)return;
    auto target=safeZoneTarget(config_.royale->zone,run.zone);
    if(insideCircle(RoyaleCircle{target.center,target.radius*Fixed::fromRatio(9,10)},a.pos))return;
    auto delta=target.center-a.pos.xz();auto yaw=fxAtan2Turns(delta.x,delta.y);if(yaw<Fixed{})yaw+=1_fx;
    a.input.yaw=yaw;a.input.forward=1000;a.input.buttons|=Sprint;
}
void ActorHost::tickRoyale(Simulation& sim) {
    auto& s=state(sim);auto& run=s.royale;if(!run.enabled||run.standings.finished)return;
    const auto& r=*config_.royale;auto now=sim.tick();
    if(auto events=stepSafeZone(r.zone,run.zone,now))for(const auto& e:*events)sim.emit({6,0,{},{},int64_t(e.phase)|int64_t(uint8_t(e.kind))<<8,Vec3{e.circle.center.x,{},e.circle.center.y}});
    auto circle=safeZoneCircle(r.zone,run.zone,now);
    auto landed=[&](const CharacterRef& a){return run.divers.empty()||run.divers[a.slot-1].phase==SkydivePhase::Landed;};
    if(auto hit=safeZoneDamage(r.zone,run.zone,now))forEachCharacter(sim,[&](CharacterRef a){if(a.health>0&&landed(a)&&!insideCircle(circle,a.pos))damage(sim,0,a,hit);});
    if(r.bombardment)if(auto events=stepBombardment(*r.bombardment,run.bombardment,circle,now))for(const auto& e:*events){
        if(e.kind!=BombardmentEventKind::Shell)continue;
        sim.emit({8,0,{},{},e.damage,Vec3{e.circle.center.x,groundBelow({e.circle.center.x,500_fx,e.circle.center.y}),e.circle.center.y}});
        forEachCharacter(sim,[&](CharacterRef a){
            if(a.health<=0||!landed(a)||!insideCircle(e.circle,a.pos))return;
            auto head=a.pos+Vec3{{},stanceHeight(a,config_.motor),{}};if(collision_.cast(head,{{},100_fx,{}}).hit)return;   // under a roof
            damage(sim,0,a,e.damage);
        });
    }
    if(r.supplyDrops)if(auto events=stepSupplyDrops(*r.supplyDrops,run.drops,safeZoneTarget(r.zone,run.zone),now))for(const auto& e:*events){
        Vec3 at{e.point.x,groundBelow({e.point.x,1000_fx,e.point.y}),e.point.y};
        sim.emit({7,0,{},{},int64_t(e.id)|int64_t(uint8_t(e.kind))<<16,at});
        if(e.kind==SupplyDropEventKind::Landed)for(auto item:config_.crate)if(sim.world().countExisting<Loot>()<kMaxLoot){
            Loot l{s.nextObject++,at,item.type,item.count};
            if(item.type>=100){const auto& w=config_.weapons[size_t(item.type-100)];l.count=1;l.ammo=w.magazine;l.reserve=w.reserve;}
            spawnLoot(sim,l);
        }
    }
}
// Mirrors downs / outs / revives into the standings and applies their verdicts (finished knocked mates, winner).
void ActorHost::settleRoyale(Simulation& sim) {
    auto& s=state(sim);auto& run=s.royale;if(!run.enabled||run.standings.finished)return;
    const auto& cfg=config_.royale->standings;auto now=sim.tick();
    std::vector<std::pair<int64_t,int64_t>> outs;std::vector<KillFeedEvent> feed;
    forEachCharacter(sim,[&](CharacterRef a){
        auto& c=run.standings.players[a.slot];auto attacker=int64_t(run.lastAttacker[a.slot-1]),weapon=int64_t(run.lastWeapon[a.slot-1]);
        if(a.health<=0&&c.life!=RoyaleLife::Out)outs.push_back({a.slot,attacker});
        else if(a.health>0&&a.downed&&c.life==RoyaleLife::Standing){if(auto e=reportDown(cfg,run.standings,a.slot,attacker,weapon,now))feed.insert(feed.end(),e->begin(),e->end());}
        else if(a.health>0&&!a.downed&&c.life==RoyaleLife::Knocked){
            int64_t reviver=0;Fixed best;
            forEachCharacter(sim,[&](CharacterRef m){if(m.team==a.team&&m.slot!=a.slot&&m.health>0&&!m.downed){auto d=(m.pos-a.pos).lengthSq();if(!reviver||d<best){reviver=m.slot;best=d;}}});
            if(reviver)if(auto e=reportRevive(run.standings,a.slot,reviver,now))feed.insert(feed.end(),e->begin(),e->end());
        }
    });
    if(!outs.empty())if(auto e=reportOutTogether(cfg,run.standings,outs,now))feed.insert(feed.end(),e->begin(),e->end());
    for(const auto& e:feed){
        run.feed.push_back(e);if(run.feed.size()>6)run.feed.erase(run.feed.begin());
        if(e.kind==KillFeedKind::Knocked||e.kind==KillFeedKind::Eliminated||e.kind==KillFeedKind::Revived)
            sim.emit({5,uint8_t(e.actor),{},{},e.subject|int64_t(uint8_t(e.kind))<<8,character(sim,uint8_t(e.subject)).pos});
        else sim.emit({5,0,{},{},e.subject|int64_t(uint8_t(e.kind))<<8,{}});
    }
    forEachCharacter(sim,[&](CharacterRef a){if(a.health>0&&run.standings.players[a.slot].life==RoyaleLife::Out){   // finished while knocked
        a.health=0;a.downed=false;a.bleedUntil=0;sim.emit({2,uint8_t(run.standings.players[a.slot].knockedBy),{},{},a.slot,a.pos});
    }});
    if(run.standings.finished)s.winner=run.standings.winner>0?int32_t(run.standings.winner):-1;
}
void io(Archive& a,RoyaleView& v){
    a(v.enabled);if(!v.enabled)return;
    a(v.stage,v.phase,v.divePhase,v.ticksLeft,v.alive,v.teamsIn,v.placement,v.kills,v.circle.center.x,v.circle.center.y,v.circle.radius,
      v.target.center.x,v.target.center.y,v.target.radius,v.carrier,v.bombardment,v.bombArea.center.x,v.bombArea.center.y,v.bombArea.radius,v.crates,
      v.feed,v.finished,v.doorOpen,v.outside,v.winner,v.teams);
}
RoyaleView ActorHost::royaleView(Simulation& sim,uint8_t slot) const {
    RoyaleView v;const auto& run=state(sim).royale;if(!run.enabled||!slot||slot>run.lastAttacker.size())return v;
    const auto& r=*config_.royale;auto now=sim.tick();v.enabled=true;
    v.stage=uint8_t(run.zone.stage);v.phase=uint8_t(run.zone.phase);v.ticksLeft=safeZoneTicksLeft(r.zone,run.zone,now);
    v.circle=safeZoneCircle(r.zone,run.zone,now);v.target=safeZoneTarget(r.zone,run.zone);
    if(!run.divers.empty()){v.divePhase=uint8_t(run.divers[slot-1].phase);v.carrier=insertionPosition(run.flight,now);}
    for(const auto& [id,c]:run.standings.players)v.alive+=c.life!=RoyaleLife::Out;
    v.teamsIn=uint16_t(royaleTeamsIn(run.standings));
    const auto& own=run.standings.players.at(slot);v.kills=uint16_t(own.kills);v.placement=uint16_t(run.standings.teams.at(own.team).placement);
    v.bombardment=run.bombardment.stage==BombardmentStage::Warned||run.bombardment.stage==BombardmentStage::Active;v.bombArea=run.bombardment.area;
    v.feed=run.feed;v.finished=run.standings.finished;v.winner=int32_t(run.standings.winner);v.teams=uint16_t(run.standings.teams.size());
    v.outside=!insideCircle(v.circle,character(sim,slot).pos);if(r.flight)v.doorOpen=insertionDoorOpen(*r.flight,run.flight,now);
    if(r.supplyDrops)for(const auto& f:run.drops.falling)if(v.crates.size()<8)v.crates.push_back(supplyDropPosition(*r.supplyDrops,f,now,groundBelow({f.point.x,1000_fx,f.point.y})));
    return v;
}
}
