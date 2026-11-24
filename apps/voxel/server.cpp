// aiverse_voxel_server: dedicated authoritative server of the voxel sandbox.
//   aiverse_voxel_server [--project DIR] [--port 27200] [--ticks N] [--loopback]
// UDP on --port, WebSocket on --port + 1; one slot per map player; prints the checksum when --ticks ends the run.
#include "engine/platform/TemplatesDir.h"
#include "engine/voxel/VoxelSession.h"
#include "engine/voxel/Project.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
int main(int argc,char** argv){
    using namespace aiverse;
    try{
        std::string project=aiverse::platform::templatesDir(".")+"/voxel_sandbox";uint16_t port=27200;uint32_t ticks=0;bool loopback=false;
        for(int i=1;i<argc;++i){std::string a=argv[i];
            if(a=="--project"&&i+1<argc)project=argv[++i];else if(a=="--port"&&i+1<argc)port=uint16_t(std::stoi(argv[++i]));
            else if(a=="--ticks"&&i+1<argc)ticks=uint32_t(std::stoul(argv[++i]));else if(a=="--loopback")loopback=true;
            else throw std::runtime_error("usage: aiverse_voxel_server [--project DIR] [--port 27200] [--ticks N] [--loopback]");}
        auto data=voxelgame::readProject([&](const auto& file,std::string& text){std::ifstream in(project+"/"+file);if(!in)return false;text.assign(std::istreambuf_iterator<char>(in),{});return true;});
        voxelgame::Server server(data.rules,data.map,1);net::UdpListener udp;net::WsListener ws;
        if(!udp.open(port,loopback)||!ws.open(uint16_t(port+1),loopback))throw std::runtime_error("cannot bind voxel listeners");
        const auto& cfg=server.module().config();
        std::printf("Voxel authority: UDP %u, WebSocket %u; %u Hz; %u slots\n",unsigned(port),unsigned(port+1),cfg.tickRate,cfg.maxPlayers);std::fflush(stdout);
        auto previous=net::nowMs();double accumulated=0;const double step=1000.0/cfg.tickRate;
        while(!ticks||server.simulation().tick()<ticks){
            auto now=net::nowMs();accumulated+=double(now-previous);previous=now;
            auto admit=[&](auto conns){for(auto& c:conns){uint8_t slot=0;for(uint8_t i=1;i<=cfg.maxPlayers;++i)if(!server.occupied(i)){slot=i;break;}if(!slot||!server.attach(slot,std::move(c)))c->close();}};
            admit(udp.poll(now));admit(ws.poll(now));server.poll(now);
            int budget=8;while(accumulated>=step&&budget--&&(!ticks||server.simulation().tick()<ticks)){server.step();accumulated-=step;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("ticks=%u checksum=%llu sent=%llu\n",server.simulation().tick(),(unsigned long long)server.simulation().checksum(),(unsigned long long)server.sentBytes());
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
