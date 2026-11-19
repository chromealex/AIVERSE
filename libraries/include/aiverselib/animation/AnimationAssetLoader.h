// Pure runtime parser for animation assets authored by Workspace/MCP Editor.
// Shared by native RTS/FPS/RPG projects and the standalone model viewer.
#pragma once
#include "AnimationMontage.h"
#include "GenericRetarget.h"
#include "engine/data/Json.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
namespace aiverse::lib::animation {
inline float finiteAnimationNumber(const Json& n,float fallback) {
    if(n.isNull())return fallback;
    if(!n.isNumber())throw std::invalid_argument("animation number expected");
    const double d=n.toDouble();
    if(!std::isfinite(d)||std::abs(d)>1.e6)
        throw std::invalid_argument("nonfinite or excessive animation number");
    return float(d);
}
inline std::shared_ptr<const MontageDefinition> compileMontageAsset(
        const render::Model& model,const Json& document) {
    if(!document.isObject() || !document["sections"].isArray())
        throw std::invalid_argument("montage asset must contain sections");
    MontageDefinition def;
    def.model=&model;
    def.slot=document.getStr("slot","UpperBody");
    def.blendIn=finiteAnimationNumber(document["blendIn"],.1f);
    def.blendOut=finiteAnimationNumber(document["blendOut"],.1f);
    const auto mask=document.getStr("maskRoot");
    if(!mask.empty()) {
        def.maskRoot=findNodePath(model,mask);
        if(def.maskRoot<0)
            throw std::invalid_argument("montage mask bone not found: "+mask);
    }
    for(const auto& item:document["sections"].items()) {
        if(!item.isObject())
            throw std::invalid_argument("montage section must be an object");
        MontageSection section;
        section.name=item.getStr("name");
        const std::string clip=item.getStr("clip");
        section.clip=model.findClip(clip);
        if(section.clip<0)
            throw std::invalid_argument("montage clip not found: "+clip);
        section.begin=finiteAnimationNumber(item["begin"],0.f);
        section.end=finiteAnimationNumber(item["end"],0.f);
        const auto next=item.getInt("next",-1);
        if(next< -1 || next>INT32_MAX)
            throw std::invalid_argument("invalid montage next section");
        section.next=int(next);
        if(!item["notifies"].isNull() && !item["notifies"].isArray())
            throw std::invalid_argument("montage notifies must be an array");
        for(const auto& n:item["notifies"].items()){
            if(!n.isObject())throw std::invalid_argument("invalid montage notify");
            section.notifies.push_back({
                finiteAnimationNumber(n["time"],-1.f),n.getStr("name")});
        }
        def.sections.push_back(std::move(section));
    }
    return MontageDefinition::compile(std::move(def));
}
} // namespace aiverse::lib::animation
