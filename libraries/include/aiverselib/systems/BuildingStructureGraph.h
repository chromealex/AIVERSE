#pragma once
// CPU-only functional layout graph: exact openings, structural wall segments,
// movable panels, and semantic sockets. No style/material, genre, editor, ECS
// or renderer dependency. Distances are metres. Side coordinates run +X on
// front/back and +Z on left/right (building-local axes).
#include "BuildingStructure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
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
    float width=1.2f,height=2.4f,depth=0.f;
    float open=0.f;
    // Negative means automatic placement. For wall features, position is the
    // group centre along that wall; count>1 repeats with 'spacing' clear gap.
    // For roof: position = X centre, positionZ = Z centre.
    float position=-1.f,positionZ=-1.f,spacing=0.35f;
};
struct FunctionalBuildingRecipe {
    float width=16.f,height=8.f,depth=12.f;
    uint64_t seed=1;
    std::vector<BuildingFeature> features;
};
enum class BuildingGraphPart : uint8_t {
    Floor, Wall, Trim, DoorLeaf, Roof, RoofPanel
};
struct BuildingGraphSolid {
    BuildingGraphPart part=BuildingGraphPart::Wall;
    float x0=0,y0=0,z0=0,x1=0,y1=0,z1=0;
    uint32_t featureIndex=UINT32_MAX;
};
struct BuildingGraphSocket {
    std::string id; // stable within one recipe: entrance/<feature>/<copy> or roof/<feature>
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    BuildingFeatureSide side=BuildingFeatureSide::Front;
    uint32_t featureIndex=0,repeatIndex=0;
    float x=0,y=0,z=0;
    float width=0,height=0,depth=0,open=0;
};
struct BuildingStructureGraph {
    std::vector<BuildingGraphSolid> solids;
    std::vector<BuildingGraphSocket> sockets;
    std::string error;
    explicit operator bool() const noexcept {return error.empty();}
};
inline const char* buildingFeatureName(BuildingFeatureKind kind) {
    switch(kind){
      case BuildingFeatureKind::LargeGate:return "large-gate";
      case BuildingFeatureKind::SmallGate:return "small-gate";
      case BuildingFeatureKind::PersonnelDoor:return "personnel-door";
      case BuildingFeatureKind::SlidingRoof:return "sliding-roof";
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
inline std::string validateFunctionalRecipe(const FunctionalBuildingRecipe& recipe) {
    const auto finite=[](float v){return std::isfinite(v);};
    if(!finite(recipe.width)||!finite(recipe.height)||!finite(recipe.depth)||
       recipe.width<8.f||recipe.width>160.f||recipe.depth<8.f||recipe.depth>160.f||
       recipe.height<4.f||recipe.height>80.f)
        return "Functional building W/H/D must fit 8..160 / 4..80 / 8..160 metres";
    if(recipe.features.size()>24)return "Too many building properties (maximum 24)";
    uint32_t roofs=0,portals=0;
    for(const auto& f:recipe.features){
        if(!finite(f.width)||!finite(f.height)||!finite(f.depth)||!finite(f.open)||
           !finite(f.position)||!finite(f.positionZ)||!finite(f.spacing)||
           f.open<0||f.open>1||f.count<1||f.count>8||
           f.position < -1.f||f.positionZ < -1.f||f.spacing<0.35f||f.spacing>8.f)
            return "Invalid feature count, placement, spacing or open fraction";
        if(f.kind==BuildingFeatureKind::SlidingRoof){
            if(++roofs>1||f.count!=1||f.width<1||f.depth<1||
               f.width>recipe.width*0.48f||f.depth>recipe.depth*0.75f)
                return "Sliding roof requires one aperture fitting inside the roof";
            if(f.position>=0&&(f.position-f.width*0.5f<0.6f||
                               f.position+f.width*0.5f>recipe.width-0.6f))
                return "Sliding roof X position crosses its structural frame";
            if(f.positionZ>=0&&(f.positionZ-f.depth*0.5f<0.6f||
                                f.positionZ+f.depth*0.5f>recipe.depth-0.6f))
                return "Sliding roof Z position crosses its structural frame";
            continue;
        }
        portals+=f.count;
        if(portals>16)return "Too many wall entrances (maximum 16)";
        const float maxWidth=f.kind==BuildingFeatureKind::LargeGate?20.f:
                             f.kind==BuildingFeatureKind::SmallGate?8.f:3.f;
        if(f.width<0.85f||f.width>maxWidth||f.height<2.f||
           f.height>recipe.height-0.6f)
            return "Door/gate dimensions do not fit the building or feature kind";
        const float length=(f.side==BuildingFeatureSide::Front||
                            f.side==BuildingFeatureSide::Back)?recipe.width:recipe.depth;
        if(f.position>=0){
            const float span=float(f.count)*f.width+float(f.count-1)*f.spacing;
            if(f.position-span*0.5f<0.72f||
               f.position+span*0.5f>length-0.72f)
                return "Explicit wall opening group extends outside the usable wall";
        }
    }
    return {};
}
// Every wall and roof SOLID, door leaf and socket is compiled from one graph.
// Render/physics simply consume the same solids: openings cannot retain an
// unseen closed collider. Sockets remain stable under palette changes.
inline BuildingStructureGraph buildFunctionalStructureGraph(
    const FunctionalBuildingRecipe& recipe) {
    BuildingStructureGraph graph;
    graph.error=validateFunctionalRecipe(recipe);
    if(!graph.error.empty())return graph;
    const float W=recipe.width,H=recipe.height,D=recipe.depth;
    constexpr float floorTop=0.24f,wall=0.36f,roofThickness=0.28f;
    auto solid=[&](BuildingGraphPart p,float x0,float y0,float z0,
                   float x1,float y1,float z1,uint32_t owner=UINT32_MAX){
        if(x1-x0>0.0001f&&y1-y0>0.0001f&&z1-z0>0.0001f)
            graph.solids.push_back({p,x0,y0,z0,x1,y1,z1,owner});
    };
    solid(BuildingGraphPart::Floor,0,0,0,W,floorTop,D);
    struct Hole {
        uint32_t feature=0,copy=0;
        BuildingFeature value;
        float start=0,end=0;
        bool placed=false;
    };
    std::array<std::vector<Hole>,4> walls;
    const BuildingFeature* roof=nullptr;
    uint32_t roofIndex=0;
    for(uint32_t i=0;i<recipe.features.size();++i){
        const auto& f=recipe.features[i];
        if(f.kind==BuildingFeatureKind::SlidingRoof){roof=&f;roofIndex=i;continue;}
        auto& row=walls[size_t(f.side)];
        for(uint32_t j=0;j<f.count;++j)row.push_back({i,j,f});
    }
    for(uint32_t side=0;side<4;++side){
        auto& row=walls[side];
        const float length=side<2?W:D,margin=0.72f,clearance=0.35f;
        // Place exact groups first; reserve their full footprint including
        // minimum structural piers before packing automatic properties.
        for(auto& hole:row)if(hole.value.position>=0){
            const auto& f=hole.value;
            const float span=float(f.count)*f.width+float(f.count-1)*f.spacing;
            hole.start=f.position-span*0.5f+float(hole.copy)*(f.width+f.spacing);
            hole.end=hole.start+f.width;
            hole.placed=true;
        }
        auto byStart=[](const Hole& a,const Hole& b){
            if(a.start!=b.start)return a.start<b.start;
            if(a.feature!=b.feature)return a.feature<b.feature;
            return a.copy<b.copy;
        };
        std::vector<Hole> occupied;
        for(const auto& hole:row)if(hole.placed)occupied.push_back(hole);
        std::sort(occupied.begin(),occupied.end(),byStart);
        for(size_t i=1;i<occupied.size();++i)
            if(occupied[i].start-occupied[i-1].end<clearance-0.0001f){
                graph.error="Explicit openings overlap or leave too little pier on "+
                            std::string(buildingFeatureSideName(BuildingFeatureSide(side)));
                return graph;
            }
        // When all are automatic, preserve the older evenly spaced layout.
        if(occupied.empty()&&!row.empty()){
            float needed=0;
            for(const auto& hole:row)needed+=hole.value.width;
            const float gap=(length-2*margin-needed)/float(row.size()+1);
            if(gap<clearance){
                graph.error="Entrances cannot fit on "+
                            std::string(buildingFeatureSideName(BuildingFeatureSide(side)));
                return graph;
            }
            float cursor=margin+gap;
            for(auto& hole:row){hole.start=cursor;hole.end=cursor+hole.value.width;
                hole.placed=true;cursor=hole.end+gap;}
        }else{
            // Mixed automatic/exact: deterministic first-fit into the free
            // intervals. No auto feature can displace an explicit socket.
            for(auto& hole:row){
                if(hole.placed)continue;
                float cursor=margin;
                bool inserted=false;
                std::sort(occupied.begin(),occupied.end(),byStart);
                for(size_t j=0;j<=occupied.size();++j){
                    const float end=j<occupied.size()?occupied[j].start-clearance:length-margin;
                    if(cursor+hole.value.width<=end+0.0001f){
                        hole.start=cursor;hole.end=cursor+hole.value.width;
                        hole.placed=true;occupied.push_back(hole);inserted=true;break;
                    }
                    if(j<occupied.size())
                        cursor=std::max(cursor,occupied[j].end+clearance);
                }
                if(!inserted){
                    graph.error="No free wall span for automatic opening on "+
                                std::string(buildingFeatureSideName(BuildingFeatureSide(side)));
                    return graph;
                }
            }
        }
        std::sort(row.begin(),row.end(),byStart);
        const auto wallSection=[&](BuildingGraphPart type,float a,float b,
                                    float bottom,float top,uint32_t owner=UINT32_MAX){
            if(b-a<0.0001f||top-bottom<0.0001f)return;
            switch(BuildingFeatureSide(side)){
              case BuildingFeatureSide::Front:solid(type,a,bottom,0,b,top,wall,owner);break;
              case BuildingFeatureSide::Back:solid(type,a,bottom,D-wall,b,top,D,owner);break;
              case BuildingFeatureSide::Left:solid(type,0,bottom,a,wall,top,b,owner);break;
              case BuildingFeatureSide::Right:solid(type,W-wall,bottom,a,W,top,b,owner);break;
            }
        };
        float cursor=0.f;
        for(const auto& hole:row){
            wallSection(BuildingGraphPart::Wall,cursor,hole.start,floorTop,H);
            const float top=floorTop+hole.value.height;
            wallSection(BuildingGraphPart::Wall,hole.start,hole.end,top,H);
            constexpr float frame=0.14f;
            // Frame and leaf boundaries do not create a closed wall collider.
            wallSection(BuildingGraphPart::Trim,hole.start-frame,hole.start,floorTop,top,hole.feature);
            wallSection(BuildingGraphPart::Trim,hole.end,hole.end+frame,floorTop,top,hole.feature);
            wallSection(BuildingGraphPart::Trim,hole.start-frame,hole.end+frame,top,top+0.15f,hole.feature);
            const float half=hole.value.width*0.5f;
            const float shift=half*hole.value.open;
            if(hole.value.open<0.9999f){
                wallSection(BuildingGraphPart::DoorLeaf,hole.start-shift,
                            hole.start+half-shift,floorTop+0.05f,top-0.05f,hole.feature);
                wallSection(BuildingGraphPart::DoorLeaf,hole.end-half+shift,
                            hole.end+shift,floorTop+0.05f,top-0.05f,hole.feature);
            }
            const float longitudinal=(hole.start+hole.end)*0.5f;
            float px=longitudinal,pz=-1.f;
            if(side==1)pz=D+1.f;
            if(side==2){px=-1.f;pz=longitudinal;}
            if(side==3){px=W+1.f;pz=longitudinal;}
            graph.sockets.push_back({"entrance/"+std::to_string(hole.feature)+"/"+
                std::to_string(hole.copy),hole.value.kind,BuildingFeatureSide(side),
                hole.feature,hole.copy,px,0.f,pz,
                hole.value.width,hole.value.height,0.f,hole.value.open});
            cursor=hole.end;
        }
        wallSection(BuildingGraphPart::Wall,cursor,length,floorTop,H);
    }
    const float roofBase=H-roofThickness;
    if(!roof)solid(BuildingGraphPart::Roof,0,roofBase,0,W,H,D);
    else {
        const float cx=roof->position>=0?roof->position:W*0.5f;
        const float cz=roof->positionZ>=0?roof->positionZ:D*0.5f;
        const float x0=cx-roof->width*0.5f,x1=cx+roof->width*0.5f;
        const float z0=cz-roof->depth*0.5f,z1=cz+roof->depth*0.5f;
        solid(BuildingGraphPart::Roof,0,roofBase,0,x0,H,D);
        solid(BuildingGraphPart::Roof,x1,roofBase,0,W,H,D);
        solid(BuildingGraphPart::Roof,x0,roofBase,0,x1,H,z0);
        solid(BuildingGraphPart::Roof,x0,roofBase,z1,x1,H,D);
        const float half=roof->width*0.5f,travel=half*roof->open;
        solid(BuildingGraphPart::RoofPanel,x0-travel,roofBase,z0,
              x0+half-travel,H,z1,roofIndex);
        solid(BuildingGraphPart::RoofPanel,x0+half+travel,roofBase,z0,
              x1+travel,H,z1,roofIndex);
        graph.sockets.push_back({"roof/"+std::to_string(roofIndex),
            BuildingFeatureKind::SlidingRoof,roof->side,roofIndex,0,
            cx,H,cz,roof->width,0.f,roof->depth,roof->open});
    }
    return graph;
}
} // namespace aiverse::lib::systems
