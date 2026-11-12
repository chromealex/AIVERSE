#include <aiverselib/net/LocalTickets.h>
#include "modules/fps/Project.h"
#include <fstream>
#include <iterator>
#include <thread>
#include <chrono>
#include "engine/net/authority/Session.h"
#include "engine/net/Udp.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::lib::session;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
void rejected(){
    for(int scenario=0;scenario<5;++scenario){
        auto pair=net::makeLoopbackPair();ClientAdmission client(std::move(pair.first),"test-only-token");AdmissionGate gate("match");CHECK(gate.enqueue(std::move(pair.second),0));
        for(uint64_t now=0;now<10;++now){client.update(now);auto peers=gate.poll(now,[&](const std::string&,const std::string&,uint64_t)->std::optional<Principal>{if(scenario==0)return {};return Principal{scenario==4?"":"alice",scenario==1?"another-match":"match",uint8_t(scenario==3?0:1),uint64_t(scenario==2?0:1000)};});CHECK(peers.empty());}
        CHECK(client.state()==net::ConnState::Closed&&gate.pending()==0);
    }
    AdmissionGate gate("match");std::vector<std::unique_ptr<net::Conn>> clients;
    for(int i=0;i<65;++i){auto pair=net::makeLoopbackPair();clients.push_back(std::move(pair.first));CHECK(gate.enqueue(std::move(pair.second),0)==(i<64));}
    CHECK(gate.pending()==64);CHECK(gate.poll(10000,{}).empty()&&gate.pending()==0);
    auto pair=net::makeLoopbackPair();ClientAdmission waiting(std::move(pair.first),"test-only-token");waiting.update(0);waiting.update(10000);CHECK(waiting.state()==net::ConnState::Closed);
    auto malformed=net::makeLoopbackPair();AdmissionGate strict("match");CHECK(strict.enqueue(std::move(malformed.second),0));malformed.first->send(net::Bytes(300,1));CHECK(strict.poll(1,{}).empty()&&strict.pending()==0);
}
void authority(bool udpMode){
    auto rules=Json::parse(R"({"fps":{"mode":"deathmatch","maxPlayers":2,"tickRate":60,"snapshotRate":20,"interestRadius":200}})");
    auto map=Json::parse(R"({"players":[{"name":"Alice","team":1},{"name":"Bob","team":2}],"spawns":[[0,1,0],[10,1,0]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");
    auto server=std::make_unique<net::authority::Server>(rules,map);AdmissionGate gate("match");net::UdpListener listener;
    if(udpMode){CHECK(listener.open(0));net::setUdpTestFaults({10,19});}
    std::unique_ptr<net::authority::Client> client;std::shared_ptr<Access> lease;
    auto connect=[&](uint64_t now){
        std::unique_ptr<net::Conn> connection;
        if(udpMode)connection=net::connectUdp("127.0.0.1",listener.port());
        else{auto pair=net::makeLoopbackPair({10,3,42});connection=std::move(pair.first);CHECK(gate.enqueue(std::move(pair.second),now));}
        CHECK(connection);client=std::make_unique<net::authority::Client>(rules,map,std::make_unique<ClientAdmission>(std::move(connection),"test-only-token"));
    };
    auto pump=[&](uint64_t begin,uint64_t end){
        for(uint64_t now=begin;now<end;now+=5){
            client->poll(now);
            if(udpMode)for(auto& connection:listener.poll(now))CHECK(gate.enqueue(std::move(connection),now));
            auto accepted=gate.poll(now,[&](const std::string& token,const std::string& audience,uint64_t at)->std::optional<Principal>{if(token!="test-only-token"||server->occupied(1))return {};return Principal{"alice",audience,1,at+20000};});
            for(auto& peer:accepted){lease=peer.access;CHECK(server->attach(peer.access->principal.slot,std::move(peer.connection)));}
            server->poll(now);
            if(now%20==0){fps::Input input;input.right=100;client->submit(input);server->step();}
        }
        CHECK(client->ready()&&client->connected()&&client->snapshot().localSlot==1);
        for(const auto& actor:client->snapshot().actors)if(actor.slot!=1)CHECK(actor.ammo==0&&actor.reserve==0&&actor.bag.empty());
    };
    connect(0);pump(0,3000);auto first=client->snapshot().ack;CHECK(first>40);
    client.reset();for(uint64_t now=3000;now<3200;now+=5){if(udpMode)listener.poll(now);server->poll(now);server->step();}
    CHECK(!server->occupied(1));connect(3200);pump(3200,6000);CHECK(client->snapshot().ack>first+40);
    client.reset();for(uint64_t now=6000;now<6200;now+=5){if(udpMode)listener.poll(now);server->poll(now);server->step();}
    auto saved=server->simulation().serialize();auto checksum=server->simulation().checksum();server=std::make_unique<net::authority::Server>(rules,map);std::string error;
    CHECK(server->simulation().deserialize(saved,&error)&&server->simulation().checksum()==checksum);
    connect(6200);pump(6200,9000);CHECK(client->snapshot().ack>first+80);
    CHECK(lease);lease->revoked=true;server->poll(9001);CHECK(!server->occupied(1));
    net::setUdpTestFaults({});
    std::cout<<(udpMode?"UDP with 10% outgoing test loss":"loopback with jitter")<<": token admission, slot ownership, reconnect, server restore, private fields and revocation OK\n";
}
void remote(const std::string& project,const std::string& address,const std::string& tokenFile,bool expectRevoke=false){
    auto data=fps::readProject([&](const auto& name,std::string& text){std::ifstream file(project+"/"+name);if(!file)return false;text.assign(std::istreambuf_iterator<char>(file),{});return true;});
    std::ifstream file(tokenFile);std::string token;std::getline(file,token);CHECK(file&&aiverse::lib::session::detail::tokenValid(token));
    auto connection=net::connectTo(address);CHECK(connection);
    net::authority::Client client(data.rules,data.map,std::make_unique<ClientAdmission>(std::move(connection),token));
    auto started=net::nowMs();bool ready=false;
    while(net::nowMs()-started<4000){auto now=net::nowMs();client.poll(now);if(client.ready()){ready=true;fps::Input input;input.right=50;client.submit(input);}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(ready&&client.snapshot().localSlot==1&&client.snapshot().ack>20);
    CHECK(expectRevoke?!client.connected():client.connected());
    std::cout<<"Local profile ticket: authority slot 1, acknowledged inputs OK\n";
}
void localTickets(){
    LocalTickets table;auto valid=Json::parse(R"({"version":1,"tickets":[{"token":"test-token","player":"alice","audience":"match","slot":1,"expires":1000}]})");
    CHECK(table.read(valid));auto p=table.verify("test-token","match",900,50);CHECK(p&&p->slot==1&&p->expiresAt==100050);
    CHECK(!table.verify("test-token","other",900,50));CHECK(!table.verify("test-token","match",1000,50));CHECK(!table.verify("test-token","match",1,50));
    CHECK(!table.read(Json::parse(R"({"version":1.5,"tickets":[]})")));CHECK(table.verify("test-token","match",900,50));
    CHECK(!table.read(Json::parse(R"({"version":1,"tickets":[{"token":"x","player":"alice","audience":"match","slot":1.5,"expires":1000}]})")));
    table.clear();CHECK(!table.verify("test-token","match",900,50));
}
int main(int argc,char** argv){try{localTickets();if(argc==4||argc==5){remote(argv[1],argv[2],argv[3],argc==5&&std::string(argv[4])=="--expect-revoke");return 0;}rejected();authority(argc==2&&std::string(argv[1])=="--udp");}catch(const std::exception& e){net::setUdpTestFaults({});std::cerr<<e.what()<<'\n';return 1;}}
