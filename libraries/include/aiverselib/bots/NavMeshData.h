#pragma once
#include "Config.h"
#include "NavMesh.h"
namespace aiverse::lib::bots {
inline Vec3 navVector(const Json& value){
    if(!value.isArray()||value.size()!=3)throw JsonError("navmesh: expected [x,y,z]");
    Vec3 result;
    Fixed* coords[]={&result.x,&result.y,&result.z};
    for(size_t i=0;i<3;++i){if(!value[i].isNumber())throw JsonError("navmesh: expected numeric coordinate");*coords[i]=value[i].fx();}return result;
}
inline NavBuildConfig readNavConfig(const Json& j,Fixed radius,Fixed height,Fixed step){
    keys(j,{"min","max","cellSize","maxCells","maxPolygons","maxCandidatesPerCell"});NavBuildConfig c;c.radius=radius;c.height=height;c.stepHeight=step;
    c.bounds={navVector(j["min"]),navVector(j["max"])};
    c.cellSize=number(j,"cellSize",c.cellSize,Fixed::fromRatio(1,4),32_fx);
    c.maxCells=uint32_t(integer(j,"maxCells",c.maxCells,1,262144));c.maxPolygons=uint32_t(integer(j,"maxPolygons",c.maxPolygons,1,262144));
    c.maxCandidatesPerCell=uint32_t(integer(j,"maxCandidatesPerCell",c.maxCandidatesPerCell,1,1024));
    if(!valid(c))throw JsonError("navmesh: invalid bounds or agent");
    return c;
}
inline Json navVectorJson(Vec3 p){Json a=Json::array();a.push(Json::number(p.x));a.push(Json::number(p.y));a.push(Json::number(p.z));return a;}
// Inspection/export artifact; rebuild from authoritative collision rather than trusting edited caches.
inline Json navMeshJson(const NavMesh& mesh){
    Json j=Json::object();j.set("version",Json::number(int64_t(1)));j.set("collisionHash",Json::string(std::to_string(mesh.collisionHash)));
    j.set("min",navVectorJson(mesh.config.bounds.min));j.set("max",navVectorJson(mesh.config.bounds.max));j.set("cellSize",Json::number(mesh.config.cellSize));
    j.set("radius",Json::number(mesh.config.radius));j.set("height",Json::number(mesh.config.height));j.set("stepHeight",Json::number(mesh.config.stepHeight));
    Json polygons=Json::array();for(const auto& p:mesh.polygons){Json item=Json::object();Json vertices=Json::array(),neighbors=Json::array();
        for(auto v:p.vertices)vertices.push(navVectorJson(v));
        for(auto n:p.neighbors)neighbors.push(Json::number(int64_t(n)));
        item.set("component",Json::number(int64_t(p.component)));item.set("vertices",vertices);item.set("neighbors",neighbors);item.set("center",navVectorJson(p.center));polygons.push(item);
    }j.set("polygons",polygons);return j;
}
}
