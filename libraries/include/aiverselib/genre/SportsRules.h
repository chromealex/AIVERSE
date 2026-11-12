#pragma once
// sports-rules: deterministic ball-sport rules for football, hockey, handball-like and arcade games.
// Ball possession (pickup radius, possession lock, tackles contested with the passed RNG), passes and shots
// (target leading, power, seeded accuracy error), interception at the closest approach along the ball path,
// goals, out of bounds and generic restarts (kickoff, throw-in, corner, goal kick, free kick, penalty, face-off),
// periods with a running / stopped / added-stoppage clock and golden-goal overtime, fouls from contact rules.
// Field frame: Vec2{x along the length, y across}; goals at x = +-halfLength; team index 0 attacks +x in the
// first period (ends swap each period when swapEnds). Player bodies are the owner's (movement, collisions); it
// reports SportsPlayer snapshots every tick and SportsActions from players or bots (same struct).
// Fixed math, integer ticks, RNG only through the passed engine Random (sim.rng() or a saved RNG).
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr Fixed kSportsCoordLimit=Fixed::fromInt(10000),kSportsMaxSpeed=Fixed::fromInt(200);
inline constexpr std::size_t kSportsMaxPlayers=64,kSportsMaxActions=256,kSportsMaxSpots=32;
inline constexpr uint32_t kSportsMaxTicks=100000000;

enum class SportsBoundary:uint8_t{Lines,Walls,FaceOff};    // out-of-bounds restarts / rebounds / face-off at a spot
enum class SportsClockMode:uint8_t{Running,Stopped,Added};  // dead ball: clock runs, stops, or adds stoppage time
enum class SportsRestart:uint8_t{None,Kickoff,ThrowIn,Corner,GoalKick,FreeKick,Penalty,FaceOff};
enum class SportsPhase:uint8_t{Live,Dead,Finished};
enum class SportsActionKind:uint8_t{None,Pass,Shoot,Tackle};
enum class SportsEventKind:uint8_t{
    None,Possession,Interception,Pass,Shot,TackleWon,TackleLost,Foul,Goal,OutOfBounds,RestartAwarded,RestartTaken,
    PeriodStarted,PeriodEnded,OvertimeStarted,MatchEnded,ActionIgnored
};
// team: team concerned; player: player concerned; value: restart kind, score, period, reason; extra: other player.
struct SportsEvent{SportsEventKind kind=SportsEventKind::None;int64_t team=0,player=0,value=0,extra=0;bool operator==(const SportsEvent&)const=default;};

