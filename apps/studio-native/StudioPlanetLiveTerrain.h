// Completed local Surface read and render-only grid reconstruction.
// A terrain grid is atomic within the shared diagnostic payload budget.
#pragma once
#include "engine/game/Surface.h"
#include "engine/data/Json.h"
#include "apps/studio-native/StudioPlanetPreview.h"
#include <algorithm>
#include <cmath>
namespace aiverse::studio {
inline Json sampleStudioPlanetTerrain(const game::Surface& surface,uint32_t tick,
        const std::string& bodyId,size_t maxBytes){
    Json out=Json::object();out.set("active",Json::fromBool(false));
    const int bi=surface.findBody(bodyId);
    if(!surface.tab||bi<0||tick==UINT32_MAX)return out;
    const auto& body=surface.bodies[size_t(bi)];const int n=body.faceSize;
    if(n<1||n>512||body.radius<=Fixed{}||body.radius>2048_fx||
       body.cornerBase+size_t(6)*(n+1)*(n+1)>surface.corners.size())return out;
    // Worst case 7 JSON bytes per signed int16 corner, plus metadata.
    int step=std::max(1,(n+63)/64);
    while(step<n&&size_t(6)*((n+step-1)/step+1)*((n+step-1)/step+1)*7+1024>maxBytes)++step;
    const int side=(n+step-1)/step+1;
    if(size_t(6)*side*side*7+1024>maxBytes)return out;
    Json data=Json::object(),values=Json::array();
    for(int f=0;f<6;++f)for(int y=0;y<side;++y)for(int x=0;x<side;++x)
        values.push(Json::number(int64_t(surface.corner(bi,f,std::min(n,x*step),std::min(n,y*step)))));
    data.set("body",Json::string(bodyId));data.set("faceSize",Json::number(int64_t(n)));
    data.set("radius",Json::number(body.radius.toDouble(),6));
    data.set("step",Json::number(int64_t(step)));data.set("side",Json::number(int64_t(side)));
    data.set("corners",std::move(values));
    out.set("active",Json::fromBool(true));out.set("tick",Json::number(int64_t(tick)));
    out.set("grid",std::move(data));return out;
}
inline std::optional<render::MeshBuilder> buildStudioLivePlanetTerrain(const Json& grid,
        const editor::DocBody& body,const StudioPlanetPreview& authored){
    const int n=body.faceSize;
    const auto step=grid.getInt("step",0),side=grid.getInt("side",0);
    const double radius=grid["radius"].toDouble(NAN);
    if(n<1||n>512||step<1||step>n||side!=(n+step-1)/step+1||side>65||
       grid.getStr("body")!=body.id||grid.getInt("faceSize")!=n||
       !std::isfinite(radius)||std::abs(radius-authored.radius)>.0001||
       !grid["corners"].isArray()||grid["corners"].size()!=size_t(6)*side*side)return {};
    const auto& values=grid["corners"].items();
    for(const auto& v:values)if(!v.isNumber()||v.integer()<-32768||v.integer()>32767||v.fx()!=Fixed::fromInt(v.integer())||radius+double(v.integer())/16.<=.01)return {};
    game::CubeSphere cube(n);render::MeshBuilder mesh;
    const auto point=[&](int f,int x,int y){
        const float height=float(values[(size_t(f)*side+y)*side+x].integer())/16.f;
        const auto dir=StudioPlanetPreview::f3(cube.dirFromFace(f,Fixed::fromInt(std::min<int64_t>(n,x*step)),Fixed::fromInt(std::min<int64_t>(n,y*step))));
        return dir*(float(radius)+height);
    };
    for(int f=0;f<6;++f)for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x){
        std::array<render::float3,4> p{point(f,x,y),point(f,x+1,y),point(f,x+1,y+1),point(f,x,y+1)};
        if(render::dot(render::cross(p[1]-p[0],p[2]-p[0]),p[0])<0)std::swap(p[1],p[3]);
        const auto base=uint32_t(mesh.vertexCount());
        const auto ci=body.cellIndex(f,int(x*step),int(y*step));
        const auto color=terrainPreviewColor(ci<body.cells.size()?body.cells[ci]:'.');
        for(const auto& v:p){
            const float length=std::sqrt(render::dot(v,v));
            if(!std::isfinite(length)||length<.01f)return {};
            mesh.addVertex(v,v*(1.f/length),color);
        }
        mesh.addTriangle(base,base+1,base+2);mesh.addTriangle(base,base+2,base+3);
    }
    return mesh;
}
} // namespace aiverse::studio
