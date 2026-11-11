#include "engine/meta/LiveOps.h"
#include "engine/meta/Rating.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::meta;
void check(bool v,const char* why) {if(!v)throw std::runtime_error(why);}
Json j(const char* s){return Json::parse(s);}
struct Fixture {
    Service service; int serial=0; Time now=1;
    explicit Fixture(const ServiceConfig& c):service(c,{}, {}, [](int64_t){return int64_t(1);}) {
        for(auto p:{"a","b","c","d"})ok(p,"profile","create","{}");
    }
    ServiceReply call(const char* p,const char* s,const char* action,const Json& args,bool host=false,const std::string& op="") {
        return service.execute({p,host},{s,action,op.empty()?std::to_string(++serial):op,args},now);
    }
    Json ok(const char* p,const char* s,const char* action,const char* args,bool host=false) {
        auto r=call(p,s,action,j(args),host);if(!r.result)throw std::runtime_error(std::string(s)+"/"+action+": "+r.result.detail);return r.data;
    }
    void fail(const char* p,const char* s,const char* action,const char* args,bool host=false) {
        auto before=service.save().dump(-1);auto r=call(p,s,action,j(args),host);check(!r.result,"expected rejection");check(before==service.save().dump(-1),"rejection mutated state");
    }
    int64_t balance(const char* p,const char* asset) {return service.save()["profiles"][p]["balances"].getInt(asset);}
};
void rewards(const ServiceConfig& cfg) {
    Fixture f(cfg);
    auto args=j(R"({"id":"chest"})");auto r=f.call("a","loot","open",args,false,"open");check(bool(r.result),"loot open");
    auto before=f.service.save().dump(-1);auto dup=f.call("a","loot","open",args,false,"open");check(dup.result.code==Code::Duplicate&&dup.data.dump(-1)==r.data.dump(-1)&&before==f.service.save().dump(-1),"loot retry");
    f.ok("a","loot","open",R"({"id":"chest"})");f.ok("a","loot","open",R"({"id":"chest"})");check(f.balance("a","sword")==1,"pity");
    for(int i=0;i<3;++i)f.ok("a","loot","open",R"({"id":"chest"})");check(f.balance("a","gems")==22,"duplicate conversion");
    f.ok("a","albums","claim",R"({"id":"launch"})");f.fail("a","albums","claim",R"({"id":"launch"})");
    f.ok("a","albums","offer",R"({"id":"launch","trade":"t","target":"b","give":{"gold":10},"want":{"gems":1}})");
    f.fail("c","albums","accept",R"({"id":"launch","trade":"t"})");auto a=f.balance("a","gold"),b=f.balance("b","gold");
    f.ok("b","albums","accept",R"({"id":"launch","trade":"t"})");check(f.balance("a","gold")==a-10&&f.balance("b","gold")==b+10,"trade settlement");
    f.ok("a","albums","offer",R"({"id":"launch","trade":"bad","target":"b","give":{"gold":10},"want":{"gems":1000}})");f.fail("b","albums","accept",R"({"id":"launch","trade":"bad"})");
    f.fail("a","research","unlock",R"({"id":"tech","node":"child"})");f.ok("a","research","unlock",R"({"id":"tech","node":"root"})");f.ok("a","research","unlock",R"({"id":"tech","node":"child"})");f.ok("a","research","prestige",R"({"id":"tech"})");f.fail("a","research","prestige",R"({"id":"tech"})");
    f.ok("a","offline","checkpoint",R"({"id":"mine"})",true);a=f.balance("a","gold");f.now=26;f.ok("a","offline","claim",R"({"id":"mine"})");check(f.balance("a","gold")==a+4,"offline ticks");f.ok("a","offline","claim",R"({"id":"mine"})");check(f.balance("a","gold")==a+4,"offline double claim");
    f.ok("a","offline","tier",R"({"id":"mine","tier":1})",true);f.now=1026;f.ok("a","offline","claim",R"({"id":"mine"})");check(f.balance("a","gold")==a+54,"offline cap and tier");
}
void activities(const ServiceConfig& cfg) {
    Fixture f(cfg);
    auto t1=f.ok("a","jobs","start",R"({"id":"forge","task":"one"})");auto t2=f.ok("a","jobs","start",R"({"id":"forge","task":"two"})");auto t3=f.ok("a","jobs","start",R"({"id":"forge","task":"three"})");
    check(t1.getInt("finish")==11&&t2.getInt("finish")==11&&t3.getInt("start")==11,"queue slots");f.fail("a","jobs","claim",R"({"id":"forge","task":"one"})");
    auto gold=f.balance("a","gold");f.ok("a","jobs","cancel",R"({"id":"forge","task":"three"})");check(f.balance("a","gold")==gold+5,"cancel refund");
    f.ok("a","jobs","speed",R"({"id":"forge","task":"one","seconds":5})");f.now=6;f.ok("a","jobs","claim",R"({"id":"forge","task":"one"})");f.fail("a","jobs","claim",R"({"id":"forge","task":"one"})");
    f.ok("a","quests","activate",R"({"id":"daily"})");f.ok("a","quests","reroll",R"({"id":"daily"})");f.fail("a","quests","reroll",R"({"id":"daily"})");
    f.fail("a","quests","event",R"({"id":"daily","metric":"wins","amount":2})");f.ok("a","quests","event",R"({"id":"daily","metric":"wins","amount":2})",true);f.fail("a","quests","claim",R"({"id":"daily"})");f.ok("a","quests","event",R"({"id":"daily","metric":"builds","amount":2})",true);f.ok("a","quests","claim",R"({"id":"daily"})");f.fail("a","quests","claim",R"({"id":"daily"})");
    f.ok("a","events","join",R"({"id":"festival"})");f.fail("a","events","claim",R"({"id":"festival","phase":"first"})");f.now=50;f.ok("a","events","claim",R"({"id":"festival","phase":"first"})");f.now=100;f.ok("a","events","claim",R"({"id":"festival","phase":"final"})");f.fail("a","events","claim",R"({"id":"festival","phase":"final"})");
    f.ok("a","quests","activate",R"({"id":"daily"})");check(f.service.view("a",100)["liveops"]["quests"]["daily"].getInt("step")==0,"daily reset");
    f.ok("a","notifications","schedule",R"({"id":"gift"})",true);check(f.service.view("a",104)["liveops"]["notices"].size()==0,"notice delay");check(f.service.view("a",115)["liveops"]["badges"].getInt("rewards")==2,"notice repeats");f.now=115;f.ok("a","notifications","ack",R"({"id":"gift"})");check(f.service.view("a",115)["liveops"]["notices"].size()==0,"notice ack");
}
void retention(const ServiceConfig& cfg) {
    Fixture f(cfg);f.ok("a","referrals","code",R"({"id":"friends","code":"WELCOME"})");f.fail("a","referrals","code",R"({"id":"friends","code":"WELCOME"})");
    f.ok("b","referrals","bind",R"({"id":"friends","inviter":"a"})");f.fail("a","referrals","bind",R"({"id":"friends","inviter":"b"})");f.fail("b","referrals","claim",R"({"id":"friends","milestone":"win"})");f.ok("b","economy","event",R"({"metric":"wins","amount":1})",true);f.ok("b","referrals","claim",R"({"id":"friends","milestone":"win"})");check(f.balance("a","gems")==22&&f.balance("b","gems")==21,"referral both rewards");
    auto ticket=f.ok("a","ads","begin",R"({"id":"bonus"})").getStr("ticket");auto args=j(R"({"id":"bonus","token":"provider1"})");args.set("ticket",Json::string(ticket));check(f.call("a","ads","complete",args).result.code==Code::Denied,"ad trusted callback");check(bool(f.call("a","ads","complete",args,true).result),"ad complete");check(!f.call("a","ads","complete",args,true).result,"ad duplicate callback");f.fail("a","ads","begin",R"({"id":"bonus"})");f.now=6;ticket=f.ok("a","ads","begin",R"({"id":"bonus"})").getStr("ticket");args.set("ticket",Json::string(ticket));check(!f.call("a","ads","complete",args,true).result,"ad global token reuse");f.now=16;args.set("token",Json::string("provider2"));check(!f.call("a","ads","complete",args,true).result,"expired ad");
    f.ok("a","chains","show",R"({"id":"welcome"})");f.ok("a","chains","buy",R"({"id":"welcome"})");f.fail("a","chains","show",R"({"id":"welcome"})");f.now=21;f.ok("a","chains","show",R"({"id":"welcome"})");f.ok("a","chains","dismiss",R"({"id":"welcome"})");f.fail("a","chains","show",R"({"id":"welcome"})");
}
void competition(const ServiceConfig& cfg) {
    Fixture f(cfg);f.ok("a","clan","create",R"({"id":"red"})");f.ok("b","clan","create",R"({"id":"blue"})");
    f.ok("a","wars","enroll",R"({"id":"launch"})");f.ok("b","wars","enroll",R"({"id":"launch"})");f.ok("a","wars","pair",R"({"id":"launch"})",true);f.fail("a","wars","score",R"({"id":"launch","amount":2})");f.ok("a","wars","score",R"({"id":"launch","amount":2})",true);f.ok("b","wars","score",R"({"id":"launch","amount":1})",true);f.fail("a","wars","settle",R"({"id":"launch"})",true);
    f.ok("a","asyncPvp","publish",R"({"id":"raid","snapshot":{"level":1}})",true);f.ok("b","asyncPvp","publish",R"({"id":"raid","snapshot":{"level":2}})",true);auto attack=f.ok("a","asyncPvp","begin",R"({"id":"raid","target":"b","attack":"raid1"})");check(attack["snapshot"].getInt("level")==2,"defense snapshot");f.fail("c","asyncPvp","begin",R"({"id":"raid","target":"b","attack":"raid2"})");f.fail("a","asyncPvp","complete",R"({"id":"raid","attack":"raid1","win":true,"event":"battle1"})");f.ok("a","asyncPvp","complete",R"({"id":"raid","attack":"raid1","win":true,"event":"battle1"})",true);f.fail("a","asyncPvp","begin",R"({"id":"raid","target":"b","attack":"raid3"})");f.ok("b","asyncPvp","revenge",R"({"id":"raid","target":"a","attack":"revenge"})");
    f.ok("a","matchmaking","join",R"({"id":"duel","ticket":"a","region":"eu"})");f.ok("b","matchmaking","join",R"({"id":"duel","ticket":"b","region":"eu"})");f.fail("a","matchmaking","assemble",R"({"id":"duel","match":"m"})");auto match=f.ok("a","matchmaking","assemble",R"({"id":"duel","match":"m"})",true);check(match["teams"].size()==2&&match["teams"][0].size()==1,"assembled match");f.ok("a","matchmaking","leave",R"({"id":"duel","match":"m"})");f.fail("a","matchmaking","join",R"({"id":"duel","ticket":"again","region":"eu"})");f.ok("c","matchmaking","join",R"({"id":"duel","ticket":"c","region":"eu"})");f.ok("b","matchmaking","backfill",R"({"id":"duel","match":"m"})",true);f.ok("b","matchmaking","finish",R"({"id":"duel","match":"m"})",true);
    auto ratingArgs=R"({"id":"ranked","period":1,"matches":[{"id":"match1","a":["a"],"b":["b"],"score":1000}]})";f.fail("a","ratings","period",ratingArgs);auto ratings=f.ok("a","ratings","period",ratingArgs,true);check(ratings["a"].getInt("milli")>1500000&&ratings["b"].getInt("milli")<1500000,"rating outcome");f.fail("a","ratings","period",ratingArgs,true);f.fail("a","ratings","period",R"({"id":"ranked","period":2,"matches":[{"id":"match1","a":["a"],"b":["b"],"score":1000}]})",true);f.ok("a","ratings","period",R"({"id":"ranked","period":2,"matches":[],"idle":["a"]})",true);
    f.now=100;f.ok("a","wars","settle",R"({"id":"launch"})",true);check(f.service.view("a",100)["liveops"]["wars"]["launch"].getInt("outcome")==1,"war winner");check(f.service.save()["profiles"]["a"]["mail"].size()==1,"war mail");f.fail("a","wars","settle",R"({"id":"launch"})",true);
}
void boundaries(Json source) {
    auto& live=source.member("extensions").member("liveops");
    live.member("loot").set("outer",j(R"({"cost":{"gold":1},"rolls":1,"pity":0,"entries":[{"id":"nested","weight":1,"rare":false,"table":"chest"}]})"));
    auto cfg=ServiceConfig::parse(source);Fixture f(cfg);auto amount=f.balance("a","gold");auto nested=f.ok("a","loot","open",R"({"id":"outer"})");check(nested["draws"].size()==2&&f.balance("a","gold")==amount,"nested loot root cost only");
    Service noRng(cfg,f.service.save());check(noRng.execute({"a",false},{"loot","open","disabled",j(R"({"id":"outer"})")},1).result.code==Code::Unavailable,"RNG required");
    Service badRng(cfg,f.service.save(),{},[](int64_t){return int64_t(0);});auto saved=badRng.save().dump(-1);check(!badRng.execute({"a",false},{"loot","open","invalid",j(R"({"id":"outer"})")},1).result&&badRng.save().dump(-1)==saved,"invalid draw rollback");
    live.member("matchmaking").member("duel").set("teamSize",Json::number(int64_t(2)));Fixture parties(ServiceConfig::parse(source));
    parties.fail("a","matchmaking","join",R"({"id":"duel","ticket":"ab","region":"eu","players":["a","b"]})");
    parties.ok("a","matchmaking","join",R"({"id":"duel","ticket":"ab","region":"eu","players":["a","b"]})",true);
    parties.ok("c","matchmaking","join",R"({"id":"duel","ticket":"cd","region":"us","players":["c","d"]})",true);
    parties.fail("a","matchmaking","assemble",R"({"id":"duel","match":"party"})",true);parties.ok("c","matchmaking","cancel",R"({"id":"duel","ticket":"cd"})");
    parties.ok("c","matchmaking","join",R"({"id":"duel","ticket":"cd2","region":"eu","players":["c","d"]})",true);
    auto match=parties.ok("a","matchmaking","assemble",R"({"id":"duel","match":"party"})",true);check(match["teams"][0][0].str()=="a"&&match["teams"][0][1].str()=="b"&&match["teams"][1].size()==2,"parties remain intact");
    live.member("ratings").member("ranked").set("method",Json::string("elo"));live.member("matchmaking").member("duel").set("teamSize",Json::number(int64_t(1)));live.member("matchmaking").member("duel").set("initialRange",Json::number(int64_t(0)));Fixture widening(ServiceConfig::parse(source));
    widening.ok("a","ratings","period",R"({"id":"ranked","period":1,"matches":[{"id":"elo1","a":["a"],"b":["b"],"score":1000}]})",true);
    widening.ok("a","matchmaking","join",R"({"id":"duel","ticket":"a","region":"eu"})");widening.ok("b","matchmaking","join",R"({"id":"duel","ticket":"b","region":"eu"})");widening.fail("a","matchmaking","assemble",R"({"id":"duel","match":"wide"})",true);widening.now=14;widening.ok("a","matchmaking","assemble",R"({"id":"duel","match":"wide"})",true);
    Fixture event(cfg);event.ok("a","events","join",R"({"id":"festival"})");event.now=150;event.fail("a","events","claim",R"({"id":"festival","phase":"final"})");
    auto old=f.service.save();old.erase("live");Service migrated(cfg,old);check(migrated.save()["live"].isObject(),"old snapshot migration");
}

