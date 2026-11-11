#include "LiveOpsCompetition.h"
namespace aiverse::meta::live {
Json competition(Call& c) {
    if(c.req.service=="ratings")return ratings(c);
    if(c.req.service=="matchmaking")return matchmake(c);
    const auto& a=c.args();auto action=c.req.action;auto global=c.shared[c.id];if(!global.isObject())global=Json::object();
    if(c.req.service=="wars") {
        keys(a,{"id","amount"});auto entries=global["entries"];if(!entries.isObject())entries=Json::object();
        if(action=="enroll") {
            need(active(c.def,c.now)&&!global.getBool("paired"),Code::Unavailable,"war enrollment closed");auto clan=c.state["membership"].getStr(c.who.player);auto members=c.state["clans"][clan]["members"];
            need(!clan.empty()&&members.getStr(c.who.player)=="owner",Code::Denied,"clan owner");need(!entries.has(clan),Code::Conflict,"clan enrolled");int64_t strength=0;
            for(auto& [p,role]:members.members()){(void)role;need(!c.state["live"]["players"][p]["wars"].has(c.id),Code::Conflict,"member already enrolled");strength+=c.state["extras"][p].getInt("league");}
            entries.set(clan,row({{"members",members},{"strength",number(strength)},{"score",number(0)},{"contributions",Json::object()}}));
            for(auto& [p,role]:members.members()){(void)role;object(c.player(p),"wars").set(c.id,row({{"clan",str(clan)}}));}
        } else if(action=="pair") {
            c.authority();need(active(c.def,c.now)&&!global.getBool("paired"),Code::Unavailable,"war already paired or closed");need(entries.size()>=2,Code::Insufficient,"war clans");std::vector<std::pair<std::string,Json>> sorted(entries.members().begin(),entries.members().end());
            std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){return a.second.getInt("strength")!=b.second.getInt("strength")?a.second.getInt("strength")<b.second.getInt("strength"):a.first<b.first;});
            auto pairs=Json::array();for(size_t i=0;i<sorted.size();i+=2){auto pair=Json::array();pair.push(str(sorted[i].first));if(i+1<sorted.size())pair.push(str(sorted[i+1].first));pairs.push(pair);}global.set("pairs",pairs);global.set("paired",Json::fromBool(true));
        } else if(action=="score") {
            c.authority();need(active(c.def,c.now)&&global.getBool("paired")&&!global.getBool("settled"),Code::Unavailable,"war not active");auto clan=c.self[c.id].getStr("clan");need(entries[clan]["members"].has(c.who.player),Code::Denied,"war roster");auto entry=entries[clan];auto n=integer(a["amount"],1);entry.set("score",number(add(entry.getInt("score"),n)));object(entry,"contributions").set(c.who.player,number(add(entry["contributions"].getInt(c.who.player),n)));entries.set(clan,entry);
        } else if(action=="settle") {
            c.authority();need(c.def.getInt("end")&&c.now>=c.def.getInt("end")&&global.getBool("paired")&&!global.getBool("settled"),Code::Unavailable,"war settlement");
            for(auto& pair:global["pairs"].items())for(size_t i=0;i<pair.size();++i){auto clan=pair[i].str();auto entry=entries[clan];int outcome=pair.size()==1?0:(entry.getInt("score")>entries[pair[1-i].str()].getInt("score")?1:entry.getInt("score")<entries[pair[1-i].str()].getInt("score")?-1:0);
                for(auto& [p,role]:entry["members"].members()){(void)role;auto result=row({{"clan",str(clan)},{"score",entry["score"]},{"outcome",number(outcome)}});object(c.player(p),"wars").set(c.id,result);if(entry["contributions"].getInt(p)>=c.def.getInt("minimumContribution"))c.mail(p,bundle(c.def[outcome>0?"winReward":outcome<0?"loseReward":"drawReward"]),clan);}
            }
            global.set("settled",Json::fromBool(true));
        } else throw Stop(Code::Unsupported,action);
        global.set("entries",entries);c.shared.set(c.id,global);return row({{"paired",Json::fromBool(global.getBool("paired"))},{"settled",Json::fromBool(global.getBool("settled"))},{"pairs",global["pairs"]}});
    }
    if(c.req.service=="asyncPvp") {
        keys(a,{"id","snapshot","target","attack","win","event"});auto defenses=global["defenses"];if(!defenses.isObject())defenses=Json::object();auto attacks=global["attacks"];if(!attacks.isObject())attacks=Json::object();auto current=c.self[c.id];if(!current.isObject())current=Json::object();
        auto persist=[&](){global.set("defenses",defenses);global.set("attacks",attacks);c.shared.set(c.id,global);c.self.set(c.id,current);};
        if(action=="publish") {c.authority();need(a["snapshot"].isObject()&&a["snapshot"].dump(-1).size()<=size_t(c.def.getInt("maxSnapshotBytes")),Code::Invalid,"defense snapshot");auto d=defenses[c.who.player];if(!d.isObject())d=Json::object();d.set("snapshot",a["snapshot"]);d.set("version",number(add(d.getInt("version"),1)));defenses.set(c.who.player,d);persist();return row({{"version",d["version"]}});}
        if(action=="targets") {auto list=Json::array();for(auto& [p,d]:defenses.members()){if(p==c.who.player||c.now<d.getInt("shield")||c.now<d.getInt("lock"))continue;auto diff=c.state["extras"][p].getInt("league")-c.state["extras"][c.who.player].getInt("league");if(std::abs(diff)>c.def.getInt("ratingRange"))continue;list.push(row({{"player",str(p)},{"version",d["version"]}}));if(list.size()==50)break;}return list;}
        auto attack=id(a["attack"]);
        if(action=="begin"||action=="revenge") {
            auto target=id(a["target"]);need(target!=c.who.player&&defenses.has(target),Code::Denied,"defense target");auto d=defenses[target];bool revenge=action=="revenge";
            need(!attacks.has(attack),Code::Conflict,"attack id");need(!current.has("pending")||c.now>=attacks[current.getStr("pending")].getInt("expires")||attacks[current.getStr("pending")].getBool("done"),Code::Conflict,"attack already pending");
            if(revenge)need(current.getStr("revengeAgainst")==target&&!current.getBool("revengeUsed"),Code::Denied,"no revenge");
            need(c.now>=d.getInt("lock")&&(c.now>=d.getInt("shield")||(revenge&&c.def.getBool("revengeBypassShield"))),Code::Unavailable,"defender protected");
            auto diff=c.state["extras"][target].getInt("league")-c.state["extras"][c.who.player].getInt("league");need(std::abs(diff)<=c.def.getInt("ratingRange"),Code::Unavailable,"opponent rating");
            auto bucket=c.now/c.def.getInt("period"),used=current.getInt("bucket",-1)==bucket?current.getInt("used"):0;need(used<c.def.getInt("attackLimit"),Code::Limit,"attack limit");c.pay(bundle(c.def["attackCost"]),{});
            auto expires=add(c.now,c.def.getInt("timeout"));attacks.set(attack,row({{"owner",str(c.who.player)},{"target",str(target)},{"expires",number(expires)},{"snapshot",d["snapshot"]},{"version",d["version"]},{"reward",c.def["winReward"]},{"shieldSeconds",c.def["shieldSeconds"]},{"done",Json::fromBool(false)}}));
            d.set("lock",number(expires));d.set("lockedBy",str(attack));defenses.set(target,d);current.set("pending",str(attack));current.set("bucket",number(bucket));current.set("used",number(used+1));if(revenge)current.set("revengeUsed",Json::fromBool(true));persist();return attacks[attack];
        }
        need(action=="complete",Code::Unsupported,action);c.authority();auto pending=attacks[attack];need(pending.isObject()&&pending.getStr("owner")==c.who.player&&!pending.getBool("done")&&c.now<pending.getInt("expires"),Code::Unavailable,"attack expired or completed");need(a["win"].type()==Json::Type::Bool,Code::Invalid,"win");c.once("async:"+id(a["event"]));
        if(a["win"].boolean())c.pay({},bundle(pending["reward"]));auto target=pending.getStr("target");auto d=defenses[target];need(d.getStr("lockedBy")==attack,Code::Conflict,"defense lock");d.set("lock",number(0));d.set("shield",number(add(c.now,pending.getInt("shieldSeconds"))));defenses.set(target,d);pending.set("done",Json::fromBool(true));pending.set("win",a["win"]);attacks.set(attack,pending);
        auto revenge=c.state["live"]["players"][target]["asyncPvp"][c.id];if(!revenge.isObject())revenge=Json::object();revenge.set("revengeAgainst",str(c.who.player));revenge.set("revengeUsed",Json::fromBool(false));object(c.player(target),"asyncPvp").set(c.id,revenge);persist();return row({{"win",a["win"]},{"target",str(target)}});
    }
    throw Stop(Code::Unsupported,c.req.service);
}
}
