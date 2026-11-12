#pragma once
// Summons (match-progression package): summon circle objectives (stand inside to fill progress; leaving
// resets it; single use or cooldown; restore a dead acquired ally or recruit a new one), player summon charges
// with recharge, and the lifecycle of summoned allies (owner, deck slot, unit, lifetime, death, restore,
// owner removal). The policy emits SummonRequests; the owner spawns the unit and confirms with the new entity id
// (cancel keeps the point unused). View helper summonPointView gives the circle state for HUD/visuals.
// Fixed math, integer ticks, ascending point/player/ally ids, ticks strictly increase.
#include "engine/core/Fixed.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kSummonMaxPoints=4096,kSummonMaxAllies=65536,kSummonMaxPlayers=64;
inline constexpr Fixed kSummonCoordLimit=Fixed::fromInt(100000);
inline constexpr uint32_t kSummonMaxTicks=10000000;

enum class SummonAction:uint8_t {Restore,Recruit,RestoreOrRecruit};
struct SummonPointDef {int64_t id=0,team=0;Vec3 position{};Fixed radius=3_fx;uint32_t progressTicks=0,cooldownTicks=0;bool singleUse=true;SummonAction action=SummonAction::Restore;}; // 0 ticks = config default, team 0 = anyone
struct SummonConfig {
    uint32_t progressTicks=180,cooldownTicks=0,maxAllies=15,charges=0,chargeRecharge=0; // charges 0 = points need no charge
    bool resetOnExit=true;
};
inline std::string summonConfigError(const SummonConfig& c){
    if(c.progressTicks<1||c.progressTicks>kSummonMaxTicks||c.cooldownTicks>kSummonMaxTicks||c.chargeRecharge>kSummonMaxTicks)return "summons: ticks must be within 1..10000000";
    if(c.maxAllies>kSummonMaxAllies||c.charges>1000)return "summons.maxAllies: must be 0..65536 (charges 0..1000)";
    return {};
}
inline std::string summonPointError(const SummonPointDef& p){
    auto b=[](Fixed v){return v>= -kSummonCoordLimit&&v<=kSummonCoordLimit;};
    if(p.id<=0||p.team<0)return "summon point: id must be positive, team >= 0";
    if(!b(p.position.x)||!b(p.position.y)||!b(p.position.z)||p.radius<=Fixed{}||p.radius>Fixed::fromInt(1000))return "summon point "+std::to_string(p.id)+": position/radius out of range";
    if(p.progressTicks>kSummonMaxTicks||p.cooldownTicks>kSummonMaxTicks||uint8_t(p.action)>2)return "summon point "+std::to_string(p.id)+": ticks/action out of range";
    return {};
}
struct AllyRecord {int64_t entity=0,owner=0,unit=0;int32_t slot=-1;uint32_t spawnedAt=0,expiresAt=0;bool alive=true,acquired=true;}; // expiresAt 0 = permanent
struct SummonPointState {int64_t occupant=0;uint32_t progress=0,readyAt=0;bool used=false,pending=false;};
struct PlayerSummons {uint32_t charges=0,nextChargeAt=0;};
struct SummonState {std::map<int64_t,SummonPointState> points;std::map<int64_t,AllyRecord> allies;std::map<int64_t,PlayerSummons> players;uint32_t lastTick=0;bool initialized=false;};
// Per tick: players near points (owner gate canSummon: match progressing, hero alive, no modal choice, ...).
struct SummonPresence {int64_t player=0,team=0;Vec3 position{};bool canSummon=true;};
struct SummonInput {std::vector<SummonPresence> players;std::vector<int64_t> died,removed;}; // ally entity ids
// Restore: respawn `restore` (a dead acquired ally: same slot/unit); Recruit: the owner picks the deck slot.
struct SummonRequest {int64_t point=0,player=0,restore=0,unit=0;int32_t slot=-1;bool recruit=false;};
enum class SummonEventKind:uint8_t {Entered,Left,ProgressReset,Requested,Confirmed,Cancelled,PointUsed,PointReady,AllyDied,AllyExpired,AllyRemoved,ChargeGained,ChargeUsed};
struct SummonEvent {SummonEventKind kind=SummonEventKind::Entered;int64_t point=0,player=0,ally=0,value=0;};
struct SummonReport {std::vector<SummonEvent> events;std::vector<SummonRequest> requests;std::vector<int64_t> despawn;};
enum class SummonView:uint8_t {Ready,Progress,Used,Cooldown,SquadFull,Blocked,Pending};

