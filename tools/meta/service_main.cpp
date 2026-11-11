#include "engine/meta/Service.h"
#include <iostream>
#include <random>
// Trusted subprocess protocol. The HTTP host constructs identity/authority/time from its session.
int main(int argc,char** argv) {
    if(argc!=2) {std::cerr<<"Usage: aiverse_meta_service service.json\n";return 2;}
    try {
        auto config=aiverse::meta::ServiceConfig::parse(aiverse::Json::parseFile(argv[1]));
        std::string line;
        while(std::getline(std::cin,line)) {
            try {
                auto input=aiverse::Json::parse(line); std::random_device entropy;
                aiverse::meta::Service service(config,input["state"],{},[&](int64_t total) {
                    return std::uniform_int_distribution<int64_t>(1,total)(entropy);
                });
                auto req=input["request"];
                aiverse::meta::ServiceRequest request{req.getStr("service"),req.getStr("action"),req.getStr("operation"),req["arguments"]};
                auto reply=service.execute({input.getStr("player"),input.getBool("authority")},request,input.getInt("now",-1));
                auto out=aiverse::Json::object(); out.set("code",aiverse::Json::number(int64_t(reply.result.code))); out.set("detail",aiverse::Json::string(reply.result.detail)); out.set("data",reply.data); out.set("state",service.save());
                std::cout<<out.dump(-1)<<std::endl;
            } catch(const std::exception& e) { auto out=aiverse::Json::object(); out.set("code",aiverse::Json::number(int64_t(aiverse::meta::Code::Invalid))); out.set("detail",aiverse::Json::string(e.what())); std::cout<<out.dump(-1)<<std::endl; }
        }
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
