#pragma once
// Melee/action bot policy (genre-bots): spacing band with approach/retreat/strafe, attack timing aware of
// attack phases, hit-confirmed combo cancels and punish windows, dodge/guard answers timed against the
// opponent's telegraphed windup, and lock-on use. The output is MeleeInput: exactly what a player controller
// feeds to stepMelee/stepDodge/stepGuard/stepLockOn and to the locomotion owner. The bot never changes
// combat, body or health state; the owner authorizes and applies the inputs like a player's.
#include "GenreCommon.h"
#include "../genre/CombatActions.h"
namespace aiverse::lib::bots {
struct MeleeBotConfig {
    Fixed minRange=Fixed::fromRatio(6,5),maxRange=Fixed::fromRatio(11,5); // spacing band between body centres (metres)
    Fixed attackRange=2_fx;        // open a string when the target is this close
    int64_t opener=1;              // AttackSet id requested to start a string; follow-ups come from AttackDef::combos
    uint32_t strafeTicks=30;       // strafe side flips every strafeTicks inside the band (0 = hold instead)
    int64_t dodgeMilli=500;        // preference 0..1000 for a dodge over a guard when both can answer a telegraph
    int64_t openMilli=350;         // per-tick chance 0..1000 to commit to an available opening (punish windows always commit)
    bool useDodge=true,useGuard=true,useLockOn=true,useCombos=true;
    bool respectGuard=true;        // do not open into a raised guard unless the target is visibly vulnerable
    BotDifficulty difficulty{};
};
enum class MeleeMove:uint8_t {Hold,Approach,Retreat,Strafe};
// Answer chosen for the opponent's current swing. Ignore = misread (acts as if nothing happened);
// Retreat = read, but neither dodge nor guard is available (backs off).
enum class MeleeDefense:uint8_t {None,Dodge,Guard,Ignore,Retreat};
// Persisted bot memory (genre::save record kind 56). threatAttack/threatStarted identify the swing that was read.
struct MeleeBotState {
    int64_t target=0,threatAttack=0;uint32_t noticedAt=0,threatStarted=0,lastDecision=0,lastTick=0;
    MeleeMove move=MeleeMove::Hold;MeleeDefense defense=MeleeDefense::None;bool threatRead=false,decided=false,initialized=false;
};
// Read-only copy of the bot's own authoritative state, supplied by the owner each tick before stepping it.
struct MeleeBotSelf {int64_t id=0,team=0;genre::CombatPose pose{};genre::MeleeState melee{};genre::CombatActorState combat{};};
// Player-equivalent inputs: move -> locomotion wish (world X/Z, |move| <= 1), face -> desired yaw (turns),
// attack -> stepMelee request (0 none), dodge + dodgeDirection -> stepDodge request, guard -> stepGuard held,
// lock -> stepLockOn command.
struct MeleeInput {Vec2 move{};Fixed face{};int64_t attack=0;bool dodge=false;Vec2 dodgeDirection{};bool guard=false;genre::LockCommand lock{};};

inline bool valid(const MeleeBotConfig& c){
    return c.minRange>=Fixed{}&&c.minRange<=c.maxRange&&c.maxRange<=kBotCoordLimit&&c.attackRange>Fixed{}&&c.attackRange<=kBotCoordLimit
        &&c.opener>0&&c.strafeTicks<=36000&&c.dodgeMilli>=0&&c.dodgeMilli<=1000&&c.openMilli>=0&&c.openMilli<=1000&&valid(c.difficulty);
}
inline bool valid(const MeleeBotState& s){
    if(uint8_t(s.move)>uint8_t(MeleeMove::Strafe)||uint8_t(s.defense)>uint8_t(MeleeDefense::Retreat)||s.target<0||s.threatAttack<0)return false;
    if(!s.initialized)return s.target==0&&s.threatAttack==0&&s.noticedAt==0&&s.threatStarted==0&&s.lastDecision==0&&s.lastTick==0
        &&s.move==MeleeMove::Hold&&s.defense==MeleeDefense::None&&!s.threatRead&&!s.decided;
    return s.noticedAt<=s.lastTick&&s.lastDecision<=s.lastTick&&s.threatStarted<=s.lastTick&&(s.target||s.threatAttack==0)
        &&(s.threatAttack||(s.threatStarted==0&&!s.threatRead&&s.defense==MeleeDefense::None));
}
inline Fixed strafeSide(uint32_t tick,uint32_t strafeTicks){return strafeTicks&&(tick/strafeTicks)%2?-1_fx:1_fx;}

// Once per bot per tick, before the owner applies inputs. nullopt: invalid config/definitions/self/targets,
// a threat that starts in the future, or a repeated/backwards tick (state untouched). RNG is consumed only for
// telegraph misreads, dodge-vs-guard choices and taking openings, each with chances strictly between 0 and 1000.
inline std::optional<MeleeInput> stepMeleeBot(const MeleeBotConfig& c,const genre::AttackSet& attacks,const genre::DodgeConfig& dodge,const genre::GuardConfig& guard,
    MeleeBotState& state,uint32_t tick,const MeleeBotSelf& self,std::span<const PerceivedTarget> seen,Random& rng){
    using genre::AttackPhase;
    if(!valid(c)||!genre::valid(attacks)||!attacks.contains(c.opener)||!genre::valid(dodge)||!genre::valid(guard)||!valid(state)
        ||self.id<=0||self.team<0||!genre::valid(self.pose)||(self.melee.attack&&!attacks.contains(self.melee.attack))||!validTargets(seen)
        ||(state.initialized&&tick<=state.lastTick))return std::nullopt;
    for(const auto& t:seen)if(t.threat.attack&&t.threat.started>tick)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;
    MeleeInput out;out.face=self.pose.yaw;
    const auto* t=pickTarget(self.pose.position,self.id,self.team,seen,self.combat.lock.target?self.combat.lock.target:s.target);
    if(!t){s.target=0;s.threatAttack=0;s.threatStarted=0;s.threatRead=false;s.defense=MeleeDefense::None;s.move=MeleeMove::Hold;state=s;return out;}
    if(t->id!=s.target){s.target=t->id;s.noticedAt=tick;s.threatAttack=0;s.threatStarted=0;s.threatRead=false;s.defense=MeleeDefense::None;s.decided=false;}
    auto delta=t->position-self.pose.position;Vec2 flat{delta.x,delta.z};auto distance=flat.length();
    auto toward=planarUnit(flat);if(toward==Vec2{})toward=yawForward(self.pose.yaw);
    Vec2 away{-toward.x,-toward.y};auto side=strafeSide(tick,c.strafeTicks);
    out.face=yawToward(self.pose.position,t->position,self.pose.yaw);
    bool reacted=tick-s.noticedAt>=c.difficulty.reactionTicks;
    bool stunned=genre::staggered(self.combat.poise,tick),dodgingNow=genre::dodging(dodge,self.combat.dodge,tick);
    auto phase=genre::attackPhase(attacks,self.melee,tick);

    // Telegraph tracking: a new swing (attack id + start tick) must be read again after the reaction delay.
    const auto& th=t->threat;
    if(!th.attack){s.threatAttack=0;s.threatStarted=0;s.threatRead=false;s.defense=MeleeDefense::None;}
    else if(th.attack!=s.threatAttack||th.started!=s.threatStarted){s.threatAttack=th.attack;s.threatStarted=th.started;s.threatRead=false;s.defense=MeleeDefense::None;}
    uint64_t activeStart=uint64_t(th.started)+th.windup,activeEnd=activeStart+th.active;
    if(th.attack&&!s.threatRead&&uint64_t(tick)>=uint64_t(th.started)+c.difficulty.reactionTicks&&tick<activeEnd&&distance<=th.reach){
        s.threatRead=true;
        bool dodgeReady=c.useDodge&&(!self.combat.dodge.dodged||tick>=self.combat.dodge.readyAt)&&!dodgingNow;
        bool canGuard=c.useGuard&&!th.unblockable;
        if(botChance(rng,c.difficulty.misreadMilli))s.defense=MeleeDefense::Ignore;
        else if(dodgeReady&&canGuard)s.defense=botChance(rng,c.dodgeMilli)?MeleeDefense::Dodge:MeleeDefense::Guard;
        else if(dodgeReady)s.defense=MeleeDefense::Dodge;
        else if(canGuard)s.defense=MeleeDefense::Guard;
        else s.defense=MeleeDefense::Retreat;
    }
    bool defending=false;
    if(th.attack&&s.threatRead&&tick<activeEnd&&!stunned){
        if(s.defense==MeleeDefense::Dodge){
            // Start so that [invulnFrom,invulnTo) covers the first active frames; late reads dodge at once.
            defending=true;bool started=self.combat.dodge.dodged&&self.combat.dodge.started>=th.started;
            uint64_t at=activeStart>dodge.invulnFrom?activeStart-dodge.invulnFrom:0;
            if(!started&&!dodgingNow&&tick>=at&&phase==AttackPhase::Idle){out.dodge=true;out.dodgeDirection=planarUnit(rightOf(toward)*side+away);}
        }else if(s.defense==MeleeDefense::Guard){
            // Raise so the perfect-parry window straddles the first active frame; hold until the swing ends.
            defending=true;uint64_t lead=guard.parryTicks/2,at=activeStart>lead?activeStart-lead:0;
            if(tick>=at&&phase==AttackPhase::Idle)out.guard=true;
        }else if(s.defense==MeleeDefense::Retreat){defending=true;if(phase==AttackPhase::Idle)out.move=away;}
    }

    // Offense: open inside attackRange (respecting a raised guard unless punishable); continue a string only on
    // hit confirm, timed with the buffer so stepMelee starts the follow-up at the cancel frame.
    if(!defending&&reacted&&!stunned&&!dodgingNow){
        if(phase==AttackPhase::Idle){
            // Openings are taken with openMilli per tick (so two bots rarely trade on the same frame); punishes always.
            if(distance<=c.attackRange&&self.melee.queued==0&&(t->vulnerable||((!c.respectGuard||!t->guarding)&&botChance(rng,c.openMilli))))out.attack=c.opener;
        }else if(c.useCombos&&phase!=AttackPhase::Windup&&self.melee.queued==0){
            const auto& d=attacks.at(self.melee.attack);uint64_t age=tick-self.melee.started;
            bool confirmed=std::binary_search(self.melee.hitTargets.begin(),self.melee.hitTargets.end(),t->id);
            if(confirmed&&!d.combos.empty()&&age+d.bufferTicks>=d.cancelFrom)out.attack=d.combos.front();
        }
    }
    // Spacing band, re-planned every decisionTicks; committed swings and stagger hold position.
    if(!defending){
        if(!s.decided||tick-s.lastDecision>=c.difficulty.decisionTicks){
            s.decided=true;s.lastDecision=tick;
            if(!reacted)s.move=MeleeMove::Hold;
            else if(distance>c.maxRange)s.move=MeleeMove::Approach;
            else if(distance<c.minRange)s.move=MeleeMove::Retreat;
            else s.move=c.strafeTicks?MeleeMove::Strafe:MeleeMove::Hold;
        }
        if(phase==AttackPhase::Idle&&!stunned&&!out.attack){
            if(s.move==MeleeMove::Approach)out.move=toward;
            else if(s.move==MeleeMove::Retreat)out.move=away;
            else if(s.move==MeleeMove::Strafe){
                // Circle; outside attackRange also close in so the band cannot become an endless orbit.
                auto circle=rightOf(toward)*side;out.move=distance>c.attackRange?planarUnit(circle+toward*Fixed::fromRatio(1,2)):circle;
            }
        }
    }
    if(c.useLockOn&&reacted&&self.combat.lock.target!=t->id)out.lock.toggle=true;
    state=s;return out;
}
}
