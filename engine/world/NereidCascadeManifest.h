// NEREID optional WorldScene waterfall/cascade authoring manifest.
// Same genre-neutral ECS geometry can also arrive as a read-only snapshot.
#pragma once
#include "engine/world/NereidCascadeScene.h"
#include "engine/world/NereidWaterManifest.h"
#include <string>
#include <vector>

namespace aiverse::world {

struct NereidCascadeManifest {
    std::vector<NereidCascadeSource> sources;
    uint32_t maxUploadsPerFrame=2;
};

inline NereidCascadeManifest parseNereidCascadeManifest(const Json& root) {
    NereidCascadeManifest out;
    if(!root.has("nereidCascades"))return out;
    const auto& settings=root["nereidCascades"];
    if(!settings.isObject())throw JsonError("nereidCascades: expected object");
    out.maxUploadsPerFrame=uint32_t(worldInteger(
        settings,"maxUploadsPerFrame",2,1,8));
    const auto& sources=settings["sources"];
    if(!sources.isArray() || sources.size()>NereidCascadeScene::kMaxCascades)
        throw JsonError("nereidCascades.sources: expected at most 32");
    out.sources.reserve(sources.size());
    for(const auto& item:sources.items()) {
        if(!item.isObject())throw JsonError("nereidCascades: expected source object");
        NereidCascadeSource source;
        if(item.has("id"))source.id=uint64_t(worldInteger(item,"id",0,1,2147483647));
        if(item.has("inletWaterId"))
            source.inletWaterId=uint64_t(worldInteger(item,"inletWaterId",0,1,2147483647));
        if(item.has("outletWaterId"))
            source.outletWaterId=uint64_t(worldInteger(item,"outletWaterId",0,1,2147483647));
        if(item.has("joinLength"))
            source.joinLength=nereid_manifest_detail::scalar(
                item["joinLength"],"cascade.joinLength",0.1f,50.f);
        if(item.has("joinWidthBlend"))
            source.joinWidthBlend=nereid_manifest_detail::scalar(
                item["joinWidthBlend"],"cascade.joinWidthBlend",0.f,1.f);
        if(item.has("requireFootprint")) {
            if(item["requireFootprint"].type()!=Json::Type::Bool)
                throw JsonError("nereidCascades.requireFootprint: expected boolean");
            source.requireFootprint=item.getBool("requireFootprint",true);
        }
        if(item.has("flowContinuity")) {
            const auto& flow=item["flowContinuity"];
            if(!flow.isObject())
                throw JsonError("nereidCascades.flowContinuity: expected object");
            auto& policy=source.flowPolicy;
            policy.enabled=flow.getBool("enabled",true);
            if(flow.has("radius"))
                policy.radius=nereid_manifest_detail::positive(
                    flow["radius"],"cascade.flow.radius",1000_fx);
            if(flow.has("blend"))
                policy.blend=nereid_manifest_detail::coordinate(
                    flow["blend"],"cascade.flow.blend",1_fx);
            if(flow.has("speedLimit"))
                policy.speedLimit=nereid_manifest_detail::positive(
                    flow["speedLimit"],"cascade.flow.speedLimit",1000_fx);
            if(policy.enabled &&
               !lib::nereid::validWaterCascadeFlowPolicy(policy))
                throw JsonError("nereidCascades.flowContinuity: invalid policy");
            source.useFlowPolicy=true;
        }
        auto& c=source.cascade;
        c.enabled=item.getBool("enabled",true);
        if(item.has("foam"))
            c.foam=nereid_manifest_detail::coordinate(item["foam"],"cascade.foam",1_fx);
        if(item.has("speed"))
            c.speed=nereid_manifest_detail::coordinate(item["speed"],"cascade.speed",1000_fx);
        if(item.has("widthAxis")) {
            const auto& axis=item["widthAxis"];
            if(!axis.isArray() || axis.size()!=2)
                throw JsonError("nereidCascades.widthAxis: expected [x,z]");
            c.widthAxis={nereid_manifest_detail::coordinate(axis[0],"widthAxis.x",1000_fx),
                         nereid_manifest_detail::coordinate(axis[1],"widthAxis.z",1000_fx)};
        }
        const auto& points=item["points"];
        if(!points.isArray() || points.size()>lib::nereid::kMaxWaterCascadePoints)
            throw JsonError("nereidCascades.points: invalid array");
        for(const auto& p:points.items()) {
            if(!p.isArray() || p.size()!=4)
                throw JsonError("nereidCascades.points: expected [x,y,z,halfWidth]");
            lib::nereid::WaterCascadePoint point;
            point.position={
                nereid_manifest_detail::coordinate(p[0],"cascade.x"),
                nereid_manifest_detail::coordinate(p[1],"cascade.y"),
                nereid_manifest_detail::coordinate(p[2],"cascade.z")};
            point.halfWidth=nereid_manifest_detail::positive(
                p[3],"cascade.halfWidth",1000_fx);
            c.points.push_back(point);
        }
        if(item.has("stepsPerSegment"))
            source.options.stepsPerSegment=uint8_t(worldInteger(
                item,"stepsPerSegment",4,1,16));
        if(item.has("maxVertices"))
            source.options.maxVertices=uint32_t(worldInteger(
                item,"maxVertices",8192,8,65536));
        if(item.has("uvScale"))
            source.options.uvScale=nereid_manifest_detail::scalar(
                item["uvScale"],"cascade.uvScale",0.001f,20.f);
        if(item.has("style")) {
            const auto& v=item["style"];
            if(!v.isObject())throw JsonError("nereidCascades.style: expected object");
            if(v.has("deepColor"))
                nereid_manifest_detail::color(v["deepColor"],
                    source.style.deepR,source.style.deepG,source.style.deepB);
            if(v.has("foamColor"))
                nereid_manifest_detail::color(v["foamColor"],
                    source.style.foamR,source.style.foamG,source.style.foamB);
            if(v.has("turbidity"))
                source.style.turbidity=nereid_manifest_detail::scalar(
                    v["turbidity"],"cascade.turbidity",0.f,1.f);
            if(v.has("siltColor"))
                nereid_manifest_detail::color(v["siltColor"],
                    source.style.siltR,source.style.siltG,source.style.siltB);
            if(v.has("opacity"))
                source.style.opacity=nereid_manifest_detail::scalar(
                    v["opacity"],"cascade.opacity",0.001f,0.98f);
            if(v.has("specular"))
                source.style.specular=nereid_manifest_detail::scalar(
                    v["specular"],"cascade.specular",0.85f,1.f);
            if(v.has("foamStrength"))
                source.style.foamStrength=nereid_manifest_detail::scalar(
                    v["foamStrength"],"cascade.foamStrength",0.f,1.f);
            if(v.has("refractionStrength"))
                source.style.refractionStrength=nereid_manifest_detail::scalar(
                    v["refractionStrength"],"cascade.refractionStrength",0.f,1.f);
        }
        if(c.enabled && !lib::nereid::validWaterCascade(c))
            throw JsonError("nereidCascades: invalid descending cascade geometry");
        out.sources.push_back(std::move(source));
    }
    return out;
}
} // namespace aiverse::world
