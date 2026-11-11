#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <limits>
#include <span>
namespace aiverse::lib::genre {
enum class MatchPhase:uint8_t {Warmup,Live,Overtime,Intermission,Finished};
struct MatchConfig {uint32_t warmupTicks=60,roundTicks=3600,overtimeTicks=600,intermissionTicks=120,roundsToWin=1,maxRounds=5;int64_t scoreToWin=10;};
struct TeamScore {int64_t score=0;uint32_t wins=0;uint64_t lastEvent=0;};
struct MatchState {std::map<int64_t,TeamScore> teams;MatchPhase phase=MatchPhase::Warmup;uint32_t phaseStart=0,lastTick=0,round=1;int64_t winner=0;bool initialized=false;};
inline bool valid(const MatchConfig& c){return c.warmupTicks<=10000000&&c.roundTicks>0&&c.roundTicks<=10000000&&c.overtimeTicks<=10000000&&c.intermissionTicks<=10000000&&c.roundsToWin>0&&c.roundsToWin<=100&&c.maxRounds>=c.roundsToWin&&c.maxRounds<=1000&&c.scoreToWin>0;}
inline std::optional<MatchState> makeMatch(std::span<const int64_t> teams){
    if(teams.size()<2||teams.size()>200)return {};MatchState s;for(auto team:teams)if(team<=0||!s.teams.emplace(team,TeamScore{}).second)return {};return s;
}
inline bool awardScore(MatchState& s,int64_t team,uint64_t event,int64_t points){
    auto it=s.teams.find(team);if((s.phase!=MatchPhase::Live&&s.phase!=MatchPhase::Overtime)||it==s.teams.end()||event==0||event<=it->second.lastEvent||points<0||it->second.score>std::numeric_limits<int64_t>::max()-points)return false;
    it->second.lastEvent=event;it->second.score+=points;return true;
}
inline std::pair<int64_t,int64_t> leadingTeam(const MatchState& s,bool wins=false){
    int64_t leader=-1,best=-1;for(const auto& [team,value]:s.teams){auto score=wins?int64_t(value.wins):value.score;if(score>best){best=score;leader=team;}else if(score==best)leader=-1;}return {leader,best};
}
inline bool stepMatch(const MatchConfig& c,MatchState& s,uint32_t tick){
    if(s.phaseStart>tick||!valid(c)||s.teams.size()<2||uint8_t(s.phase)>uint8_t(MatchPhase::Finished)||(s.initialized&&tick<=s.lastTick))return false;
    if(!s.initialized){s.initialized=true;s.phaseStart=tick;}s.lastTick=tick;auto age=tick-s.phaseStart;
    if(s.phase==MatchPhase::Finished)return true;
    if(s.phase==MatchPhase::Warmup){if(age>=c.warmupTicks){s.phase=MatchPhase::Live;s.phaseStart=tick;}return true;}
    if(s.phase==MatchPhase::Intermission){if(age>=c.intermissionTicks){++s.round;for(auto& [team,score]:s.teams){(void)team;score.score=0;}s.phase=MatchPhase::Live;s.phaseStart=tick;}return true;}
    auto [leader,score]=leadingTeam(s);bool finish=leader>0&&score>=c.scoreToWin;
    if(s.phase==MatchPhase::Live&&age>=c.roundTicks){
        if(leader<0&&c.overtimeTicks){s.phase=MatchPhase::Overtime;s.phaseStart=tick;return true;}finish=true;
    }
    if(s.phase==MatchPhase::Overtime&&(leader>0||age>=c.overtimeTicks))finish=true;
    if(!finish)return true;
    if(leader>0)++s.teams.at(leader).wins;
    if((leader>0&&s.teams.at(leader).wins>=c.roundsToWin)||s.round>=c.maxRounds){s.phase=MatchPhase::Finished;s.winner=leadingTeam(s,true).first;}
    else {s.phase=MatchPhase::Intermission;s.phaseStart=tick;}return true;
}
struct CaptureState {int64_t owner=0,candidate=0;uint32_t progress=0,lastTick=0;bool initialized=false;};
inline std::optional<bool> stepCapture(CaptureState& s,const std::map<int64_t,uint32_t>& occupants,uint32_t requiredTicks,uint32_t tick){
    if(s.owner<0||s.candidate<0||s.progress>10000000||!requiredTicks||requiredTicks>10000000||(s.initialized&&tick<=s.lastTick))return {};
    int64_t candidate=0;for(auto [team,count]:occupants){if(team<=0)return {};if(!count)continue;if(candidate){candidate=-1;break;}candidate=team;}
    s.initialized=true;s.lastTick=tick;if(candidate<0)return false;
    if(candidate==0){if(s.progress)--s.progress;return false;}
    if(candidate==s.owner){s.progress=0;s.candidate=0;return false;}
    if(candidate!=s.candidate){s.candidate=candidate;s.progress=0;}
    if(++s.progress>=requiredTicks){s.owner=candidate;s.candidate=0;s.progress=0;return true;}return false;
}
}
