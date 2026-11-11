#include "LiveOpsInternal.h"
#include "libraries/include/aiverselib/systems/Loot.h"
namespace aiverse::meta::live {
namespace {
void merge(Bundle& to,const Bundle& from) {for(auto& [k,v]:from)to[k]=add(to[k],v);}
void loot(Call& c,const std::string& table,Bundle& reward,Json& results,int depth=0) {
    need(depth<8,Code::Limit,"loot nesting exceeds 8");const auto& d=c.cfg.extensions["liveops"]["loot"][table];
    auto stats=c.self[table];if(!stats.isObject())stats=Json::object();
    for(int64_t roll=0;roll<d.getInt("rolls");++roll) {
        need(results.size()<1024,Code::Limit,"loot expansion exceeds 1024 draws");
        std::vector<lib::systems::LootRow> rows;for(size_t i=0;i<d["entries"].size();++i){const auto& e=d["entries"][i];rows.push_back({int64_t(i+1),e.getInt("weight"),e.getBool("rare")});}
        lib::systems::LootState pity{stats.getInt("misses")};
        auto selected=lib::systems::rollLoot(rows,{d.getInt("pity")},pity,{},c.draw);need(bool(selected),Code::Invalid,"random draw");auto entry=d["entries"][size_t(*selected-1)];
        need(pity.misses<=kMaxAmount,Code::Limit,"pity counter");stats.set("misses",number(pity.misses));
        results.push(row({{"table",str(table)},{"entry",entry["id"]}}));
        if(entry.has("uniqueAsset")&&(c.balance(entry.getStr("uniqueAsset"))+reward[entry.getStr("uniqueAsset")]>0))merge(reward,bundle(entry["duplicateReward"]));
        else if(entry.has("table"))loot(c,entry.getStr("table"),reward,results,depth+1);
        else merge(reward,bundle(entry["reward"]));
    }
    c.self.set(table,stats);
}
void accrue(Call& c,Json& current) {
    if(!current.has("anchor")){current.set("anchor",number(c.now));current.set("tier",number(0));current.set("pending",Json::object());return;}
    auto elapsed=std::min(c.now-current.getInt("anchor"),c.def.getInt("maxAway"));need(elapsed>=0,Code::Conflict,"offline clock");
    auto total=add(elapsed,current.getInt("remainder"));auto ticks=total/c.def.getInt("interval");auto pending=bundle(current["pending"]);
    const auto& tier=c.def["tiers"][size_t(current.getInt("tier"))];need(tier.isObject(),Code::Conflict,"removed offline tier");
    for(auto& [asset,n]:bundle(tier))pending[asset]=add(pending[asset],mul(n,ticks));
    current.set("pending",encode(pending));current.set("remainder",number(total%c.def.getInt("interval")));current.set("anchor",number(c.now));
}
}
Json rewards(Call& c) {
    const auto& a=c.args();auto action=c.req.action;auto current=c.self[c.id];if(!current.isObject())current=Json::object();
    if(c.req.service=="loot") {
        keys(a,{"id"});need(action=="open",Code::Unsupported,action);need(bool(c.draw),Code::Unavailable,"host RNG required");
        Bundle reward;auto results=Json::array();loot(c,c.id,reward,results);c.pay(bundle(c.def["cost"]),reward);return row({{"draws",results},{"reward",encode(reward)}});
    }
    if(c.req.service=="albums") {
        keys(a,{"id","trade","target","give","want"});need(active(c.def,c.now),Code::Unavailable,"album season");
        if(action=="claim") {need(!current.getBool("claimed"),Code::Unavailable,"album claimed");auto needed=bundle(c.def["required"]);for(auto& [asset,n]:needed)need(c.balance(asset)>=n,Code::Insufficient,asset);
            c.pay(c.def.getBool("consume")?needed:Bundle{},bundle(c.def["reward"]));current.set("claimed",Json::fromBool(true));c.self.set(c.id,current);return current;}
        auto trade=id(a["trade"]);auto trades=c.shared[c.id];if(!trades.isObject())trades=Json::object();
        if(action=="offer") {auto target=id(a["target"]);need(target!=c.who.player&&c.state["profiles"].has(target),Code::Denied,"trade target");need(!trades.has(trade),Code::Conflict,"trade id");checkBundle(a["give"],c.cfg.economy);checkBundle(a["want"],c.cfg.economy);
            for(auto* b:{&a["give"],&a["want"]})for(auto& [asset,n]:b->members()){(void)n;need(c.def["required"].has(asset),Code::Denied,"not an album asset");}
            trades.set(trade,row({{"owner",str(c.who.player)},{"target",str(target)},{"give",a["give"]},{"want",a["want"]},{"status",str("pending")}}));
        } else {need(trades.has(trade),Code::Missing,"trade");auto t=trades[trade];need(t.getStr("status")=="pending",Code::Unavailable,"trade closed");
            if(action=="cancel"){need(t.getStr("owner")==c.who.player,Code::Denied,"trade owner");t.set("status",str("cancelled"));}
            else {need(action=="accept"&&t.getStr("target")==c.who.player,Code::Denied,"trade recipient");c.pay(bundle(t["give"]),bundle(t["want"]),"seller",t.getStr("owner"));c.pay(bundle(t["want"]),bundle(t["give"]),"buyer");t.set("status",str("completed"));}
            trades.set(trade,t);
        }
        c.shared.set(c.id,trades);return trades[trade];
    }
    if(c.req.service=="research") {
        keys(a,{"id","node"});auto unlocked=current["nodes"];if(!unlocked.isObject())unlocked=Json::object();
        if(action=="unlock") {auto node=id(a["node"]);auto d=c.def["nodes"][node];need(d.isObject(),Code::Missing,"research node");need(!unlocked.has(node),Code::Unavailable,"already unlocked");
            for(auto& dep:d["requires"].items())need(unlocked.has(dep.str()),Code::Unavailable,"research dependency");
            if(d.has("group"))for(auto& [other,v]:unlocked.members()){(void)v;need(c.def["nodes"][other].getStr("group")!=d.getStr("group"),Code::Conflict,"exclusive research branch");}
            c.pay(bundle(d["cost"]),bundle(d["reward"]));unlocked.set(node,Json::fromBool(true));current.set("nodes",unlocked);
        } else {need(action=="prestige",Code::Unsupported,action);need(unlocked.size()>=size_t(c.def.getInt("prestigeCount")),Code::Insufficient,"prestige prerequisites");c.pay(bundle(c.def["prestigeCost"]),bundle(c.def["prestigeReward"]));current.set("nodes",Json::object());current.set("prestige",number(add(current.getInt("prestige"),1)));}
        c.self.set(c.id,current);return current;
    }
    if(c.req.service=="offline") {
        keys(a,{"id","tier"});need(action=="claim"||action=="tier"||action=="checkpoint",Code::Unsupported,action);if(action!="claim")c.authority();
        accrue(c,current);
        if(action=="tier") {auto tier=integer(a["tier"],0,int64_t(c.def["tiers"].size())-1);current.set("tier",number(tier));current.set("remainder",number(0));}
        if(action=="claim") {c.pay({},bundle(current["pending"]));current.set("pending",Json::object());}
        c.self.set(c.id,current);return current;
    }
    throw Stop(Code::Unsupported,c.req.service);
}
}
