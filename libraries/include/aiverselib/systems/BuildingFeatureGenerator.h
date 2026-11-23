#pragma once
// Building FUNCTIONALITY is a recipe of independent properties, never a genre,
// setting or role ("barracks"/"factory") switch. Style supplies only surfaces.
// This path builds a genuinely apertured wall/roof shell with separate doors,
// collision and sliding roof panels; it does not dress a closed existing hull.
// CPU-only; callable by both native Studio and shipping game code.
#include "BuildingStructureGraph.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::systems {

struct BuildingFeatureAnchor {
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    BuildingFeatureSide side=BuildingFeatureSide::Front;
    uint32_t index=0;
    uint32_t featureIndex=0;
    std::string socketId;
    Vec3 outside{}; // ground access point / roof hatch centre
    float width=0,height=0,depth=0,open=0;
};
struct FunctionalBuildingResult {
    StyledStructure building;
    BuildingStructureGraph graph;
    std::vector<BuildingFeatureAnchor> anchors;
    std::string error;
    explicit operator bool() const noexcept {
        return error.empty()&&!building.mesh.vertices.empty();
    }
};

// Optional portable JSON recipe. It is NOT a style file. Different styles can
// reuse exactly the same functional recipe without inheriting genre rules.
inline FunctionalBuildingRecipe readFunctionalBuildingRecipe(
    const Json& json,const std::string& path="building.features"){
    if(!json.isObject())throw JsonError(path+": expected object");
    FunctionalBuildingRecipe recipe;
    const auto number=[&](const Json& j,const std::string& p)->float{
        if(!j.isNumber())throw JsonError(p+": expected number");
        const double n=j.toDouble();
        if(!std::isfinite(n)||std::abs(n)>1000000.)
            throw JsonError(p+": invalid finite number");
        return float(n);
    };
    if(json.has("width"))recipe.width=number(json["width"],path+".width");
    if(json.has("height"))recipe.height=number(json["height"],path+".height");
    if(json.has("depth"))recipe.depth=number(json["depth"],path+".depth");
    if(json.has("seed")){
        const auto& value=json["seed"];
        if(!value.isNumber())throw JsonError(path+".seed: expected integer");
        const double f=value.toDouble(-1);
        if(!std::isfinite(f)||f<0.||std::floor(f)!=f||f>2147483647.)
            throw JsonError(path+".seed: expected integer from 0 to 2147483647");
        recipe.seed=uint64_t(f);
    }
    if(json.has("features")){
        const auto& list=json["features"];
        if(!list.isArray())throw JsonError(path+".features: expected array");
        for(size_t i=0;i<list.size();++i){
            const auto& j=list[i];
            const std::string label=path+".features["+std::to_string(i)+"]";
            if(!j.isObject()||!j["kind"].isString())
                throw JsonError(label+".kind: expected feature kind");
            BuildingFeature f;
            const auto kind=j["kind"].str();
            if(kind=="large-gate")f.kind=BuildingFeatureKind::LargeGate;
            else if(kind=="small-gate")f.kind=BuildingFeatureKind::SmallGate;
            else if(kind=="personnel-door")f.kind=BuildingFeatureKind::PersonnelDoor;
            else if(kind=="sliding-roof")f.kind=BuildingFeatureKind::SlidingRoof;
            else throw JsonError(label+".kind: unknown building property '"+kind+"'");
            if(f.kind==BuildingFeatureKind::LargeGate){f.width=5.f;f.height=4.5f;}
            else if(f.kind==BuildingFeatureKind::SmallGate){f.width=2.6f;f.height=2.8f;}
            else if(f.kind==BuildingFeatureKind::SlidingRoof){f.width=4.f;f.depth=4.f;}
            if(j.has("side")){
                if(!j["side"].isString())throw JsonError(label+".side: expected string");
                const auto side=j["side"].str();
                if(side=="front")f.side=BuildingFeatureSide::Front;
                else if(side=="back")f.side=BuildingFeatureSide::Back;
                else if(side=="left")f.side=BuildingFeatureSide::Left;
                else if(side=="right")f.side=BuildingFeatureSide::Right;
                else throw JsonError(label+".side: expected front, back, left or right");
            }
            if(j.has("count")){
                const float count=number(j["count"],label+".count");
                if(count<1||count>8||std::floor(count)!=count)
                    throw JsonError(label+".count: expected integer 1..8");
                f.count=uint32_t(count);
            }
            if(j.has("width"))f.width=number(j["width"],label+".width");
            if(j.has("height"))f.height=number(j["height"],label+".height");
            if(j.has("depth"))f.depth=number(j["depth"],label+".depth");
            if(j.has("open"))f.open=number(j["open"],label+".open");
            if(j.has("position"))f.position=number(j["position"],label+".position");
            if(j.has("positionZ"))f.positionZ=number(j["positionZ"],label+".positionZ");
            if(j.has("spacing"))f.spacing=number(j["spacing"],label+".spacing");
            recipe.features.push_back(f);
        }
    }
    const auto error=validateFunctionalRecipe(recipe);
    if(!error.empty())throw JsonError(path+": "+error);
    return recipe;
}

