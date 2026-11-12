#pragma once
// Sports positioning bot (sports-rules): one decision step per team per tick. Off the ball every player holds its
// formation slot (sportsSlotTarget for the attack / defend / neutral phase); without the ball the nearest player
// presses the holder and tackles in range, the next one marks the most dangerous opponent between it and the goal;
// a free ball is chased by the player with the earliest interception (sportsInterceptPoint). The holder shoots
// inside shootRange with an open lane, passes to the teammate with the best progress-minus-pressure score along an
// open lane, or dribbles toward goal away from pressure. Returns player-equivalent orders: a move target for the
// owner's movement and an optional genre::SportsAction for stepSports. RNG only through the passed Random.
#include "../genre/SportsFormation.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::bots {
enum class SportsBotIntent:uint8_t{Formation,Press,Mark,Chase,Support,Dribble,Pass,Shoot,Restart};
struct SportsBotConfig{
    Fixed shootRange=Fixed::fromInt(12),laneWidth=Fixed::fromRatio(9,10),pressureRadius=Fixed::fromRatio(25,10),passMinGain=Fixed::fromInt(2);
    Fixed markDistance=Fixed::fromRatio(15,10),keeperRange=Fixed::fromInt(3),runSpeed=Fixed::fromInt(6),aimFraction=Fixed::fromRatio(6,10),pressureWeight=Fixed::fromInt(2);
    int64_t tackleMilli=300,passPowerMilli=650,shotPowerMilli=1000;
    uint32_t decisionTicks=6,reactionTicks=6;
};
struct SportsBotState{std::map<int64_t,int64_t> slots;int64_t holder=0;uint32_t holderSince=0,nextDecision=0,lastTick=0;genre::SportsTeamPhase phase=genre::SportsTeamPhase::Neutral;bool initialized=false;};
struct SportsBotOrder{int64_t player=0;Vec2 moveTo{};bool sprint=false;genre::SportsAction action{};SportsBotIntent intent=SportsBotIntent::Formation;};

namespace sportsBotDetail {
inline Vec2 unit(Vec2 v){return genre::sportsDetail::unit(v);}
// True when no opponent stands within width of the segment from a to b (endpoints excluded by 0.5 m).
inline bool laneOpen(Vec2 a,Vec2 b,int64_t team,std::span<const genre::SportsPlayer> players,Fixed width){
    auto ab=b-a;auto l=ab.lengthSq();if(l<=Fixed{})return true;
    for(const auto& p:players){
        if(p.team==team||!p.active)continue;auto t=(p.position-a).dot(ab)/l;if(t<=Fixed{}||t>=1_fx)continue;
        if((a+ab*t-p.position).length()<=width)return false;
    }
    return true;
}
inline Fixed nearestOpponent(Vec2 at,int64_t team,std::span<const genre::SportsPlayer> players){
    Fixed best=Fixed::fromInt(1000);for(const auto& p:players)if(p.team!=team&&p.active)best=fxMin(best,(p.position-at).length());return best;
}
}
inline bool valid(const SportsBotConfig& c){
    using genre::sportsDetail::sportsNonneg;using genre::sportsDetail::sportsMilli;
    return sportsNonneg(c.shootRange,Fixed::fromInt(1000))&&sportsNonneg(c.laneWidth,Fixed::fromInt(100))&&sportsNonneg(c.pressureRadius,Fixed::fromInt(100))&&fxAbs(c.passMinGain)<=Fixed::fromInt(1000)
        &&sportsNonneg(c.markDistance,Fixed::fromInt(100))&&sportsNonneg(c.keeperRange,Fixed::fromInt(100))&&genre::sportsDetail::sportsPositive(c.runSpeed,genre::kSportsMaxSpeed)&&sportsNonneg(c.aimFraction,1_fx)&&sportsNonneg(c.pressureWeight,Fixed::fromInt(100))
        &&sportsMilli(c.tackleMilli)&&sportsMilli(c.passPowerMilli)&&sportsMilli(c.shotPowerMilli)&&c.decisionTicks>=1&&c.decisionTicks<=3600&&c.reactionTicks<=3600;
}
inline bool valid(const SportsBotState& s){
    if(s.slots.size()>genre::kSportsMaxSlots||s.holder<0||uint8_t(s.phase)>uint8_t(genre::SportsTeamPhase::Neutral))return false;
    for(auto [slot,player]:s.slots)if(slot<=0||player<=0)return false;return true;
}

