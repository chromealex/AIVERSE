#include "engine/net/authority/Session.h"
#include "modules/fps/Project.h"
#include <fstream>
#include <iterator>
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <charconv>

int main(int argc,char** argv) {
    try {
        std::string project="examples/fps/arena";uint16_t port=27100;uint32_t ticks=0;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--project"&&i+1<argc)project=argv[++i];
            else if((arg=="--port"||arg=="--ticks")&&i+1<argc) {
                std::string value=argv[++i];uint32_t n=0;auto [end,err]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(err!=std::errc{}||end!=value.data()+value.size()||(arg=="--port"&&(n==0||n>=65535)))throw std::runtime_error("invalid numeric argument");
                if(arg=="--port")port=uint16_t(n);else ticks=n;
            } else throw std::runtime_error("usage: aiverse_fps_server --project DIR [--port 27100] [--ticks N]");
        }
        auto data=aiverse::fps::readProject([&](const auto& file,std::string& text){std::ifstream input(project+"/"+file);if(!input)return false;text.assign(std::istreambuf_iterator<char>(input),{});return true;});
        auto rules=data.rules,map=data.map;
        aiverse::net::authority::Server server(rules,map);
        aiverse::net::UdpListener udp;aiverse::net::WsListener ws;
        if(!udp.open(port)||!ws.open(uint16_t(port+1)))throw std::runtime_error("cannot bind FPS listeners");
        std::printf("FPS authority: UDP %u, WebSocket %u; %u Hz; %zu slots\n",unsigned(port),unsigned(port+1),server.simulation().tickRate(),server.simulation().players().size());std::fflush(stdout);
        auto previous=aiverse::net::nowMs();double accumulated=0;const double step=1000.0/server.simulation().tickRate();
        while(!ticks||server.simulation().tick()<ticks) {
            auto now=aiverse::net::nowMs();accumulated+=double(now-previous);previous=now;
            auto admit=[&](auto connections) {for(auto& conn:connections) {
                uint8_t slot=0;for(size_t i=1;i<=server.simulation().players().size();++i)if(!server.occupied(uint8_t(i))){slot=uint8_t(i);break;}
                if(slot)server.attach(slot,std::move(conn));else conn->close();
            }};
            admit(udp.poll(now));admit(ws.poll(now));server.poll(now);
            int budget=8;while(accumulated>=step&&budget--&&(!ticks||server.simulation().tick()<ticks)){server.step();accumulated-=step;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("ticks=%u checksum=%llu rejected=%llu\n",server.simulation().tick(),(unsigned long long)server.simulation().checksum(),(unsigned long long)server.rejected());
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
