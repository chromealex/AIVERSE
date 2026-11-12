// This target deliberately lives outside engine/. It needs no Lua, GPU or RTS match.
#include "engine/meta/Meta.h"
#include "engine/meta/Social.h"
#include "engine/meta/Service.h"
#include "engine/meta/Client.h"
#include <iostream>
int main() {
    using namespace aiverse::meta;
    Config config; config.assets["gold"].cap=1000;
    config.offers["gift"].reward={{"gold",10}};
    // In the game host: read JSON using platform::readContentFile, then Config::parse.
    // Commit is optional only for a disposable offline demo; use atomic CAS storage in production.
    Profile profile; profile.player="demo";
    Economy meta(config,profile); Context context; context.player="demo";
    if(!meta.buy("gift",context,"first-gift")) return 1;
    Leaderboard board({}); if(!board.submit("demo",10,0,"match-1")) return 1;
    std::cout<<meta.profile().balances.at("gold")<<' '<<board.page(0).entries.front().rank<<'\n';
    ServiceConfig serviceConfig; serviceConfig.economy=config;
    serviceConfig.extensions=aiverse::Json::parse(R"({"liveops":{"offline":{"mine":{"maxAway":3600,"interval":10,"tiers":[{"gold":2}]}}}})");
    Service service(serviceConfig);
    auto reply=service.execute({"demo",false},{"profile","create","register",aiverse::Json::object()},0);
    if(!reply.result || service.view("demo",0).getStr("player")!="demo") return 2;
    auto mine=aiverse::Json::parse(R"({"id":"mine"})");
    if(!service.execute({"demo",true},{"offline","checkpoint","init-mine",mine},0).result) return 3;
    if(!service.execute({"demo",false},{"offline","claim","claim-mine",mine},10).result) return 4;
    if(service.view("demo",10)["profile"]["balances"].getInt("gold")!=2) return 5;
    RemoteBackend backend([&](const ServiceRequest& request,Backend::Callback callback) {
        callback(service.execute({"demo",false},request,10));
    });
    Client client(backend);
    if(!client.begin(requests::buy("gift","native-client-gift"))) return 6;
    client.pump();
    if(client.pending()||!client.reply().result) return 7;
    if(service.view("demo",10)["profile"]["balances"].getInt("gold")!=12) return 8;
    return 0;
}
