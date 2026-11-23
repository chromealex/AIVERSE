#pragma once
// AIVERSE's existing nav::Mesh owns raster/span/corridor navigation.
// It has NO off-mesh-link insertion API. This adapter resolves semantic
// BuildingStructureGraph vertical links onto that Mesh's polygon IDs and
// leaves stair traversal/lift action as an explicit route segment for the
// game movement owner. Never inject a fake horizontal nav portal.
#include <aiverselib/systems/BuildingCirculationRuntime.h>
#include "engine/nav/NavMesh.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
using namespace lib::systems;
struct BuildingOffMeshLink {
    std::string id,fromRoom,toRoom;
    BuildingConnectionKind kind=BuildingConnectionKind::Stair;
    Vec3 from{},to{};       // owner/world coordinates
    uint32_t fromPolygon=nav::Mesh::kNone,toPolygon=nav::Mesh::kNone;
    bool resolved=false;   // source/destination stand on actual nav polygons
    uint64_t navRevision=0;
};
struct BuildingNavLinkBatch {
    std::vector<BuildingOffMeshLink> links;
    uint64_t navRevision=0;
    std::string error;
    explicit operator bool()const noexcept{return error.empty();}
};

inline Vec3 buildingLocalToWorld(float x,float y,float z,
                                float width,float depth,
                                Vec3 origin,uint32_t yaw){
    float X=x,Z=z;
    switch(yaw){
      case 1:X=depth-z;Z=x;break;
      case 2:X=width-x;Z=depth-z;break;
      case 3:X=z;Z=width-x;break;
      default:break;
    }
    auto fx=[](float value){return Fixed::fromRatio(
        int64_t(std::llround(double(value)*1000.)),1000);};
    return {origin.x+fx(X),origin.y+fx(y),origin.z+fx(Z)};
}

// Room centres may coincide with a stair or lift shaft, where NO walkable
// floor span exists. Resolve an adjacent valid room point conservatively:
// if no point actually locates in NavMesh, the edge is not published.
inline std::optional<std::pair<uint32_t,Vec3>> buildingRoomNavLanding(
    const nav::Mesh& mesh,const BuildingGraphRoom& room,
    float W,float D,Vec3 origin,uint32_t yaw,
    Fixed tolerance=Fixed::fromRatio(2,5)){
    if(!mesh.built()||mesh.dirty()||yaw>3)return {};
    constexpr std::array<std::array<float,2>,9> samples{{
        {{0.5f,0.5f}},{{0.25f,0.25f}},{{0.75f,0.25f}},
        {{0.25f,0.75f}},{{0.75f,0.75f}},{{0.5f,0.25f}},
        {{0.5f,0.75f}},{{0.25f,0.5f}},{{0.75f,0.5f}}
    }};
    for(const auto& sample:samples){
        const float x=room.x0+(room.x1-room.x0)*sample[0];
        const float z=room.z0+(room.z1-room.z0)*sample[1];
        const Vec3 point=buildingLocalToWorld(x,room.y0,z,W,D,origin,yaw);
        auto id=mesh.locate(point,tolerance);
        if(!id)continue;
        if(*id>=mesh.rectCount())continue;
        const auto& rect=mesh.rect(*id);
        if(rect.y<point.y-tolerance||rect.y>point.y+tolerance)continue;
        return std::make_pair(*id,point);
    }
    return {};
}
inline BuildingNavLinkBatch resolveBuildingOffMeshLinks(
    const BuildingStructureGraph& graph,const nav::Mesh& mesh,
    float width,float depth,Vec3 origin={},uint32_t yaw=0,
    Fixed tolerance=Fixed::fromRatio(2,5)){
    BuildingNavLinkBatch out;
    if(!graph||yaw>3||!std::isfinite(width)||!std::isfinite(depth)||
       width<=0||depth<=0){
        out.error="Invalid building graph transform";
        return out;
    }
    if(!mesh.built()||mesh.dirty()){
        out.error="Navigation mesh is not built/refreshed";
        return out;
    }
    out.navRevision=mesh.revision();
    for(const auto& edge:graph.connections){
        if(edge.kind==BuildingConnectionKind::Passage)continue;
        const auto from=std::find_if(graph.rooms.begin(),graph.rooms.end(),
            [&](const auto& r){return r.id==edge.from;});
        const auto to=std::find_if(graph.rooms.begin(),graph.rooms.end(),
            [&](const auto& r){return r.id==edge.to;});
        if(from==graph.rooms.end()||to==graph.rooms.end()){
            out.error="Vertical link references missing room: "+edge.id;
            out.links.clear();return out;
        }
        BuildingOffMeshLink link;
        link.id=edge.id;link.fromRoom=edge.from;link.toRoom=edge.to;
        link.kind=edge.kind;link.navRevision=out.navRevision;
        auto a=buildingRoomNavLanding(mesh,*from,width,depth,origin,yaw,tolerance);
        auto b=buildingRoomNavLanding(mesh,*to,width,depth,origin,yaw,tolerance);
        if(a&&b){
            link.fromPolygon=a->first;link.from=a->second;
            link.toPolygon=b->first;link.to=b->second;
            link.resolved=true;
        }
        out.links.push_back(std::move(link));
    }
    return out;
}

struct BuildingNavRouteStep {
    std::string connectionId,fromRoom,toRoom;
    BuildingRouteAction action=BuildingRouteAction::WalkThrough;
    std::optional<Vec3> from,to;
    bool hasNavAnchors=false;
};
struct BuildingNavRoute {
    std::vector<BuildingNavRouteStep> steps;
    uint64_t navRevision=0;
    bool reachable=false;
    std::string error;
};
// Never hand out a vertical route just because the room graph is connected.
// Both endpoints must resolve to the current actual navmesh, and a lift must
// be registered/online. Movement over the off-mesh segment is controlled by
// the game's stair mover or elevator state, not nav::Mesh::findPath().
inline BuildingNavRoute planBuildingNavRoute(
    const BuildingStructureGraph& graph,const nav::Mesh& mesh,
    const BuildingNavLinkBatch& links,
    const std::string& fromRoom,const std::string& toRoom,
    const BuildingRoutePolicy& policy={},
    const std::map<uint32_t,BuildingLiftController>* lifts=nullptr){
    BuildingNavRoute result;
    result.navRevision=links.navRevision;
    if(!links||!mesh.built()||mesh.dirty()||
       links.navRevision!=mesh.revision()){
        result.error="Stale/unresolved building nav link registry";return result;
    }
    const auto route=planBuildingRoomRoute(graph,fromRoom,toRoom,policy,lifts);
    if(!route.reachable){result.error=route.error;return result;}
    for(const auto& step:route.steps){
        BuildingNavRouteStep output;
        output.connectionId=step.edgeId;
        output.fromRoom=step.fromRoom;
        output.toRoom=step.toRoom;
        output.action=step.action;
        if(step.action!=BuildingRouteAction::WalkThrough){
            const auto link=std::find_if(links.links.begin(),links.links.end(),
                [&](const auto& l){return l.id==step.edgeId;});
            if(link==links.links.end()||!link->resolved||
               link->navRevision!=mesh.revision()){
                result.steps.clear();
                result.error="No valid navmesh landing at "+step.edgeId;
                return result;
            }
            output.hasNavAnchors=true;
            if(step.fromRoom==link->fromRoom){
                output.from=link->from;output.to=link->to;
            }else{
                output.from=link->to;output.to=link->from;
            }
        }
        result.steps.push_back(std::move(output));
    }
    result.reachable=true;
    return result;
}
} // namespace aiverse::building
