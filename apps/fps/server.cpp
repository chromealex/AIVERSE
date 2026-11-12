#include "engine/net/authority/Session.h"
#include <aiverselib/net/LocalTickets.h>
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
        std::string project="examples/fps/arena",admissionFile,audience;uint16_t port=27100;uint32_t ticks=0;bool loopbackOnly=false;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--project"&&i+1<argc)project=argv[++i];
            else if(arg=="--loopback")loopbackOnly=true;
            else if(arg=="--admission-file"&&i+1<argc)admissionFile=argv[++i];
            else if(arg=="--audience"&&i+1<argc)audience=argv[++i];
            else if((arg=="--port"||arg=="--ticks")&&i+1<argc) {
                std::string value=argv[++i];uint32_t n=0;auto [end,err]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(err!=std::errc{}||end!=value.data()+value.size()||(arg=="--port"&&(n==0||n>=65535)))throw std::runtime_error("invalid numeric argument");
                if(arg=="--port")port=uint16_t(n);else ticks=n;
            } else throw std::runtime_error("usage: aiverse_fps_server --project DIR [--port 27100] [--ticks N] [--loopback] [--admission-file FILE --audience MATCH]");
        }
        if(audience.size()>128)throw std::runtime_error("match audience exceeds 128 bytes");
        if(admissionFile.empty()!=audience.empty())throw std::runtime_error("admission file and match audience must be specified together");
        auto data=aiverse::fps::readProject([&](const auto& file,std::string& text){std::ifstream input(project+"/"+file);if(!input)return false;text.assign(std::istreambuf_iterator<char>(input),{});return true;});
        auto rules=data.rules,map=data.map;
        aiverse::net::authority::Server server(rules,map);
        aiverse::net::UdpListener udp;aiverse::net::WsListener ws;
        if(!udp.open(port,loopbackOnly)||!ws.open(uint16_t(port+1),loopbackOnly))throw std::runtime_error("cannot bind FPS listeners");
        std::printf("FPS authority: UDP %u, WebSocket %u; %u Hz; %zu slots\n",unsigned(port),unsigned(port+1),server.simulation().tickRate(),server.simulation().players().size());std::fflush(stdout);
        using namespace aiverse::lib::session;
        AdmissionGate gate(audience);LocalTickets tickets;
        std::vector<std::pair<std::string,std::weak_ptr<Access>>> leases;
        uint64_t lastReload=0;bool loaded=false;
        auto utc=[](){return uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());};
        auto previous=aiverse::net::nowMs();double accumulated=0;const double step=1000.0/server.simulation().tickRate();
        while(!ticks||server.simulation().tick()<ticks) {
            auto now=aiverse::net::nowMs();accumulated+=double(now-previous);previous=now;
            if(!admissionFile.empty()&&(!loaded||now-lastReload>=1000)){
                loaded=true;lastReload=now;
                try{
                    std::ifstream file(admissionFile,std::ios::binary);std::string bytes(1048577,'\0');
                    if(!file)throw std::runtime_error("cannot read local admission file");
                    file.read(bytes.data(),std::streamsize(bytes.size()));bytes.resize(size_t(file.gcount()));
                    if(bytes.size()>1048576||!tickets.read(aiverse::Json::parse(bytes)))throw std::runtime_error("invalid local admission table");
                }catch(const std::exception&){tickets.clear();std::fprintf(stderr,"Local admission table unavailable; access revoked\n");}
                for(auto it=leases.begin();it!=leases.end();){
                    if(auto access=it->second.lock()){
                        auto renewed=tickets.verify(it->first,audience,utc(),now);
                        if(!renewed||renewed->player!=access->principal.player||renewed->slot!=access->principal.slot)access->revoked=true;
                        else access->principal.expiresAt=std::min(access->principal.expiresAt,renewed->expiresAt);
                        ++it;
                    }else it=leases.erase(it);
                }
            }
            auto admit=[&](auto connections) {for(auto& conn:connections) {
                if(!admissionFile.empty()){gate.enqueue(std::move(conn),now);continue;}
                uint8_t slot=0;for(size_t i=1;i<=server.simulation().players().size();++i)if(!server.occupied(uint8_t(i))){slot=uint8_t(i);break;}
                if(slot)server.attach(slot,std::move(conn));else conn->close();
            }};
            admit(udp.poll(now));admit(ws.poll(now));server.poll(now);
            if(!admissionFile.empty()){
                std::map<std::string,std::string> acceptedTokens;
                std::vector<uint8_t> reservedSlots;
                auto accepted=gate.poll(now,[&](const std::string& token,const std::string& match,uint64_t at)->std::optional<Principal>{
                    auto principal=tickets.verify(token,match,utc(),at);
                    if(!principal||server.occupied(principal->slot)||principal->slot>server.simulation().players().size()||std::find(reservedSlots.begin(),reservedSlots.end(),principal->slot)!=reservedSlots.end())return {};
                    reservedSlots.push_back(principal->slot);
                    acceptedTokens[principal->player]=token;return principal;
                });
                for(auto& peer:accepted){
                    auto principal=peer.access->principal;
                    if(server.attach(principal.slot,std::move(peer.connection)))leases.emplace_back(acceptedTokens.at(principal.player),peer.access);
                }
            }
            int budget=8;while(accumulated>=step&&budget--&&(!ticks||server.simulation().tick()<ticks)){server.step();accumulated-=step;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("ticks=%u checksum=%llu rejected=%llu\n",server.simulation().tick(),(unsigned long long)server.simulation().checksum(),(unsigned long long)server.rejected());
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