struct SportsField{
    Fixed halfLength=Fixed::fromInt(20),halfWidth=Fixed::fromInt(12),goalHalfWidth=Fixed::fromInt(3);
    Fixed penaltyDepth=Fixed::fromInt(6),penaltyHalfWidth=Fixed::fromInt(7),penaltySpot=Fixed::fromInt(5),goalKickDepth=Fixed::fromInt(3);
    SportsBoundary boundary=SportsBoundary::Lines;std::vector<Vec2> faceoffSpots;   // FaceOff boundary: nearest spot (else centre)
};
struct SportsRulesConfig{
    uint32_t periods=2,periodTicks=2700,overtimeTicks=0,restartDelayTicks=45,maxStoppageTicks=600;
    SportsClockMode clock=SportsClockMode::Running;bool swapEnds=true,goldenGoal=true;   // goldenGoal + overtimeTicks 0: overtime until a goal
    Fixed pickupRadius=Fixed::fromRatio(7,10),controlSpeed=Fixed::fromInt(14),dribbleOffset=Fixed::fromRatio(6,10),friction=Fixed::fromInt(5),restitution=Fixed::fromRatio(7,10);
    Fixed tackleRange=Fixed::fromRatio(14,10);int64_t tackleBaseMilli=450,interceptMilli=650,behindFoulMilli=500,failFoulMilli=120,faceoffBiasMilli=500;
    uint32_t tackleCooldownTicks=40,possessionLockTicks=20,kickLockTicks=12;
    Fixed passSpeedMin=Fixed::fromInt(7),passSpeedMax=Fixed::fromInt(18),shotSpeedMin=Fixed::fromInt(12),shotSpeedMax=Fixed::fromInt(26);
    Fixed passError=Fixed::fromRatio(3,100),shotError=Fixed::fromRatio(4,100);   // max angle error in turns at accuracy 0
};
// Skills are milli 0..1000: tackle (winning tackles), control (keeping the ball, interceptions), accuracy (kicks).
struct SportsPlayer{int64_t id=0,team=0;Vec2 position{},velocity{};int64_t tackleMilli=500,controlMilli=500,accuracyMilli=500;bool active=true;};
// Pass: target point, or receiver (> 0) whose motion is led; Shoot: target point (usually inside the goal mouth).
struct SportsAction{int64_t player=0;SportsActionKind kind=SportsActionKind::None;Vec2 target{};int64_t receiver=0,powerMilli=1000;};
struct SportsBall{Vec2 position{},velocity{};int64_t owner=0,lastTouch=0,lastTeam=0,kicker=0;uint32_t kickedAt=0,ownedSince=0;};
struct SportsState{
    std::array<int64_t,2> teams{1,2};std::array<int64_t,2> score{};SportsBall ball;
    SportsPhase phase=SportsPhase::Dead;SportsRestart restart=SportsRestart::Kickoff;int64_t restartTeam=1;Vec2 restartSpot{};uint32_t restartAt=0;
    uint32_t period=1,elapsed=0,stoppage=0,lastTick=0;bool overtime=false,initialized=false;int64_t winner=0;   // winner -1 = draw
    std::map<int64_t,uint32_t> stunned;   // player -> tick until which it cannot act / take the ball
};
struct SportsReport{std::vector<SportsEvent> events;};

namespace sportsDetail {
inline bool sportsBounded(Vec2 p,Fixed m){return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m;}
inline bool sportsPositive(Fixed v,Fixed hi){return v>Fixed{}&&v<=hi;}
inline bool sportsNonneg(Fixed v,Fixed hi){return v>=Fixed{}&&v<=hi;}
inline bool sportsMilli(int64_t v){return v>=0&&v<=1000;}
inline Vec2 unit(Vec2 v){auto l=v.length();if(l<=Fixed{})return {};return {v.x/l,v.y/l};}
inline Vec2 rotate(Vec2 v,Fixed turns){auto c=fxCosTurns(turns),s=fxSinTurns(turns);return {v.x*c-v.y*s,v.x*s+v.y*c};}
inline Fixed jitter(Random& rng,Fixed maximum){if(maximum.raw<=0)return {};return Fixed::fromRaw(int64_t(rng.below(uint32_t(std::min<int64_t>(maximum.raw*2+1,UINT32_MAX))))-maximum.raw);}
inline bool roll(Random& rng,int64_t chance){if(chance<=0)return false;if(chance>=1000)return true;return int64_t(rng.below(1000))<chance;}
inline int teamIndex(const SportsState& s,int64_t team){return team==s.teams[0]?0:team==s.teams[1]?1:-1;}
inline int64_t otherTeam(const SportsState& s,int64_t team){return team==s.teams[0]?s.teams[1]:s.teams[0];}
}

