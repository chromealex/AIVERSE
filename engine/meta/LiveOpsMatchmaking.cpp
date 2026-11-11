#include "LiveOpsCompetition.h"
#include <limits>
namespace aiverse::meta::live {
namespace {
int64_t rating(Call& c,const std::string& player) {auto key=c.def.getStr("rating");auto stored=c.state["live"]["players"][player]["ratings"][key];return stored.has("milli")?stored.getInt("milli")/1000:c.cfg.extensions["liveops"]["ratings"][key].getInt("initial");}
int64_t tolerance(Call& c,const Json& ticket) {return std::min(c.def.getInt("maxRange"),c.def.getInt("initialRange")+std::min(c.now-ticket.getInt("joined"),int64_t(1000000000))*c.def.getInt("expandPerSecond"));}
}
Json matchmake(Call& c) {
    keys(c.args(),{"id","ticket","players","region","match"});auto action=c.req.action;auto global=c.shared[c.id];if(!global.isObject())global=Json::object();
    auto tickets=global["tickets"];if(!tickets.isObject())tickets=Json::object();auto matches=global["matches"];if(!matches.isObject())matches=Json::object();
    std::vector<std::string> expired;for(auto& [id,t]:tickets.members())if(c.now>=t.getInt("expires"))expired.push_back(id);for(auto& id:expired)tickets.erase(id);
    auto persist=[&](){global.set("tickets",tickets);global.set("matches",matches);c.shared.set(c.id,global);};
    if(action=="join") {
        auto ticket=id(c.args()["ticket"]),region=id(c.args()["region"]);need(!tickets.has(ticket)&&tickets.size()<size_t(c.def.getInt("maxTickets")),Code::Limit,"ticket id/capacity");
        Json players=Json::array();if(c.args().has("players")){c.authority();players=c.args()["players"];}else players.push(str(c.who.player));
        need(players.isArray()&&players.size()>0&&players.size()<=size_t(c.def.getInt("teamSize")),Code::Invalid,"party size");std::set<std::string> unique;int64_t sum=0;
        for(auto& value:players.items()){auto p=id(value);need(c.state["profiles"].has(p)&&unique.insert(p).second,Code::Invalid,"party member");need(c.now>=global["banned"].getInt(p),Code::Unavailable,"queue penalty");
            for(auto& [id,t]:tickets.members()){(void)id;for(auto& member:t["players"].items())need(member.str()!=p,Code::Conflict,"already queued");}
            for(auto& [id,m]:matches.members()){(void)id;if(m.getBool("closed"))continue;for(auto& team:m["teams"].items())for(auto& member:team.items())need(member.str()!=p,Code::Conflict,"already matched");}
            sum+=rating(c,p);
        }
        auto record=row({{"owner",str(c.who.player)},{"players",players},{"region",str(region)},{"rating",number(sum/int64_t(players.size()))},{"joined",number(c.now)},{"expires",number(add(c.now,c.def.getInt("ticketSeconds")))}});
        tickets.set(ticket,record);persist();return record;
    }
    if(action=="cancel") {auto ticket=id(c.args()["ticket"]);need(tickets.has(ticket)&&(c.who.authority||tickets[ticket].getStr("owner")==c.who.player),Code::Denied,"ticket owner");tickets.erase(ticket);persist();return Json::object();}
    auto match=id(c.args()["match"]);
    if(action=="leave"||action=="finish") {need(matches.has(match)&&!matches[match].getBool("closed"),Code::Unavailable,"match closed");auto m=matches[match];
        if(action=="finish"){c.authority();m.set("closed",Json::fromBool(true));}
        else {bool found=false;for(auto& team:m.member("teams").itemsMut())for(size_t i=team.size();i>0;--i)if(team[i-1].str()==c.who.player){team.eraseAt(i-1);found=true;}need(found,Code::Denied,"match member");object(global,"banned").set(c.who.player,number(add(c.now,c.def.getInt("leavePenaltySeconds"))));}
        matches.set(match,m);persist();return m;
    }
    c.authority();need(action=="assemble"||action=="backfill",Code::Unsupported,action);auto m=matches[match];
    if(action=="assemble")need(!matches.has(match),Code::Conflict,"match id");else need(m.isObject()&&!m.getBool("closed"),Code::Unavailable,"match closed");
    std::vector<std::pair<std::string,Json>> ordered(tickets.members().begin(),tickets.members().end());
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.second.getInt("joined")!=b.second.getInt("joined")?a.second.getInt("joined")<b.second.getInt("joined"):a.first<b.first;});
    need(!ordered.empty(),Code::Unavailable,"queue empty");
    if(action=="assemble") {auto teams=Json::array();for(int64_t i=0;i<c.def.getInt("teams");++i)teams.push(Json::array());m=row({{"teams",teams},{"region",ordered.front().second["region"]},{"rating",ordered.front().second["rating"]},{"closed",Json::fromBool(false)}});}
    auto teams=m["teams"];std::vector<std::string> selected;const auto& seed=ordered.front().second;
    for(auto& [id,t]:ordered){if(t.getStr("region")!=m.getStr("region"))continue;auto range=tolerance(c,t);if(action=="assemble")range=std::min(range,tolerance(c,seed));if(std::abs(t.getInt("rating")-m.getInt("rating"))>range)continue;
        size_t best=teams.size();for(size_t i=0;i<teams.size();++i)if(teams[i].size()+t["players"].size()<=size_t(c.def.getInt("teamSize"))&&(best==teams.size()||teams[i].size()<teams[best].size()))best=i;
        if(best==teams.size())continue;for(auto& member:t["players"].items())teams.at(best).push(member);selected.push_back(id);
    }
    if(action=="assemble")for(auto& team:teams.items())need(team.size()==size_t(c.def.getInt("teamSize")),Code::Unavailable,"not enough compatible players");
    need(!selected.empty(),Code::Unavailable,"no backfill candidates");for(auto& id:selected)tickets.erase(id);m.set("teams",teams);matches.set(match,m);persist();return m;
}
}
