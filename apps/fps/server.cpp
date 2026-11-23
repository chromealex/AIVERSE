#include "engine/net/authority/Session.h"
#include <aiverselib/net/LocalTickets.h>
#include "engine/actor/Project.h"
#include "engine/platform/ContentReader.h"
#include <fstream>
#include <iterator>
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <charconv>
#include <filesystem>
#include <array>

namespace {
bool parseWorldId(const std::string& text,std::array<uint8_t,16>& out){
    if(text.size()!=32)return false;
    std::array<uint8_t,16> staged{};
    for(size_t i=0;i<staged.size();++i){
        unsigned value=0;
        const char* first=text.data()+i*2;
        auto [end,err]=std::from_chars(first,first+2,value,16);
        if(err!=std::errc{}||end!=first+2||value>255)return false;
        staged[i]=uint8_t(value);
    }
    if(std::none_of(staged.begin(),staged.end(),[](auto b){return b!=0;}))return false;
    out=staged;return true;
}
}

int main(int argc,char** argv) {
    try {
        std::string project="examples/fps/arena",admissionFile,audience,worldDir,worldId;
        uint16_t port=27100;uint32_t ticks=0,checkpointTicks=0;bool loopbackOnly=false;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--project"&&i+1<argc)project=argv[++i];
            else if(arg=="--loopback")loopbackOnly=true;
            else if(arg=="--admission-file"&&i+1<argc)admissionFile=argv[++i];
            else if(arg=="--audience"&&i+1<argc)audience=argv[++i];
            else if(arg=="--world-dir"&&i+1<argc)worldDir=argv[++i];
            else if(arg=="--world-id"&&i+1<argc)worldId=argv[++i];
            else if((arg=="--port"||arg=="--ticks"||arg=="--world-checkpoint-ticks")&&i+1<argc) {
                std::string value=argv[++i];uint32_t n=0;auto [end,err]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(err!=std::errc{}||end!=value.data()+value.size()||(arg=="--port"&&(n==0||n>=65535)))throw std::runtime_error("invalid numeric argument");
                if(arg=="--port")port=uint16_t(n);else if(arg=="--ticks")ticks=n;else checkpointTicks=n;
            } else throw std::runtime_error("usage: aiverse_fps_server --project DIR [--port 27100] [--ticks N] [--loopback] [--admission-file FILE --audience MATCH] [--world-dir DIR --world-id 32HEX [--world-checkpoint-ticks N]]");
        }
        if(audience.size()>128)throw std::runtime_error("match audience exceeds 128 bytes");
        if(admissionFile.empty()!=audience.empty())throw std::runtime_error("admission file and match audience must be specified together");
        auto data=aiverse::actor::readProject([&](const auto& file,std::string& text){std::ifstream input(project+"/"+file);if(!input)return false;text.assign(std::istreambuf_iterator<char>(input),{});return true;});
        auto rules=data.rules,map=data.map;
        aiverse::net::authority::Server server(rules,map,1,aiverse::platform::makeContentReader(project));
        if(worldDir.empty()!=worldId.empty()||(worldDir.empty()&&checkpointTicks))
            throw std::runtime_error("world directory and UUID must be specified together");
        if(!worldDir.empty()){
            std::array<uint8_t,16> uuid{};
            if(!parseWorldId(worldId,uuid))throw std::runtime_error("invalid world UUID (expected 32 nonzero hex digits)");
            const std::filesystem::path directory(worldDir);
            std::error_code ec;
            std::filesystem::create_directories(directory,ec);
            if(ec||!std::filesystem::is_directory(directory,ec)||ec)
                throw std::runtime_error("cannot prepare persistent world directory");
            const bool resume=std::filesystem::exists(directory/"manifest.bin",ec);
            if(ec||!server.enablePersistence(directory,uuid,resume))
                throw std::runtime_error("world persistence refused: unsupported content, invalid state, busy directory or recovery required");
            std::printf("Persistent world: %s at tick %u\n",resume?"recovered":"created",server.simulation().tick());
        }
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
            int budget=8;
            while(accumulated>=step&&budget--&&(!ticks||server.simulation().tick()<ticks)){
                if(!server.step()){
                    if(server.persistenceFailed())throw std::runtime_error("persistent tick refused; restart/recovery required");
                    accumulated=0;break;
                }
                accumulated-=step;
                if(checkpointTicks&&server.simulation().tick()%checkpointTicks==0&&!server.checkpointWorld())
                    throw std::runtime_error("persistent checkpoint refused; restart/recovery required");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("ticks=%u checksum=%llu rejected=%llu\n",server.simulation().tick(),(unsigned long long)server.simulation().checksum(),(unsigned long long)server.rejected());
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
