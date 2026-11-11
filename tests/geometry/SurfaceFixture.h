#pragma once
#include "tools/modeling/Modeling.h"
namespace aiverse::proxytest {
inline Json wallDocument(bool hole=false){
    auto doc=Json::parse(R"({"version":1,"materials":[{"id":"wood","color":[0.32,0.13,0.045,1],"roughness":0.75}],"nodes":[]})");
    for(int i=0;i<10;++i){
        if(hole&&i>=3&&i<=6)continue;
        auto node=Json::parse(R"({"material":"wood","geometry":{"kind":"cylinder","radius":0.21,"height":4,"segments":32},"rotation":[0,0,0.70710678,0.70710678]})");
        node.set("id",Json::string("log"+std::to_string(i)));auto t=Json::array();t.push(Json::number(int64_t(0)));t.push(Json::number(i*.4-1.8));t.push(Json::number(int64_t(0)));node.set("translation",t);doc.member("nodes").push(node);
    }
    return doc;
}
}
