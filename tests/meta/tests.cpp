#include "engine/meta/Meta.h"
#include "engine/meta/Social.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::meta;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(false)
static Config config() {
    return Config::parse(Json::parse(R"({
      "assets":{"gold":{"cap":1000},"gems":{"cap":100},"sword":{"cap":1},"energy":{"cap":10}},
      "offers":{
        "sword":{"cost":{"gold":100},"reward":{"sword":1},"limit":1},
        "sale":{"cost":{"gold":10},"reward":{"gems":1},"limit":2,"cooldown":5,"schedule":{"period":100,"duration":50},"condition":{"minimum":{"level":2}}},
        "iap":{"product":"gems_pack","reward":{"gems":10},"schedule":{"end":10}}
      },
      "tracks":{
        "wins":{"metric":"wins","thresholds":[1,3],"rewards":[{"gold":10},{"gold":20}]},
        "weekly":{"metric":"wins","thresholds":[2],"rewards":[{"gems":5}],"schedule":{"period":100}},
        "pass":{"metric":"xp","premiumTag":"premium","thresholds":[10],"rewards":[{"gems":1}]}
      },
      "daily":{"login":{"period":100,"rewards":[{"gold":1},{"gold":2}]}},
      "energy":{"stamina":{"asset":"energy","cap":10,"amount":3,"interval":10}}
    })"));
}
static Context context(Time now=0) { Context c; c.now=now; c.player="p"; c.values["level"]=2; return c; }
static void economy() {
    auto c=config(); CHECK(Config::parse(c.save()).save()==c.save());
    Profile p; p.player="p"; p.balances["gold"]=200; Economy e(c,p);
    CHECK(e.buy("sword",context(),"buy")); CHECK(e.profile().balances.at("gold")==100);
    CHECK(e.buy("sword",context(),"buy").code==Code::Duplicate);
    CHECK(e.buy("sale",context(),"buy").code==Code::Conflict);
    CHECK(e.buy("sword",context(),"buy2").code==Code::Unavailable);
    auto before=e.profile().save(); CHECK(e.transact("overflow",{{"gold",1}},{{"sword",1}}).code==Code::Limit); CHECK(e.profile().save()==before);
    CHECK(e.transact("bad",{{"gold",101}},{}).code==Code::Insufficient);
    CHECK(e.transact("bad",{},{{"missing",1}}).code==Code::Invalid);
    CHECK(e.buy("sale",context(),"sale1")); CHECK(e.buy("sale",context(4),"sale2").code==Code::Unavailable);
    CHECK(e.buy("sale",context(5),"sale2")); CHECK(e.buy("sale",context(10),"sale3").code==Code::Unavailable);
    CHECK(e.buy("sale",context(100),"sale3"));
    CHECK(e.buy("iap",context(),"fake").code==Code::Denied);
    CHECK(e.fulfill("iap",context(100),"store:paid")); // paid order honored after offer expiry
    CHECK(e.fulfill("iap",context(100),"store:paid").code==Code::Duplicate);
    CHECK(e.recordEvent("wins",1,context(5),"match1")); CHECK(e.recordEvent("wins",1,context(10),"match2"));
    CHECK(e.claim("weekly",0,context(10),"weekly")); CHECK(e.claim("weekly",0,context(100),"weekly2").code==Code::Insufficient);
    CHECK(e.claim("wins",0,context(),"wins")); CHECK(e.claim("wins",0,context(),"wins2").code==Code::Duplicate);
    CHECK(e.record("xp",10,"xp")); CHECK(e.claim("pass",0,context(),"pass").code==Code::Unavailable);
    auto premium=context(); premium.tags.insert("premium"); CHECK(e.claim("pass",0,premium,"pass"));
    CHECK(e.claimDaily("login",0,"d0")); CHECK(e.claimDaily("login",99,"d1").code==Code::Unavailable);
    CHECK(e.claimDaily("login",100,"d1")); CHECK(e.profile().daily.at("login").streak==2);
    CHECK(e.claimDaily("login",300,"d3")); CHECK(e.profile().daily.at("login").streak==1);
    CHECK(e.regenerate("stamina",0)); CHECK(e.regenerate("stamina",9)); CHECK(e.profile().balances.count("energy")==0);
    CHECK(e.regenerate("stamina",10)); CHECK(e.profile().balances.at("energy")==3);
    CHECK(e.regenerate("stamina",35)); CHECK(e.profile().balances.at("energy")==9);
    CHECK(e.regenerate("stamina",40)); CHECK(e.profile().balances.at("energy")==10);
    CHECK(e.regenerate("stamina",39).code==Code::Conflict);
    Mail mail; mail.id="gift"; mail.reward={{"gold",5}}; mail.expires=100; CHECK(e.deliver(mail)); CHECK(e.deliver(mail).code==Code::Duplicate);
    CHECK(e.claimMail("gift",100,"mail").code==Code::Unavailable); CHECK(e.claimMail("gift",99,"mail"));
    auto loaded=Profile::load(Json::parse(e.profile().save().dump()),c); CHECK(loaded.save()==e.profile().save());
    Economy restored(c,loaded); CHECK(restored.buy("sword",context(),"buy").code==Code::Duplicate);
    Economy failed(c,p,[](int64_t,const Json&){return false;}); CHECK(failed.buy("sword",context(),"x").code==Code::Conflict); CHECK(failed.profile().save()==p.save());
    bool threw=false; try { Config::parse(Json::parse(R"({"offers":{"x":{"reward":{"unknown":1}}}})")); } catch(...) {threw=true;} CHECK(threw);
    threw=false; try { Config::parse(Json::parse(R"({"assets":{"x":{"cap":1.5}}})")); } catch(...) {threw=true;} CHECK(threw);
    threw=false; try { Config::parse(Json::parse(R"({"offres":{}})")); } catch(...) {threw=true;} CHECK(threw);
    threw=false; try { auto bad=c; bad.energy["stamina"].interval=0; Economy invalid(bad); } catch(...) {threw=true;} CHECK(threw);
}
static void orders() {
    auto c=config(); c.offers["iap"].limit=1;
    Profile p; p.player="p"; Economy first(c,p);
    CHECK(first.reserveOrder("iap",context(),"order1"));
    CHECK(first.reserveOrder("iap",context(),"order1").code==Code::Duplicate);
    CHECK(first.reserveOrder("iap",context(),"order2").code==Code::Unavailable);
    auto saved=first.profile().save(); c.offers.erase("iap");
    Economy restored(c,Profile::load(saved,c));
    CHECK(restored.fulfillOrder("order1","provider:token"));
    CHECK(restored.profile().balances.at("gems")==10);
    CHECK(restored.fulfillOrder("order1","provider:token").code==Code::Duplicate);
    CHECK(restored.cancelOrder("order1").code==Code::Conflict);
    CHECK(restored.fulfillOrder("order1","provider:other").code==Code::Conflict);
    CHECK(restored.fulfillOrder("other","provider:token").code==Code::Conflict);
    Economy cancelled(config(),p); CHECK(cancelled.reserveOrder("iap",context(),"c"));
    CHECK(cancelled.cancelOrder("c")); CHECK(cancelled.cancelOrder("c").code==Code::Duplicate);
    CHECK(cancelled.fulfillOrder("c","provider:c").code==Code::Conflict);
    CHECK(cancelled.reserveOrder("iap",context(),"c").code==Code::Conflict);
    CHECK(cancelled.reserveOrder("iap",context(),"new"));
    Json database=p.save();
    auto cas=[&](int64_t expected,const Json& next) {
        if(Profile::load(database,config()).revision!=expected) return false;
        database=next; return true;
    };
    auto cfg=config(); cfg.offers["iap"].limit=1;
    Economy a(cfg,p,cas),b(cfg,p,cas);
    CHECK(a.reserveOrder("iap",context(),"a")); CHECK(b.reserveOrder("iap",context(),"b").code==Code::Conflict);
    Economy reloaded(cfg,Profile::load(database,cfg),cas); CHECK(reloaded.reserveOrder("iap",context(),"b").code==Code::Unavailable);
}
static void boards() {
    auto c=BoardConfig::parse(Json::parse(R"({"update":"sum","rank":"competition","season":{"period":100}})")); Leaderboard b(c);
    CHECK(b.submit("b",10,0,"b")); CHECK(b.submit("a",10,0,"a")); CHECK(b.submit("c",5,0,"c"));
    auto p=b.page(0); CHECK(p.total==3&&p.entries[0].player=="a"&&p.entries[1].rank==1&&p.entries[2].rank==3);
    CHECK(b.submit("b",10,0,"b").code==Code::Duplicate); CHECK(b.submit("b",11,0,"b").code==Code::Conflict);
    CHECK(b.submit("b",1,0,"b2")); CHECK(b.around(0,"b",0).entries[0].score==11);
    CHECK(b.page(0,0,10,{"a","c"}).entries[1].rank==2); CHECK(b.around(0,"c",0,{"a","c"}).entries[0].rank==2); CHECK(b.page(0,999).entries.empty()); CHECK(b.page(100).entries.empty());
    Leaderboard restored(c); CHECK(restored.restore(b.save())); CHECK(restored.save()==b.save()); CHECK(restored.submit("b",10,0,"b").code==Code::Duplicate);
    auto invalid=b.save(); invalid.set("revision",Json::number(int64_t(-1))); CHECK(!restored.restore(invalid)); CHECK(restored.save()==b.save());
    BoardConfig asc; asc.ascending=true; asc.rank=RankStyle::Dense; Leaderboard timed(asc); CHECK(timed.submit("p",9,0,"1")); CHECK(timed.submit("p",10,0,"2")); CHECK(timed.page(0).entries[0].score==9);
}
static void chat() {
    auto c=ChatConfig::parse(Json::parse(R"({"maxHistory":3,"cooldown":5,"retention":100})"));
    auto auth=[](const std::string& p,const std::string& action){return p!="outsider"&&(action!="delete"||p=="admin");};
    Chat ch(c,auth,[](const std::string&,const std::string& t){return t!="spam";});
    CHECK(ch.send("a","text","Привет",0,"1")); CHECK(ch.send("a","text","Привет",4,"1").code==Code::Duplicate);
    CHECK(ch.send("a","text","two",4,"2").code==Code::Limit); CHECK(ch.send("a","text","two",5,"2"));
    CHECK(ch.send("b","text","spam",5,"3").code==Code::Denied); CHECK(ch.send("b","text","hi",5,"3"));
    CHECK(ch.send("b","text",std::string("\xc0\xaf",2),10,"utf8").code==Code::Invalid);
    CHECK(ch.history("outsider",5).empty()); CHECK(ch.unread("reader",5)==3);
    CHECK(ch.markRead("reader",1)); CHECK(ch.unread("reader",5)==2); CHECK(!ch.markRead("reader",4));
    CHECK(ch.history("reader",5,3,1)[0].sequence==2);
    ch.block("reader","a"); CHECK(ch.history("reader",5).size()==1); CHECK(ch.unread("reader",5)==1);
    CHECK(ch.report("reader",3)); CHECK(ch.report("reader",3).code==Code::Duplicate);
    CHECK(ch.remove("a",3).code==Code::Denied); CHECK(ch.remove("admin",3)); CHECK(ch.unread("reader",5)==0);
    ch.mute("a",20); CHECK(ch.send("a","text","muted",10,"muted").code==Code::Denied);
    CHECK(ch.history("reader",105).empty()); Chat restored(c,auth); CHECK(restored.restore(ch.save())); CHECK(restored.save()==ch.save());
    CHECK(restored.send("a","text","Привет",20,"1").code==Code::Duplicate);
    CHECK(restored.send("a","text","ok",20,"4")); CHECK(restored.history("admin",20).size()==3);
    OfflineBackend backend; bool called=false; backend.request({"chat","send","op",Json::object()},[&](ServiceReply r){called=true; CHECK(r.result.code==Code::Unsupported);}); CHECK(!called); backend.pump(); CHECK(called);
}
static void policies() {
    auto c=context(); Condition cond; cond.minimum["missing"]=0; CHECK(!cond.matches(c));
    Schedule s{10,100,20,5}; CHECK(!s.active(9)&&s.active(10)&&s.active(14)&&!s.active(15)&&s.active(30)&&!s.active(100));
    CHECK(cohort("p","test",{0,100})==1); CHECK(cohort("p","test",{0,0})==2);
    auto base=Json::parse(R"({"x":1,"nested":{"a":1,"b":2}})"); Variant v; v.patch=Json::parse(R"({"nested":{"a":null,"b":3}})");
    auto settings=resolveSettings(base,c,{v}); CHECK(!settings["nested"].has("a")&&settings["nested"].getInt("b")==3&&settings.getInt("x")==1);
}
int main() { try { economy(); orders(); boards(); chat(); policies(); std::cout<<"meta tests passed\n"; } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;} }
