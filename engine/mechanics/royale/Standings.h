#pragma once
#include "engine/mechanics/Namespace.h"
// Last-team-standing results: competitors in teams (solo = one-player teams), knockdowns, revives,
// eliminations, kill credit, damage totals, team placements and the winner, with kill-feed events.
// A team is out when it has no competitor left standing: its knocked members are then finished at once
// (credited to whoever knocked them, when finishKnocked). Placement = 1 + teams still in after the team
// is out; teams that go out on the same tick share the better placement. When at most one team remains
// the match is finished (a winner, or a draw when the last teams go out together).
// The owner decides what a "down" is (health reached zero, zone, fall, bleed-out) and reports it; the
// standings never read health. Calls in one tick may come in any number; the tick must not go backwards.
#include "engine/mechanics/royale/Common.h"
#include <algorithm>
#include <map>
namespace aiverse::lib::mechanics {
enum class RoyaleLife:uint8_t{Standing,Knocked,Out};
struct RoyaleCompetitor{int64_t team=0;RoyaleLife life=RoyaleLife::Standing;uint32_t kills=0,knocks=0,revives=0,outTick=0;int64_t damage=0,knockedBy=0,knockedWeapon=0;bool operator==(const RoyaleCompetitor&)const=default;};
struct RoyaleTeamStanding{uint32_t placement=0,outTick=0;bool operator==(const RoyaleTeamStanding&)const=default;};
struct RoyaleStandingsConfig{bool knockdowns=true,finishKnocked=true,friendlyCredit=false;};
struct RoyaleStandings{
    std::map<int64_t,RoyaleCompetitor> players;std::map<int64_t,RoyaleTeamStanding> teams;
    int64_t winner=0;bool started=false,finished=false;uint32_t lastTick=0;
    bool operator==(const RoyaleStandings&)const=default;
};
enum class KillFeedKind:uint8_t{Knocked,Eliminated,Revived,TeamOut,Winner,Draw};
// subject: victim / reviver target / team; actor: attacker or reviver (0 = environment); value: weapon id or placement.
struct KillFeedEvent{KillFeedKind kind{};int64_t subject=0,actor=0,value=0;uint32_t tick=0;bool operator==(const KillFeedEvent&)const=default;};
using KillFeedEvents=std::vector<KillFeedEvent>;
inline bool valid(const RoyaleStandings& s){
    if(s.players.size()>kRoyaleMaxCompetitors||s.teams.size()>kRoyaleMaxCompetitors||s.lastTick>kRoyaleMaxTicks||(s.finished&&!s.started))return false;
    for(const auto& [id,c]:s.players)if(id<=0||c.team<=0||!s.teams.contains(c.team)||uint8_t(c.life)>uint8_t(RoyaleLife::Out)||c.damage<0||c.knockedBy<0)return false;
    for(const auto& [team,t]:s.teams)if(team<=0||t.placement>s.teams.size())return false;
    return s.winner>=-1&&(!s.winner||s.finished);
}
inline bool addRoyaleCompetitor(RoyaleStandings& s,int64_t player,int64_t team){
    if(s.started||player<=0||team<=0||s.players.contains(player)||s.players.size()>=kRoyaleMaxCompetitors)return false;
    s.players[player]=RoyaleCompetitor{team};s.teams.try_emplace(team);return true;
}
// Locks the roster: no joins afterwards. A match with fewer than two teams finishes at once.
inline std::optional<KillFeedEvents> startRoyaleStandings(RoyaleStandings& state,uint32_t tick){
    if(!valid(state)||state.started||tick>kRoyaleMaxTicks)return {};
    auto s=state;s.started=true;s.lastTick=tick;KillFeedEvents events;
    if(s.teams.size()<2){s.finished=true;if(s.teams.size()==1){s.winner=s.teams.begin()->first;s.teams.begin()->second.placement=1;events.push_back({KillFeedKind::Winner,s.winner,0,1,tick});}}
    state=s;return events;
}
inline uint32_t royaleTeamsIn(const RoyaleStandings& s){uint32_t n=0;for(const auto& [team,t]:s.teams)if(!t.placement)++n;return n;}
inline bool royaleTeamStanding(const RoyaleStandings& s,int64_t team){for(const auto& [id,c]:s.players)if(c.team==team&&c.life==RoyaleLife::Standing)return true;return false;}
namespace royaleDetail {
inline bool reportable(const RoyaleStandings& s,uint32_t tick){return valid(s)&&s.started&&!s.finished&&tick>=s.lastTick&&tick<=kRoyaleMaxTicks;}
inline void credit(const RoyaleStandingsConfig& c,RoyaleStandings& s,int64_t victim,int64_t actor,bool kill){
    auto it=s.players.find(actor);if(it==s.players.end()||actor==victim)return;
    if(!c.friendlyCredit&&it->second.team==s.players[victim].team)return;
    if(kill)++it->second.kills;else ++it->second.knocks;
}
inline void out(const RoyaleStandingsConfig& c,RoyaleStandings& s,int64_t victim,int64_t actor,int64_t weapon,uint32_t tick,KillFeedEvents& events){
    auto& v=s.players[victim];v.life=RoyaleLife::Out;v.outTick=tick;credit(c,s,victim,actor,true);events.push_back({KillFeedKind::Eliminated,victim,actor,weapon,tick});
}
inline void settle(const RoyaleStandingsConfig& c,RoyaleStandings& s,std::vector<int64_t> teams,uint32_t tick,KillFeedEvents& events){
    std::sort(teams.begin(),teams.end());teams.erase(std::unique(teams.begin(),teams.end()),teams.end());
    std::vector<int64_t> going;for(auto team:teams)if(!s.teams[team].placement&&!royaleTeamStanding(s,team))going.push_back(team);
    if(going.empty())return;
    for(auto team:going)for(auto& [id,p]:s.players)if(p.team==team&&p.life==RoyaleLife::Knocked){
        if(c.finishKnocked)out(c,s,id,p.knockedBy,p.knockedWeapon,tick,events);else{p.life=RoyaleLife::Out;p.outTick=tick;events.push_back({KillFeedKind::Eliminated,id,0,0,tick});}
    }
    uint32_t placement=royaleTeamsIn(s)-uint32_t(going.size())+1;   // teams going out together share the better place
    for(auto& [other,o]:s.teams)if(o.placement&&o.outTick==tick&&o.placement>placement)o.placement=placement;   // earlier report of this tick
    for(auto team:going){auto& t=s.teams[team];t.placement=placement;t.outTick=tick;events.push_back({KillFeedKind::TeamOut,team,0,placement,tick});}
    auto left=royaleTeamsIn(s);
    if(left<=1){
        s.finished=true;
        if(left==1)for(auto& [other,o]:s.teams)if(!o.placement){o.placement=1;s.winner=other;events.push_back({KillFeedKind::Winner,other,0,1,tick});}
        if(!left){s.winner=-1;events.push_back({KillFeedKind::Draw,0,0,placement,tick});}
    }
}
}
// The victim went down (health 0). With knockdowns and a standing teammate it is Knocked, otherwise Out.
inline std::optional<KillFeedEvents> reportDown(const RoyaleStandingsConfig& c,RoyaleStandings& state,int64_t victim,int64_t attacker,int64_t weapon,uint32_t tick){
    using namespace royaleDetail;
    if(!reportable(state,tick)||!state.players.contains(victim)||attacker<0||state.players.at(victim).life==RoyaleLife::Out)return {};
    auto s=state;s.lastTick=tick;KillFeedEvents events;auto& v=s.players[victim];
    if(v.life==RoyaleLife::Knocked)out(c,s,victim,attacker?attacker:v.knockedBy,weapon,tick,events);
    else{
        v.life=RoyaleLife::Knocked;
        bool mate=c.knockdowns&&royaleTeamStanding(s,v.team);
        if(mate){v.knockedBy=attacker;v.knockedWeapon=weapon;credit(c,s,victim,attacker,false);events.push_back({KillFeedKind::Knocked,victim,attacker,weapon,tick});}
        else out(c,s,victim,attacker,weapon,tick,events);
    }
    settle(c,s,{s.players[victim].team},tick,events);state=s;return events;
}
// Out regardless of state (bleed-out, zone, disconnect, fall into the void). actor 0 = environment.
inline std::optional<KillFeedEvents> reportOut(const RoyaleStandingsConfig& c,RoyaleStandings& state,int64_t victim,int64_t actor,int64_t weapon,uint32_t tick){
    using namespace royaleDetail;
    if(!reportable(state,tick)||!state.players.contains(victim)||actor<0||state.players.at(victim).life==RoyaleLife::Out)return {};
    auto s=state;s.lastTick=tick;KillFeedEvents events;auto& v=s.players[victim];
    out(c,s,victim,actor?actor:(v.life==RoyaleLife::Knocked?v.knockedBy:0),weapon,tick,events);settle(c,s,{v.team},tick,events);state=s;return events;
}
// Several competitors out on the same tick, settled together (the final circle, one explosion): when the
// last teams all go out here the match is a draw instead of depending on report order.
inline std::optional<KillFeedEvents> reportOutTogether(const RoyaleStandingsConfig& c,RoyaleStandings& state,const std::vector<std::pair<int64_t,int64_t>>& victims,uint32_t tick){
    using namespace royaleDetail;
    if(!reportable(state,tick)||victims.empty()||victims.size()>kRoyaleMaxCompetitors)return {};
    auto s=state;s.lastTick=tick;KillFeedEvents events;std::vector<int64_t> teams;
    for(auto [victim,actor]:victims){
        auto it=s.players.find(victim);if(it==s.players.end()||actor<0||it->second.life==RoyaleLife::Out)return {};
        auto& v=it->second;out(c,s,victim,actor?actor:(v.life==RoyaleLife::Knocked?v.knockedBy:0),0,tick,events);teams.push_back(v.team);
    }
    settle(c,s,teams,tick,events);state=s;return events;
}
inline std::optional<KillFeedEvents> reportRevive(RoyaleStandings& state,int64_t victim,int64_t reviver,uint32_t tick){
    using namespace royaleDetail;
    if(!reportable(state,tick)||!state.players.contains(victim)||!state.players.contains(reviver)||victim==reviver)return {};
    auto s=state;auto& v=s.players[victim];auto& r=s.players[reviver];
    if(v.life!=RoyaleLife::Knocked||r.life!=RoyaleLife::Standing||r.team!=v.team)return {};
    v.life=RoyaleLife::Standing;v.knockedBy=v.knockedWeapon=0;++r.revives;s.lastTick=tick;state=s;
    return KillFeedEvents{{KillFeedKind::Revived,victim,reviver,0,tick}};
}
inline bool reportDamage(RoyaleStandings& s,int64_t attacker,int64_t victim,int64_t amount,uint32_t tick){
    if(!royaleDetail::reportable(s,tick)||amount<=0||amount>1000000||!s.players.contains(attacker)||!s.players.contains(victim)||attacker==victim)return false;
    if(s.players[attacker].team==s.players[victim].team)return false;
    auto& d=s.players[attacker].damage;if(d>INT64_MAX/2)return false;d+=amount;s.lastTick=tick;return true;
}
// Final table: teams by placement (ties keep team id order), for result screens and meta rewards.
inline std::vector<std::pair<int64_t,uint32_t>> royaleRanking(const RoyaleStandings& s){
    std::vector<std::pair<int64_t,uint32_t>> r;for(const auto& [team,t]:s.teams)r.push_back({team,t.placement?t.placement:1});
    std::stable_sort(r.begin(),r.end(),[](auto a,auto b){return a.second<b.second;});return r;
}
}