inline FunctionalBuildingResult generateFunctionalBuilding(
    const BuildingStyle& style,const FunctionalBuildingRecipe& recipe,
    Vec3 origin={},uint32_t yaw=0){
    using namespace buildingStyleDetail;
    FunctionalBuildingResult result;
    const auto reject=[&](const std::string& why){
        result.error=why;
        result.building.fitError=why;
        return result;
    };
    if(yaw>3)return reject("Invalid building yaw (expected 0..3)");
    auto graph=buildFunctionalStructureGraph(recipe);
    if(!graph)return reject(graph.error);

    const float W=recipe.width,H=recipe.height,D=recipe.depth;
    constexpr float floorTop=0.24f,wall=0.36f,roofThick=0.28f;
    const auto& sm=style.structure;
    const int hull=sm.hull>=0?sm.hull:style.shell;
    const int trim=sm.plate>=0?sm.plate:hull;
    const int dark=sm.dark>=0?sm.dark:hull;
    const int ground=sm.pad>=0?sm.pad:style.interior;
    if(hull<0||ground<0||dark<0)return reject("Style requires hull and floor materials");
    Random rng(recipe.seed,0x6275696c64ULL);
    result.building.mesh.materials=style.materials;
    if(!style.palettes.empty()){
        const auto& p=style.palettes[rng.below(uint32_t(style.palettes.size()))];
        for(const auto& [m,c]:p){
            if(m<0||size_t(m)>=result.building.mesh.materials.size())continue;
            auto& mat=result.building.mesh.materials[size_t(m)];
            if(mat.emissive[0]+mat.emissive[1]+mat.emissive[2]>0)mat.emissive=c;
            else mat.color=c;
        }
    }
    Mesher mesh(result.building.mesh.materials.size());
    auto addBox=[&](int material,float x0,float y0,float z0,
                    float x1,float y1,float z1,bool collider=true){
        mesh.box(material,x0,y0,z0,x1,y1,z1);
        if(collider&&x1>x0&&y1>y0&&z1>z0){
            auto f=[](float n){return Fixed::fromRatio(int64_t(std::llround(double(n)*1000.)),1000);};
            result.building.boxes.push_back({
                {f(x0),f(y0),f(z0)},{f(x1),f(y1),f(z1)}
            });
        }
    };
    // This single StructureGraph is the source for BOTH visible boxes and
    // physics solids. No closed hull or extra collider can span a door/roof
    // opening that has been removed from the graph.
    auto fcoord=[](float n){return Fixed::fromRatio(
        int64_t(std::llround(double(n)*1000.)),1000);};
    for(const auto& solid:graph.solids){
        int material=hull;
        switch(solid.part){
          case BuildingGraphPart::Floor:material=ground;break;
          case BuildingGraphPart::Wall:material=hull;break;
          case BuildingGraphPart::Trim:material=trim;break;
          case BuildingGraphPart::DoorLeaf:material=dark;break;
          case BuildingGraphPart::Roof:material=trim;break;
          case BuildingGraphPart::RoofPanel:material=dark;break;
        }
        addBox(material,solid.x0,solid.y0,solid.z0,
               solid.x1,solid.y1,solid.z1);
    }
    for(const auto& socket:graph.sockets){
        BuildingFeatureAnchor anchor;
        anchor.kind=socket.kind;anchor.side=socket.side;
        anchor.index=socket.repeatIndex;anchor.featureIndex=socket.featureIndex;
        anchor.socketId=socket.id;anchor.width=socket.width;
        anchor.height=socket.height;anchor.depth=socket.depth;
        anchor.open=socket.open;
        anchor.outside={fcoord(socket.x),fcoord(socket.y),fcoord(socket.z)};
        result.anchors.push_back(anchor);
        if(anchor.kind!=BuildingFeatureKind::SlidingRoof)
            result.building.gates.push_back(anchor.outside);
    }
    result.graph=std::move(graph);
    // Geometry can rotate without changing the recipe or material identity.
    const auto ox=float(origin.x.toDouble()),oy=float(origin.y.toDouble()),
               oz=float(origin.z.toDouble());
    bool first=true;
    for(auto& v:mesh.v){
        const float x=v.pos[0],z=v.pos[2],nx=v.nrm[0],nz=v.nrm[2];
        switch(yaw){
          case 1:v.pos[0]=D-z;v.pos[2]=x;v.nrm[0]=-nz;v.nrm[2]=nx;break;
          case 2:v.pos[0]=W-x;v.pos[2]=D-z;v.nrm[0]=-nx;v.nrm[2]=-nz;break;
          case 3:v.pos[0]=z;v.pos[2]=W-x;v.nrm[0]=nz;v.nrm[2]=-nx;break;
          default:break;
        }
        v.pos[0]+=ox;v.pos[1]+=oy;v.pos[2]+=oz;
        for(int k=0;k<3;++k){
            if(first||v.pos[k]<result.building.mesh.boundsMin[k])
                result.building.mesh.boundsMin[k]=v.pos[k];
            if(first||v.pos[k]>result.building.mesh.boundsMax[k])
                result.building.mesh.boundsMax[k]=v.pos[k];
        }
        first=false;
    }
    for(auto& b:result.building.boxes){
        auto a=buildingDetail::turn(b.min,yaw,fcoord(W),fcoord(D))+origin;
        auto c=buildingDetail::turn(b.max,yaw,fcoord(W),fcoord(D))+origin;
        b={{fxMin(a.x,c.x),fxMin(a.y,c.y),fxMin(a.z,c.z)},
           {fxMax(a.x,c.x),fxMax(a.y,c.y),fxMax(a.z,c.z)}};
    }
    for(auto& gate:result.building.gates)
        gate=buildingDetail::turn(gate,yaw,fcoord(W),fcoord(D))+origin;
    for(auto& a:result.anchors)
        a.outside=buildingDetail::turn(a.outside,yaw,fcoord(W),fcoord(D))+origin;
    result.building.mesh.vertices=std::move(mesh.v);
    for(size_t i=0;i<mesh.perMaterial.size();++i)
        if(!mesh.perMaterial[i].empty())
            result.building.mesh.parts.push_back({int(i),std::move(mesh.perMaterial[i])});
    result.building.constructionSize={W,H,D};
    return result;
}

} // namespace aiverse::lib::systems
