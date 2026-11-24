#pragma once
// CPU-only functional layout graph: exact openings, structural wall segments,
// movable panels, and semantic sockets. No style/material, genre, editor, ECS
// or renderer dependency. Distances are metres. Side coordinates run +X on
// front/back and +Z on left/right (building-local axes).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::lib::systems {

enum class BuildingFeatureKind : uint8_t {
    LargeGate, SmallGate, PersonnelDoor, SlidingRoof, Window, Stairs, Elevator
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
    uint32_t floor=0;
    float sill=0.9f;
};
struct FunctionalBuildingRecipe {
    float width=16.f,height=8.f,depth=12.f;
    uint64_t seed=1;
    uint32_t floors=1,roomsX=1,roomsZ=1;
    std::vector<BuildingFeature> features;
};
enum class BuildingGraphPart : uint8_t {
    Floor, Wall, Trim, DoorLeaf, Roof, RoofPanel, WindowPane, Partition,
    StairTread, StairLanding, ElevatorPlatform, ElevatorGuide
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
struct BuildingGraphRoom {std::string id;uint32_t floor=0,x=0,z=0;float x0=0,y0=0,z0=0,x1=0,y1=0,z1=0;};
enum class BuildingConnectionKind : uint8_t { Passage, Stair, Elevator };
struct BuildingGraphConnection {std::string id,from,to;uint32_t floor=0;float x=0,y=0,z=0,width=1.2f,height=2.2f;
    BuildingConnectionKind kind=BuildingConnectionKind::Passage;
};
// Authored aperture overlap with a ground-floor room, not a clearance guarantee.
struct BuildingExteriorAperture {std::string socketId,roomId;float start=0,end=0;};
struct BuildingStructureGraph {
    std::vector<BuildingGraphSolid> solids;
    std::vector<BuildingGraphSocket> sockets;
    std::vector<BuildingGraphRoom> rooms;
    std::vector<BuildingGraphConnection> connections;
    std::string error;
    std::vector<BuildingExteriorAperture> exteriorApertures;
    explicit operator bool() const noexcept {return error.empty();}
};
inline const char* buildingFeatureName(BuildingFeatureKind kind) {
    switch(kind){
      case BuildingFeatureKind::LargeGate:return "large-gate";
      case BuildingFeatureKind::SmallGate:return "small-gate";
      case BuildingFeatureKind::PersonnelDoor:return "personnel-door";
      case BuildingFeatureKind::SlidingRoof:return "sliding-roof";
      case BuildingFeatureKind::Window:return "window";
      case BuildingFeatureKind::Stairs:return "stairs";
      case BuildingFeatureKind::Elevator:return "elevator";
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
    if(recipe.floors<1||recipe.floors>8||recipe.roomsX<1||recipe.roomsX>6||recipe.roomsZ<1||recipe.roomsZ>6||recipe.floors*recipe.roomsX*recipe.roomsZ>128)
        return "Invalid floors or room counts";
    const float storey=(recipe.height-0.52f)/float(recipe.floors);
    if(storey<2.8f||recipe.width/float(recipe.roomsX)<2.5f||recipe.depth/float(recipe.roomsZ)<2.5f)
        return "Storey or room dimensions too small";
    uint32_t roofs=0,portals=0,verticals=0;
    for(const auto& f:recipe.features){
        if(!finite(f.width)||!finite(f.height)||!finite(f.depth)||!finite(f.open)||
           !finite(f.position)||!finite(f.positionZ)||!finite(f.spacing)||
           f.open<0||f.open>1||f.count<1||f.count>8||
           (f.position<0.f&&f.position!=-1.f)||
           (f.positionZ<0.f&&f.positionZ!=-1.f)||
           f.spacing<0.35f||f.spacing>8.f)
            return "Invalid feature count, placement, spacing or open fraction";
        if(f.kind==BuildingFeatureKind::SlidingRoof){
            if(f.floor!=0)return "Roof floor must be zero";
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
        if(f.kind==BuildingFeatureKind::Stairs||f.kind==BuildingFeatureKind::Elevator){
            if(++verticals>2||recipe.floors<2||f.count!=1||f.floor!=0)
                return "Vertical cores require 2+ floors, start at floor 0 and have count=1 (max 2)";
            if(f.kind==BuildingFeatureKind::Stairs){
                if(f.width<2.4f||f.depth<5.6f)
                    return "Switchback stairs require at least 2.4m width and 5.6m depth";
                const float storey=(recipe.height-0.52f)/float(recipe.floors);
                // Switchback: two flights, maximum 0.18m rise and minimum
                // 0.24m run per tread, plus a 0.9m turning landing.
                const float climb=storey+0.24f;
                const int perFlight=int(std::ceil(climb/0.18f+0.0001f)+1)/2;
                if(storey>8.5f||
                   f.depth<0.9f+float(perFlight)*0.24f)
                    return "Stair well too short for <=0.18m risers and >=0.24m treads";
            }else if(f.width<1.4f||f.depth<1.4f)
                return "Lift platform requires at least 1.4 by 1.4 metres";
            if(f.width>recipe.width/float(recipe.roomsX)-0.7f||
               f.depth>recipe.depth/float(recipe.roomsZ)-0.7f)
                return "Vertical core must fit completely inside one room cell";
            continue;
        }
        if(f.floor>=recipe.floors)return "Opening floor out of range";
        const bool window=f.kind==BuildingFeatureKind::Window;
        if(!window&&f.floor!=0)return "Entrances only on ground floor";
        if(!std::isfinite(f.sill)||f.sill<0.f||f.sill>4.f)return "Invalid sill";
        portals+=f.count;
        if(portals>32)return "Too many wall apertures";
        const float maxWidth=window?6.f:f.kind==BuildingFeatureKind::LargeGate?20.f:
                             f.kind==BuildingFeatureKind::SmallGate?8.f:3.f;
        if(f.width<0.85f||f.width>maxWidth||
           f.height<(window?0.4f:2.f)||f.height>(window?3.5f:storey-0.6f)||
           (window&&(f.sill<0.65f||f.sill+f.height>storey-0.4f)))
            return "Opening too large for selected storey";
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
    constexpr float floorTop=0.24f,wall=0.36f,roofThickness=0.28f,slabThick=0.24f;
    const float storey=(H-floorTop-roofThickness)/float(recipe.floors);
    auto solid=[&](BuildingGraphPart p,float x0,float y0,float z0,
                   float x1,float y1,float z1,uint32_t owner=UINT32_MAX){
        if(x1-x0>0.0001f&&y1-y0>0.0001f&&z1-z0>0.0001f)
            graph.solids.push_back({p,x0,y0,z0,x1,y1,z1,owner});
    };
    solid(BuildingGraphPart::Floor,0,0,0,W,floorTop,D);
    // Inter-storey slabs are cut AFTER core placement, so neither their
    // visible mesh nor the conservative collider fills a staircase/lift shaft.
    struct Hole {
        uint32_t feature=0,copy=0;
        BuildingFeature value;
        float start=0,end=0;
        bool placed=false;
    };
    std::array<std::vector<Hole>,4> walls;
    const BuildingFeature* roof=nullptr;
    uint32_t roofIndex=0;
    struct VerticalCore {BuildingFeatureKind kind;uint32_t feature;
        float x0,z0,x1,z1;uint32_t roomX,roomZ;};
    std::vector<VerticalCore> cores;
    const float roomW=W/float(recipe.roomsX),roomD=D/float(recipe.roomsZ);
    for(uint32_t i=0;i<recipe.features.size();++i){
        const auto& f=recipe.features[i];
        if(f.kind==BuildingFeatureKind::SlidingRoof){roof=&f;roofIndex=i;continue;}
        if(f.kind==BuildingFeatureKind::Stairs||f.kind==BuildingFeatureKind::Elevator){
            // Unspecified cores use the first available room cell, not the
            // building centre where a partition intersection may be located.
            const uint32_t defaultX=cores.empty()?0:std::min(1u,recipe.roomsX-1);
            const float cx=f.position>=0?f.position:(float(defaultX)+0.5f)*roomW;
            const float cz=f.positionZ>=0?f.positionZ:roomD*0.5f;
            const float x0=cx-f.width*0.5f,x1=cx+f.width*0.5f;
            const float z0=cz-f.depth*0.5f,z1=cz+f.depth*0.5f;
            if(x0<0.35f||x1>W-0.35f||z0<0.35f||z1>D-0.35f){
                graph.error="Vertical core must stay inside the structural perimeter";
                return graph;
            }
            const uint32_t ix=std::min(uint32_t(cx/roomW),recipe.roomsX-1);
            const uint32_t iz=std::min(uint32_t(cz/roomD),recipe.roomsZ-1);
            if(x0<float(ix)*roomW+0.35f||x1>float(ix+1)*roomW-0.35f||
               z0<float(iz)*roomD+0.35f||z1>float(iz+1)*roomD-0.35f){
                graph.error="Vertical core must fit inside one room without crossing partitions";
                return graph;
            }
            for(const auto& previous:cores)
                if(x0<previous.x1+0.35f&&x1>previous.x0-0.35f&&
                   z0<previous.z1+0.35f&&z1>previous.z0-0.35f){
                    graph.error="Vertical cores overlap or lack separating supports";
                    return graph;
                }
            cores.push_back({f.kind,i,x0,z0,x1,z1,ix,iz});
            continue;
        }
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
        // A partially open door leaf must retract into the usable side wall,
        // never protrude beyond the recipe's exact final model envelope.
        for(const auto& hole:row){
            if(hole.value.open>=0.9999f)continue; // leaves are fully retracted
            const float retract=hole.value.width*0.5f*hole.value.open;
            if(hole.start-retract<0.f||hole.end+retract>length){
                graph.error="Door leaves cannot retract within the requested wall span";
                return graph;
            }
        }
        const auto wallSection=[&](BuildingGraphPart type,float a,float b,
                                    float bottom,float top,uint32_t owner=UINT32_MAX){
            if(b-a<0.0001f||top-bottom<0.0001f)return;
            // Window panes are a thin separate solid, not an opaque
            // full-thickness wall painted like glass.
            const float t=type==BuildingGraphPart::WindowPane?0.05f:wall;
            const float inset=(wall-t)*0.5f;
            switch(BuildingFeatureSide(side)){
              case BuildingFeatureSide::Front:solid(type,a,bottom,inset,b,top,inset+t,owner);break;
              case BuildingFeatureSide::Back:solid(type,a,bottom,D-inset-t,b,top,D-inset,owner);break;
              case BuildingFeatureSide::Left:solid(type,inset,bottom,a,inset+t,top,b,owner);break;
              case BuildingFeatureSide::Right:solid(type,W-inset-t,bottom,a,W-inset,top,b,owner);break;
            }
        };
        float cursor=0.f;
        for(const auto& hole:row){
            wallSection(BuildingGraphPart::Wall,cursor,hole.start,floorTop,H);
            const bool window=hole.value.kind==BuildingFeatureKind::Window;
            const float bottom=window?floorTop+storey*float(hole.value.floor)+
                (hole.value.floor?slabThick:0.f)+hole.value.sill:floorTop;
            const float top=bottom+hole.value.height;
            if(window)wallSection(BuildingGraphPart::Wall,hole.start,hole.end,floorTop,bottom);
            wallSection(BuildingGraphPart::Wall,hole.start,hole.end,top,H);
            constexpr float frame=0.14f;
            wallSection(BuildingGraphPart::Trim,hole.start-frame,hole.start,bottom,top,hole.feature);
            wallSection(BuildingGraphPart::Trim,hole.end,hole.end+frame,bottom,top,hole.feature);
            wallSection(BuildingGraphPart::Trim,hole.start-frame,hole.end+frame,top,top+0.15f,hole.feature);
            if(window){
                wallSection(BuildingGraphPart::WindowPane,hole.start+0.06f,hole.end-0.06f,
                            bottom+0.06f,top-0.06f,hole.feature);
            }else{
                const float half=hole.value.width*0.5f,shift=half*hole.value.open;
                if(hole.value.open<0.9999f){
                    wallSection(BuildingGraphPart::DoorLeaf,hole.start-shift,hole.start+half-shift,
                                bottom+0.05f,top-0.05f,hole.feature);
                    wallSection(BuildingGraphPart::DoorLeaf,hole.end-half+shift,hole.end+shift,
                                bottom+0.05f,top-0.05f,hole.feature);
                }
            }
            const float longitudinal=(hole.start+hole.end)*0.5f;
            float px=longitudinal,pz=-1.f;
            if(side==1)pz=D+1.f;
            if(side==2){px=-1.f;pz=longitudinal;}
            if(side==3){px=W+1.f;pz=longitudinal;}
            graph.sockets.push_back({(window?"window/":"entrance/")+std::to_string(hole.feature)+"/"+
                std::to_string(hole.copy),hole.value.kind,BuildingFeatureSide(side),
                hole.feature,hole.copy,px,window?bottom:0.f,pz,
                hole.value.width,hole.value.height,0.f,hole.value.open});
            cursor=hole.end;
        }
        wallSection(BuildingGraphPart::Wall,cursor,length,floorTop,H);
    }
    // A room is an accessible rectangular cell, not merely a decorative
    // rectangle. Real internal partitions are segmented around each opening.
    // Connections record room adjacency for later navmesh/door integrations.
    const float cellW=W/float(recipe.roomsX),cellD=D/float(recipe.roomsZ);
    auto roomId=[](uint32_t f,uint32_t x,uint32_t z){
        return std::string("room/")+std::to_string(f)+"/"+
            std::to_string(x)+"/"+std::to_string(z);
    };
    for(uint32_t f=0;f<recipe.floors;++f){
        const float level=floorTop+storey*float(f);
        const float bottom=level+(f?slabThick:0.f);
        const float ceiling=std::min(H-roofThickness,level+storey);
        const float lintel=std::min(bottom+2.2f,ceiling-0.2f);
        for(uint32_t z=0;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x)
            graph.rooms.push_back({roomId(f,x,z),f,x,z,float(x)*cellW,bottom,
                float(z)*cellD,float(x+1)*cellW,ceiling,float(z+1)*cellD});
        for(uint32_t x=1;x<recipe.roomsX;++x)for(uint32_t z=0;z<recipe.roomsZ;++z){
            const float at=float(x)*cellW,mid=(float(z)+0.5f)*cellD;
            const float a=mid-0.6f,c=mid+0.6f;
            solid(BuildingGraphPart::Partition,at-0.12f,bottom,float(z)*cellD,
                  at+0.12f,ceiling,a);
            solid(BuildingGraphPart::Partition,at-0.12f,bottom,c,
                  at+0.12f,ceiling,float(z+1)*cellD);
            solid(BuildingGraphPart::Partition,at-0.12f,lintel,a,
                  at+0.12f,ceiling,c);
            graph.connections.push_back({"passage/x/"+std::to_string(f)+"/"+
                std::to_string(x)+"/"+std::to_string(z),roomId(f,x-1,z),
                roomId(f,x,z),f,at,bottom,mid,1.2f,lintel-bottom});
        }
        for(uint32_t z=1;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x){
            const float at=float(z)*cellD,mid=(float(x)+0.5f)*cellW;
            const float a=mid-0.6f,c=mid+0.6f;
            solid(BuildingGraphPart::Partition,float(x)*cellW,bottom,at-0.12f,
                  a,ceiling,at+0.12f);
            solid(BuildingGraphPart::Partition,c,bottom,at-0.12f,
                  float(x+1)*cellW,ceiling,at+0.12f);
            solid(BuildingGraphPart::Partition,a,lintel,at-0.12f,
                  c,ceiling,at+0.12f);
            graph.connections.push_back({"passage/z/"+std::to_string(f)+"/"+
                std::to_string(x)+"/"+std::to_string(z),roomId(f,x,z-1),
                roomId(f,x,z),f,mid,bottom,at,1.2f,lintel-bottom});
        }
    }
    // Wall sockets carry the exact authored opening centre and width. Clip
    // the opening interval against every boundary cell; a wide gate may
    // overlap more than one room. Do not add exterior shortcuts to room routes.
    for(const auto& socket:graph.sockets){
        if(socket.kind!=BuildingFeatureKind::PersonnelDoor&&
           socket.kind!=BuildingFeatureKind::SmallGate&&
           socket.kind!=BuildingFeatureKind::LargeGate)continue;
        const bool alongX=socket.side==BuildingFeatureSide::Front||
                          socket.side==BuildingFeatureSide::Back;
        const float center=alongX?socket.x:socket.z;
        for(const auto& room:graph.rooms){
            if(room.floor!=0)continue;
            const bool boundary=socket.side==BuildingFeatureSide::Front?room.z==0:
                socket.side==BuildingFeatureSide::Back?room.z+1==recipe.roomsZ:
                socket.side==BuildingFeatureSide::Left?room.x==0:room.x+1==recipe.roomsX;
            if(!boundary)continue;
            const float a=std::max(center-socket.width*0.5f,alongX?room.x0:room.z0);
            const float b=std::min(center+socket.width*0.5f,alongX?room.x1:room.z1);
            if(b-a>0.001f)graph.exteriorApertures.push_back({socket.id,room.id,a,b});
        }
    }
    // Cut rectangular shafts from EVERY upper slab using a bounded planar
    // partition. Each emitted floor box remains a real collision solid.
    for(uint32_t floor=1;floor<recipe.floors;++floor){
        const float y=floorTop+storey*float(floor);
        std::vector<float> xs{0.f,W},zs{0.f,D};
        for(const auto& core:cores){
            xs.push_back(core.x0);xs.push_back(core.x1);
            zs.push_back(core.z0);zs.push_back(core.z1);
        }
        std::sort(xs.begin(),xs.end());std::sort(zs.begin(),zs.end());
        xs.erase(std::unique(xs.begin(),xs.end()),xs.end());
        zs.erase(std::unique(zs.begin(),zs.end()),zs.end());
        for(size_t ix=1;ix<xs.size();++ix)for(size_t iz=1;iz<zs.size();++iz){
            const float mx=(xs[ix-1]+xs[ix])*0.5f,mz=(zs[iz-1]+zs[iz])*0.5f;
            bool hole=false;
            for(const auto& core:cores)
                if(mx>core.x0&&mx<core.x1&&mz>core.z0&&mz<core.z1){hole=true;break;}
            if(!hole)solid(BuildingGraphPart::Floor,xs[ix-1],y,zs[iz-1],
                           xs[ix],y+slabThick,zs[iz]);
        }
    }
    // Model two opposite stairs flights with a turn landing. Treads are
    // physical boxes; semantic vertical links remain separate from navmesh.
    auto roomIdFor=[](uint32_t floor,uint32_t x,uint32_t z){
        return std::string("room/")+std::to_string(floor)+"/"+
            std::to_string(x)+"/"+std::to_string(z);
    };
    for(const auto& core:cores){
        const float cx=(core.x0+core.x1)*0.5f,cz=(core.z0+core.z1)*0.5f;
        if(core.kind==BuildingFeatureKind::Elevator){
            // Slim fixed guide rails make the lift-shaft footprint visible
            // without filling its movement volume with a solid collider.
            constexpr float e=0.09f;
            for(const float x:{core.x0+0.12f,core.x1-0.12f})
                for(const float z:{core.z0+0.12f,core.z1-0.12f})
                    solid(BuildingGraphPart::ElevatorGuide,x-e,floorTop,z-e,
                          x+e,H-roofThickness,z+e,core.feature);
        }
        for(uint32_t floor=0;floor+1<recipe.floors;++floor){
            const float bottom=floorTop+storey*float(floor)+(floor?slabThick:0.f);
            const float next=floorTop+storey*float(floor+1)+slabThick;
            const float climb=next-bottom;
            const std::string from=roomIdFor(floor,core.roomX,core.roomZ);
            const std::string to=roomIdFor(floor+1,core.roomX,core.roomZ);
            const std::string token=std::to_string(core.feature)+"/"+std::to_string(floor);
            graph.connections.push_back({"vertical/"+token,from,to,floor,cx,bottom,cz,
                core.x1-core.x0,climb,
                core.kind==BuildingFeatureKind::Stairs?
                    BuildingConnectionKind::Stair:BuildingConnectionKind::Elevator});
            graph.sockets.push_back({(core.kind==BuildingFeatureKind::Stairs?"stairs/":"lift/")+token,
                core.kind,BuildingFeatureSide::Front,core.feature,floor,cx,bottom,cz,
                core.x1-core.x0,climb,core.z1-core.z0,0.f});
            if(core.kind==BuildingFeatureKind::Elevator){
                // ONE cabin floor at the initial station. It must not make
                // a fixed blocking slab on EVERY upper storey. Moving the
                // platform and doors is a separate authoritative runtime
                // mechanism, not simulated by the static geometry recipe.
                if(floor==0)solid(BuildingGraphPart::ElevatorPlatform,
                    core.x0,bottom,core.z0,core.x1,bottom+0.18f,core.z1,core.feature);
                continue;
            }
            const float mid=bottom+climb*0.5f;
            const float lane=(core.x1-core.x0-0.18f)*0.5f;
            const float halfRun=core.z1-core.z0-0.9f;
            const int steps=std::max(8,int(std::ceil(climb/0.18f)));
            const int perFlight=(steps+1)/2;
            const float rise=climb/float(2*perFlight);
            for(int j=0;j<perFlight;++j){
                const float z0=core.z0+halfRun*float(j)/float(perFlight);
                const float z1=core.z0+halfRun*float(j+1)/float(perFlight);
                const float y0=bottom+rise*float(j),y1=bottom+rise*float(j+1);
                solid(BuildingGraphPart::StairTread,core.x0,y0,z0,
                      core.x0+lane,y1,z1,core.feature);
                // Return flight climbs towards -Z after the turn landing.
                const float back0=core.z0+halfRun*float(perFlight-j-1)/float(perFlight);
                const float back1=core.z0+halfRun*float(perFlight-j)/float(perFlight);
                const float by0=mid+rise*float(j),by1=mid+rise*float(j+1);
                solid(BuildingGraphPart::StairTread,core.x1-lane,by0,back0,
                      core.x1,by1,back1,core.feature);
            }
            solid(BuildingGraphPart::StairLanding,core.x0,mid-0.14f,
                  core.z1-0.9f,core.x1,mid,core.z1,core.feature);
        }
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