inline bool valid(const SportsField& f){
    using namespace sportsDetail;
    if(!sportsPositive(f.halfLength,kSportsCoordLimit)||!sportsPositive(f.halfWidth,kSportsCoordLimit)||!sportsPositive(f.goalHalfWidth,f.halfWidth)||!sportsNonneg(f.penaltyDepth,f.halfLength)
        ||!sportsNonneg(f.penaltyHalfWidth,f.halfWidth)||!sportsNonneg(f.penaltySpot,f.halfLength)||!sportsNonneg(f.goalKickDepth,f.halfLength)||uint8_t(f.boundary)>uint8_t(SportsBoundary::FaceOff)||f.faceoffSpots.size()>kSportsMaxSpots)return false;
    for(auto p:f.faceoffSpots)if(fxAbs(p.x)>f.halfLength||fxAbs(p.y)>f.halfWidth)return false;return true;
}
inline bool valid(const SportsRulesConfig& c){
    using namespace sportsDetail;
    return c.periods>=1&&c.periods<=16&&c.periodTicks>=1&&c.periodTicks<=kSportsMaxTicks&&c.overtimeTicks<=kSportsMaxTicks&&c.restartDelayTicks<=100000&&c.maxStoppageTicks<=kSportsMaxTicks
        &&uint8_t(c.clock)<=uint8_t(SportsClockMode::Added)&&sportsPositive(c.pickupRadius,Fixed::fromInt(100))&&sportsNonneg(c.controlSpeed,kSportsMaxSpeed)&&sportsNonneg(c.dribbleOffset,Fixed::fromInt(100))
        &&sportsNonneg(c.friction,Fixed::fromInt(1000))&&sportsNonneg(c.restitution,1_fx)&&sportsNonneg(c.tackleRange,Fixed::fromInt(100))&&sportsMilli(c.tackleBaseMilli)&&sportsMilli(c.interceptMilli)&&sportsMilli(c.behindFoulMilli)
        &&sportsMilli(c.failFoulMilli)&&sportsMilli(c.faceoffBiasMilli)&&c.tackleCooldownTicks<=100000&&c.possessionLockTicks<=100000&&c.kickLockTicks<=100000
        &&sportsPositive(c.passSpeedMin,kSportsMaxSpeed)&&c.passSpeedMax>=c.passSpeedMin&&c.passSpeedMax<=kSportsMaxSpeed&&sportsPositive(c.shotSpeedMin,kSportsMaxSpeed)&&c.shotSpeedMax>=c.shotSpeedMin
        &&c.shotSpeedMax<=kSportsMaxSpeed&&sportsNonneg(c.passError,Fixed::fromRatio(1,4))&&sportsNonneg(c.shotError,Fixed::fromRatio(1,4));
}
inline bool valid(const SportsState& s){
    using namespace sportsDetail;
    if(s.teams[0]<=0||s.teams[1]<=0||s.teams[0]==s.teams[1]||s.score[0]<0||s.score[1]<0||!sportsBounded(s.ball.position,kSportsCoordLimit)||!sportsBounded(s.ball.velocity,kSportsMaxSpeed)
        ||s.ball.owner<0||s.ball.lastTouch<0||s.ball.kicker<0||uint8_t(s.phase)>uint8_t(SportsPhase::Finished)||uint8_t(s.restart)>uint8_t(SportsRestart::FaceOff)
        ||!sportsBounded(s.restartSpot,kSportsCoordLimit)||s.period<1||s.period>64||s.elapsed>kSportsMaxTicks||s.stoppage>kSportsMaxTicks||s.winner< -1||s.stunned.size()>kSportsMaxPlayers)return false;
    if(s.ball.lastTeam!=0&&teamIndex(s,s.ball.lastTeam)<0)return false;
    if(s.restartTeam!=0&&teamIndex(s,s.restartTeam)<0)return false;
    if(s.winner>0&&teamIndex(s,s.winner)<0)return false;
    for(auto [id,until]:s.stunned){(void)until;if(id<=0)return false;}return true;
}
inline std::optional<SportsState> makeSportsMatch(int64_t teamA,int64_t teamB){
    SportsState s;s.teams={teamA,teamB};s.restartTeam=teamA;if(!valid(s))return {};return s;
}
// +1 when `team` attacks the +x goal in the current period, -1 otherwise (0 for an unknown team).
inline int sportsAttackDirection(const SportsRulesConfig& c,const SportsState& s,int64_t team){
    auto i=sportsDetail::teamIndex(s,team);if(i<0)return 0;bool swapped=c.swapEnds&&(s.period-1)%2==1;return (i==0)!=swapped?1:-1;
}
inline int64_t sportsScore(const SportsState& s,int64_t team){auto i=sportsDetail::teamIndex(s,team);return i<0?0:s.score[std::size_t(i)];}
// Ticks left in the current period (stoppage included for the Added clock); 0 in unlimited golden-goal overtime.
inline uint32_t sportsTicksLeft(const SportsRulesConfig& c,const SportsState& s){
    uint32_t length=s.overtime?c.overtimeTicks:c.periodTicks;if(!length)return 0;
    if(c.clock==SportsClockMode::Added)length+=std::min(s.stoppage,c.maxStoppageTicks);return s.elapsed>=length?0:length-s.elapsed;
}
// Ball position after `ticks` free-rolling ticks (same friction integration as stepSports, no walls).
inline Vec2 sportsBallAfter(const SportsRulesConfig& c,Vec2 position,Vec2 velocity,uint32_t ticks,Fixed dt){
    for(uint32_t i=0;i<ticks;++i){auto speed=velocity.length();if(speed<=Fixed{})break;auto slower=fxMax(Fixed{},speed-c.friction*dt);velocity=velocity*(slower/speed);position+=velocity*dt;}
    return position;
}
// Earliest tick (<= horizon) at which a player running at `speed` reaches the free ball's predicted position
// within the pickup radius; found=false when it cannot (point = last predicted position).
struct SportsIntercept{Vec2 point{};uint32_t ticks=0;bool found=false;};
inline SportsIntercept sportsInterceptPoint(const SportsRulesConfig& c,const SportsBall& ball,Vec2 player,Fixed speed,Fixed dt,uint32_t horizon=120){
    auto p=ball.position,v=ball.velocity;
    for(uint32_t k=0;k<=horizon;++k){
        if(k){auto sp=v.length();if(sp>Fixed{}){auto slower=fxMax(Fixed{},sp-c.friction*dt);v=v*(slower/sp);p+=v*dt;}}
        if((p-player).length()-c.pickupRadius<=speed*dt*int64_t(k))return {p,k,true};
    }
    return {p,horizon,false};
}

