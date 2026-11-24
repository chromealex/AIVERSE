#pragma once
// Building FUNCTIONALITY is a recipe of independent properties, never a genre,
// setting or role switch. Style supplies architectural presentation inside the same structural solids.
// This path builds a genuinely apertured wall/roof shell with separate doors,
// collision and sliding roof panels; it does not dress a closed existing hull.
// CPU-only; callable by both native Studio and shipping game code.
#include "BuildingStructureGraph.h"
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
    if(json.has("roofRise"))recipe.roofRise=number(json["roofRise"],path+".roofRise");
    if(json.has("roofProfile")){
        if(!json["roofProfile"].isString())throw JsonError(path+".roofProfile: expected string");
        bool found=false;
        for(int i=0;i<=int(FunctionalRoofProfile::Pagoda);++i){const auto p=FunctionalRoofProfile(i);if(json["roofProfile"].str()==functionalRoofProfileName(p)){recipe.roofProfile=p;found=true;break;}}
        if(!found)throw JsonError(path+".roofProfile: unknown profile");
    }
    if(json.has("seed")){
        const auto& value=json["seed"];
        if(!value.isNumber())throw JsonError(path+".seed: expected integer");
        const double f=value.toDouble(-1);
        if(!std::isfinite(f)||f<0.||std::floor(f)!=f||f>2147483647.)
            throw JsonError(path+".seed: expected integer from 0 to 2147483647");
        recipe.seed=uint64_t(f);
    }
    if(json.has("floors")){
        const double n=json["floors"].toDouble(-1);
        if(!json["floors"].isNumber()||n<1||n>8||std::floor(n)!=n)
            throw JsonError(path+".floors: expected integer 1..8");
        recipe.floors=uint32_t(n);
    }
    for(const char* key:{"roomsX","roomsZ"})if(json.has(key)){
        const double n=json[key].toDouble(-1);
        if(!json[key].isNumber()||n<1||n>6||std::floor(n)!=n)
            throw JsonError(path+"."+key+": expected integer 1..6");
        if(std::string(key)=="roomsX")recipe.roomsX=uint32_t(n);
        else recipe.roomsZ=uint32_t(n);
    }
    if(json.has("footprint")){
        const auto& rows=json["footprint"];
        if(!rows.isArray()||rows.size()>6)throw JsonError(path+".footprint: expected up to six binary rows");
        for(const auto& row:rows.items()){
            if(!row.isString()||row.str().size()>6)throw JsonError(path+".footprint: invalid row");
            recipe.footprint.push_back(row.str());
        }
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
            else if(kind=="window")f.kind=BuildingFeatureKind::Window;
            else if(kind=="stairs")f.kind=BuildingFeatureKind::Stairs;
            else if(kind=="elevator")f.kind=BuildingFeatureKind::Elevator;
            else throw JsonError(label+".kind: unknown building property '"+kind+"'");
            if(f.kind==BuildingFeatureKind::LargeGate){f.width=5.f;f.height=4.5f;}
            else if(f.kind==BuildingFeatureKind::SmallGate){f.width=2.6f;f.height=2.8f;}
            else if(f.kind==BuildingFeatureKind::SlidingRoof){f.width=4.f;f.depth=4.f;}
            else if(f.kind==BuildingFeatureKind::Window){f.width=1.6f;f.height=1.1f;f.sill=0.9f;}
            else if(f.kind==BuildingFeatureKind::Stairs){f.width=2.6f;f.depth=6.6f;f.height=0.f;}
            else if(f.kind==BuildingFeatureKind::Elevator){f.width=2.f;f.depth=2.f;f.height=0.f;}
            if(j.has("side")){
                if(!j["side"].isString())throw JsonError(label+".side: expected string");
                const auto side=j["side"].str();
                if(side=="front")f.side=BuildingFeatureSide::Front;
                else if(side=="back")f.side=BuildingFeatureSide::Back;
                else if(side=="left")f.side=BuildingFeatureSide::Left;
                else if(side=="right")f.side=BuildingFeatureSide::Right;
                else throw JsonError(label+".side: expected front, back, left or right");
            }
            if(j.has("wallLine")){
                const auto& value=j["wallLine"];const double n=value.toDouble(-2);
                if(!value.isNumber()||n<-1||n>6||std::floor(n)!=n)throw JsonError(label+".wallLine: expected integer -1..6");
                f.wallLine=int(n);
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
            if(j.has("sill"))f.sill=number(j["sill"],label+".sill");
            if(j.has("floor")){
                const double n=j["floor"].toDouble(-1);
                if(!j["floor"].isNumber()||n<0||n>7||std::floor(n)!=n)
                    throw JsonError(label+".floor: expected integer 0..7");
                f.floor=uint32_t(n);
            }
            recipe.features.push_back(f);
        }
    }
    const auto error=validateFunctionalRecipe(recipe);
    if(!error.empty())throw JsonError(path+": "+error);
    return recipe;
}

