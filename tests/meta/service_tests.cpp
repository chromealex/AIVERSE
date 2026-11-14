#include "engine/meta/Service.h"
#include "engine/meta/Modules.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse; using namespace aiverse::meta;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(false)
int main(int argc,char** argv) {try {
    CHECK(argc==2); auto json=Json::parseFile(argv[1]);
    json.member("extensions").member("tournaments").member("launch").set("end",Json::number(int64_t(100)));
    auto cfg=ServiceConfig::parse(json); Service s(cfg); int sequence=0; Time now=1;
    auto call=[&](std::string p,std::string service,std::string action,std::string args="{}",bool admin=false,std::string op="") {
        return s.execute({p,admin},{service,action,op.empty()?std::to_string(++sequence):op,Json::parse(args)},now);
    };
    CHECK(call("a","profile","create").result); CHECK(call("b","profile","create").result);
    auto freshView=s.view("a",now); bool starterVisible=false; for(const auto& offer:freshView["offers"].items()) if(offer.str()=="starter") starterVisible=true;
    CHECK(starterVisible);
    CHECK(call("a","economy","grant",R"({"reward":{"gems":100}})").result.code==Code::Denied);
    CHECK(call("a","friends","request",R"({"target":"b"})").result);
    CHECK(call("b","friends","accept",R"({"target":"a"})").result); CHECK(s.view("a",now)["friends"].has("b"));
    CHECK(call("b","friends","block",R"({"target":"a"})").result); CHECK(!s.view("a",now)["friends"].has("b"));
    CHECK(call("a","friends","request",R"({"target":"b"})").result.code==Code::Denied);
    CHECK(call("b","friends","unblock",R"({"target":"a"})").result);
    CHECK(call("a","economy","buy",R"({"id":"sword"})").result);
    CHECK(call("a","items","redeem",R"({"id":"shop-item","type":"sword"})",false,"redeem").result);
    CHECK(call("a","items","redeem",R"({"id":"shop-item","type":"sword"})",false,"redeem").result.code==Code::Duplicate);
    CHECK(call("a","items","redeem",R"({"id":"shop-item2","type":"sword"})").result.code==Code::Insufficient);
    CHECK(call("a","items","salvage",R"({"id":"shop-item"})").result);
    CHECK(call("a","economy","grant",R"({"reward":{"gold":500}})",true).result);
    CHECK(call("a","clan","create",R"({"id":"crew"})").result);
    CHECK(call("b","clan","join",R"({"id":"crew"})").result.code==Code::Denied);
    CHECK(call("a","clan","invite",R"({"id":"crew","target":"b"})").result);
    CHECK(call("b","clan","join",R"({"id":"crew"})").result);
    CHECK(call("a","clan","leave",R"({"id":"crew"})").result.code==Code::Conflict);
    CHECK(call("b","clan","role",R"({"id":"crew","target":"a","role":"member"})").result.code==Code::Denied);
    auto before=s.save(); CHECK(call("a","clan","donate",R"({"id":"crew","asset":"gold","amount":999999})").result.code==Code::Insufficient); CHECK(s.save()==before);
    CHECK(call("a","clan","donate",R"({"id":"crew","asset":"gold","amount":200})",false,"donate").result);
    CHECK(call("a","clan","donate",R"({"amount":200,"id":"crew","asset":"gold"})",false,"donate").result.code==Code::Duplicate);
    CHECK(call("a","clan","goal",R"({"id":"crew","goal":"founders"})").result);
    CHECK(call("a","clan","goal",R"({"id":"crew","goal":"founders"})").result.code==Code::Duplicate);
    CHECK(call("a","items","grant",R"({"id":"one","type":"sword"})").result.code==Code::Denied);
    for(auto uid:{"one","two","three"}) CHECK(call("a","items","grant",std::string("{\"id\":\"")+uid+"\",\"type\":\"sword\"}",true).result);
    CHECK(call("a","items","equip",R"({"id":"one","slot":"hand"})").result);
    CHECK(call("a","items","salvage",R"({"id":"one"})").result.code==Code::Denied);
    CHECK(call("a","items","unequip",R"({"id":"one"})").result);
    CHECK(call("a","items","merge",R"({"id":"one","inputs":["two","two"]})").result.code==Code::Invalid);
    CHECK(call("a","items","merge",R"({"id":"one","inputs":["two","three"]})").result);
    CHECK(s.view("a",now)["extras"]["items"].size()==1);
    CHECK(call("a","items","upgrade",R"({"id":"one"})").result);
    CHECK(call("a","entitlement","grant",R"({"id":"premium","event":"pay1","sequence":1,"start":1,"end":50})",true).result);
    CHECK(s.view("a",1)["tags"].size()==1); CHECK(s.view("a",50)["tags"].size()==0);
    CHECK(call("b","entitlement","grant",R"({"id":"premium","event":"pay1","sequence":1,"start":1,"end":50})",true).result.code==Code::Conflict);
    CHECK(call("a","entitlement","revoke",R"({"id":"premium","event":"refund","sequence":2,"start":1,"end":50})",true).result);
    CHECK(s.view("a",1)["tags"].size()==0);
    CHECK(call("a","entitlement","grant",R"({"id":"premium","event":"old","sequence":1,"start":1,"end":50})",true).result.code==Code::Conflict);
    CHECK(call("a","board","submit",R"({"id":"global","score":100})").result.code==Code::Denied);
    CHECK(call("a","board","submit",R"({"id":"global","score":100})",true).result);
    CHECK(call("a","chat","send",R"({"id":"global","text":"hello"})").result);
    CHECK(s.view("b",now)["chats"]["global"].size()==1);
    CHECK(call("b","chat","delete",R"({"id":"global","sequence":1})").result.code==Code::Denied);
    CHECK(call("a","tournament","join",R"({"id":"launch"})").result); CHECK(call("b","tournament","join",R"({"id":"launch"})").result);
    CHECK(call("a","tournament","submit",R"({"id":"launch","score":10})",true).result);
    CHECK(call("a","tournament","settle",R"({"id":"launch"})",true).result.code==Code::Unavailable);
    now=100; CHECK(call("a","tournament","settle",R"({"id":"launch"})",true).result);
    CHECK(s.view("a",now)["profile"]["mail"].size()>0);
    CHECK(call("a","tournament","settle",R"({"id":"launch"})",true).result.code==Code::Duplicate);
    Service restored(cfg,Json::parse(s.save().dump())); CHECK(restored.save()==s.save()); CHECK(restored.view("a",now)==s.view("a",now));
    Service failed(cfg,s.save(),[](int64_t,const Json&){return false;}); before=failed.save();
    CHECK(failed.execute({"a",false},{"items","salvage","cas",Json::parse(R"({"id":"one"})")},now).result.code==Code::Conflict); CHECK(failed.save()==before);
    CHECK(s.execute({"a",false},{"items","salvage","clock",Json::parse(R"({"id":"one"})")},0).result.code==Code::Conflict);
    bool called=false; RemoteBackend remote([](const ServiceRequest&,Backend::Callback done){done({{},Json::object()});});
    remote.request({"meta","view","",Json::object()},[&](ServiceReply r){called=bool(r.result);}); CHECK(!called);remote.pump();CHECK(called);
    // An ascending best-score tournament must not seed a winning synthetic zero.
    auto timedJson=json; timedJson.member("extensions").member("tournaments").member("launch").member("board").set("ascending",Json::fromBool(true));
    Service timed(ServiceConfig::parse(timedJson));
    auto timedCall=[&](const std::string& who,const std::string& service,const std::string& action,const std::string& args,Time at,bool authority=false) {
        return timed.execute({who,authority},{service,action,std::to_string(++sequence),Json::parse(args)},at);
    };
    CHECK(timedCall("a","profile","create","{}",1).result); CHECK(timedCall("b","profile","create","{}",1).result);
    CHECK(timedCall("a","tournament","join",R"({"id":"launch"})",1).result);
    CHECK(timedCall("b","tournament","join",R"({"id":"launch"})",1).result);
    CHECK(timedCall("a","tournament","submit",R"({"id":"launch","score":20})",2,true).result);
    CHECK(timedCall("a","tournament","submit",R"({"id":"launch","score":25})",3,true).result);
    CHECK(timedCall("a","tournament","settle",R"({"id":"launch"})",100,true).result);
    CHECK(timed.view("a",100)["tournaments"]["launch"]["result"].getInt("score")==20);
    CHECK(timed.view("b",100)["tournaments"]["launch"]["result"].isNull());
    Backend::Callback late;
    { RemoteBackend scoped([&](const ServiceRequest&, Backend::Callback done){late=std::move(done);});
      scoped.request({"meta","view","",Json::object()},[](ServiceReply){}); }
    late({{},Json::object()}); // A transport may finish after its UI owner was destroyed.
    { // Persistent-world accounts: only the world server writes, the latest save wins, absence is counted.
        auto m=modules::all(); modules::world(m); Service w(m,cfg); int n=0; Time at=1000;
        auto run=[&](std::string p,std::string action,std::string args="{}",bool admin=true){return w.execute({p,admin},{"world",action,"w"+std::to_string(++n),Json::parse(args)},at);};
        CHECK(w.execute({"a",true},{"profile","create","c1",Json::object()},at).result); CHECK(w.execute({"b",true},{"profile","create","c2",Json::object()},at).result);
        CHECK(run("a","enter","{}",false).result.code==Code::Denied);
        auto first=run("a","enter"); CHECK(first.result&&!first.data.getBool("known")&&first.data.getInt("away")==0);
        CHECK(run("a","save",R"({"account":{"wallet":5}})").result); at=1060; CHECK(run("a","save",R"({"account":{"wallet":7}})").result);
        CHECK(run("a","save",R"({"account":7})").result.code==Code::Invalid);
        at=1100; CHECK(run("a","leave",R"({"account":{"wallet":9}})").result);
        size_t receipts=0; for(auto&[k,v]:w.save()["receipts"].members()){(void)v;if(k.rfind("[\"a\",",0)==0)++receipts;} CHECK(receipts<=2); // create + the last world call
        at=2000; auto away=run("b","absent",R"({"seconds":600})"); CHECK(away.result&&away.data["players"].size()==1&&away.data["players"][size_t(0)].str()=="a");
        CHECK(run("b","absent",R"({"seconds":5000})").data["players"].size()==0);
        auto back=run("a","enter"); CHECK(back.result&&back.data.getBool("known")&&back.data["account"].getInt("wallet")==9&&back.data.getInt("away")==900);
        CHECK(run("b","absent",R"({"seconds":0})").data["players"].size()==0); // he is online again
        Service again(m,cfg,Json::parse(w.save().dump())); CHECK(again.save()==w.save());
    }
    std::cout<<"Extended meta service tests passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