namespace sportsDetail {
inline void award(const SportsRulesConfig& c,SportsState& s,uint32_t tick,SportsRestart kind,int64_t team,Vec2 spot,std::vector<SportsEvent>& events){
    s.phase=SportsPhase::Dead;s.restart=kind;s.restartTeam=team;s.restartSpot=spot;s.restartAt=tick+c.restartDelayTicks;
    s.ball.owner=0;s.ball.velocity={};s.ball.position=spot;events.push_back({SportsEventKind::RestartAwarded,team,0,int64_t(kind),0});
}
// Goal x of the goal `team` defends.
inline Fixed defendedGoal(const SportsRulesConfig& c,const SportsField& f,const SportsState& s,int64_t team){return f.halfLength*int64_t(-sportsAttackDirection(c,s,team));}
inline void startPeriod(const SportsRulesConfig& c,SportsState& s,uint32_t tick,std::vector<SportsEvent>& events){
    s.elapsed=0;s.stoppage=0;events.push_back({s.overtime?SportsEventKind::OvertimeStarted:SportsEventKind::PeriodStarted,0,0,int64_t(s.period),0});
    award(c,s,tick,SportsRestart::Kickoff,s.teams[(s.period-1)%2],{},events);
}
inline void finish(SportsState& s,std::vector<SportsEvent>& events){
    s.phase=SportsPhase::Finished;s.ball.owner=0;s.ball.velocity={};
    s.winner=s.score[0]>s.score[1]?s.teams[0]:s.score[1]>s.score[0]?s.teams[1]:-1;events.push_back({SportsEventKind::MatchEnded,s.winner,0,s.score[0],s.score[1]});
}
inline const SportsPlayer* nearest(std::span<const SportsPlayer> players,int64_t team,Vec2 spot,const SportsState& s,uint32_t tick){
    const SportsPlayer* best=nullptr;Fixed bestD{};
    for(const auto& p:players){
        if(!p.active||p.team!=team)continue;auto st=s.stunned.find(p.id);if(st!=s.stunned.end()&&tick<st->second)continue;
        auto d=(p.position-spot).lengthSq();if(!best||d<bestD||(d==bestD&&p.id<best->id)){best=&p;bestD=d;}
    }
    return best;
}
inline void take(SportsState& s,const SportsPlayer& p,uint32_t tick){s.ball.owner=p.id;s.ball.lastTouch=p.id;s.ball.lastTeam=p.team;s.ball.ownedSince=tick;s.ball.velocity={};}
// Ball crossing a boundary between from and to; returns true when the ball left play (restart awarded / goal).
inline bool boundaries(const SportsRulesConfig& c,const SportsField& f,SportsState& s,uint32_t tick,Vec2 from,std::vector<SportsEvent>& events){
    auto& b=s.ball;auto to=b.position;
    if(fxAbs(to.x)>f.halfLength){
        auto side=to.x>Fixed{}?1:-1;auto line=f.halfLength*int64_t(side);
        auto t=to.x!=from.x?(line-from.x)/(to.x-from.x):Fixed{};auto y=from.y+(to.y-from.y)*fxClamp(t,Fixed{},1_fx);
        if(fxAbs(y)<=f.goalHalfWidth){
            int64_t scorer=sportsAttackDirection(c,s,s.teams[0])==side?s.teams[0]:s.teams[1];
            auto i=teamIndex(s,scorer);++s.score[std::size_t(i)];events.push_back({SportsEventKind::Goal,scorer,b.lastTeam==scorer?b.lastTouch:0,s.score[std::size_t(i)],b.lastTouch});
            if(s.overtime&&c.goldenGoal){b.position={line,y};finish(s,events);return true;}
            award(c,s,tick,SportsRestart::Kickoff,otherTeam(s,scorer),{},events);return true;
        }
        if(f.boundary==SportsBoundary::Walls){b.position.x=line;b.position.y=fxClamp(b.position.y,-f.halfWidth,f.halfWidth);if(!b.owner)b.velocity.x=-b.velocity.x*c.restitution;return false;}
        events.push_back({SportsEventKind::OutOfBounds,b.lastTeam,b.lastTouch,side,0});
        if(f.boundary==SportsBoundary::FaceOff)return false;   // handled by caller
        int64_t defender=sportsAttackDirection(c,s,s.teams[0])==side?s.teams[1]:s.teams[0];
        if(b.lastTeam==defender)award(c,s,tick,SportsRestart::Corner,otherTeam(s,defender),{line,y>=Fixed{}?f.halfWidth:-f.halfWidth},events);
        else award(c,s,tick,SportsRestart::GoalKick,defender,{line-f.goalKickDepth*int64_t(side),Fixed{}},events);
        return true;
    }
    if(fxAbs(to.y)>f.halfWidth){
        auto side=to.y>Fixed{}?1:-1;auto line=f.halfWidth*int64_t(side);
        if(f.boundary==SportsBoundary::Walls){b.position.y=line;if(!b.owner)b.velocity.y=-b.velocity.y*c.restitution;return false;}
        auto t=to.y!=from.y?(line-from.y)/(to.y-from.y):Fixed{};auto x=fxClamp(from.x+(to.x-from.x)*fxClamp(t,Fixed{},1_fx),-f.halfLength,f.halfLength);
        events.push_back({SportsEventKind::OutOfBounds,b.lastTeam,b.lastTouch,2*side,0});
        if(f.boundary==SportsBoundary::FaceOff)return false;   // handled by caller
        award(c,s,tick,SportsRestart::ThrowIn,b.lastTeam?otherTeam(s,b.lastTeam):s.teams[0],{x,line},events);return true;
    }
    return false;
}
}

