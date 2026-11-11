#include "ServiceInternal.h"
#include "LiveOps.h"
#include <algorithm>
namespace aiverse::meta {
using namespace detail;
namespace {
Json initialState() {
    auto j=Json::object(); j.set("version",number(1)); j.set("revision",number(0)); j.set("time",number(0));
    for(auto k:{"profiles","extras","friends","requests","blocks","clans","membership","tournaments","boards","chats","receipts","events","live"}) j.set(k,Json::object());
    return j;
}
Context context(const Json& state,const std::string& player,Time now,const ServiceConfig& c) {
    Context ctx; ctx.player=player; ctx.now=now;
    auto p=Profile::load(state["profiles"][player],c.economy); ctx.values=p.progress;
    if(state["extras"][player].has("created")) ctx.values["accountAge"]=std::max(int64_t(0),now-state["extras"][player].getInt("created"));
    for(auto& [key,v]:state["extras"][player]["rights"].members()) {
        if(!v.getBool("revoked")&&v.getInt("start")<=now&&(!v.getInt("end")||now<v.getInt("end"))) {
            auto tag=c.extensions["entitlements"][key].getStr("tag"); if(!tag.empty()) ctx.tags.insert(tag);
        }
    }
    return ctx;
}
Result transact(Json& state,const ServiceConfig& c,const std::string& p,const Bundle& cost,const Bundle& reward,const std::string& op) {
    Economy e(c.economy,Profile::load(state["profiles"][p],c.economy)); auto r=e.transact(op,cost,reward);
    if(r) state.member("profiles").set(p,e.profile().save()); return r;
}
std::string pairKey(const std::string& a,const std::string& b) { auto j=Json::array(); j.push(str(a)); j.push(str(b)); return j.dump(-1); }
bool blocked(const Json& s,const std::string& a,const std::string& b) {return s["blocks"][a].has(b)||s["blocks"][b].has(a);}
}
Service::Service(ServiceConfig config,Json state,Commit commit,RewardDraw draw):config_(std::move(config)),state_(std::move(state)),commit_(std::move(commit)),draw_(std::move(draw)) {
    // Validate typed callers with the same parser and fail closed on incompatible persisted state.
    Json cfg=Json::object(); cfg.set("economy",config_.economy.save()); cfg.set("extensions",config_.extensions);
    (void)ServiceConfig::parse(cfg);
    if(state_.isNull()) state_=initialState();
    if(!state_.has("live")) state_.set("live",Json::object());
    keys(state_,{"version","revision","time","profiles","extras","friends","requests","blocks","clans","membership","tournaments","boards","chats","receipts","events","live"});
    integer(state_["version"],1,1); integer(state_["revision"]); integer(state_["time"]);
    for(auto k:{"profiles","extras","friends","requests","blocks","clans","membership","tournaments","boards","chats","receipts","events","live"}) require(state_[k].isObject(),std::string("state ")+k);
    for(auto& [p,v]:state_["profiles"].members()) require(Profile::load(v,config_.economy).player==p,"profile identity");
}
ServiceReply Service::execute(const Principal& who,const ServiceRequest& request,Time now) {
    try {
        id(str(who.player)); require(now>=0&&now<=kMaxAmount,"time");
        if(request.service=="meta"&&request.action=="view") return {{},view(who.player,now)};
        require(!request.operation.empty()&&request.operation.size()<=128,"operation id");
        auto fingerprint=Json::array(); fingerprint.push(str(request.service)); fingerprint.push(str(request.action)); fingerprint.push(canonical(request.arguments)); fingerprint.push(Json::fromBool(who.authority));
        auto receipt=pairKey(who.player,request.operation); auto body=fingerprint.dump(-1);
        if(state_["receipts"].has(receipt)) {
            auto& old=state_["receipts"][receipt];
            if(old.getStr("body")!=body) return fail(Code::Conflict,"operation payload changed");
            return {{Code::Duplicate,{}},old["data"]};
        }
        if(now<state_.getInt("time")) return fail(Code::Conflict,"clock rollback");
        if(state_.getInt("revision")==kMaxAmount) return fail(Code::Limit,"revision");
        Json next=state_; auto reply=dispatch(next,who,request,now); if(!reply.result) return reply;
        Json entry=Json::object(); entry.set("body",str(body)); entry.set("data",reply.data); next.member("receipts").set(receipt,entry);
        next.set("revision",number(state_.getInt("revision")+1)); next.set("time",number(now));
        if(commit_&&!commit_(state_.getInt("revision"),next)) return fail(Code::Conflict,"storage CAS");
        state_=std::move(next); return reply;
    } catch(const std::exception& e) { return fail(Code::Invalid,e.what()); }
}
ServiceReply Service::dispatch(Json& s,const Principal& who,const ServiceRequest& r,Time now) {
    const auto& a=r.arguments; require(a.isObject(),"arguments");
    auto p=who.player; auto op=pairKey(p,r.operation); auto action=r.action; auto service=r.service;
    auto ok=[](Json data=Json::object())->ServiceReply {return {{},std::move(data)};};
    auto missing=[](){return fail(Code::Missing,"definition or object");};
    auto denied=[](){return fail(Code::Denied,"permission");};
    auto limit=[](){return fail(Code::Limit,"capacity or limit");};
    auto target=[&](){auto t=id(a["target"]); require(s["profiles"].has(t),"unknown player"); return t;};
    auto& x=config_.extensions;
    if(service=="profile"&&action=="create") {
        keys(a,{}); if(s["profiles"].has(p)) return {{Code::Duplicate,{}},Json::object()};
        Profile profile; profile.player=p; Economy e(config_.economy,profile);
        auto rr=e.transact("initial",{},x.has("initial")?bundle(x["initial"]):Bundle{}); if(!rr) return {rr,{}};
        s.member("profiles").set(p,e.profile().save()); object(s.member("extras"),p).set("created",number(now)); return ok();
    }
    if(!s["profiles"].has(p)) return missing();
    if(x["liveops"].has(service)) return dispatchLiveOps(config_,s,who,r,now,context(s,p,now,config_),draw_);
    if(service=="economy") {
        Economy e(config_.economy,Profile::load(s["profiles"][p],config_.economy)); auto ctx=context(s,p,now,config_); Result result;
        if(action=="buy"||action=="daily"||action=="energy"||action=="mail"||action=="reserve") {
            keys(a,{"id"}); auto key=id(a["id"]);
            if(action=="buy") result=e.buy(key,ctx,op);
            if(action=="daily") result=e.claimDaily(key,now,op);
            if(action=="energy") result=e.regenerate(key,now);
            if(action=="mail") result=e.claimMail(key,now,op);
            if(action=="reserve") result=e.reserveOrder(key,ctx,op);
        } else if(action=="claim") { keys(a,{"id","tier"}); result=e.claim(id(a["id"]),size_t(integer(a["tier"])),ctx,op); }
        else if(action=="event") { if(!who.authority) return denied(); keys(a,{"metric","amount"}); result=e.recordEvent(id(a["metric"]),integer(a["amount"]),ctx,op); }
        else if(action=="grant") { if(!who.authority) return denied(); keys(a,{"reward"}); result=e.transact(op,{},bundle(a["reward"])); }
        else if(action=="deliver") {
            if(!who.authority) return denied(); keys(a,{"id","reward","expires"}); Mail mail; mail.id=id(a["id"]); mail.reward=bundle(a["reward"]); mail.expires=integer(a["expires"]); result=e.deliver(mail);
        } else if(action=="fulfill"||action=="cancel") {
            if(!who.authority) return denied(); keys(a,{"order","token"}); require(a["order"].isString()&&!a["order"].str().empty()&&a["order"].str().size()<=512,"order ID"); auto order=a["order"].str();
            if(action=="cancel") result=e.cancelOrder(order);
            else { auto token=id(a["token"]); auto event="payment:"+token;
                auto owner=pairKey(p,order); if(s["events"].has(event)&&s["events"].getStr(event)!=owner) return fail(Code::Conflict,"payment token used");
                result=e.fulfillOrder(order,token); if(result) s.member("events").set(event,str(owner)); }
        } else return fail(Code::Unsupported,action);
        if(result) s.member("profiles").set(p,e.profile().save());
        Json data=Json::object(); if(action=="reserve") data.set("order",str(op)); return {result,data};
    }
    if(service=="friends") {
        if(!x.has("friends")) return missing(); keys(a,{"target"}); auto t=target(); if(t==p) return denied();
        auto& fs=s.member("friends"); auto& req=s.member("requests");
        if(action=="block") {object(s.member("blocks"),p).set(t,Json::fromBool(true)); object(fs,p).erase(t); object(fs,t).erase(p); object(req,p).erase(t); object(req,t).erase(p); return ok();}
        if(action=="unblock") {object(s.member("blocks"),p).erase(t); return ok();}
        if(action=="remove") {object(fs,p).erase(t); object(fs,t).erase(p); return ok();}
        if(action=="reject") {object(req,p).erase(t); return ok();}
        if(blocked(s,p,t)) return denied();
        auto max=x["friends"].getInt("maxFriends",100); auto requests=x["friends"].getInt("maxRequests",100);
        if(fs[p].has(t)) return ok();
        if(action=="request") {if(req[t].size()>=size_t(requests)) return limit(); object(req,t).set(p,Json::fromBool(true)); return ok();}
        if(action=="accept") {
            if(!req[p].has(t)) return denied(); if(fs[p].size()>=size_t(max)||fs[t].size()>=size_t(max)) return limit();
            object(fs,p).set(t,Json::fromBool(true)); object(fs,t).set(p,Json::fromBool(true)); object(req,p).erase(t); object(req,t).erase(p); return ok();
        }
        return fail(Code::Unsupported,action);
    }
    if(service=="clan") {
        if(!x.has("clans")) return missing(); auto cfg=x["clans"]; auto& clans=s.member("clans"); auto& membership=s.member("membership");
        if(action=="create") {
            keys(a,{"id"}); auto cid=id(a["id"]); if(clans.has(cid)||membership.has(p)) return fail(Code::Conflict,"clan exists or already joined");
            auto rr=transact(s,config_,p,cfg.has("createCost")?bundle(cfg["createCost"]):Bundle{}, {},op); if(!rr) return {rr,{}};
            auto g=Json::object(); auto members=Json::object(); members.set(p,str("owner"));
            g.set("members",members); g.set("invites",Json::object()); g.set("bank",Json::object()); g.set("claimed",Json::object()); clans.set(cid,g); membership.set(p,str(cid)); return ok();
        }
        keys(a,{"id","target","role","asset","amount","goal"}); auto cid=id(a["id"]); if(!clans.has(cid)) return missing(); auto& g=clans.member(cid);
        auto role=g["members"].getStr(p); bool officer=role=="owner"||role=="officer";
        if(action=="join") {
            if(membership.has(p)||!g["invites"].has(p)||g["invites"].getInt(p)<=now) return denied();
            if(g["members"].size()>=size_t(cfg.getInt("maxMembers",50))) return limit();
            g.member("members").set(p,str("member")); g.member("invites").erase(p); membership.set(p,str(cid)); return ok();
        }
        if(role.empty()) return denied();
        if(action=="invite") {auto t=target(); if(!officer||blocked(s,p,t)||membership.has(t)) return denied();
            if(g["invites"].size()>=size_t(cfg.getInt("maxMembers",50))*2) {for(auto it=g.member("invites").membersMut().begin();it!=g.member("invites").membersMut().end();) {if(it->second.integer()<=now) it=g.member("invites").membersMut().erase(it); else ++it;}}
            if(g["invites"].size()>=size_t(cfg.getInt("maxMembers",50))*2) return limit();
            auto duration=cfg.getInt("inviteSeconds",86400); if(duration>kMaxAmount-now) return limit(); g.member("invites").set(t,number(now+duration)); return ok();}
        if(action=="role"||action=="transfer") {auto t=target(); if(role!="owner"||!g["members"].has(t)||t==p) return denied();
            if(action=="transfer") {g.member("members").set(t,str("owner")); g.member("members").set(p,str("officer"));}
            else {auto v=id(a["role"]); if(v!="member"&&v!="officer") return denied(); g.member("members").set(t,str(v));} return ok();}
        if(action=="leave"||action=="kick") {auto t=action=="leave"?p:target(); auto tr=g["members"].getStr(t);
            if(tr.empty()||(action=="kick"&&(!officer||tr=="owner"||(tr=="officer"&&role!="owner")))) return denied();
            if(tr=="owner"&&g["members"].size()>1) return fail(Code::Conflict,"transfer ownership first");
            g.member("members").erase(t); membership.erase(t); if(g["members"].size()==0) clans.erase(cid); return ok();}
        if(action=="donate") {auto asset=id(a["asset"]); auto n=integer(a["amount"],1); if(!config_.economy.assets.count(asset)) return missing();
            auto total=g["bank"].getInt(asset); if(n>kMaxAmount-total) return limit(); auto rr=transact(s,config_,p,{{asset,n}},{},op); if(!rr) return {rr,{}};
            g.member("bank").set(asset,number(total+n)); return ok();}
        if(action=="goal") {auto goal=id(a["goal"]); if(!cfg["goals"].has(goal)) return missing(); auto def=cfg["goals"][goal];
            auto claim=pairKey(p,goal); if(g["claimed"].has(claim)) return {{Code::Duplicate,{}},{}};
            if(g["bank"].getInt(def.getStr("asset"))<def.getInt("amount")) return fail(Code::Insufficient,"clan goal");
            auto rr=transact(s,config_,p,{},bundle(def["reward"]),op); if(!rr) return {rr,{}}; g.member("claimed").set(claim,Json::fromBool(true)); return ok();}
        return fail(Code::Unsupported,action);
    }
    if(service=="items") {
        keys(a,{"id","type","slot","inputs"}); auto& extra=object(s.member("extras"),p); object(extra,"items"); object(extra,"equipment");
        auto& items=extra.member("items"); auto& equipment=extra.member("equipment");
        auto uid=id(a["id"]);
        if(action=="grant"||action=="redeem") {if(action=="grant"&&!who.authority) return denied(); auto type=id(a["type"]); if(!x["items"].has(type)) return missing(); if(items.has(uid)) return fail(Code::Conflict,"instance exists");
            if(action=="redeem") {auto asset=x["items"][type].getStr("asset");if(asset.empty())return missing();auto rr=transact(s,config_,p,{{asset,1}},{},op);if(!rr)return {rr,{}};}
            auto item=Json::object(); item.set("type",str(type)); item.set("level",number(0)); items.set(uid,item); return ok();}
        if(!items.has(uid)) return missing(); auto item=items[uid]; auto type=item.getStr("type"); auto def=x["items"][type]; if(def.isNull()) return missing();
        auto equipped=[&](const std::string& key){for(auto& [slot,v]:equipment.members()) {(void)slot;if(v.str()==key)return true;}return false;};
        if(action=="equip") {auto slot=id(a["slot"]); bool valid=false; for(auto& v:def["slots"].items()) if(v.str()==slot) valid=true;
            if(!valid||equipped(uid)) return denied(); if(equipment.has(slot)) return fail(Code::Conflict,"slot occupied"); equipment.set(slot,str(uid)); return ok();}
        if(action=="unequip") {for(auto it=equipment.membersMut().begin();it!=equipment.membersMut().end();) {if(it->second.str()==uid) it=equipment.membersMut().erase(it); else ++it;} return ok();}
        if(action=="upgrade") {auto level=item.getInt("level"); if(level<0||size_t(level)>=def["upgradeCosts"].size()) return limit();
            auto rr=transact(s,config_,p,bundle(def["upgradeCosts"][size_t(level)]),{},op); if(!rr) return {rr,{}};
            item.set("level",number(level+1)); items.set(uid,item); return ok();}
        if(equipped(uid)) return denied();
        if(action=="salvage") {auto rr=transact(s,config_,p,{},bundle(def["salvage"]),op); if(!rr) return {rr,{}}; items.erase(uid); return ok();}
        if(action=="merge") {if(!def.has("mergeCount")) return missing(); require(a["inputs"].isArray(),"merge inputs"); std::set<std::string> inputs{uid};
            for(auto& v:a["inputs"].items()) require(inputs.insert(id(v)).second,"duplicate merge input");
            if(inputs.size()!=size_t(def.getInt("mergeCount"))) return fail(Code::Invalid,"merge count includes id");
            for(auto& key:inputs) if(!items.has(key)||items[key].getStr("type")!=type||items[key].getInt("level")!=item.getInt("level")||equipped(key)) return denied();
            for(auto& key:inputs) items.erase(key); item.set("type",def["mergeTo"]); item.set("level",number(0)); items.set(uid,item); return ok();}
        return fail(Code::Unsupported,action);
    }
    if(service=="entitlement") {
        if(!who.authority) return denied(); keys(a,{"id","event","sequence","start","end"}); auto key=id(a["id"]); auto event=id(a["event"]);
        if(!x["entitlements"].has(key)) return missing(); auto seq=integer(a["sequence"],1); auto& rights=object(object(s.member("extras"),p),"rights");
        auto eventKey="entitlement:"+event; auto owner=pairKey(p,key);
        if(s["events"].has(eventKey)) return fail(Code::Conflict,"provider event already processed");
        if(seq<=rights[key].getInt("sequence")) return fail(Code::Conflict,"stale provider sequence");
        if(action!="grant"&&action!="revoke") return fail(Code::Unsupported,action);
        auto start=integer(a["start"]),end=integer(a["end"]); if(end&&end<=start) return fail(Code::Invalid,"entitlement interval");
        auto right=Json::object(); right.set("sequence",number(seq)); right.set("start",number(start)); right.set("end",number(end)); right.set("revoked",Json::fromBool(action=="revoke"));
        rights.set(key,right); s.member("events").set(eventKey,str(owner)); return ok();
    }
    if(service=="board") {
        keys(a,{"id","score"}); auto key=id(a["id"]); if(!config_.boards.count(key)) return missing(); if(!who.authority) return denied();
        if(action!="submit") return fail(Code::Unsupported,action); Leaderboard board(config_.boards.at(key));
        if(s["boards"].has(key)) {auto rr=board.restore(s["boards"][key]); if(!rr) return {rr,{}};}
        auto rr=board.submit(p,integer(a["score"],-kMaxAmount),now,op); if(rr) s.member("boards").set(key,board.save()); return {rr,{}};
    }
    if(service=="tournament") {
        keys(a,{"id","score"}); auto key=id(a["id"]); if(!x["tournaments"].has(key)) return missing(); auto cfg=x["tournaments"][key];
        auto& tournaments=s.member("tournaments"); auto& t=object(tournaments,key);
        bool active=now>=cfg.getInt("start")&&now<cfg.getInt("end");
        object(t,"entrants"); object(t,"groups"); auto& entrants=t.member("entrants"); auto& groups=t.member("groups");
        if(action=="join") {if(!active||t.getBool("closed")) return fail(Code::Unavailable,"tournament closed"); if(entrants.has(p)) return ok();
            auto league=std::min(s["extras"][p].getInt("league"),cfg.getInt("maxLeague")); size_t count=0;
            for(auto& [u,v]:entrants.members()) {(void)u;if(v.getInt("league")==league)++count;}
            auto group=std::to_string(league)+":"+std::to_string(count/size_t(cfg.getInt("groupSize")));
            auto entry=Json::object(); entry.set("league",number(league)); entry.set("group",str(group)); entrants.set(p,entry);
            Leaderboard board(BoardConfig::parse(cfg["board"])); if(groups.has(group)) {auto rr=board.restore(groups[group]);if(!rr)return {rr,{}};}
            groups.set(group,board.save()); return ok();}
        if(action=="submit") {if(!who.authority) return denied(); if(!active||t.getBool("closed")||!entrants.has(p)) return fail(Code::Unavailable,"not enrolled or closed");
            auto group=entrants[p].getStr("group"); Leaderboard board(BoardConfig::parse(cfg["board"])); auto rr=board.restore(groups[group]); if(!rr)return {rr,{}};
            rr=board.submit(p,integer(a["score"],-kMaxAmount),now,op); if(rr) groups.set(group,board.save()); return {rr,{}};}
        if(action=="settle") {if(!who.authority) return denied(); if(now<cfg.getInt("end")) return fail(Code::Unavailable,"tournament active"); if(t.getBool("closed")) return {{Code::Duplicate,{}},{}};
            auto results=Json::object();
            for(auto& [gid,data]:groups.members()) { (void)gid; Leaderboard board(BoardConfig::parse(cfg["board"])); auto rr=board.restore(data); if(!rr)return {rr,{}};
                auto page=board.page(cfg.getInt("end")-1,0,10000);
                for(size_t i=0;i<page.entries.size();++i) {auto& row=page.entries[i]; auto league=entrants[row.player].getInt("league");
                    if(i<size_t(cfg.getInt("promote"))) league=std::min(league+1,cfg.getInt("maxLeague"));
                    else if(i>=page.entries.size()-std::min(page.entries.size(),size_t(cfg.getInt("demote")))) league=std::max(int64_t(0),league-1);
                    object(s.member("extras"),row.player).set("league",number(league));
                    auto result=Json::object(); result.set("rank",number(int64_t(row.rank))); result.set("league",number(league)); result.set("score",number(row.score)); results.set(row.player,result);
                    if(row.rank&&row.rank<=cfg["rewards"].size()) { Economy e(config_.economy,Profile::load(s["profiles"][row.player],config_.economy)); Mail m; m.id="tournament:"+key; m.reward=bundle(cfg["rewards"][row.rank-1]);
                        rr=e.deliver(m);if(!rr)return {rr,{}};s.member("profiles").set(row.player,e.profile().save()); }
                }
            }
            t.set("results",results); t.set("closed",Json::fromBool(true)); return ok(results);
        }
        return fail(Code::Unsupported,action);
    }
    if(service=="chat") {
        keys(a,{"id","text","kind","sequence","target","until"}); auto key=id(a["id"]); if(!config_.chats.count(key)) return missing();
        Chat chat(config_.chats.at(key),[&](const std::string& user,const std::string& verb){return user==p&&(verb!="delete"||who.authority);});
        if(s["chats"].has(key)) {auto rr=chat.restore(s["chats"][key]);if(!rr)return {rr,{}};} Result rr;
        if(action=="send") {require(a["text"].isString(),"text");rr=chat.send(p,a.has("kind")?id(a["kind"]):"text",a["text"].str(),now,op);}
        else if(action=="read") rr=chat.markRead(p,integer(a["sequence"]));
        else if(action=="report") rr=chat.report(p,integer(a["sequence"],1));
        else if(action=="delete") rr=chat.remove(p,integer(a["sequence"],1));
        else if(action=="mute") {if(!who.authority)return denied();chat.mute(target(),integer(a["until"]));}
        else if(action=="block"||action=="unblock") chat.block(p,target(),action=="block");
        else return fail(Code::Unsupported,action);
        if(rr)s.member("chats").set(key,chat.save());return {rr,{}};
    }
    return fail(Code::Unsupported,service);
}
Json Service::view(const std::string& p,Time now) const {
    require(now>=0&&now<=kMaxAmount,"view time");
    require(state_["profiles"].has(p),"unknown player");
    auto out=Json::object(); out.set("revision",state_["revision"]); out.set("player",str(p));
    auto profile=state_["profiles"][p]; profile.erase("receipts"); profile.erase("orders"); out.set("profile",profile); out.set("extras",state_["extras"][p]);
    out.set("friends",state_["friends"][p]); out.set("requests",state_["requests"][p]);
    out.set("clanId",state_["membership"][p]); auto cid=state_["membership"].getStr(p);
    if(!cid.empty()) {auto clan=state_["clans"][cid]; clan.erase("claimed"); out.set("clan",clan);}
    auto invites=Json::array();for(auto& [key,g]:state_["clans"].members())if(g["invites"].getInt(p)>now)invites.push(str(key));out.set("invites",invites);
    Economy e(config_.economy,Profile::load(state_["profiles"][p],config_.economy)); auto offers=Json::array(); auto ctx=context(state_,p,now,config_);
    for(auto& key:e.visibleOffers(ctx))offers.push(str(key));out.set("offers",offers);
    auto tags=Json::array();for(auto& tag:ctx.tags)tags.push(str(tag));out.set("tags",tags);
    auto boards=Json::object();for(auto& [key,cfg]:config_.boards) {Leaderboard board(cfg);if(state_["boards"].has(key)) require(bool(board.restore(state_["boards"][key])),"invalid board state");
        auto rows=Json::array();for(auto& row:board.page(now).entries) {auto v=Json::object();v.set("player",str(row.player)); v.set("score",number(row.score)); v.set("rank",number(int64_t(row.rank)));rows.push(v);}boards.set(key,rows);}
    out.set("boards",boards);
    auto chats=Json::object();for(auto& [key,cfg]:config_.chats) {Chat chat(cfg,[&](const std::string& user,const std::string& verb){return user==p&&verb=="read";});
        if(state_["chats"].has(key))require(bool(chat.restore(state_["chats"][key])),"invalid chat state");auto rows=Json::array();
        for(auto& m:chat.history(p,now)) {auto v=Json::object();v.set("sequence",number(m.sequence)); v.set("sender",str(m.sender)); v.set("text",str(m.text)); rows.push(v);}chats.set(key,rows);}
    out.set("chats",chats);
    auto tournaments=Json::object();for(auto& [key,v]:state_["tournaments"].members()) {auto t=Json::object();t.set("entry",v["entrants"][p]); t.set("result",v["results"][p]); t.set("closed",Json::fromBool(v.getBool("closed")));tournaments.set(key,t);}out.set("tournaments",tournaments);
    out.set("liveops",viewLiveOps(config_,state_,p,now));
    return out;
}
} // namespace aiverse::meta
