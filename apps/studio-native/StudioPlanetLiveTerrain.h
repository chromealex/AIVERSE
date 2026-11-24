// Completed local Surface read and render-only grid reconstruction.
// A terrain grid is atomic within the shared diagnostic payload budget.
#pragma once
#include "engine/game/Surface.h"
#include "engine/data/Json.h"
#include "apps/studio-native/StudioPlanetPreview.h"
#include "apps/studio-native/StudioForestCameraFocus.h"
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>
namespace aiverse::studio {
inline Json sampleStudioPlanetTerrain(const game::Surface& surface,uint32_t tick,
        const std::string& bodyId,size_t maxBytes,const std::optional<StudioForestCameraFocus>& camera={}){
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
    // Native-only caller supplies the Scene camera and its independent budget.
    // Choose a bounded set of coarse tiles and sample their exact fine corners.
    if(step>1&&camera&&camera->valid()&&camera->body==bodyId){
        game::CubeSphere cube(n);
        const auto clip=camera->viewProjection();
        struct Candidate { int f,x,y,group; float distance,target; };
        std::vector<Candidate> candidates;
        for(int f=0;f<6;++f)for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x){
            const int x0=x*step,y0=y*step,x1=std::min(n,x0+step),y1=std::min(n,y0+step);
            const auto dir=StudioPlanetPreview::f3(cube.dirFromFace(f,Fixed::fromRatio(x0+x1,2),Fixed::fromRatio(y0+y1,2)));
            const auto p=dir*float(body.radius.toDouble());
            const auto [group,distance,target]=camera->rank(p,float(body.radius.toDouble()),clip);
            if(group==0)candidates.push_back({f,x,y,group,distance,target});
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
            return std::tie(a.group,a.distance,a.target,a.f,a.y,a.x)<std::tie(b.group,b.distance,b.target,b.f,b.y,b.x);
        });
        Json patches=Json::array();
        size_t remaining=maxBytes-(size_t(6)*side*side*7+1024);
        constexpr size_t maxPatches=64;
        for(const auto& c:candidates){
            if(patches.size()>=maxPatches)break;
            const int x0=c.x*step,y0=c.y*step,x1=std::min(n,x0+step),y1=std::min(n,y0+step);
            const size_t cost=size_t(x1-x0+1)*(y1-y0+1)*7+128;
            if(cost>remaining)continue;
            Json patch=Json::object(),fine=Json::array();
            patch.set("face",Json::number(int64_t(c.f)));patch.set("x",Json::number(int64_t(c.x)));patch.set("y",Json::number(int64_t(c.y)));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)fine.push(Json::number(int64_t(surface.corner(bi,c.f,x,y))));
            patch.set("corners",std::move(fine));patches.push(std::move(patch));remaining-=cost;
        }
        data.set("patches",std::move(patches));
    }
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
    using PatchKey=std::tuple<int,int,int>;
    std::map<PatchKey,const Json*> patches;
    if(grid.has("patches")){
        if(!grid["patches"].isArray()||grid["patches"].size()>64)return {};
        for(const auto& patch:grid["patches"].items()){
            const auto f=patch.getInt("face",-1),x=patch.getInt("x",-1),y=patch.getInt("y",-1);
            if(f<0||f>=6||x<0||x>=side-1||y<0||y>=side-1||step<=1)return {};
            const int w=int(std::min<int64_t>(step,n-x*step)),h=int(std::min<int64_t>(step,n-y*step));
            const auto& fine=patch["corners"];
            if(!fine.isArray()||fine.size()!=size_t(w+1)*(h+1))return {};
            for(const auto& v:fine.items())if(!v.isNumber()||v.integer()<-32768||v.integer()>32767||v.fx()!=Fixed::fromInt(v.integer())||radius+double(v.integer())/16.<=.01)return {};
            // Every patch corner must agree with the shared coarse grid.
            const auto& a=fine.items();
            const auto coarse=[&](int xx,int yy){return values[(size_t(f)*side+yy)*side+xx].integer();};
            if(a[0].integer()!=coarse(int(x),int(y))||a[size_t(w)].integer()!=coarse(int(x+1),int(y))||
                a[size_t(h)*(w+1)].integer()!=coarse(int(x),int(y+1))||a.back().integer()!=coarse(int(x+1),int(y+1)))return {};
            if(!patches.emplace(PatchKey{int(f),int(x),int(y)},&fine).second)return {};
        }
    }
    const auto emit=[&](std::array<render::float3,4> p,int f,int x,int y){
        if(render::dot(render::cross(p[1]-p[0],p[2]-p[0]),p[0])<0)std::swap(p[1],p[3]);
        const auto base=uint32_t(mesh.vertexCount());
        const auto ci=body.cellIndex(f,x,y);
        const auto color=terrainPreviewColor(ci<body.cells.size()?body.cells[ci]:'.');
        for(const auto& v:p){
            const float length=std::sqrt(render::dot(v,v));
            if(!std::isfinite(length)||length<.01f)return false;
            mesh.addVertex(v,v*(1.f/length),color);
        }
        mesh.addTriangle(base,base+1,base+2);mesh.addTriangle(base,base+2,base+3);return true;
    };
    for(int f=0;f<6;++f)for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x){
        const auto it=patches.find({f,x,y});
        if(it==patches.end()){
            if(!emit({point(f,x,y),point(f,x+1,y),point(f,x+1,y+1),point(f,x,y+1)},f,int(x*step),int(y*step)))return {};
            continue;
        }
        const int x0=int(x*step),y0=int(y*step),w=std::min(n-x0,int(step)),h=std::min(n-y0,int(step));
        const auto& fine=it->second->items();
        const auto lerp=[](render::float3 a,render::float3 b,float t){return a+(b-a)*t;};
        const auto refined=[&](int xx,int yy){return patches.count({f,xx,yy})!=0;};
        const auto local=[&](int i,int j){
            // Snap fine/coarse boundaries to the exact coarse chord, including
            // face seams. Adjacent fine tiles on one face share exact samples.
            if(j==0&&!refined(x,y-1))return lerp(point(f,x,y),point(f,x+1,y),float(i)/w);
            if(j==h&&!refined(x,y+1))return lerp(point(f,x,y+1),point(f,x+1,y+1),float(i)/w);
            if(i==0&&!refined(x-1,y))return lerp(point(f,x,y),point(f,x,y+1),float(j)/h);
            if(i==w&&!refined(x+1,y))return lerp(point(f,x+1,y),point(f,x+1,y+1),float(j)/h);
            const auto dir=StudioPlanetPreview::f3(cube.dirFromFace(f,Fixed::fromInt(x0+i),Fixed::fromInt(y0+j)));
            return dir*(float(radius)+float(fine[size_t(j)*(w+1)+i].integer())/16.f);
        };
        for(int j=0;j<h;++j)for(int i=0;i<w;++i)
            if(!emit({local(i,j),local(i+1,j),local(i+1,j+1),local(i,j+1)},f,x0+i,y0+j))return {};
    }
    return mesh;
}
} // namespace aiverse::studio