// One authoritative step per strictly increasing tick. players: every player on the field (unique positive ids,
// team = one of the two match teams); actions: at most one per player (others ignored with ActionIgnored).
// Order: restart taking, actions by player id (tackles, passes, shots), ball motion (dribble or free roll),
// pickups / interceptions along the ball path, goals and boundaries, clock and period transitions.
// Invalid input or a repeated/backwards tick returns nullopt with the state and the RNG untouched.
inline std::optional<SportsReport> stepSports(const SportsRulesConfig& c,const SportsField& f,SportsState& s,uint32_t tick,Fixed dt,
    std::span<const SportsPlayer> players,std::span<const SportsAction> actions,Random& rng){
    using namespace sportsDetail;using K=SportsEventKind;
    if(!valid(c)||!valid(f)||!valid(s)||dt<=Fixed{}||dt>1_fx||players.size()>kSportsMaxPlayers||actions.size()>kSportsMaxActions||(s.initialized&&tick<=s.lastTick))return {};
    std::map<int64_t,const SportsPlayer*> byId;
    for(const auto& p:players){
        if(p.id<=0||teamIndex(s,p.team)<0||!sportsBounded(p.position,kSportsCoordLimit)||!sportsBounded(p.velocity,kSportsMaxSpeed)||!sportsMilli(p.tackleMilli)||!sportsMilli(p.controlMilli)||!sportsMilli(p.accuracyMilli)||!byId.emplace(p.id,&p).second)return {};
    }
    for(const auto& a:actions)if(!byId.contains(a.player)||uint8_t(a.kind)>uint8_t(SportsActionKind::Tackle)||!sportsBounded(a.target,kSportsCoordLimit)||a.powerMilli<0||a.powerMilli>1000||a.receiver<0)return {};
    if(s.ball.owner&&!byId.contains(s.ball.owner))return {};
    auto next=s;SportsReport report;auto& ev=report.events;auto& b=next.ball;
    if(!next.initialized){next.initialized=true;startPeriod(c,next,tick,ev);}
    for(auto it=next.stunned.begin();it!=next.stunned.end();)if(it->second<=tick||!byId.contains(it->first))it=next.stunned.erase(it);else ++it;
    auto stunned=[&](int64_t id){auto it=next.stunned.find(id);return it!=next.stunned.end()&&tick<it->second;};
    if(next.phase==SportsPhase::Finished){next.lastTick=tick;s=std::move(next);return report;}
    // Restart: the nearest available player of the awarded team takes it (face-off: contested by both nearest).
    if(next.phase==SportsPhase::Dead&&tick>=next.restartAt){
        const SportsPlayer* taker=nullptr;
        if(next.restart==SportsRestart::FaceOff||next.restartTeam==0){
            auto a=nearest(players,next.teams[0],next.restartSpot,next,tick),o=nearest(players,next.teams[1],next.restartSpot,next,tick);
            if(a&&o){auto chance=std::clamp<int64_t>(c.faceoffBiasMilli+(a->controlMilli-o->controlMilli)/2,0,1000);taker=roll(rng,chance)?a:o;}else taker=a?a:o;
        }else taker=nearest(players,next.restartTeam,next.restartSpot,next,tick);
        if(taker){b.position=next.restartSpot;take(next,*taker,tick);next.phase=SportsPhase::Live;ev.push_back({K::RestartTaken,taker->team,taker->id,int64_t(next.restart),0});next.restart=SportsRestart::None;}
    }
    auto ballFrom=b.position;
    // Actions in player-id order.
    std::vector<const SportsAction*> order;for(const auto& a:actions)if(a.kind!=SportsActionKind::None)order.push_back(&a);
    std::stable_sort(order.begin(),order.end(),[](auto x,auto y){return x->player<y->player;});
    std::vector<int64_t> acted;
    for(auto ap:order){
        const auto& a=*ap;const auto& p=*byId.at(a.player);
        bool repeat=std::find(acted.begin(),acted.end(),a.player)!=acted.end();acted.push_back(a.player);
        if(repeat||next.phase!=SportsPhase::Live||!p.active||stunned(p.id)){ev.push_back({K::ActionIgnored,p.team,p.id,int64_t(a.kind),0});continue;}
        if(a.kind==SportsActionKind::Tackle){
            const SportsPlayer* holder=b.owner?byId.at(b.owner):nullptr;
            if(!holder||holder->team==p.team||tick<b.ownedSince+c.possessionLockTicks||(holder->position-p.position).length()>c.tackleRange){ev.push_back({K::ActionIgnored,p.team,p.id,int64_t(a.kind),0});continue;}
            auto chance=std::clamp<int64_t>(c.tackleBaseMilli+(p.tackleMilli-holder->controlMilli)/2,50,950);
            bool won=roll(rng,chance);
            auto heading=unit(holder->velocity);bool behind=heading!=Vec2{}&&heading.dot(unit(p.position-holder->position))< -Fixed::fromRatio(1,2);
            bool foul=roll(rng,behind?c.behindFoulMilli:won?0:c.failFoulMilli);
            if(foul){
                ev.push_back({K::Foul,p.team,p.id,behind?1:0,holder->id});next.stunned[p.id]=tick+c.tackleCooldownTicks;
                auto goalX=defendedGoal(c,f,next,p.team);auto spot=holder->position;
                bool box=fxAbs(spot.x-goalX)<=f.penaltyDepth&&fxAbs(spot.y)<=f.penaltyHalfWidth&&f.penaltyDepth>Fixed{};
                if(box)award(c,next,tick,SportsRestart::Penalty,holder->team,{goalX+(goalX>Fixed{}? -f.penaltySpot:f.penaltySpot),Fixed{}},ev);
                else award(c,next,tick,SportsRestart::FreeKick,holder->team,{fxClamp(spot.x,-f.halfLength,f.halfLength),fxClamp(spot.y,-f.halfWidth,f.halfWidth)},ev);
                break;
            }
            if(won){take(next,p,tick);ev.push_back({K::TackleWon,p.team,p.id,0,holder->id});}
            else{next.stunned[p.id]=tick+c.tackleCooldownTicks;ev.push_back({K::TackleLost,p.team,p.id,0,holder->id});}
            continue;
        }
        if(b.owner!=p.id){ev.push_back({K::ActionIgnored,p.team,p.id,int64_t(a.kind),0});continue;}
        bool shot=a.kind==SportsActionKind::Shoot;
        auto speed=shot?c.shotSpeedMin+(c.shotSpeedMax-c.shotSpeedMin)*Fixed::fromRatio(a.powerMilli,1000):c.passSpeedMin+(c.passSpeedMax-c.passSpeedMin)*Fixed::fromRatio(a.powerMilli,1000);
        auto target=a.target;
        if(!shot&&a.receiver){
            auto r=byId.find(a.receiver);if(r==byId.end()||r->second->team!=p.team||r->second->id==p.id){ev.push_back({K::ActionIgnored,p.team,p.id,int64_t(a.kind),0});continue;}
            target=r->second->position;for(int i=0;i<2;++i){auto time=(target-b.position).length()/speed;target=r->second->position+r->second->velocity*time;}
        }
        auto dir=unit(target-b.position);if(dir==Vec2{})dir={Fixed::fromInt(sportsAttackDirection(c,next,p.team)),Fixed{}};
        auto error=(shot?c.shotError:c.passError)*Fixed::fromRatio(1000-p.accuracyMilli,1000);
        dir=rotate(dir,jitter(rng,error));
        b.velocity=dir*speed;b.owner=0;b.kicker=p.id;b.kickedAt=tick;b.lastTouch=p.id;b.lastTeam=p.team;
        ev.push_back({shot?K::Shot:K::Pass,p.team,p.id,(speed*Fixed::fromInt(1000)).toInt(),a.receiver});
    }
    // Ball motion.
    if(next.phase==SportsPhase::Live){
        ballFrom=b.position;
        if(b.owner){
            const auto& o=*byId.at(b.owner);auto dir=unit(o.velocity);if(dir==Vec2{})dir={Fixed::fromInt(sportsAttackDirection(c,next,o.team)),Fixed{}};
            b.position=o.position+dir*c.dribbleOffset;b.velocity=o.velocity;
        }else{
            auto speed=b.velocity.length();
            if(speed>Fixed{}){auto slower=fxMax(Fixed{},speed-c.friction*dt);b.velocity=b.velocity*(slower/speed);}
            b.position+=b.velocity*dt;
            // Pickups and interceptions at the closest approach along this tick's path: earliest, then nearest, then id.
            struct Cand{Fixed t,d;const SportsPlayer* p;};std::vector<Cand> cands;auto seg=b.position-ballFrom;auto len=seg.lengthSq();
            for(const auto& p:players){
                if(!p.active||stunned(p.id)||(p.id==b.kicker&&tick<b.kickedAt+c.kickLockTicks))continue;
                auto t=len>Fixed{}?fxClamp((p.position-ballFrom).dot(seg)/len,Fixed{},1_fx):Fixed{};auto d=(ballFrom+seg*t-p.position).length();
                if(d<=c.pickupRadius)cands.push_back({t,d,&p});
            }
            std::sort(cands.begin(),cands.end(),[](const Cand& x,const Cand& y){if(x.t!=y.t)return x.t<y.t;if(x.d!=y.d)return x.d<y.d;return x.p->id<y.p->id;});
            auto ballSpeed=b.velocity.length();
            for(const auto& cand:cands){
                bool control=ballSpeed<=c.controlSpeed||roll(rng,std::clamp<int64_t>(c.interceptMilli+(cand.p->controlMilli-500)/2,0,1000));
                if(!control)continue;
                bool intercepted=b.kicker&&b.lastTeam!=cand.p->team;b.position=ballFrom+seg*cand.t;take(next,*cand.p,tick);
                ev.push_back({intercepted?K::Interception:K::Possession,cand.p->team,cand.p->id,0,b.kicker});break;
            }
        }
        if(boundaries(c,f,next,tick,ballFrom,ev)){}
        else if(f.boundary==SportsBoundary::FaceOff&&(fxAbs(b.position.x)>f.halfLength||fxAbs(b.position.y)>f.halfWidth)){
            Vec2 spot{};Fixed best{};bool found=false;auto at=Vec2{fxClamp(b.position.x,-f.halfLength,f.halfLength),fxClamp(b.position.y,-f.halfWidth,f.halfWidth)};
            for(auto p:f.faceoffSpots){auto d=(p-at).lengthSq();if(!found||d<best){found=true;best=d;spot=p;}}
            award(c,next,tick,SportsRestart::FaceOff,0,spot,ev);
        }
    }
    // Clock.
    if(next.phase!=SportsPhase::Finished){
        bool live=next.phase==SportsPhase::Live;
        if(live||c.clock!=SportsClockMode::Stopped)++next.elapsed;
        if(!live&&c.clock==SportsClockMode::Added&&next.stoppage<c.maxStoppageTicks)++next.stoppage;
        uint32_t length=next.overtime?c.overtimeTicks:c.periodTicks;
        if(c.clock==SportsClockMode::Added)length+=std::min(next.stoppage,c.maxStoppageTicks);
        if((!next.overtime||c.overtimeTicks)&&next.elapsed>=length){
            ev.push_back({K::PeriodEnded,0,0,int64_t(next.period),0});
            if(!next.overtime&&next.period<c.periods){++next.period;startPeriod(c,next,tick,ev);}
            else if(next.score[0]==next.score[1]&&!next.overtime&&(c.overtimeTicks||c.goldenGoal)){next.overtime=true;++next.period;startPeriod(c,next,tick,ev);}
            else finish(next,ev);
        }
    }
    next.lastTick=tick;s=std::move(next);return report;
}
}