int main(int argc,char** argv) {
    try {
        check(argc==3,"service and liveops paths required");auto source=Json::parseFile(argv[1]);source.member("extensions").set("liveops",Json::parseFile(argv[2]));source.member("extensions").member("liveops").member("quests").member("daily").set("period",Json::number(int64_t(100)));auto cfg=ServiceConfig::parse(source);
        rewards(cfg);activities(cfg);retention(cfg);competition(cfg);boundaries(source);
        const RatedGame games[]={{1400,30,1},{1550,100,0},{1700,300,0}};auto rating=glicko2({1500,200,.06},games,.5);
        check(std::abs(rating.value-1464.06)<.01&&std::abs(rating.deviation-151.52)<.01&&std::abs(rating.volatility-.059996)<.000001,"published Glicko2 example");check(elo(1500,1500,1,32)==1516,"Elo expected win");
        Fixture f(cfg);auto snapshot=f.service.save();Service rejected(cfg,snapshot,[](uint64_t,const Json&){return false;},[](int64_t){return int64_t(1);});auto r=rejected.execute({"a",false},{"loot","open","cas",j(R"({"id":"chest"})")},2);check(r.result.code==Code::Conflict&&rejected.save().dump(-1)==snapshot.dump(-1),"CAS rollback");
        Service restored(cfg,snapshot,{},[](int64_t){return int64_t(1);});check(restored.view("a",2).dump(-1)==f.service.view("a",2).dump(-1),"restore");
        auto bad=source;bad.member("extensions").member("liveops").member("research").member("tech").member("nodes").member("root").set("requires",j(R"(["child"])") );bool threw=false;try{(void)ServiceConfig::parse(bad);}catch(const std::exception&){threw=true;}check(threw,"research cycle rejected");
        bad=source;bad.member("extensions").member("liveops").member("loot").member("chest").set("cost",j(R"({"unknown":1})"));threw=false;try{(void)ServiceConfig::parse(bad);}catch(const std::exception&){threw=true;}check(threw,"unknown asset rejected");
        std::cout<<"All 15 LiveOps groups, atomicity, restore and rating reference passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
