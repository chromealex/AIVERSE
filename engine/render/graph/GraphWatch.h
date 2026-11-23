// One opt-in subscription to a published native instance. Owner thread only.
#pragma once
#include "GraphTelemetry.h"
#include <charconv>
#include <optional>

namespace aiverse::graph_telemetry {
class Watch {
    History history_;
    Identity identity_;
    std::string domain_,entity_,reason_="not-started";
    bool running_=false;
    uint64_t last_=0;
    static std::optional<Identity> identity(const Json& sample){
        if(!sample.getBool("available"))return std::nullopt;
        auto world=sample.getStr("worldEpoch"),domain=sample.getStr("domain");
        auto program=sample.getStr("programIdentity");
        std::string entity;
        if(domain=="motionweave")entity=sample.getStr("sourceId")+":"+sample.getStr("sourceGeneration")+":"+sample.getStr("presentationEntity");
        else if(domain=="spectrafx")entity=sample.getStr("instanceSerial");
        else return std::nullopt;
        if(world.empty()||program.empty()||entity.empty())return std::nullopt;
        return Identity{world,entity,program,domain};
    }
    static uint64_t sequence(const Json& sample){
        const auto s=sample.getStr("sequence");uint64_t n=0;
        const auto p=std::from_chars(s.data(),s.data()+s.size(),n);
        return p.ec==std::errc{}&&p.ptr==s.data()+s.size()?n:0;
    }
public:
    bool running() const{return running_;}
    const std::string& domain() const{return domain_;}
    const std::string& entity() const{return entity_;}
    void stop(const std::string& reason="stopped"){running_=false;reason_=reason;}
    void clear(){history_.clear();identity_={};domain_.clear();entity_.clear();last_=0;stop("cleared");}
    void start(const std::string& domain,const std::string& entity,const Json& sample){
        auto id=identity(sample);const auto seq=sequence(sample);
        if(!id||id->domain!=domain||!seq)throw std::invalid_argument("current published instance unavailable");
        History next;
        if(!next.publish(*id,seq,sample))throw std::invalid_argument("initial sample exceeds history budget");
        history_=std::move(next);identity_=std::move(*id);domain_=domain;entity_=entity;
        last_=seq;running_=true;reason_="capturing";
    }
    bool capture(const Json& sample){
        if(!running_)return false;
        const auto id=identity(sample);
        if(!id||*id<identity_||identity_<*id){stop("instance-retired-or-replaced");return false;}
        const auto seq=sequence(sample);
        if(!seq||seq<last_){stop("publication-sequence-reset");return false;}
        if(seq==last_)return false;
        if(!history_.publish(identity_,seq,sample)){stop("history-budget");return false;}
        last_=seq;return true;
    }
    Json status() const{
        Json r=Json::object();r.set("available",Json::fromBool(true));r.set("running",Json::fromBool(running_));
        r.set("domain",Json::string(domain_));r.set("entity",Json::string(entity_));
        r.set("reason",Json::string(reason_));r.set("worldEpoch",Json::string(identity_.world));
        r.set("programIdentity",Json::string(identity_.program));r.set("frames",history_.read(identity_));
        r.set("scope",Json::string("native-owner-published-samples"));return r;
    }
};
} // namespace aiverse::graph_telemetry
