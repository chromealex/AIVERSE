// SpectraFX opt-in project GPU event routes, authored as fx/gpu/routes.json.
// Safe default: only events belonging to the local viewer are forwarded.
// Presentation routing does not mutate or subscribe to authoritative ECS state.
#pragma once
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <string>
#include <optional>
#include "../data/Json.h"
#include "SpectraFxHybridPlayback.h"
#include "SpectraFxSignificance.h"

namespace aiverse::fx {

struct SpectraFxGpuEventRoute {
    uint16_t eventType = 0;
    SpectraFxHybridPlayback::Route playback;
    // A source game must explicitly opt in to its own event types. Never
    // render opponents' hidden events merely because they exist in the sim.
    bool ownPlayerOnly = true;
    float importance = 1.0f; // host-defined presentation order, not gameplay
};

class SpectraFxGpuRouteAsset {
public:
    using Routes = std::unordered_map<uint16_t, SpectraFxGpuEventRoute>;
    static bool read(const Json& doc, Routes& result, std::string* error=nullptr,
                     SpectraFxGpuBudgetGovernor::Config* gpuBudget=nullptr,
                     std::optional<FxQuality>* sceneQuality=nullptr,
                     SpectraFxSignificance::Config* cuePriority=nullptr) {
        auto fail=[&](const std::string& reason){
            if(error) *error=reason;
            return false;
        };
        if(!doc.isObject() || doc.getInt("version",-1)!=1)
            return fail("SpectraFX GPU routes require version 1");
        for(const auto& p:doc.members())
            if(p.first!="version" && p.first!="routes" &&
               p.first!="gpuFrameBudgetMs" && p.first!="minGpuSpawnScale" &&
               p.first!="sceneQuality" && p.first!="cuePriority")
                return fail("SpectraFX GPU routes: unknown field "+p.first);
        std::optional<FxQuality> parsedQuality;
        if(doc.has("sceneQuality")) {
            if(!doc["sceneQuality"].isString())
                return fail("sceneQuality must be off, low, medium or high");
            const auto name=doc["sceneQuality"].str();
            if(name=="off") parsedQuality=FxQuality::Off;
            else if(name=="low") parsedQuality=FxQuality::Low;
            else if(name=="medium") parsedQuality=FxQuality::Medium;
            else if(name=="high") parsedQuality=FxQuality::High;
            else return fail("unknown sceneQuality");
        }
        SpectraFxGpuBudgetGovernor::Config budget;
        if(doc.has("gpuFrameBudgetMs")) {
            const Json& field=doc["gpuFrameBudgetMs"];
            if(!field.isNumber()) return fail("gpuFrameBudgetMs must be numeric");
            const double x=field.toDouble();
            if(!std::isfinite(x) || x<0 || x>1000)
                return fail("gpuFrameBudgetMs must be 0..1000 milliseconds");
            budget.targetFrameMs=float(x);
        }
        if(doc.has("minGpuSpawnScale")) {
            const Json& field=doc["minGpuSpawnScale"];
            if(!field.isNumber()) return fail("minGpuSpawnScale must be numeric");
            const double x=field.toDouble();
            if(!std::isfinite(x) || x<0 || x>1)
                return fail("minGpuSpawnScale must be 0..1");
            budget.minSpawnScale=float(x);
        }
        SpectraFxGpuBudgetGovernor validation;
        if(!validation.configure(budget))
            return fail("SpectraFX GPU frame-budget settings invalid");
        SpectraFxSignificance::Config ranking;
        const Json& authoredPriority=doc["cuePriority"];
        if(!authoredPriority.isNull()) {
            if(!authoredPriority.isObject())
                return fail("cuePriority must be object");
            ranking.enabled=true; // presence explicitly opts in
            for(const auto& p:authoredPriority.members())
                if(p.first!="enabled" && p.first!="nearDistance" &&
                   p.first!="farDistance" && p.first!="targetRadiusPixels" &&
                   p.first!="peripheralMultiplier" &&
                   p.first!="behindMultiplier")
                    return fail("unknown cuePriority field "+p.first);
            if(authoredPriority.has("enabled")) {
                if(authoredPriority["enabled"].type()!=Json::Type::Bool)
                    return fail("cuePriority.enabled must be bool");
                ranking.enabled=authoredPriority["enabled"].boolean();
            }
            const auto numeric=[&](const char* key,float& field) {
                if(!authoredPriority.has(key))return true;
                const Json& j=authoredPriority[key];
                if(!j.isNumber())return false;
                const double d=j.toDouble();
                if(!std::isfinite(d) || std::abs(d)>1000000)return false;
                field=float(d);
                return true;
            };
            if(!numeric("nearDistance",ranking.nearDistance) ||
               !numeric("farDistance",ranking.farDistance) ||
               !numeric("targetRadiusPixels",ranking.targetRadiusPixels) ||
               !numeric("peripheralMultiplier",ranking.peripheralMultiplier) ||
               !numeric("behindMultiplier",ranking.behindMultiplier))
                return fail("invalid cuePriority number");
            SpectraFxSignificance validation;
            if(!validation.configure(ranking))
                return fail("cuePriority settings out of range");
        }
        const Json& values=doc["routes"];
        if(!values.isArray() || values.size()>64)
            return fail("SpectraFX GPU routes must contain at most 64 entries");
        Routes next;
        std::unordered_map<std::string,uint32_t> poolCapacities;
        for(const auto& value:values.items()) {
            if(!value.isObject()) return fail("SpectraFX GPU route must be object");
            for(const auto& p:value.members())
                if(p.first!="eventType" && p.first!="asset" &&
                   p.first!="oneShotSeconds" && p.first!="enabled" &&
                   p.first!="ownPlayerOnly" &&
                   p.first!="sharedPool" && p.first!="sharedPoolCapacity" &&
                   p.first!="importance")
                    return fail("SpectraFX GPU route: unknown field "+p.first);
            const Json& event=value["eventType"];
            if(!event.isNumber() ||
               event.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
               event.integer(-1)<0 || event.integer(-1)>65535)
                return fail("SpectraFX eventType must be uint16");
            const Json& asset=value["asset"];
            if(!asset.isString() ||
               !SpectraFxGpuEmitterLibrary::validName(asset.str()))
                return fail("SpectraFX invalid GPU emitter asset name");
            SpectraFxGpuEventRoute entry;
            entry.eventType=uint16_t(event.integer());
            entry.playback.asset=asset.str();
            if(value.has("enabled")) {
                if(value["enabled"].type()!=Json::Type::Bool)
                    return fail("SpectraFX enabled must be bool");
                entry.playback.enabled=value["enabled"].boolean();
            }
            if(value.has("ownPlayerOnly")) {
                if(value["ownPlayerOnly"].type()!=Json::Type::Bool)
                    return fail("SpectraFX ownPlayerOnly must be bool");
                entry.ownPlayerOnly=value["ownPlayerOnly"].boolean();
                // Hidden enemy events must not leak through FX.
                if(!entry.ownPlayerOnly)
                    return fail("SpectraFX shared event visibility requires a host visibility adapter");
            }
            if(value.has("importance")) {
                const Json& val=value["importance"];
                if(!val.isNumber())return fail("importance must be numeric");
                const double d=val.toDouble();
                if(!std::isfinite(d) || d<0.01 || d>100)
                    return fail("importance must be a number in [0.01,100]");
                entry.importance=float(d);
            }
            if(value.has("sharedPool")) {
                if(!value["sharedPool"].isString() ||
                   !SpectraFxGpuEmitterLibrary::validName(value["sharedPool"].str()))
                    return fail("sharedPool requires a non-empty safe pool name");
                entry.playback.sharedPool=value["sharedPool"].str();
            }
            if(value.has("sharedPoolCapacity")) {
                const Json& capacity=value["sharedPoolCapacity"];
                if(!capacity.isNumber() ||
                   capacity.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
                   capacity.integer(-1)<1 || capacity.integer(-1)>65536)
                    return fail("sharedPoolCapacity requires an integer in 1..65536");
                if(entry.playback.sharedPool.empty())
                    return fail("sharedPoolCapacity requires sharedPool");
                entry.playback.sharedPoolCapacity=uint32_t(capacity.integer());
            }
            if(value.has("oneShotSeconds")) {
                if(!value["oneShotSeconds"].isNumber())
                    return fail("SpectraFX oneShotSeconds must be numeric");
                const double seconds=value["oneShotSeconds"].toDouble();
                if(!(seconds>0 && seconds<=60))
                    return fail("SpectraFX oneShotSeconds must be in (0,60]");
                entry.playback.oneShotSeconds=float(seconds);
            }
            if(!entry.playback.sharedPool.empty()) {
                const auto pair=poolCapacities.emplace(entry.playback.sharedPool,
                                                      entry.playback.sharedPoolCapacity);
                if(!pair.second && pair.first->second!=entry.playback.sharedPoolCapacity)
                    return fail("sharedPoolCapacity mismatch across routes in same pool");
            }
            if(!next.emplace(entry.eventType,std::move(entry)).second)
                return fail("SpectraFX duplicate eventType route");
        }
        result=std::move(next);
        if(gpuBudget) *gpuBudget=budget;
        if(sceneQuality) *sceneQuality=parsedQuality;
        if(cuePriority) *cuePriority=ranking;
        if(error) error->clear();
        return true;
    }
    static bool readText(const std::string& text,Routes& routes,
                         std::string* error=nullptr,
                         SpectraFxGpuBudgetGovernor::Config* gpuBudget=nullptr,
                         std::optional<FxQuality>* sceneQuality=nullptr,
                         SpectraFxSignificance::Config* cuePriority=nullptr) {
        try {return read(Json::parse(text),routes,error,gpuBudget,sceneQuality,cuePriority);}
        catch(const std::exception& e) {
            if(error) *error=e.what();
            return false;
        }
    }
};
} // namespace aiverse::fx