inline uint32_t aliveAllies(const SummonState& s,int64_t player){uint32_t n=0;for(const auto& [id,a]:s.allies)if(a.owner==player&&a.alive)++n;return n;}
inline const AllyRecord* restoreCandidate(const SummonState& s,int64_t player){ // lowest slot, then lowest entity
    const AllyRecord* best=nullptr;for(const auto& [id,a]:s.allies)if(a.owner==player&&!a.alive&&a.acquired&&(!best||a.slot<best->slot))best=&a;return best;
}
inline uint32_t pointTicks(const SummonConfig& c,const SummonPointDef& p){return p.progressTicks?p.progressTicks:c.progressTicks;}
// What a point can do for this player now (nullopt request kind = nothing).
inline std::optional<SummonRequest> summonFor(const SummonConfig& c,const SummonPointDef& p,const SummonState& s,int64_t player){
    bool full=c.maxAllies&&aliveAllies(s,player)>=c.maxAllies;
    if(p.action!=SummonAction::Recruit)if(const auto* a=restoreCandidate(s,player);a&&!full)return SummonRequest{p.id,player,a->entity,a->unit,a->slot,false};
    if(p.action!=SummonAction::Restore&&!full)return SummonRequest{p.id,player,0,0,-1,true};
    return std::nullopt;
}
inline SummonView summonPointView(const SummonConfig& c,const SummonPointDef& p,const SummonState& s,int64_t player,uint32_t tick,int64_t* progressMilli=nullptr){
    auto it=s.points.find(p.id);SummonPointState ps=it==s.points.end()?SummonPointState{}:it->second;
    if(progressMilli)*progressMilli=ps.occupant==player?int64_t(ps.progress)*1000/pointTicks(c,p):0;
    if(ps.used)return SummonView::Used;
    if(ps.pending)return SummonView::Pending;
    if(tick<ps.readyAt)return SummonView::Cooldown;
    if(c.maxAllies&&aliveAllies(s,player)>=c.maxAllies)return SummonView::SquadFull;
    if(!summonFor(c,p,s,player))return SummonView::Blocked;
    if(c.charges){auto pl=s.players.find(player);if(pl==s.players.end()||!pl->second.charges)return SummonView::Blocked;}
    return ps.occupant==player&&ps.progress?SummonView::Progress:SummonView::Ready;
}
inline bool valid(const SummonState& s){
    if(s.points.size()>kSummonMaxPoints||s.allies.size()>kSummonMaxAllies||s.players.size()>kSummonMaxPlayers)return false;
    for(const auto& [id,p]:s.points)if(id<=0||p.occupant<0||p.progress>kSummonMaxTicks||(p.used&&p.pending))return false;
    for(const auto& [id,a]:s.allies)if(id<=0||a.entity!=id||a.owner<=0||a.slot< -1||(a.expiresAt&&a.expiresAt<a.spawnedAt))return false;
    for(const auto& [id,p]:s.players)if(id<=0||p.charges>1000)return false;
    return true;
}
// Initialize charges for a player (call once at match start).
inline bool addSummoner(const SummonConfig& c,SummonState& s,int64_t player,uint32_t tick){
    if(player<=0||s.players.contains(player)||s.players.size()>=kSummonMaxPlayers)return false;
    (void)tick;s.players[player]={c.charges,0};return true;
}
// Spend a charge for a non-point summon ability; false when none is available.
inline bool useSummonCharge(SummonState& s,int64_t player){auto it=s.players.find(player);if(it==s.players.end()||!it->second.charges)return false;--it->second.charges;return true;}
inline std::optional<SummonReport> stepSummons(const SummonConfig& c,const std::vector<SummonPointDef>& defs,SummonState& state,uint32_t tick,const SummonInput& in){
    if(!summonConfigError(c).empty()||(state.initialized&&tick<=state.lastTick)||defs.size()>kSummonMaxPoints||in.players.size()>kSummonMaxPlayers)return std::nullopt;
    for(std::size_t i=0;i<defs.size();++i)if(!summonPointError(defs[i]).empty()||(i&&defs[i].id<=defs[i-1].id))return std::nullopt;
    auto players=in.players;std::sort(players.begin(),players.end(),[](const SummonPresence& a,const SummonPresence& b){return a.player<b.player;});
    for(std::size_t i=0;i<players.size();++i)if(players[i].player<=0||(i&&players[i].player==players[i-1].player))return std::nullopt;
    auto s=state;SummonReport r;auto ev=[&](SummonEventKind k,int64_t point,int64_t player,int64_t ally=0,int64_t value=0){r.events.push_back({k,point,player,ally,value});};
    // Ally lifecycle: deaths, removals, expiry.
    auto died=in.died;std::sort(died.begin(),died.end());
    for(auto id:died)if(auto a=s.allies.find(id);a!=s.allies.end()&&a->second.alive){a->second.alive=false;ev(SummonEventKind::AllyDied,0,a->second.owner,id);if(!a->second.acquired)s.allies.erase(a);}
    auto removed=in.removed;std::sort(removed.begin(),removed.end());
    for(auto id:removed)if(auto a=s.allies.find(id);a!=s.allies.end()){ev(SummonEventKind::AllyRemoved,0,a->second.owner,id);s.allies.erase(a);}
    for(auto it=s.allies.begin();it!=s.allies.end();){
        if(it->second.alive&&it->second.expiresAt&&tick>=it->second.expiresAt){ev(SummonEventKind::AllyExpired,0,it->second.owner,it->first);r.despawn.push_back(it->first);it=s.allies.erase(it);}
        else ++it;
    }
    // Charges.
    for(auto& [id,p]:s.players)if(c.chargeRecharge&&p.charges<c.charges&&p.nextChargeAt&&tick>=p.nextChargeAt){++p.charges;p.nextChargeAt=p.charges<c.charges?tick+c.chargeRecharge:0;ev(SummonEventKind::ChargeGained,0,id,0,p.charges);}
    // Points: occupant = lowest player id inside (eligible team) that can summon there.
    for(const auto& def:defs){
        auto& ps=s.points[def.id];
        if(ps.readyAt&&tick==ps.readyAt)ev(SummonEventKind::PointReady,def.id,0);
        int64_t inside=0;
        for(const auto& p:players){
            if(def.team&&p.team!=def.team)continue;
            auto dx=p.position.x-def.position.x,dz=p.position.z-def.position.z;if(dx*dx+dz*dz>def.radius*def.radius)continue;
            if(!p.canSummon||ps.used||ps.pending||tick<ps.readyAt||!summonFor(c,def,s,p.player))continue;
            if(c.charges){auto pl=s.players.find(p.player);if(pl==s.players.end()||!pl->second.charges)continue;}
            inside=p.player;break;
        }
        if(inside!=ps.occupant){
            if(ps.occupant){ev(SummonEventKind::Left,def.id,ps.occupant);if(ps.progress&&c.resetOnExit){ev(SummonEventKind::ProgressReset,def.id,ps.occupant,0,ps.progress);ps.progress=0;}}
            if(inside)ev(SummonEventKind::Entered,def.id,inside);
            ps.occupant=inside;
        }
        if(!inside)continue;
        if(++ps.progress<pointTicks(c,def))continue;
        auto req=*summonFor(c,def,s,inside);ps.progress=0;ps.pending=true;
        if(c.charges){--s.players[inside].charges;ev(SummonEventKind::ChargeUsed,def.id,inside,0,s.players[inside].charges);if(c.chargeRecharge&&!s.players[inside].nextChargeAt)s.players[inside].nextChargeAt=tick+c.chargeRecharge;}
        ev(SummonEventKind::Requested,def.id,inside,req.restore);r.requests.push_back(req);
    }
    s.lastTick=tick;s.initialized=true;if(!valid(s))return std::nullopt;state=std::move(s);return r;
}
// The owner spawned the unit of a request: register the ally (restores replace the dead record), consume or
// cool down the point. lifetime 0 = permanent; acquired allies can be restored after death.
inline bool confirmSummon(const SummonConfig& c,const std::vector<SummonPointDef>& defs,SummonState& s,const SummonRequest& q,int64_t entity,int64_t unit,int32_t slot,uint32_t tick,uint32_t lifetime=0,bool acquired=true){
    if(entity<=0||s.allies.contains(entity)||s.allies.size()>=kSummonMaxAllies||q.player<=0||slot< -1)return false;
    const SummonPointDef* def=nullptr;for(const auto& d:defs)if(d.id==q.point)def=&d;
    if(q.point&&(!def||!s.points[q.point].pending))return false;
    if(q.restore){auto a=s.allies.find(q.restore);if(a==s.allies.end()||a->second.alive||a->second.owner!=q.player)return false;s.allies.erase(a);}
    s.allies[entity]={entity,q.player,unit,slot,tick,lifetime?tick+lifetime:0,true,acquired};
    if(def){auto& ps=s.points[q.point];ps.pending=false;ps.progress=0;if(def->singleUse)ps.used=true;else{auto cd=def->cooldownTicks?def->cooldownTicks:c.cooldownTicks;ps.readyAt=cd?tick+cd:0;}}
    return true;
}
// The owner could not spawn (no free spot, match ended): the point becomes available again, charge refunded.
inline bool cancelSummon(const SummonConfig& c,SummonState& s,const SummonRequest& q){
    auto it=s.points.find(q.point);if(it==s.points.end()||!it->second.pending)return false;it->second.pending=false;
    if(c.charges)if(auto p=s.players.find(q.player);p!=s.players.end()&&p->second.charges<c.charges)++p->second.charges;
    return true;
}
// Register an ally spawned outside points (recruit from a level-up, scripted reinforcements).
inline bool registerAlly(SummonState& s,const AllyRecord& a){
    if(a.entity<=0||a.owner<=0||s.allies.contains(a.entity)||s.allies.size()>=kSummonMaxAllies||(a.expiresAt&&a.expiresAt<a.spawnedAt))return false;
    s.allies[a.entity]=a;s.allies[a.entity].alive=true;return true;
}
// A player left or was defeated: forget its allies and return the live ones for the owner to despawn.
inline std::vector<int64_t> removeSummoner(SummonState& s,int64_t player){
    std::vector<int64_t> out;for(auto it=s.allies.begin();it!=s.allies.end();){if(it->second.owner==player){if(it->second.alive)out.push_back(it->first);it=s.allies.erase(it);}else ++it;}
    s.players.erase(player);for(auto& [id,p]:s.points)if(p.occupant==player){p.occupant=0;p.progress=0;}
    return out;
}
}
