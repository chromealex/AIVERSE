// Volumetric particle authoring adapter. Legacy particle compiler must not
// silently discard this material; only presentation/native GPU paths lower it.
#pragma once
#include "SpectraParticleVolumeTexture.h"
#include "SpectraVolumeAsset.h"
#include "VfxGraph.h"
#include <algorithm>
#include <cmath>
namespace aiverse::spectra {
enum class VolumetricParticleShape : uint8_t { Sphere, Ellipsoid, Box, Capsule };
struct VolumetricParticleMaterial {
    bool enabled=false;
    float density=2,emission=0;
    uint32_t steps=12;
    VolumetricParticleShape shape=VolumetricParticleShape::Sphere;
    render::float3 axes{1,1,1}; // radii relative to particle half-size
    float noiseFrequency=3,noiseStrength=0;
    uint32_t noiseOctaves=3;
    bool adaptiveLod=true;
    uint32_t minimumSteps=4;
    float lodSmallPixels=8,lodFullPixels=96;
    float boundScale()const{
        const float m=std::max(axes.x,std::max(axes.y,axes.z));
        if(shape==VolumetricParticleShape::Box)return std::sqrt(axes.x*axes.x+axes.y*axes.y+axes.z*axes.z);
        if(shape==VolumetricParticleShape::Capsule)return m+axes.y;
        return m;
    }
    bool sortBackToFront=true; // per-emitter centre-depth ordering, not exact OIT
    bool depthAware=false; // requires detached current-frame depth texture at draw
    uint32_t shadowSteps=4; // 0 disables self-shadowing
    float shadowStrength=1,anisotropy=0,ambient=.35f;
    float directIntensity=.65f;
    bool sparseTexture=true; // automatically fall back to dense if bricks cost more
    std::shared_ptr<const ParticleVolumeTexture> texture;
    bool valid()const{
        return std::isfinite(density)&&density>=0&&density<=100&&
            std::isfinite(emission)&&emission>=0&&emission<=16&&steps>=4&&steps<=32&&minimumSteps>=4&&minimumSteps<=steps&&
            std::isfinite(lodSmallPixels)&&std::isfinite(lodFullPixels)&&lodSmallPixels>=1&&lodFullPixels>lodSmallPixels&&lodFullPixels<=4096&&uint32_t(shape)<=3&&
            std::isfinite(axes.x)&&std::isfinite(axes.y)&&std::isfinite(axes.z)&&
            axes.x>=.1f&&axes.x<=4&&axes.y>=.1f&&axes.y<=4&&axes.z>=.1f&&axes.z<=4&&
            (shape!=VolumetricParticleShape::Sphere||(axes.x==1&&axes.y==1&&axes.z==1))&&
            std::isfinite(noiseFrequency)&&noiseFrequency>=.1f&&noiseFrequency<=32&&
            std::isfinite(noiseStrength)&&noiseStrength>=0&&noiseStrength<=1&&noiseOctaves>=1&&noiseOctaves<=4&&shadowSteps<=8&&
            std::isfinite(shadowStrength)&&shadowStrength>=0&&shadowStrength<=8&&
            std::isfinite(anisotropy)&&std::abs(anisotropy)<=.85f&&
            std::isfinite(ambient)&&ambient>=0&&ambient<=4&&
            std::isfinite(directIntensity)&&directIntensity>=0&&directIntensity<=16&&(!texture||(enabled&&texture->valid()));
    }
};

inline bool hasVolumetricParticles(const Json& graph){
    for(const auto& n:graph["nodes"].items())if(n.getStr("type")=="VolumetricParticleOutput")return true;
    return false;
}
inline bool validParticleVolumeTexturePath(const std::string& path){
    return path.size()>5&&path.size()<=512&&path.substr(path.size()-5)==".spvt"&&
        fx::validSdfResourcePath(path.substr(0,path.size()-5)+".sdfbin");
}
inline bool lowerVolumetricParticleGraph(const Json& graph,Json& lowered,VolumetricParticleMaterial& material,
        std::vector<std::string>& errors,const fx::SdfResourceReader& reader={}){
    using namespace volume::asset_detail;
    const auto fail=[&](const std::string& why){errors.push_back("volumetric particle output: "+why);return false;};
    if(!hasVolumetricParticles(graph)){lowered=graph;return true;}
    if(graph.getInt("version")!=2||!graph["nodes"].isArray()||!graph["links"].isArray()||!graph["outputs"].isArray())
        return fail("requires graph v2");
    Json next=graph;VolumetricParticleMaterial m;m.enabled=true;
    std::string id;
    for(auto& node:next.find("nodes")->itemsMut()){
        if(node.getStr("type")!="VolumetricParticleOutput")continue;
        if(!id.empty()||node.getStr("id").empty())return fail("one volumetric particle output required");
        id=node.getStr("id");
        const auto& p=node["properties"];
        if(!keys(p,{"shape","axes","density","emission","steps","noiseFrequency","noiseStrength","noiseOctaves",
                    "shadowSteps","shadowStrength","anisotropy","ambient","directIntensity","depthAware","sortBackToFront",
                    "adaptiveLod","minimumSteps","lodSmallPixels","lodFullPixels","sparseTexture","densityTexture"}))
            return fail("invalid material properties");
        if(p.has("shape")&&!p["shape"].isString())return fail("invalid shape");
        const auto shape=p.getStr("shape","sphere");
        if(shape=="sphere")m.shape=VolumetricParticleShape::Sphere;
        else if(shape=="ellipsoid")m.shape=VolumetricParticleShape::Ellipsoid;
        else if(shape=="box")m.shape=VolumetricParticleShape::Box;
        else if(shape=="capsule")m.shape=VolumetricParticleShape::Capsule;
        else return fail("unknown shape");
        const auto flag=[&](const char* k,bool& out){
            if(!p.has(k))return true;
            if(p[k].type()!=Json::Type::Bool)return false;
            out=p.getBool(k);return true;
        };
        if(!vector(p,"axes",m.axes)||!number(p,"density",m.density)||!number(p,"emission",m.emission)||
           !integer(p,"steps",m.steps)||!number(p,"noiseFrequency",m.noiseFrequency)||!number(p,"noiseStrength",m.noiseStrength)||
           !integer(p,"noiseOctaves",m.noiseOctaves)||!integer(p,"shadowSteps",m.shadowSteps)||!number(p,"shadowStrength",m.shadowStrength)||
           !number(p,"anisotropy",m.anisotropy)||!number(p,"ambient",m.ambient)||!number(p,"directIntensity",m.directIntensity)||
           !flag("depthAware",m.depthAware)||!flag("sortBackToFront",m.sortBackToFront)||!flag("adaptiveLod",m.adaptiveLod)||
           !integer(p,"minimumSteps",m.minimumSteps)||!number(p,"lodSmallPixels",m.lodSmallPixels)||!number(p,"lodFullPixels",m.lodFullPixels)||
           !flag("sparseTexture",m.sparseTexture)||!m.valid())return fail("invalid material range or type");
        if(p.has("densityTexture")){
            if(!p["densityTexture"].isString())return fail("invalid density texture");
            auto path=p["densityTexture"].str();
            if(!path.empty()){
                if(!validParticleVolumeTexturePath(path)||!reader)return fail("density texture requires safe project SPVT reader");
                std::string bytes,error;auto texture=std::make_shared<ParticleVolumeTexture>();
                if(!reader(path,bytes)||!decodeParticleVolumeTexture(bytes,*texture,error))return fail("cannot load SPVT: "+path);
                m.texture=std::move(texture);
            }
        }
        node.set("type",Json::string("SpriteOutput"));
        node.set("properties",Json::parse(R"({"texture":"builtin:soft","blend":"additive"})"));
    }
    if(graph["outputs"].size()!=1||graph["outputs"][size_t(0)].str()!=id)return fail("requires exactly one particle output");
    for(const auto& link:graph["links"].items())
        if(link.getStr("to")==id&&link.has("toPort"))return fail("material data links not supported yet");
    material=std::move(m);lowered=std::move(next);return true;
}
}
