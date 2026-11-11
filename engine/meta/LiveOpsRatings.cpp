#include "LiveOpsCompetition.h"
#include "Rating.h"
#include <cmath>
namespace aiverse::meta::live {
namespace {
Rating readRating(const Json& value,const Json& cfg) {return {value.has("milli")?double(value.getInt("milli"))/1000:double(cfg.getInt("initial")),value.has("rdMilli")?double(value.getInt("rdMilli"))/1000:350.0,value.has("sigmaMicro")?double(value.getInt("sigmaMicro"))/1000000:0.06};}
}
Json ratings(Call& c) {
    c.authority();keys(c.args(),{"id","period","matches","idle"});need(c.req.action=="period",Code::Unsupported,c.req.action);
    auto period=integer(c.args()["period"],1);need(period>c.shared[c.id].getInt("period"),Code::Conflict,"rating period must increase");
    need(c.args()["matches"].isArray()&&c.args()["matches"].size()<=1000,Code::Invalid,"rating matches");
    std::map<std::string,std::vector<RatedGame>> games;std::map<std::string,int64_t> leaves;
    for(auto& match:c.args()["matches"].items()) {
        keys(match,{"id","a","b","score","leavers"});c.once("rating:"+key(c.id,id(match["id"])));
        auto score=integer(match["score"],0,1000);need(score==0||score==500||score==1000,Code::Invalid,"match score");
        std::set<std::string> players;std::vector<std::string> teams[2];Rating averages[2];
        for(int t=0;t<2;++t){const auto& list=match[t==0?"a":"b"];need(list.isArray()&&list.size()>0&&list.size()<=100,Code::Invalid,"team size");double total=0,variance=0;
            for(auto& value:list.items()){auto p=id(value);need(players.insert(p).second&&c.state["profiles"].has(p),Code::Invalid,"duplicate/unknown competitor");teams[t].push_back(p);auto r=readRating(c.state["live"]["players"][p]["ratings"][c.id],c.def);total+=r.value;variance+=r.deviation*r.deviation;}
            averages[t]={total/double(list.size()),std::sqrt(variance/double(list.size())),0.06};
        }
        for(int t=0;t<2;++t)for(auto& p:teams[t])games[p].push_back({averages[1-t].value,averages[1-t].deviation,double(t==0?score:1000-score)/1000});
        std::set<std::string> leavers; if(match.has("leavers"))need(match["leavers"].isArray(),Code::Invalid,"leavers");
        for(auto& v:match["leavers"].items()){auto p=id(v);need(players.count(p)&&leavers.insert(p).second,Code::Invalid,"leaver");leaves[p]++;}
    }
    if(c.args().has("idle"))need(c.args()["idle"].isArray(),Code::Invalid,"idle players");
    for(auto& v:c.args()["idle"].items()){auto p=id(v);need(c.state["profiles"].has(p)&&!games.count(p),Code::Invalid,"idle player");games[p]={};}
    auto result=Json::object();
    for(auto& [p,outcomes]:games) {
        auto stored=c.state["live"]["players"][p]["ratings"][c.id];auto before=readRating(stored,c.def),after=before;auto played=stored.getInt("games"),streak=stored.getInt("streak");
        if(c.def.getStr("method")=="glicko2")after=glicko2(before,outcomes,double(c.def.getInt("tauMilli"))/1000);
        else {double change=0;auto k=played<c.def.getInt("provisionalGames")?c.def.getInt("provisionalK"):c.def.getInt("k");for(auto& m:outcomes)change+=elo(before.value,m.opponent,m.score,double(k))-before.value;after.value+=change;}
        for(auto& m:outcomes){if(m.score==1){streak=add(streak,1);if(streak>1)after.value+=double(c.def.getInt("streakBonus"));}else streak=0;}
        after.value-=double(leaves[p]*c.def.getInt("leavePenalty"));after.value=std::clamp(after.value,double(c.def.getInt("minimum")),double(c.def.getInt("maximum")));
        auto record=row({{"milli",number(int64_t(std::llround(after.value*1000)))},{"rdMilli",number(std::max(int64_t(1),int64_t(std::llround(after.deviation*1000))))},{"sigmaMicro",number(std::max(int64_t(1),int64_t(std::llround(after.volatility*1000000))))},{"games",number(add(played,int64_t(outcomes.size())))},{"streak",number(streak)}});
        object(c.player(p),"ratings").set(c.id,record);result.set(p,record);
    }
    c.shared.set(c.id,row({{"period",number(period)}}));return result;
}
}
