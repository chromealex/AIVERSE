#pragma once
// Building FUNCTIONALITY is a recipe of independent properties, never a genre,
// setting or role ("barracks"/"factory") switch. Style supplies only surfaces.
// This path builds a genuinely apertured wall/roof shell with separate doors,
// collision and sliding roof panels; it does not dress a closed existing hull.
// CPU-only; callable by both native Studio and shipping game code.
#include "BuildingStructure.h"
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

enum class BuildingFeatureKind : uint8_t {
    LargeGate, SmallGate, PersonnelDoor, SlidingRoof
};
enum class BuildingFeatureSide : uint8_t { Front, Back, Left, Right };

struct BuildingFeature {
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    BuildingFeatureSide side=BuildingFeatureSide::Front;
    uint32_t count=1;
    // Aperture dimensions are actual metres, NOT percentages of the style.
    // SlidingRoof uses width/depth for its clear aperture.
    float width=1.2f,height=2.4f,depth=0.f;
    float open=0.f; // 0 = doors/roof closed, 1 = panels completely retracted
};
struct FunctionalBuildingRecipe {
    float width=16.f,height=8.f,depth=12.f; // outer bounds in world metres
    uint64_t seed=1;
    std::vector<BuildingFeature> features;
};
struct BuildingFeatureAnchor {
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    BuildingFeatureSide side=BuildingFeatureSide::Front;
    uint32_t index=0;
    Vec3 outside{}; // ground access point / roof hatch centre
    float width=0,height=0,depth=0,open=0;
};
struct FunctionalBuildingResult {
    StyledStructure building;
    std::vector<BuildingFeatureAnchor> anchors;
    std::string error;
    explicit operator bool() const noexcept {
        return error.empty()&&!building.mesh.vertices.empty();
    }
};

