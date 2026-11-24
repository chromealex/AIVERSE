// Authored data adapter for the existing genre-neutral SpectraSourceComponent.
// Does not register a second ECS component or own presentation resources.
#pragma once
#include "libraries/include/aiverselib/mechanics/EcsVfxPresentation.h"
#include "engine/data/Json.h"
#include <cmath>
#include <stdexcept>

namespace aiverse::lib::mechanics {
inline constexpr const char* kSpectraSourceDocumentKey="SpectraSource";
inline bool validSpectraSourceName(const std::string& name){
    return !name.empty()&&name.size()<=48&&name!="fx"&&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos;
}
inline SpectraSourceComponent readSpectraSourceDocument(const Json& doc){
    if(!doc.isObject())throw std::runtime_error("SpectraSource must be an object");
    SpectraSourceComponent source;
    source.effect=doc.getStr("effect");
    if(!doc["effect"].isString()||(!source.effect.empty()&&!validSpectraSourceName(source.effect)))
        throw std::runtime_error("Invalid SpectraFX effect name");
    const double scale=doc["scale"].toDouble();
    if(!doc["scale"].isNumber()||!std::isfinite(scale)||scale<0.001||scale>100)
        throw std::runtime_error("SpectraFX scale must be 0.001..100");
    source.scale=Fixed::fromRaw(int64_t(std::llround(scale*double(Fixed::kOne))));
    const auto revision=doc["revision"].integer(-1);
    if(!doc["revision"].isNumber()||revision<0||revision>4294967295LL||
       doc["revision"].numberLiteral().find_first_of(".eE-")!=std::string::npos)
        throw std::runtime_error("Invalid SpectraFX source revision");
    source.revision=uint32_t(revision);
    for(const char* key:{"enabled","killOnDetach"})
        if(doc[key].type()!=Json::Type::Bool)throw std::runtime_error("Invalid SpectraFX source flag");
    source.enabled=doc.getBool("enabled");source.killOnDetach=doc.getBool("killOnDetach");
    const auto& params=doc["parameters"];
    if(!params.isNull()){
        if(!params.isObject()||params.size()>8)throw std::runtime_error("Invalid SpectraFX parameter overrides");
        for(const auto& [name,value]:params.members()){
            const double n=value.toDouble();
            if(!spectra::instanceParameterNameOk(name)||!value.isNumber()||!std::isfinite(n)||std::abs(n)>1e7)
                throw std::runtime_error("Invalid SpectraFX parameter: "+name);
            source.parameters.emplace(name,Fixed::fromRaw(int64_t(std::llround(n*double(Fixed::kOne)))));
        }
    }
    return source;
}
inline Json writeSpectraSourceDocument(const SpectraSourceComponent& source){
    Json out=Json::object();
    out.set("effect",Json::string(source.effect));out.set("scale",Json::number(source.scale.toDouble(),6));
    out.set("revision",Json::number(int64_t(source.revision)));
    out.set("enabled",Json::fromBool(source.enabled));out.set("killOnDetach",Json::fromBool(source.killOnDetach));
    if(!source.parameters.empty()){
        Json params=Json::object();
        for(const auto& [name,value]:source.parameters)params.set(name,Json::number(value.toDouble(),8));
        out.set("parameters",std::move(params));
    }
    return out;
}
}
