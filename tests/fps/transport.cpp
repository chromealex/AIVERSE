#include "engine/net/authority/Session.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
int main(int argc,char** argv) {
    try {
        std::string project=argc>1?argv[1]:"examples/fps/arena";
        auto rules=Json::parseFile(project+"/rules.json"),map=Json::parseFile(project+"/map.json");
        rules.member("fps").set("mode",Json::string("deathmatch"));
        for(bool websocket:{false,true}) {
            net::authority::Server server(rules,map);net::UdpListener udp;net::WsListener ws;
            if(!(websocket?ws.open(0):udp.open(0)))throw std::runtime_error("cannot bind loopback listener");
            auto connection=websocket?net::connectWebSocket("ws://127.0.0.1:"+std::to_string(ws.port())):net::connectUdp("127.0.0.1",udp.port());
            if(!connection)throw std::runtime_error("cannot connect");
            net::authority::Client client(rules,map,std::move(connection));
            if(!websocket)net::setUdpTestFaults({10,19});
            for(uint64_t now=1;now<15000;now+=5) {
                auto peers=websocket?ws.poll(now):udp.poll(now);for(auto& peer:peers)server.attach(1,std::move(peer));
                client.poll(now);server.poll(now);
                if(now%20==1){fps::Input input;input.right=now<3000?1000:0;client.submit(input);server.step();}
            }
            net::setUdpTestFaults({});
            if(!client.ready()||client.snapshot().ack<300||!client.connected())throw std::runtime_error("transport failed to deliver authority updates");
            std::printf("%s: authority tick=%u input ack=%u (UDP test loss 10%%)\n",websocket?"WebSocket":"UDP",client.snapshot().tick,client.snapshot().ack);
        }
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