// Style-specific relief is contained in the original graph-owned wall.
// No decoration projects into apertures or navigable rooms. The conservative
// collision boundary differs from the deepest visible surface by at most 9 cm.
inline bool dressFunctionalWall(buildingStyleDetail::Mesher& mesh,
    const BuildingStyle& style,const BuildingGraphSolid& s,float W,float D,uint64_t seed,int hull,int trim){
    const auto* residence=style.structureKind("residence");
    if(!residence||residence->body==StructureBody::Armour||s.part!=BuildingGraphPart::Wall)return false;
    const auto profile=residence->body;
    constexpr float e=.001f;
    int side=-1;
    if(s.z0<e&&s.z1<.37f)side=0;
    else if(s.z1>D-e&&s.z0>D-.37f)side=1;
    else if(s.x0<e&&s.x1<.37f)side=2;
    else if(s.x1>W-e&&s.x0>W-.37f)side=3;
    if(side<0)return false;
    const float a=side<2?s.x0:s.z0,b=side<2?s.x1:s.z1;
    const float span=b-a,h=s.y1-s.y0,thickness=side<2?s.z1-s.z0:s.x1-s.x0;
    if(span<.45f||h<.35f||thickness<.19f)return false;
    const float recess=std::min(.09f,thickness*.25f);
    const int accent=style.panels.accent>=0?style.panels.accent:trim;
    const int face=style.panels.material>=0?style.panels.material:hull;
    const int frame=style.window.frame>=0?style.window.frame:trim;
    auto piece=[&](int material,float l,float r,float bottom,float top,float depth){
        l=std::clamp(l,a,b);r=std::clamp(r,a,b);
        bottom=std::clamp(bottom,s.y0,s.y1);top=std::clamp(top,s.y0,s.y1);
        if(r-l<.002f||top-bottom<.002f)return;
        const float near=std::clamp(depth,0.f,recess-.005f),far=recess+.012f;
        switch(side){
          case 0:mesh.box(material,l,bottom,s.z0+near,r,top,s.z0+far);break;
          case 1:mesh.box(material,l,bottom,s.z1-far,r,top,s.z1-near);break;
          case 2:mesh.box(material,s.x0+near,bottom,l,s.x0+far,top,r);break;
          default:mesh.box(material,s.x1-far,bottom,l,s.x1-near,top,r);break;
        }
    };
    auto core=s;
    if(side==0)core.z0+=recess;else if(side==1)core.z1-=recess;
    else if(side==2)core.x0+=recess;else core.x1-=recess;
    mesh.box(hull,core.x0,core.y0,core.z0,core.x1,core.y1,core.z1);
    const float edge=std::min(.12f,span*.18f);
    piece(frame,a,a+edge,s.y0,s.y1,0);piece(frame,b-edge,b,s.y0,s.y1,0);
    piece(frame,a,b,s.y0,s.y0+edge,0);piece(frame,a,b,s.y1-edge,s.y1,0);
    const int cols=std::clamp(int(span/((profile==StructureBody::Stacked||profile==StructureBody::Facet)?1.35f:2.5f)),1,24);
    const int rows=std::clamp(int(h/2.8f),1,12);
    Random detail(seed^uint64_t(std::llround(a*1000))^uint64_t(side+1)*0x9e3779b9ULL,0x666163616465ULL);
    for(int row=0;row<rows;++row)for(int col=0;col<cols;++col){
        const float x0=a+span*col/cols,x1=a+span*(col+1)/cols;
        const float y0=s.y0+h*row/rows,y1=s.y0+h*(row+1)/rows;
        const float w=x1-x0,ht=y1-y0,m=std::min({.10f,w*.13f,ht*.13f});
        if(w<.22f||ht<.22f)continue;
        piece(face,x0+m,x1-m,y0+m,y1-m,recess*.55f);
        piece(frame,x0,x0+m,y0,y1,0);
        switch(profile){
          case StructureBody::Gable:case StructureBody::Pagoda:
            piece(frame,x0,x1,y1-m,y1,0);
            if(profile==StructureBody::Pagoda)piece(accent,x0+m,x1-m,y0+ht*.7f,y0+ht*.8f,0);
            break;
          case StructureBody::Spire:case StructureBody::Pyramid:case StructureBody::Ziggurat:
            for(int q=1;q<=3;++q)piece(trim,x0,x1,y0+ht*q/4,y0+ht*q/4+m*.6f,0);
            if(profile==StructureBody::Spire)for(int q=0;q<4;++q){
                const float half=w*(.4f-.075f*q);
                piece(accent,(x0+x1)*.5f-half,(x0+x1)*.5f+half,y0+ht*(.32f+.0325f*q),y0+ht*(.3525f+.0325f*q),0);
            }
            break;
          case StructureBody::Barrel:case StructureBody::Dome:case StructureBody::Bulb:
            piece(trim,x0,x1,y0+ht*.46f,y0+ht*.46f+m,0);
            if(profile!=StructureBody::Barrel){
                const float rad=std::min(w,ht)*.28f;
                for(int q=0;q<8;++q){
                    const float angle=6.2831853f*q/8;
                    const float px=(x0+x1)*.5f+rad*std::cos(angle),py=(y0+y1)*.5f+rad*std::sin(angle);
                    piece(accent,px-m*.5f,px+m*.5f,py-m*.5f,py+m*.5f,0);
                }
            }
            break;
          case StructureBody::Stacked:case StructureBody::Facet:case StructureBody::Flat:
            piece(trim,x0,x1,y1-m,y1,0);
            if(profile==StructureBody::Stacked&&detail.below(3)==0)piece(accent,x0+m,x1-m,y0+ht*.2f,y0+ht*.35f,0);
            if(profile==StructureBody::Facet)piece(accent,x0+w*.3f,x0+w*.42f,y0+m,y1-m,0);
            break;
          case StructureBody::Greenhouse:case StructureBody::Terrace:
            piece(trim,x0,x1,y0+ht*.22f,y0+ht*.22f+m,0);
            piece(accent,x0+w*.22f,x1-w*.22f,y0+ht*.23f,y0+ht*.38f,0);break;
          case StructureBody::Shed:
            if(detail.below(2)==0)piece(accent,x0+m,x1-m,y0+ht*.45f,y0+ht*.45f+m,0);break;
          default:break;
        }
    }
    return true;
}

