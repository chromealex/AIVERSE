#include "engine/meta/Client.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::meta;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
 auto config=ServiceConfig::parse(Json::parse(R"({"economy":{"version":1,"assets":{"gold":{"cap":1000},"sword":{"cap":10}},"offers":{"sword":{"cost":{"gold":10},"reward":{"sword":1}}}},"extensions":{"initial":{"gold":100}}})"));
 Service service(config);CHECK(service.execute({"alice",false},{"profile","create","create",Json::object()},0).result);
 int calls=0;bool lose=true;Json disk=Json::object();bool writable=true;
 RemoteBackend backend([&](const ServiceRequest& r,Backend::Callback callback){++calls;auto reply=service.execute({"alice",false},r,1);if(lose){lose=false;reply=transportFailure("lost reply");}callback(reply);});
 auto persist=[&](const Json& out){if(!writable)return false;disk=out;return true;};
 {Client client(backend,persist);writable=false;CHECK(!client.begin(requests::buy("sword","buy-1"))&&calls==0);writable=true;
  CHECK(client.begin(requests::buy("sword","buy-1")));CHECK(!client.begin(requests::buy("sword","buy-2")));CHECK(!client.retry());client.pump();CHECK(client.pending()&&!client.inFlight());CHECK(disk.getStr("operation")=="buy-1");}
 {Client restored(backend,persist,disk);CHECK(restored.retry());restored.pump();CHECK(!restored.pending()&&restored.reply().result.code==Code::Duplicate);CHECK(disk.size()==0);}
 CHECK(service.view("alice",1)["profile"]["balances"].getInt("gold")==90);
 CHECK(service.view("alice",1)["profile"]["balances"].getInt("sword")==1);
 {Client client(backend,persist);CHECK(client.begin(requests::buy("sword","buy-2")));writable=false;client.pump();CHECK(client.pending());writable=true;CHECK(client.retry());client.pump();CHECK(client.reply().result.code==Code::Duplicate&&!client.pending());}
 RemoteBackend closedEvent([](const ServiceRequest&,Backend::Callback cb){cb({{Code::Unavailable,"event ended"},Json::object()});});
 Client terminal(closedEvent);CHECK(terminal.begin(requests::view()));terminal.pump();CHECK(!terminal.pending()&&!terminal.retry());
 // A delayed callback after destroying its consumer must not access the dead client.
 {Client temporary(backend);CHECK(temporary.begin(requests::view()));}backend.pump();
 Backend::Callback old;RemoteBackend duplicate([&](const ServiceRequest&,Backend::Callback cb){old=cb;cb({{},Json::object()});});
 Client client(duplicate);CHECK(client.begin(requests::view()));client.pump();old(transportFailure("late"));client.pump();CHECK(!client.pending());
 CHECK(service.view("alice",1)["profile"]["balances"].getInt("gold")==80);
 std::cout<<"Meta client: persisted outbox, restart/retry, duplicate replies and lifetime OK\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
