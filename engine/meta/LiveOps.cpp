#include "LiveOpsInternal.h"
namespace aiverse::meta::live {
Call::Call(const ServiceConfig& c,Json& s,const Principal& w,const ServiceRequest& r,Time t,const Context& ctx,const RewardDraw& rng)
:cfg(c),state(s),who(w),req(r),now(t),context(ctx),draw(rng),id(detail::id(r.arguments["id"])),operation(key(w.player,r.operation)),
 def(c.extensions["liveops"][r.service][id]),
 self(object(object(object(object(s,"live"),"players"),w.player),r.service)),
 shared(object(object(object(s,"live"),"shared"),r.service)) {
    need(def.isObject(),Code::Missing,"liveops definition: "+r.service+"/"+id);
}
Json& Call::player(const std::string& p) {need(state["profiles"].has(p),Code::Missing,"player");return object(object(object(state,"live"),"players"),p);}
void Call::pay(const Bundle& cost,const Bundle& reward,const std::string& suffix,const std::string& playerId) {
    auto p=playerId.empty()?who.player:playerId;
    Economy e(cfg.economy,Profile::load(state["profiles"][p],cfg.economy));
    auto result=e.transact(operation+":"+suffix,cost,reward);
    need(bool(result),result.code,result.detail);state.member("profiles").set(p,e.profile().save());
}
void Call::mail(const std::string& p,const Bundle& reward,const std::string& suffix) {
    Economy e(cfg.economy,Profile::load(state["profiles"][p],cfg.economy));
    Mail m;m.id=key(req.service+":"+id,suffix);m.reward=reward;
    auto result=e.deliver(m);need(bool(result),result.code,result.detail);state.member("profiles").set(p,e.profile().save());
}
int64_t Call::balance(const std::string& asset,const std::string& p) const {return state["profiles"][p.empty()?who.player:p]["balances"].getInt(asset);}
int64_t Call::metric(const std::string& metric,const std::string& p) const {return state["profiles"][p.empty()?who.player:p]["progress"].getInt(metric);}
void Call::once(const std::string& providerEvent) {
    auto& used=object(object(state,"live"),"tokens");
    need(!used.has(providerEvent),Code::Conflict,"provider event already consumed");used.set(providerEvent,str(who.player));
}
}
namespace aiverse::meta {
ServiceReply dispatchLiveOps(const ServiceConfig& cfg,Json& state,const Principal& who,const ServiceRequest& req,Time now,const Context& ctx,const RewardDraw& draw) {
    try {
        // Create both children before taking references to either child of Json's vector-backed object.
        auto& root=detail::object(state,"live");detail::object(root,"players");detail::object(root,"shared");detail::object(root,"tokens");
        live::Call c(cfg,state,who,req,now,ctx,draw); Json data;
        if(req.service=="loot"||req.service=="albums"||req.service=="research"||req.service=="offline") data=live::rewards(c);
        else if(req.service=="jobs"||req.service=="quests"||req.service=="events"||req.service=="notifications") data=live::activities(c);
        else if(req.service=="wars"||req.service=="asyncPvp"||req.service=="matchmaking"||req.service=="ratings") data=live::competition(c);
        else if(req.service=="referrals"||req.service=="ads"||req.service=="chains") data=live::retention(c);
        else return detail::fail(Code::Unsupported,req.service);
        return {{},std::move(data)};
    } catch(const live::Stop& e) {return detail::fail(e.code,e.what());}
}
Json viewLiveOps(const ServiceConfig& cfg,const Json& state,const std::string& player,Time now) {
    using namespace live;
    auto out=state["live"]["players"][player]; if(!out.isObject())out=Json::object();
    auto notices=Json::array();std::map<std::string,int64_t> groups;
    for(auto& [id,n]:out["notifications"].members()) {
        const auto& d=cfg.extensions["liveops"]["notifications"][id]; if(!d.isObject()||now<n.getInt("due"))continue;
        if(d.getInt("lifetime")&&now>=n.getInt("due")+d.getInt("lifetime"))continue;
        auto occurrence=d.getInt("repeat")?(now-n.getInt("due"))/d.getInt("repeat")+1:1;
        if(occurrence<=n.getInt("read"))continue;
        notices.push(row({{"id",str(id)},{"group",d["group"]},{"priority",d["priority"]},{"count",number(occurrence-n.getInt("read"))},{"metadata",d["metadata"]}}));
        groups[d.getStr("group")]=add(groups[d.getStr("group")],occurrence-n.getInt("read"));
    }
    std::stable_sort(notices.itemsMut().begin(),notices.itemsMut().end(),[](const Json& a,const Json& b){return a.getInt("priority")>b.getInt("priority");});
    out.set("badges",encode(groups));out.set("notices",notices);
    return out;
}
}
