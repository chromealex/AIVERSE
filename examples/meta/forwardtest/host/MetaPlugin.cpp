// Project-native host adapter. Networking/outbox live on platform frames, outside simulation.
#include "engine/platform/Plugins.h"
#include "engine/meta/Client.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <future>
#include <chrono>
using namespace aiverse;
using namespace aiverse::meta;
namespace {
std::string env(const char* key) { auto p=std::getenv(key); return p?p:""; }
std::string read(const std::string& path) { std::ifstream f(path); return {std::istreambuf_iterator<char>(f),{}}; }
bool persist(const std::string& path,const Json& value) {
    const auto temp=path+".pending";
    {std::ofstream f(temp); f<<value.dump(-1); f.flush(); if(!f) return false;}
    return std::rename(temp.c_str(),path.c_str())==0;
}
ServiceReply rpc(const ServiceRequest& request) {
    auto body=Json::object(); body.set("service",Json::string(request.service)); body.set("action",Json::string(request.action));
    body.set("operation",Json::string(request.operation)); body.set("arguments",request.arguments);
    auto payload=body.dump(-1); auto token=read(env("META_PLAYER_TOKEN_FILE"));
    while(!token.empty() && (token.back()=='\n'||token.back()=='\r')) token.pop_back();
    if(token.empty()) return transportFailure("missing player session file");
    int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return transportFailure("socket");
    timeval timeout{2,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)); setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    int noSignal=1; setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&noSignal,sizeof(noSignal));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(18765); inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
    if(connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0) {close(fd);return transportFailure("connect");}
    std::string wire="POST /rpc HTTP/1.0\r\nHost: 127.0.0.1:18765\r\nContent-Type: application/json\r\nAuthorization: Bearer "+token+"\r\nContent-Length: "+std::to_string(payload.size())+"\r\n\r\n"+payload;
    for(size_t n=0;n<wire.size();) {auto sent=send(fd,wire.data()+n,wire.size()-n,0);if(sent<=0){close(fd);return transportFailure("send");}n+=size_t(sent);}
    std::string response; char buffer[4096]; ssize_t n;
    while((n=recv(fd,buffer,sizeof(buffer),0))>0) {response.append(buffer,size_t(n));if(response.size()>1024*1024)break;}
    close(fd); auto split=response.find("\r\n\r\n");
    if(n<0||split==std::string::npos) return transportFailure("incomplete reply");
    if(response.find(" 200 ")==std::string::npos) return transportFailure("HTTP failure");
    if(env("META_DROP_REPLY")=="1" && request.operation=="game-banner-v1") return transportFailure("injected lost reply after server commit");
    try {auto j=Json::parse(response.substr(split+4));return {{Code(j.getInt("code",2)),j.getStr("detail")},j["data"]};}
    catch(...) {return transportFailure("invalid JSON reply");}
}
class MetaGamePlugin final:public plugins::NativePlugin {
    std::unique_ptr<RemoteBackend> backend;
    std::unique_ptr<Client> client;
    std::vector<ServiceRequest> commands;
    std::future<ServiceReply> transport;
    Backend::Callback transportCallback;
    size_t step=0; bool submitted=false,done=false;
public:
    void call(const std::string& method,const std::string&,plugins::Reply reply) override {
        if(method=="init") {
            if(env("META_PLAYER_TOKEN_FILE").empty()) {done=true;reply.ok();return;}
            auto path=env("META_OUTBOX_FILE"); if(path.empty()){done=true;reply.fail("outbox path required");return;}
            backend=std::make_unique<RemoteBackend>([this](const ServiceRequest& r,Backend::Callback cb){
                transportCallback=std::move(cb);
                transport=std::async(std::launch::async,[r]{return rpc(r);});
            });
            Json saved=Json::object(); if(std::filesystem::exists(path))saved=Json::parse(read(path));
            client=std::make_unique<Client>(*backend,[path](const Json& j){return persist(path,j);},saved);
            commands={{"profile","create","game-create-v1",Json::object()},requests::buy("welcome","game-welcome-v1"),requests::buy("banner","game-banner-v1"),requests::chatSend("global","Ready for River Valley!","game-chat-v1"),requests::boardSubmit("wins",999999,"forged-score-v1"),requests::view()};
            if(client->pending()) client->retry();
        }
        reply.ok();
    }
    void update() override {
        if(done||!client)return;
        if(transport.valid() && transport.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto reply=transport.get();auto cb=std::move(transportCallback);cb(std::move(reply));
        }
        client->pump();
        if(client->pending()) {if(!client->inFlight()){std::cout<<"META_PENDING_RETRY\n"<<std::flush;done=true;}return;}
        if(submitted) {
            auto r=client->reply();std::cout<<"META_STEP "<<step<<" code="<<int(r.result.code)<<" data="<<r.data.dump(-1)<<"\n"<<std::flush;
            if(step==4 ? r.result.code!=Code::Denied : !r.result) {std::cout<<"META_FAIL step="<<step<<"\n"<<std::flush;done=true;return;}
            if(step==5) {
                const auto& j=r.data;
                bool ok=j["profile"]["balances"].getInt("gold")==75 && j["profile"]["balances"].getInt("banner")==1 && j["chats"]["global"].size()==1 && j["boards"]["wins"].size()==1;
                std::cout<<(ok?"META_ACCEPTANCE_PASS":"META_ACCEPTANCE_FAIL")<<"\n"<<std::flush;
                persist(env("META_RESULT_FILE"),j);done=true;return;
            }
            ++step;submitted=false;
        }
        submitted=client->begin(commands.at(step));
        if(!submitted){std::cout<<"META_FAIL begin\n"<<std::flush;done=true;}
    }
};
AIVERSE_PLUGIN("meta-game",MetaGamePlugin);
}
