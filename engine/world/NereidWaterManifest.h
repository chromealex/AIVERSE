// NEREID opt-in WorldScene JSON presentation manifest.
// This parser is not an authoritative ECS loader; native games may instead
// provide an ECS snapshot via WorldScene::setNereidSurfaces.
#pragma once
#include "engine/world/NereidWaterScene.h"
#include "engine/world/NereidEntityManifest.h"
#include "engine/data/Json.h"
#include "engine/core/FixedMath.h"
#include <string>
#include <vector>
#include <algorithm>

namespace aiverse::world {
struct NereidManifest {
    std::vector<NereidSceneSurface> surfaces;
    uint32_t maxUploadsPerFrame=2;
    size_t maxEstimatedUploadBytes=8u*1024u*1024u;
    float visibilityRange=0;
    bool asyncPrepare=false;
    size_t maxStagingBytes=16u*1024u*1024u;
};

namespace nereid_manifest_detail {
inline Fixed coordinate(const Json& j,const char* field,Fixed max=100000_fx) {
    if(!j.isNumber() || j.fx() < -max || j.fx() > max)
        throw JsonError(std::string("nereidWater.")+field+": invalid number");
    return j.fx();
}
inline Fixed positive(const Json& j,const char* field,Fixed max=100000_fx) {
    const Fixed x=coordinate(j,field,max);
    if(x<=0_fx)throw JsonError(std::string("nereidWater.")+field+": must be positive");
    return x;
}
inline float scalar(const Json& j,const char* field,float lo,float hi) {
    if(!j.isNumber())throw JsonError(std::string("nereidWater.")+field+": expected number");
    const float v=float(j.fx().toDouble());
    if(!std::isfinite(v)||v<lo||v>hi)
        throw JsonError(std::string("nereidWater.")+field+": outside range");
    return v;
}
inline lib::nereid::WaterKind kind(const std::string& s) {
    using K=lib::nereid::WaterKind;
    if(s=="ocean")return K::Ocean;
    if(s=="lake")return K::Lake;
    if(s=="river")return K::River;
    if(s=="pool")return K::Pool;
    if(s=="puddle")return K::Puddle;
    throw JsonError("nereidWater.kind: unknown value");
}
inline lib::nereid::WaterSurfaceLod lod(const std::string& s) {
    using L=lib::nereid::WaterSurfaceLod;
    if(s=="near")return L::Near;
    if(s=="medium")return L::Medium;
    if(s=="far")return L::Far;
    throw JsonError("nereidWater.lod: unknown value");
}
inline void color(const Json& j,float& r,float& g,float& b) {
    if(!j.isArray()||j.size()!=3)throw JsonError("nereidWater.color: expected RGB triple");
    r=scalar(j[0],"color",0.f,1.f);
    g=scalar(j[1],"color",0.f,1.f);
    b=scalar(j[2],"color",0.f,1.f);
}
inline NereidSceneSurface surface(const Json& j) {
    using namespace lib::nereid;
    if(!j.isObject())throw JsonError("nereidWater.surface: expected object");
    NereidSceneSurface s;
    if(j.has("id"))s.id=uint64_t(worldInteger(j,"id",0,1,2147483647));
    s.body.kind=kind(j.getStr("kind","lake"));
    const auto shape=j.getStr("shape","box");
    if(shape=="infinite")s.body.footprint=WaterFootprint::Infinite;
    else if(shape=="box")s.body.footprint=WaterFootprint::Box;
    else if(shape=="disc")s.body.footprint=WaterFootprint::Disc;
    else if(shape=="polygon"||shape=="river") {
        s.useGeometry=true;
        s.geometry.kind=shape=="polygon"?WaterGeometryKind::Polygon:WaterGeometryKind::RiverSpline;
    }else throw JsonError("nereidWater.shape: unknown value");
    s.body.enabled=j.getBool("enabled",true);
    s.body.surfaceY=coordinate(j["surfaceY"],"surfaceY");
    s.body.bottomY=coordinate(j["bottomY"],"bottomY");
    if(s.body.surfaceY<=s.body.bottomY)
        throw JsonError("nereidWater: surfaceY must exceed bottomY");
    if(j.has("center"))s.body.center=worldVector(j["center"],100000_fx);
    if(j.has("flow"))s.body.flow=worldVector(j["flow"],1000_fx);
    if(j.has("priority"))s.body.priority=int32_t(worldInteger(j,"priority",0,-1000000,1000000));
    if(s.body.footprint==WaterFootprint::Box&&!s.useGeometry) {
        if(!j["halfExtents"].isArray()||j["halfExtents"].size()!=2)
            throw JsonError("nereidWater.halfExtents: expected [x,z]");
        s.body.halfExtents={
            positive(j["halfExtents"][0],"halfExtents",100000_fx),
            positive(j["halfExtents"][1],"halfExtents",100000_fx)};
    }
    if(s.body.footprint==WaterFootprint::Disc&&!s.useGeometry)
        s.body.radius=positive(j["radius"],"radius");
    if(s.useGeometry) {
        const auto& pts=j["points"];
        if(!pts.isArray()||pts.size()>kMaxWaterGeometryPoints)
            throw JsonError("nereidWater.points: invalid array");
        for(const auto& p:pts.items()) {
            const size_t required=s.geometry.kind==WaterGeometryKind::Polygon?2:3;
            if(!p.isArray()||p.size()!=required)
                throw JsonError("nereidWater.points: expected [x,z] or [x,z,width]");
            WaterGeometryPoint point;
            point.x=coordinate(p[0],"point.x");
            point.z=coordinate(p[1],"point.z");
            if(required==3)point.halfWidth=positive(p[2],"point.width");
            s.geometry.points.push_back(point);
        }
        if(!validWaterGeometry(s.geometry))
            throw JsonError("nereidWater.geometry: insufficient or invalid points");
    }
    if(j.has("currentField")) {
        const auto& current=j["currentField"];
        if(!current.isObject() || !s.useGeometry ||
           s.geometry.kind!=WaterGeometryKind::RiverSpline)
            throw JsonError("nereidWater.currentField: expected river spline profile");
        auto& field=s.currentField;
        field.enabled=current.getBool("enabled",true);
        if(current.has("bankFade"))
            field.bankFade=coordinate(current["bankFade"],"current.bankFade",1_fx);
        const auto& nodes=current["nodes"];
        if(!nodes.isArray() || nodes.size()!=s.geometry.points.size())
            throw JsonError("nereidWater.currentField.nodes: expected one [downstream,lateral,shear] per river point");
        for(const auto& node:nodes.items()) {
            if(!node.isArray() || node.size()!=3)
                throw JsonError("nereidWater.currentField.nodes: expected triple");
            field.nodes.push_back({
                coordinate(node[0],"current.downstream",1000_fx),
                coordinate(node[1],"current.lateral",1000_fx),
                coordinate(node[2],"current.shear",1000_fx)});
        }
        if(field.enabled && !validWaterCurrentField(field,&s.geometry))
            throw JsonError("nereidWater.currentField: invalid river current profile");
        s.useCurrentField=true;
    }
    if(j.has("vortices")) {
        const auto& vortices=j["vortices"];
        if(!vortices.isArray() || vortices.size()>kMaxWaterVortices)
            throw JsonError("nereidWater.vortices: expected at most 16 entries");
        for(const auto& entry:vortices.items()) {
            if(!entry.isArray() || entry.size()!=5)
                throw JsonError("nereidWater.vortices: expected [x,z,radius,circulation,radial]");
            WaterVortex vortex;
            vortex.center={
                coordinate(entry[0],"vortex.x"),
                coordinate(entry[1],"vortex.z")};
            vortex.radius=positive(entry[2],"vortex.radius",10000_fx);
            vortex.circulation=coordinate(entry[3],"vortex.circulation",1000_fx);
            vortex.radial=coordinate(entry[4],"vortex.radial",1000_fx);
            s.vortices.vortices.push_back(vortex);
        }
        s.useVortices=true;
        if(!validWaterVortexField(s.vortices))
            throw JsonError("nereidWater.vortices: invalid vortex field");
    }
    if(j.has("lod"))s.mesh.lod=lod(j.getStr("lod"));
    if(j.has("autoLod")) {
        if(j["autoLod"].type()!=Json::Type::Bool)
            throw JsonError("nereidWater.autoLod: expected boolean");
        s.autoLod=j.getBool("autoLod");
    }
    if(j.has("mediumDistance"))
        s.mediumDistance=scalar(j["mediumDistance"],"mediumDistance",1.f,1000000.f);
    if(j.has("farDistance"))
        s.farDistance=scalar(j["farDistance"],"farDistance",1.f,1000000.f);
    if(j.has("lodHysteresis"))
        s.lodHysteresis=scalar(j["lodHysteresis"],"lodHysteresis",0.f,100000.f);
    if(s.farDistance<=s.mediumDistance ||
       s.lodHysteresis>=s.mediumDistance ||
       2*s.lodHysteresis>=s.farDistance-s.mediumDistance)
        throw JsonError("nereidWater: invalid LOD distances/hysteresis");
    if(j.has("oceanPatchRadius"))
        s.mesh.oceanPatchRadius=scalar(j["oceanPatchRadius"],"oceanPatchRadius",1.f,100000.f);
    if(j.has("uvScale"))s.mesh.uvScale=scalar(j["uvScale"],"uvScale",0.0001f,128.f);
    if(j.has("foamWidth"))s.mesh.foamWidth=scalar(j["foamWidth"],"foamWidth",0.f,1000.f);
    if(j.has("rippleTileGrid"))
        s.mesh.rippleTileGrid=uint8_t(worldInteger(j,"rippleTileGrid",1,1,8));
    if(j.has("maxVertices"))
        s.mesh.maxVertices=uint32_t(worldInteger(j,"maxVertices",65536,4,65536));
    if(j.has("style")) {
        const auto& v=j["style"];
        if(!v.isObject())throw JsonError("nereidWater.style: expected object");
        if(v.has("deepColor"))color(v["deepColor"],s.style.deepR,s.style.deepG,s.style.deepB);
        if(v.has("foamColor"))color(v["foamColor"],s.style.foamR,s.style.foamG,s.style.foamB);
        if(v.has("opacity"))s.style.opacity=scalar(v["opacity"],"opacity",0.001f,0.98f);
        if(v.has("specular"))s.style.specular=scalar(v["specular"],"specular",0.85f,1.f);
        if(v.has("foamStrength"))s.style.foamStrength=scalar(v["foamStrength"],"foamStrength",0.f,1.f);
        if(v.has("refractionStrength"))s.style.refractionStrength=scalar(v["refractionStrength"],"refractionStrength",0.f,1.f);
        if(v.has("absorption"))s.style.absorption=scalar(v["absorption"],"absorption",0.f,4.f);
        if(v.has("shoreFoamDepth"))s.style.shoreFoamDepth=scalar(v["shoreFoamDepth"],"shoreFoamDepth",0.f,20.f);
        if(v.has("depthTint"))s.style.depthTint=scalar(v["depthTint"],"depthTint",0.f,1.f);
        if(v.has("underwaterDistortion"))s.style.underwaterDistortion=scalar(v["underwaterDistortion"],"underwaterDistortion",0.f,1.f);
        if(v.has("underwaterScattering"))s.style.underwaterScattering=scalar(v["underwaterScattering"],"underwaterScattering",0.f,1.f);
        if(v.has("turbidity"))s.style.turbidity=scalar(v["turbidity"],"turbidity",0.f,1.f);
        if(v.has("siltColor"))color(v["siltColor"],s.style.siltR,s.style.siltG,s.style.siltB);
        if(v.has("rapidFoamStrength"))s.style.rapidFoamStrength=scalar(v["rapidFoamStrength"],"rapidFoamStrength",0.f,1.f);
        if(v.has("rapidFlowThreshold"))s.style.rapidFlowThreshold=scalar(v["rapidFlowThreshold"],"rapidFlowThreshold",0.f,100.f);
        if(v.has("rapidNoiseScale"))s.style.rapidNoiseScale=scalar(v["rapidNoiseScale"],"rapidNoiseScale",0.1f,8.f);
        if(v.has("rapidFoamColor"))
            color(v["rapidFoamColor"],s.style.rapidFoamR,
                  s.style.rapidFoamG,s.style.rapidFoamB);
        if(v.has("rapidDetail")) {
            if(!v["rapidDetail"].isString())
                throw JsonError("nereidWater.rapidDetail: expected low, medium or high");
            const std::string quality=v["rapidDetail"].str();
            if(quality=="low")s.style.rapidDetail=WaterRapidDetail::Low;
            else if(quality=="medium")s.style.rapidDetail=WaterRapidDetail::Medium;
            else if(quality=="high")s.style.rapidDetail=WaterRapidDetail::High;
            else throw JsonError("nereidWater.rapidDetail: unknown quality");
        }
    }
    if(j.has("grade")){
        const auto& g=j["grade"];if(!g.isObject()||!s.useGeometry||s.geometry.kind!=WaterGeometryKind::RiverSpline)throw JsonError("nereidWater.grade requires river");
        auto& grade=s.mesh.grade;s.mesh.useGrade=true;
        if(g.has("origin")){if(!g["origin"].isArray()||g["origin"].size()!=2)throw JsonError("nereidWater.grade.origin");grade.origin={coordinate(g["origin"][0],"grade.origin"),coordinate(g["origin"][1],"grade.origin")};}
        if(g.has("risePerMetre")){if(!g["risePerMetre"].isArray()||g["risePerMetre"].size()!=2)throw JsonError("nereidWater.grade.risePerMetre");grade.risePerMetre={coordinate(g["risePerMetre"][0],"grade.rise",1_fx),coordinate(g["risePerMetre"][1],"grade.rise",1_fx)};}
        if(g.has("slopeDegrees")){
            const auto slope=coordinate(g["slopeDegrees"],"grade.slopeDegrees",20_fx);
            const auto heading=g.has("downhillHeadingDegrees")?coordinate(g["downhillHeadingDegrees"],"grade.heading",360_fx):0_fx;
            const auto angle=slope/360_fx;const auto rise=fxSinTurns(angle)/fxCosTurns(angle);
            grade.risePerMetre={-fxSinTurns(heading/360_fx)*rise,-fxCosTurns(heading/360_fx)*rise};
        }
        grade.automaticCurrent=g.getBool("automaticCurrent",true);
        if(g.has("responseSeconds"))grade.responseSeconds=positive(g["responseSeconds"],"grade.response",60_fx);
        if(g.has("maxSpeed"))grade.maxSpeed=positive(g["maxSpeed"],"grade.maxSpeed",100_fx);
        if(!validWaterRiverGrade(grade))throw JsonError("nereidWater.grade invalid");
    }
    return s;
}
} // namespace nereid_manifest_detail

inline NereidManifest parseNereidWaterManifest(const Json& root) {
    NereidManifest out;
    const auto config=nereidEntityManifest(root);
    if(config.isObject()&&config.size()==0)return out;
    if(!config.isObject())throw JsonError("nereidWater: expected object");
    out.maxUploadsPerFrame=uint32_t(worldInteger(config,"maxUploadsPerFrame",2,1,16));
    if(config.has("asyncPrepare") &&
       config["asyncPrepare"].type()!=Json::Type::Bool)
        throw JsonError("nereidWater.asyncPrepare: expected boolean");
    out.asyncPrepare=config.getBool("asyncPrepare",false);
    out.maxStagingBytes=size_t(worldInteger(config,"maxStagingBytes",
        16*1024*1024,1024,64*1024*1024));
    out.maxEstimatedUploadBytes=size_t(worldInteger(config,"maxEstimatedUploadBytes",8*1024*1024,1024,64*1024*1024));
    if(config.has("visibilityRange"))out.visibilityRange=nereid_manifest_detail::scalar(config["visibilityRange"],"visibilityRange",0.f,1000000.f);
    const auto& surfaces=config["surfaces"];
    if(!surfaces.isArray()||surfaces.size()>NereidWaterScene::kMaxSurfaces)
        throw JsonError("nereidWater.surfaces: expected at most 64");
    out.surfaces.reserve(surfaces.size());
    for(const auto& j:surfaces.items())
        out.surfaces.push_back(nereid_manifest_detail::surface(j));
    // Validate explicit and synthetic identifiers before constructing GPU state.
    std::vector<uint64_t> seen;
    for(size_t i=0;i<out.surfaces.size();++i) {
        const auto id=out.surfaces[i].id?out.surfaces[i].id:
            ((uint64_t(1)<<63)|(uint64_t(i)+1));
        if(std::find(seen.begin(),seen.end(),id)!=seen.end())
            throw JsonError("nereidWater: duplicate surface id");
        seen.push_back(id);
    }
    return out;
}
} // namespace aiverse::world
