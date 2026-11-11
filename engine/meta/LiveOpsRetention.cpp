#include "LiveOpsInternal.h"
namespace aiverse::meta::live {
Json retention(Call& c) {
    auto action=c.req.action;const auto& a=c.args();auto current=c.self[c.id];if(!current.isObject())current=Json::object();auto global=c.shared[c.id];if(!global.isObject())global=Json::object();
    if(c.req.service=="referrals") {
        keys(a,{"id","code","inviter","milestone"});
        if(action=="code") {auto code=id(a["code"]);auto d=c.def["codes"][code];need(d.isObject()&&active(d,c.now),Code::Unavailable,"promotion unavailable");need(!current["codes"].has(code),Code::Unavailable,"promotion claimed");need(global["codes"].getInt(code)<d.getInt("maxUses"),Code::Limit,"promotion limit");
            c.pay({},bundle(d["reward"]));object(current,"codes").set(code,Json::fromBool(true));object(global,"codes").set(code,number(global["codes"].getInt(code)+1));
        } else if(action=="bind") {
            auto inviter=id(a["inviter"]);need(inviter!=c.who.player&&c.state["profiles"].has(inviter),Code::Denied,"inviter");need(!current.has("inviter"),Code::Conflict,"referral already bound");need(c.context.values.count("accountAge")&&c.context.values.at("accountAge")<=c.def.getInt("maxAccountAge"),Code::Unavailable,"referral age");
            auto walk=inviter;std::set<std::string> visited{c.who.player};while(!walk.empty()){need(visited.insert(walk).second,Code::Conflict,"referral cycle");walk=c.state["live"]["players"][walk]["referrals"][c.id].getStr("inviter");}
            auto count=global["invites"].getInt(inviter);need(count<c.def.getInt("maxInvitees"),Code::Limit,"inviter limit");current.set("inviter",str(inviter));object(global,"invites").set(inviter,number(count+1));
        } else if(action=="claim") {
            auto milestone=id(a["milestone"]);auto d=c.def["milestones"][milestone];need(d.isObject()&&current.has("inviter"),Code::Missing,"referral milestone");need(!current["claims"].has(milestone),Code::Unavailable,"referral claimed");need(c.metric(d.getStr("metric"))>=d.getInt("target"),Code::Insufficient,"referral progress");
            c.pay({},bundle(d["inviterReward"]),"inviter",current.getStr("inviter"));c.pay({},bundle(d["inviteeReward"]),"invitee");object(current,"claims").set(milestone,Json::fromBool(true));
        } else throw Stop(Code::Unsupported,action);
        c.self.set(c.id,current);c.shared.set(c.id,global);return current;
    }
    if(c.req.service=="ads") {
        keys(a,{"id","ticket","token"});auto bucket=c.now/c.def.getInt("period");
        if(action=="begin") {
            need(!current.has("last")||c.now-current.getInt("last")>=c.def.getInt("cooldown"),Code::Limit,"ad cooldown");auto used=current.getInt("bucket",-1)==bucket?current.getInt("used"):0;need(used<c.def.getInt("limit"),Code::Limit,"ad limit");
            auto ticket=c.operation;object(current,"tickets").set(ticket,row({{"expires",number(add(c.now,c.def.getInt("timeout")))},{"reward",c.def["reward"]},{"done",Json::fromBool(false)}}));current.set("bucket",number(bucket));current.set("used",number(used+1));current.set("last",number(c.now));c.self.set(c.id,current);return row({{"ticket",str(ticket)}});
        }
        need(action=="complete",Code::Unsupported,action);c.authority();need(a["ticket"].isString(),Code::Invalid,"ad ticket");auto ticket=a["ticket"].str();auto t=current["tickets"][ticket];need(t.isObject()&&!t.getBool("done")&&c.now<t.getInt("expires"),Code::Unavailable,"ad ticket expired or complete");
        c.once("ad:"+id(a["token"]));c.pay({},bundle(t["reward"]));t.set("done",Json::fromBool(true));object(current,"tickets").set(ticket,t);c.self.set(c.id,current);return t;
    }
    if(c.req.service=="chains") {
        keys(a,{"id"});if(!current.has("node"))current.set("node",c.def["start"]);auto node=current.getStr("node");need(!node.empty(),Code::Unavailable,"chain completed");auto step=c.def["nodes"][node];need(step.isObject(),Code::Conflict,"removed chain node");
        if(action=="show") {auto bucket=c.now/c.def.getInt("period");auto shows=current.getInt("bucket",-1)==bucket?current.getInt("shows"):0;
            need(shows<c.def.getInt("maxShows"),Code::Limit,"offer impression limit");need(!current.has("last")||c.now-current.getInt("last")>=c.def.getInt("cooldown"),Code::Unavailable,"offer cooldown");
            Economy e(c.cfg.economy,Profile::load(c.state["profiles"][c.who.player],c.cfg.economy));auto visible=e.visibleOffers(c.context);need(std::find(visible.begin(),visible.end(),step.getStr("offer"))!=visible.end(),Code::Unavailable,"offer conditions");
            current.set("bucket",number(bucket));current.set("shows",number(shows+1));current.set("last",number(c.now));current.set("shown",Json::fromBool(true));
        } else {
            need(current.getBool("shown"),Code::Unavailable,"offer not shown");need(action=="buy"||action=="dismiss",Code::Unsupported,action);
            if(action=="buy") {Economy e(c.cfg.economy,Profile::load(c.state["profiles"][c.who.player],c.cfg.economy));auto result=e.buy(step.getStr("offer"),c.context,c.operation);need(bool(result),result.code,result.detail);c.state.member("profiles").set(c.who.player,e.profile().save());}
            current.set("node",str(step.getStr(action=="buy"?"accept":"reject")));current.set("shown",Json::fromBool(false));
        }
        c.self.set(c.id,current);return row({{"state",current},{"offer",step["offer"]}});
    }
    throw Stop(Code::Unsupported,c.req.service);
}
}