// roster: this team's players with their preferred roles; players: every player on the field (as given to stepSports).
inline std::optional<std::vector<SportsBotOrder>> stepSportsBot(const SportsBotConfig& c,const genre::SportsRulesConfig& rules,const genre::SportsField& field,
    const genre::SportsFormation& formation,SportsBotState& s,uint32_t tick,Fixed dt,int64_t team,std::span<const genre::SportsSlotPlayer> roster,
    std::span<const genre::SportsPlayer> players,const genre::SportsState& match,Random& rng){
    using namespace sportsBotDetail;using genre::SportsTeamPhase;using genre::SportsActionKind;
    if(!valid(c)||!valid(rules)||!valid(field)||!valid(formation)||!valid(s)||!valid(match)||dt<=Fixed{}||dt>1_fx||roster.size()>genre::kSportsMaxPlayers||players.size()>genre::kSportsMaxPlayers
        ||genre::sportsDetail::teamIndex(match,team)<0||(s.initialized&&tick<=s.lastTick))return {};
    std::map<int64_t,const genre::SportsPlayer*> byId;for(const auto& p:players)if(!byId.emplace(p.id,&p).second)return {};
    for(const auto& r:roster){auto it=byId.find(r.id);if(it==byId.end()||it->second->team!=team)return {};}
    auto next=s;const int dir=genre::sportsAttackDirection(rules,match,team);
    const Vec2 goal{field.halfLength*int64_t(dir),Fixed{}},ownGoal{-goal.x,Fixed{}};
    const auto& ball=match.ball;
    // Possession changes are perceived after reactionTicks.
    if(!next.initialized||ball.owner!=next.holder){next.holder=ball.owner;next.holderSince=tick;}
    SportsTeamPhase seen=SportsTeamPhase::Neutral;
    if(ball.owner){seen=byId.contains(ball.owner)&&byId.at(ball.owner)->team==team?SportsTeamPhase::Attack:SportsTeamPhase::Defend;}
    if(!next.initialized||tick>=next.holderSince+c.reactionTicks||seen==SportsTeamPhase::Attack)next.phase=seen;
    std::vector<genre::SportsSlotPlayer> current(roster.begin(),roster.end());for(auto& r:current)r.position=byId.at(r.id)->position;
    next.slots=genre::sportsAssignSlots(field,formation,dir,current,next.slots);
    std::map<int64_t,Vec2> home;
    for(const auto& slot:formation.slots){auto it=next.slots.find(slot.id);if(it!=next.slots.end())home[it->second]=genre::sportsSlotTarget(field,slot,dir,next.phase,ball.position);}
    std::vector<SportsBotOrder> orders;
    for(const auto& r:roster){SportsBotOrder o;o.player=r.id;auto h=home.find(r.id);o.moveTo=h!=home.end()?h->second:byId.at(r.id)->position;orders.push_back(o);}
    std::sort(orders.begin(),orders.end(),[](const auto& a,const auto& b){return a.player<b.player;});
    // Keeper: off the ball it guards the line between the ball and the goal centre, at most keeperRange out.
    for(const auto& sl:formation.slots){
        auto it=next.slots.find(sl.id);if(sl.role!=genre::SportsRole::Keeper||it==next.slots.end()||next.phase==SportsTeamPhase::Attack)continue;
        auto to=ball.position-ownGoal;auto d=to.length();auto& o=*std::find_if(orders.begin(),orders.end(),[&](const auto& x){return x.player==it->second;});
        o.moveTo=ownGoal+unit(to)*fxMin(c.keeperRange,fxMax(Fixed{},d-1_fx));o.moveTo.x=fxClamp(o.moveTo.x,-field.halfLength,field.halfLength);
    }
    auto order=[&](int64_t id)->SportsBotOrder&{return *std::find_if(orders.begin(),orders.end(),[&](const auto& o){return o.player==id;});};
    auto nearestOwn=[&](Vec2 at,int64_t skip,int64_t skip2=0)->int64_t{int64_t best=0;Fixed bd{};for(const auto& r:roster){if(r.id==skip||r.id==skip2||!byId.at(r.id)->active)continue;auto d=(byId.at(r.id)->position-at).lengthSq();if(!best||d<bd){best=r.id;bd=d;}}return best;};
    if(match.phase==genre::SportsPhase::Dead){
        if(match.restartTeam==team||match.restartTeam==0){auto taker=nearestOwn(match.restartSpot,0);if(taker){auto& o=order(taker);o.moveTo=match.restartSpot;o.sprint=true;o.intent=SportsBotIntent::Restart;}}
    }else if(match.phase==genre::SportsPhase::Live&&next.phase==SportsTeamPhase::Attack&&byId.contains(ball.owner)&&byId.at(ball.owner)->team==team){
        const auto& holder=*byId.at(ball.owner);auto& o=order(holder.id);
        auto pressure=nearestOpponent(holder.position,team,players);
        for(auto& x:orders)if(x.player!=holder.id)x.intent=SportsBotIntent::Support;
        // Dribble toward goal, sidestepping the nearest pressure.
        auto toGoal=unit(goal-holder.position);o.moveTo=holder.position+toGoal*Fixed::fromInt(4);o.sprint=true;o.intent=SportsBotIntent::Dribble;
        if(pressure<c.pressureRadius){
            for(const auto& p:players)if(p.team!=team&&p.active&&(p.position-holder.position).length()==pressure){auto away=unit(holder.position-p.position);o.moveTo=holder.position+unit(toGoal*Fixed::fromInt(2)+away)*Fixed::fromInt(4);break;}
        }
        if(tick>=next.nextDecision){
            next.nextDecision=tick+c.decisionTicks;
            auto aimY=field.goalHalfWidth*c.aimFraction;
            Vec2 left{goal.x,aimY},right{goal.x,-aimY};
            bool leftOpen=laneOpen(holder.position,left,team,players,c.laneWidth),rightOpen=laneOpen(holder.position,right,team,players,c.laneWidth);
            if((goal-holder.position).length()<=c.shootRange&&(leftOpen||rightOpen)){
                Vec2 aim=leftOpen&&rightOpen?(rng.below(2)?left:right):leftOpen?left:right;
                o.action={holder.id,SportsActionKind::Shoot,aim,0,c.shotPowerMilli};o.intent=SportsBotIntent::Shoot;
            }else{
                int64_t best=0;Fixed bestScore{};
                for(const auto& r:roster){
                    if(r.id==holder.id)continue;const auto& mate=*byId.at(r.id);if(!mate.active)continue;
                    if(!laneOpen(holder.position,mate.position,team,players,c.laneWidth))continue;
                    auto gain=(mate.position.x-holder.position.x)*int64_t(dir);auto free=nearestOpponent(mate.position,team,players);
                    auto score=gain-fxMax(Fixed{},c.pressureRadius-free)*c.pressureWeight;
                    if(!best||score>bestScore){best=r.id;bestScore=score;}
                }
                if(best&&(bestScore>=c.passMinGain||(pressure<c.pressureRadius&&bestScore> -c.passMinGain*2))){o.action={holder.id,SportsActionKind::Pass,{},best,c.passPowerMilli};o.intent=SportsBotIntent::Pass;}
            }
        }
    }else if(match.phase==genre::SportsPhase::Live&&next.phase==SportsTeamPhase::Defend&&ball.owner&&byId.contains(ball.owner)){
        const auto& holder=*byId.at(ball.owner);auto presser=nearestOwn(holder.position,0);
        if(presser){
            auto& o=order(presser);o.moveTo=holder.position+holder.velocity*Fixed::fromRatio(3,10);o.sprint=true;o.intent=SportsBotIntent::Press;
            if((byId.at(presser)->position-holder.position).length()<=rules.tackleRange&&tick>=ball.ownedSince+rules.possessionLockTicks&&genre::sportsDetail::roll(rng,c.tackleMilli))
                o.action={presser,SportsActionKind::Tackle,holder.position,0,0};
            const genre::SportsPlayer* danger=nullptr;Fixed bd{};
            for(const auto& p:players)if(p.team!=team&&p.active&&p.id!=holder.id){auto d=(p.position-ownGoal).lengthSq();if(!danger||d<bd||(d==bd&&p.id<danger->id)){danger=&p;bd=d;}}
            int64_t keeper=0;for(const auto& sl:formation.slots){auto it=next.slots.find(sl.id);if(it!=next.slots.end()&&sl.role==genre::SportsRole::Keeper)keeper=it->second;}
            if(danger){auto marker=nearestOwn(danger->position,presser,keeper);if(marker){auto& m=order(marker);m.moveTo=danger->position+unit(ownGoal-danger->position)*c.markDistance;m.intent=SportsBotIntent::Mark;}}
        }
    }else if(match.phase==genre::SportsPhase::Live&&!ball.owner){
        int64_t chaser=0;uint32_t bestTicks=0;Vec2 point{};
        for(const auto& r:roster){
            const auto& p=*byId.at(r.id);if(!p.active)continue;auto it=genre::sportsInterceptPoint(rules,ball,p.position,c.runSpeed,dt);
            auto ticks=it.found?it.ticks:UINT32_MAX;if(!chaser||ticks<bestTicks){chaser=r.id;bestTicks=ticks;point=it.point;}
        }
        if(chaser){auto& o=order(chaser);o.moveTo=point;o.sprint=true;o.intent=SportsBotIntent::Chase;}
    }
    next.lastTick=tick;next.initialized=true;s=std::move(next);return orders;
}
}
