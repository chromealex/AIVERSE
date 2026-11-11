#include "engine/meta/Meta.h"
#include "engine/meta/Social.h"
#include "engine/meta/Service.h"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) { std::cerr<<"Usage: aiverse_meta_validate economy|boards|chats|service file.json\n"; return 2; }
    try {
        auto j=aiverse::Json::parseFile(argv[2]); const std::string kind=argv[1];
        if(kind=="service") (void)aiverse::meta::ServiceConfig::parse(j);
        else if(kind=="economy") (void)aiverse::meta::Config::parse(j);
        else if(kind=="boards"||kind=="chats") {
            if(!j.isObject()) throw std::runtime_error("expected named definitions");
            std::set<std::string> ids;
            for(auto& [id,v]:j.members()) {
                if(id.empty()||!ids.insert(id).second) throw std::runtime_error("empty/duplicate id");
                if(kind=="boards") (void)aiverse::meta::BoardConfig::parse(v); else (void)aiverse::meta::ChatConfig::parse(v);
            }
        } else throw std::runtime_error("unknown definition kind");
        std::cout<<"valid\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
