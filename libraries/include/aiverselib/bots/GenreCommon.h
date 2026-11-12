#pragma once
// Shared pieces of the genre bot adapters (genre-bots): difficulty knobs, the owner-supplied perception
// record, target selection and deterministic aim error. Bots never write simulation state: every policy
// returns the same input struct a player controller produces, and the authoritative owner validates and
// applies it through its ordinary command path. Fixed math, integer ticks, RNG only through the passed-in
// engine Random (sim.rng() or an RNG the owner saves with the simulation).
#include "../genre/Melee.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::bots {
inline constexpr Fixed kBotCoordLimit=Fixed::fromInt(100000);
inline constexpr Fixed kBotSizeLimit=Fixed::fromInt(1000);
inline constexpr std::size_t kBotMaxTargets=1024;

// ---- Difficulty: perception delay, re-plan cadence, aim error and telegraph misreads. Difficulty never
// changes damage, health, cooldowns or fire rate; it only changes which inputs the bot produces and when.
struct BotDifficulty {
    uint32_t reactionTicks=8;                 // ticks from first perceiving a target/telegraph to acting on it
    uint32_t decisionTicks=4;                 // movement/cover/aim-error re-plan interval (>=1)
    Fixed aimError=Fixed::fromRatio(1,200);   // max |yaw| and |pitch| aim error in turns, re-sampled each decision
    int64_t misreadMilli=0;                   // chance 0..1000 to ignore an opponent's telegraphed attack
};
inline bool valid(const BotDifficulty& d){
    return d.reactionTicks<=3600&&d.decisionTicks>=1&&d.decisionTicks<=3600&&d.aimError>=Fixed{}&&d.aimError<=Fixed::fromRatio(1,8)&&d.misreadMilli>=0&&d.misreadMilli<=1000;
}
// 0 easy, 1 normal, 2 hard. Projects may author their own values instead (genreBots settings section).
inline std::optional<BotDifficulty> difficultyPreset(int level){
    switch(level){
    case 0:return BotDifficulty{20,8,Fixed::fromRatio(1,40),600};
    case 1:return BotDifficulty{5,4,Fixed::fromRatio(1,200),150};
    case 2:return BotDifficulty{2,2,Fixed::fromRatio(1,2000),0};
    default:return std::nullopt;
    }
}

// ---- Perception. The owner fills these from visibility-authorized senses (LOS/FOV/hearing), never from
// omniscient state: an invisible target must be omitted or marked visible=false.
// A telegraphed melee attack as an observer sees it (animation phase); attack 0 = not attacking.
struct MeleeThreat {int64_t attack=0;uint32_t started=0,windup=0,active=0;Fixed reach{};bool unblockable=false;};
struct PerceivedTarget {
    int64_t id=0,team=0;            // stable positive id; team 0 = unaffiliated (always hostile)
    Vec3 position{},velocity{};     // metres, metres/second (velocity drives shooter lead)
    Fixed yaw{};                    // facing in turns (0 faces +Z)
    bool visible=true;              // false: remembered but not currently seen (never shot at, never locked)
    bool guarding=false;            // guard visibly raised
    bool vulnerable=false;          // visibly recovering/staggered: a punish window
    int64_t healthMilli=1000;       // visible health fraction 0..1000 (owner-defined estimate)
    MeleeThreat threat{};
};
inline bool botBounded(Vec3 p,Fixed m){return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m&&p.z>= -m&&p.z<=m;}
inline bool valid(const MeleeThreat& t){return t.attack>=0&&t.windup<=3600&&t.active<=3600&&t.reach>=Fixed{}&&t.reach<=kBotSizeLimit&&(t.attack||(t.started==0&&t.windup==0&&t.active==0));}
inline bool valid(const PerceivedTarget& t){
    return t.id>0&&t.team>=0&&botBounded(t.position,kBotCoordLimit)&&botBounded(t.velocity,kBotSizeLimit)&&t.yaw>= -1_fx&&t.yaw<=1_fx
        &&t.healthMilli>=0&&t.healthMilli<=1000&&valid(t.threat);
}
inline bool validTargets(std::span<const PerceivedTarget> targets){
    if(targets.size()>kBotMaxTargets)return false;
    std::vector<int64_t> ids;ids.reserve(targets.size());
    for(const auto& t:targets){if(!valid(t))return false;ids.push_back(t.id);}
    std::sort(ids.begin(),ids.end());return std::adjacent_find(ids.begin(),ids.end())==ids.end();
}
// The threat a perceived attacker shows this tick: only windup/active phases are telegraphed.
inline MeleeThreat meleeThreat(const genre::AttackSet& defs,const genre::MeleeState& s,uint32_t tick,Fixed reach,bool unblockable=false){
    auto phase=genre::attackPhase(defs,s,tick);
    if(phase!=genre::AttackPhase::Windup&&phase!=genre::AttackPhase::Active)return {};
    const auto& d=defs.at(s.attack);return {s.attack,s.started,d.windup,d.active,reach,unblockable};
}

// ---- Geometry helpers (yaw convention of genre/CombatActions.h: yaw 0 faces +Z, +X is right).
inline Fixed wrapBotTurns(Fixed t){int64_t r=t.raw%Fixed::kOne;if(r>Fixed::kOne/2)r-=Fixed::kOne;else if(r<= -Fixed::kOne/2)r+=Fixed::kOne;return Fixed::fromRaw(r);}
inline Vec2 planarUnit(Vec2 v){auto l=v.length();if(l.raw<=0)return {};return {v.x/l,v.y/l};}
inline Vec2 rightOf(Vec2 forward){return {forward.y,-forward.x};}
inline Fixed yawToward(Vec3 from,Vec3 to,Fixed fallback){auto d=to-from;if(d.x.raw==0&&d.z.raw==0)return fallback;return fxAtan2Turns(d.x,d.z);}
inline Fixed pitchToward(Vec3 from,Vec3 to){auto d=to-from;return fxAtan2Turns(d.y,Vec2{d.x,d.z}.length());}
inline Vec2 yawForward(Fixed yaw){return {fxSinTurns(yaw),fxCosTurns(yaw)};}
// Uniform integer offset in [-maximum,maximum] (raw units); consumes RNG only when maximum > 0.
inline Fixed botJitter(Random& rng,Fixed maximum){if(maximum.raw<=0)return {};return Fixed::fromRaw(int64_t(rng.below(uint32_t(maximum.raw*2+1)))-maximum.raw);}
// Draws a 0..999 roll against a milli chance; consumes RNG only for chances strictly between 0 and 1000.
inline bool botChance(Random& rng,int64_t milli){if(milli<=0)return false;if(milli>=1000)return true;return int64_t(rng.below(1000))<milli;}

// Hostile visible target: keep `prefer` while it is visible and hostile, else the nearest (distance, id).
// The bot itself (selfId) and same nonzero team members are skipped.
inline const PerceivedTarget* pickTarget(Vec3 from,int64_t selfId,int64_t team,std::span<const PerceivedTarget> seen,int64_t prefer){
    const PerceivedTarget* best=nullptr;Fixed bestDistance{};
    for(const auto& t:seen){
        if(!t.visible||t.id==selfId||(team!=0&&t.team==team))continue;
        if(t.id==prefer)return &t;
        auto d=(t.position-from).lengthSq();
        if(!best||d<bestDistance||(d==bestDistance&&t.id<best->id)){best=&t;bestDistance=d;}
    }
    return best;
}
}