inline const char* buildingFeatureName(BuildingFeatureKind kind) {
    switch(kind){
      case BuildingFeatureKind::LargeGate: return "large-gate";
      case BuildingFeatureKind::SmallGate: return "small-gate";
      case BuildingFeatureKind::PersonnelDoor: return "personnel-door";
      case BuildingFeatureKind::SlidingRoof: return "sliding-roof";
    }
    return "unknown";
}
inline const char* buildingFeatureSideName(BuildingFeatureSide side) {
    switch(side){
      case BuildingFeatureSide::Front:return "front";
      case BuildingFeatureSide::Back:return "back";
      case BuildingFeatureSide::Left:return "left";
      case BuildingFeatureSide::Right:return "right";
    }
    return "unknown";
}
// Strict data-level validation; works without any selected style/setting.
inline std::string validateFunctionalRecipe(const FunctionalBuildingRecipe& recipe){
    const auto finite=[](float v){return std::isfinite(v);};
    if(!finite(recipe.width)||!finite(recipe.height)||!finite(recipe.depth)||
       recipe.width<8.f||recipe.width>160.f||recipe.depth<8.f||recipe.depth>160.f||
       recipe.height<4.f||recipe.height>80.f)
        return "Functional building W/H/D must fit 8..160 / 4..80 / 8..160 metres";
    if(recipe.features.size()>24)return "Too many building properties (maximum 24)";
    uint32_t roof=0,portals=0;
    for(const auto& f:recipe.features){
        if(!finite(f.width)||!finite(f.height)||!finite(f.depth)||!finite(f.open)||
           f.open<0.f||f.open>1.f||f.count<1||f.count>8)
            return "Feature parameters must be finite; open is 0..1, count 1..8";
        if(f.kind==BuildingFeatureKind::SlidingRoof){
            if(++roof>1||f.count!=1||f.width<1.f||f.depth<1.f||
               f.width>recipe.width*0.48f||f.depth>recipe.depth*0.75f)
                return "Sliding roof requires one aperture fitting inside the roof";
            continue;
        }
        portals+=f.count;
        if(portals>16)return "Too many wall entrances (maximum 16)";
        const float maxWidth=f.kind==BuildingFeatureKind::LargeGate?20.f:
                             f.kind==BuildingFeatureKind::SmallGate?8.f:3.f;
        if(f.width<0.85f||f.width>maxWidth||f.height<2.f||f.height>recipe.height-0.6f)
            return "Door/gate dimensions do not fit the building or feature kind";
    }
    return {};
}

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
    const auto invalid=validateFunctionalRecipe(recipe);
    if(!invalid.empty())return reject(invalid);
    if(yaw>3)return reject("Invalid building yaw (expected 0..3)");

    const float W=recipe.width,H=recipe.height,D=recipe.depth;
    constexpr float floorTop=0.24f,wall=0.36f,roofThick=0.28f;
    const auto& sm=style.structure;
    const int hull=sm.hull>=0?sm.hull:style.shell;
    const int trim=sm.plate>=0?sm.plate:hull;
    const int dark=sm.dark>=0?sm.dark:hull;
    const int ground=sm.pad>=0?sm.pad:style.interior;
    const int light=sm.glow>=0?sm.glow:trim;
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
    // Foundation and floor are real solids. They do not block doors above floorTop.
    addBox(ground,0,0,0,W,floorTop,D);

    // A functional property is independent of a style: all entrances of a
    // given side are packed in one deterministic row with non-overlap.
    struct Aperture {
        BuildingFeature value;
        float start=0,end=0;
        uint32_t sequence=0;
    };
    std::array<std::vector<Aperture>,4> apertures;
    const BuildingFeature* roof=nullptr;
    for(const auto& f:recipe.features){
        if(f.kind==BuildingFeatureKind::SlidingRoof){roof=&f;continue;}
        auto& row=apertures[size_t(f.side)];
        for(uint32_t i=0;i<f.count;++i)
            row.push_back({f,0,0,i});
    }
    for(size_t side=0;side<4;++side){
        auto& row=apertures[side];
        const float len=(side==0||side==1)?W:D;
        const float margin=0.72f;
        float needed=0;
        for(auto& f:row)needed+=f.value.width;
        const float gap=(len-2*margin-needed)/float(row.size()+1);
        if(!row.empty()&&gap<0.35f)
            return reject("Entrances on "+std::string(buildingFeatureSideName(BuildingFeatureSide(side)))+
                          " wall cannot fit; reduce gate widths or count");
        float cursor=margin+gap;
        for(auto& f:row){
            f.start=cursor;f.end=cursor+f.value.width;
            cursor=f.end+gap;
        }
    }
    auto wallSection=[&](BuildingFeatureSide side,float a,float b,float y0,float y1,
                         int material,bool collider=true){
        if(b-a<0.0001f||y1-y0<0.0001f)return;
        switch(side){
          case BuildingFeatureSide::Front:addBox(material,a,y0,0,b,y1,wall,collider);break;
          case BuildingFeatureSide::Back:addBox(material,a,y0,D-wall,b,y1,D,collider);break;
          case BuildingFeatureSide::Left:addBox(material,0,y0,a,wall,y1,b,collider);break;
          case BuildingFeatureSide::Right:addBox(material,W-wall,y0,a,W,y1,b,collider);break;
        }
    };
    auto fcoord=[](float n){return Fixed::fromRatio(int64_t(std::llround(double(n)*1000.)),1000);};
    // Entirely segmented vertical walls. No invisible closed hull remains
    // behind an aperture; collision is segmented identically to the mesh.
    for(size_t idx=0;idx<4;++idx){
        auto side=BuildingFeatureSide(idx);
        const auto& row=apertures[idx];
        const float length=(idx==0||idx==1)?W:D;
        float cursor=0;
        for(const auto& aperture:row){
            const auto& f=aperture.value;
            wallSection(side,cursor,aperture.start,floorTop,H,hull);
            const float y1=floorTop+f.height;
            wallSection(side,aperture.start,aperture.end,y1,H,hull);
            const float frame=0.14f;
            wallSection(side,aperture.start-frame,aperture.start,floorTop,y1,trim);
            wallSection(side,aperture.end,aperture.end+frame,floorTop,y1,trim);
            wallSection(side,aperture.start-frame,aperture.end+frame,
                        y1,y1+0.15f,trim);
            // Dual sliding leaves; at open=1 the passage is completely clear.
            // These are separate physical solids and disappear from the
            // aperture as they retract into the adjoining wall.
            const float mid=(aperture.start+aperture.end)*0.5f;
            const float half=(aperture.end-aperture.start)*0.5f;
            const float leftShift=-half*f.open,rightShift=half*f.open;
            if(f.open<0.9999f){
                wallSection(side,aperture.start+leftShift,mid+leftShift,
                            floorTop+0.05f,y1-0.05f,dark);
                wallSection(side,mid+rightShift,aperture.end+rightShift,
                            floorTop+0.05f,y1-0.05f,dark);
            }
            // Surface/semantic anchor for gameplay triggers or animators.
            float px=(aperture.start+aperture.end)*0.5f,pz=-1.f;
            if(side==BuildingFeatureSide::Back)pz=D+1.f;
            if(side==BuildingFeatureSide::Left){pz=px;px=-1.f;}
            if(side==BuildingFeatureSide::Right){pz=px;px=W+1.f;}
            BuildingFeatureAnchor a;
            a.kind=f.kind;a.side=side;a.index=aperture.sequence;
            a.width=f.width;a.height=f.height;a.open=f.open;
            a.outside={fcoord(px),Fixed{},fcoord(pz)};
            result.anchors.push_back(a);
            result.building.gates.push_back(a.outside);
            cursor=aperture.end;
        }
        wallSection(side,cursor,length,floorTop,H,hull);
    }
    // Real roof cut-out, not a roof prop layered over an unbroken slab.
    const float roofBase=H-roofThick;
    if(roof){
        const float x0=(W-roof->width)*0.5f,x1=(W+roof->width)*0.5f;
        const float z0=(D-roof->depth)*0.5f,z1=(D+roof->depth)*0.5f;
        addBox(trim,0,roofBase,0,x0,H,D);
        addBox(trim,x1,roofBase,0,W,H,D);
        addBox(trim,x0,roofBase,0,x1,H,z0);
        addBox(trim,x0,roofBase,z1,x1,H,D);
        // Two real movable panel bodies slide outward across the roof ring.
        const float half=roof->width*0.5f,slide=half*roof->open;
        // Panels are not destroyed at fully open: they remain visible and
        // collide where they retract over the solid side roof. Their positions
        // can be recomputed at any animation fraction without filling the void.
        addBox(dark,x0-slide,roofBase,z0,x0+half-slide,H,z1);
        addBox(dark,x0+half+slide,roofBase,z0,x1+slide,H,z1);
        BuildingFeatureAnchor a;
        a.kind=BuildingFeatureKind::SlidingRoof;a.width=roof->width;
        a.depth=roof->depth;a.open=roof->open;
        a.outside={fcoord(W*0.5f),fcoord(H),fcoord(D*0.5f)};
        result.anchors.push_back(a);
    }else addBox(trim,0,roofBase,0,W,H,D);

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