inline bool dressFunctionalRoofCell(buildingStyleDetail::Mesher& mesh,const FunctionalBuildingRecipe& recipe,
    const BuildingGraphSolid& s,int material){
    using P3=buildingStyleDetail::P3;
    const auto profile=recipe.roofProfile;
    if(s.part!=BuildingGraphPart::Roof||profile==FunctionalRoofProfile::Flat||
       profile==FunctionalRoofProfile::Terrace||profile==FunctionalRoofProfile::Pagoda||
       s.y0<functionalCoreHeight(recipe)-.0001f)return false;
    auto top=[&](float x,float z)->P3{
        const float f=functionalRoofFraction(profile,x*2.f/recipe.width-1.f,z*2.f/recipe.depth-1.f);
        return {x,std::clamp(s.y0+f*recipe.roofRise,s.y0,s.y1),z};
    };
    const std::array<P3,4> a={P3{s.x0,s.y0,s.z0},P3{s.x1,s.y0,s.z0},P3{s.x1,s.y0,s.z1},P3{s.x0,s.y0,s.z1}};
    const std::array<P3,4> b={top(s.x0,s.z0),top(s.x1,s.z0),top(s.x1,s.z1),top(s.x0,s.z1)};
    // Split the top into planar triangles (curved profiles are not a planar quad).
    mesh.poly(material,{b[0],b[1],b[2]},{0,1,0});
    mesh.poly(material,{b[0],b[2],b[3]},{0,1,0});
    const std::array<P3,4> outward={P3{0,0,-1},P3{1,0,0},P3{0,0,1},P3{-1,0,0}};
    for(int i=0;i<4;++i){const int j=(i+1)%4;
        mesh.poly(material,{a[i],a[j],b[j],b[i]},outward[i]);}
    return true;
}

inline FunctionalBuildingResult generateFunctionalBuilding(
    const BuildingStyle& style,const FunctionalBuildingRecipe& recipe,
    Vec3 origin={},uint32_t yaw=0,
    bool includeInitialLiftPlatform=true,
    bool includeDynamicPanels=true){
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
                    float x1,float y1,float z1,bool collider=true,bool visible=true){
        if(visible)mesh.box(material,x0,y0,z0,x1,y1,z1);
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
        // Dynamic gameplay spawn: do not bake the movable elevator platform
        // into the static mesh or static AABB collision. Studio/GLB preview
        // defaults to including the initial platform. The runtime owner
        // creates a separate movable model and collider using the pose
        // exported by BuildingLiftController.
        if(!includeInitialLiftPlatform&&
           solid.part==BuildingGraphPart::ElevatorPlatform)continue;
        // Runtime uses a dedicated authoritative SpatialWorld owner for
        // movable gates and roof panels. Studio preview still bakes them.
        if(!includeDynamicPanels&&
           (solid.part==BuildingGraphPart::DoorLeaf||
            solid.part==BuildingGraphPart::RoofPanel))continue;
        int material=hull;
        switch(solid.part){
          case BuildingGraphPart::Floor:material=ground;break;
          case BuildingGraphPart::Wall:material=hull;break;
          case BuildingGraphPart::Trim:material=trim;break;
          case BuildingGraphPart::DoorLeaf:material=dark;break;
          case BuildingGraphPart::Roof:material=trim;break;
          case BuildingGraphPart::RoofPanel:material=dark;break;
          case BuildingGraphPart::WindowPane:material=style.window.glass>=0?style.window.glass:trim;break;
          case BuildingGraphPart::Partition:material=style.interior>=0?style.interior:hull;break;
          case BuildingGraphPart::StairTread:material=sm.plate>=0?sm.plate:hull;break;
          case BuildingGraphPart::StairLanding:material=sm.pad>=0?sm.pad:trim;break;
          case BuildingGraphPart::ElevatorPlatform:material=sm.plate>=0?sm.plate:trim;break;
          case BuildingGraphPart::ElevatorGuide:material=sm.dark>=0?sm.dark:hull;break;
        }
        const bool dressed=dressFunctionalWall(mesh,style,solid,W,D,recipe.seed,hull,trim)||
            dressFunctionalRoofCell(mesh,recipe,solid,style.roof>=0?style.roof:material);
        addBox(material,solid.x0,solid.y0,solid.z0,
               solid.x1,solid.y1,solid.z1,true,!dressed);
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
        if(anchor.kind!=BuildingFeatureKind::SlidingRoof&&anchor.kind!=BuildingFeatureKind::Window&&
           anchor.kind!=BuildingFeatureKind::Stairs&&anchor.kind!=BuildingFeatureKind::Elevator)
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
