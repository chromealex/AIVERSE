// Bounded authoring preview using the canonical CubeSphere and MapDoc.
// Body-local coordinates (not simulation anchors or orbital render space).
#pragma once
#include "apps/studio-native/StudioVisualPicking.h"
#include "engine/game/Sphere.h"
#include <optional>
#include <limits>

namespace aiverse::studio {
struct StudioPlanetPreview {
    std::string bodyId;
    int faceSize=0;
    float radius=0,extent=0;
    render::MeshBuilder mesh;
    std::vector<StudioDocumentProxy> markers;
    std::map<std::string,Json> placements;
    std::map<std::string,std::string> authoredIds;
    static render::float3 f3(Vec3 v){return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};}
    static Fixed fixed(float v){return Fixed::fromRaw(int64_t(std::llround(double(v)*Fixed::kOne)));}
    static float height(const editor::DocBody& body,int face,float a,float b){
        const int n=body.faceSize;
        const int x=std::min(n-1,int(std::floor(a))),y=std::min(n-1,int(std::floor(b)));
        const float u=a-x,v=b-y;
        const auto h=[&](int i,int j){return float(body.corners[body.cornerIndex(face,i,j)])/16.f;};
        return (h(x,y)*(1-u)+h(x+1,y)*u)*(1-v)+(h(x,y+1)*(1-u)+h(x+1,y+1)*u)*v;
    }
    static std::optional<StudioPlanetPreview> build(const editor::MapDoc& doc,int bi){
        if(bi<0||size_t(bi)>=doc.data.bodies.size())return {};
        const auto& body=doc.data.bodies[size_t(bi)];
        const int n=body.faceSize;
        const double r=body.meta["radius"].toDouble(32);
        if(n<1||n>512||!std::isfinite(r)||r<=0||r>2048||
           body.corners.size()!=size_t(6)*(n+1)*(n+1))return {};
        StudioPlanetPreview out;out.bodyId=body.id;out.faceSize=n;out.radius=float(r);out.extent=out.radius;
        game::CubeSphere cube(n);
        // Coarse authoring preview, same face boundaries on all sides.
        const int step=std::max(1,(n+63)/64);
        const auto point=[&](int face,float x,float y){
            const auto dir=f3(cube.dirFromFace(face,fixed(x),fixed(y)));
            return dir*(out.radius+height(body,face,x,y));
        };
        for(int f=0;f<6;++f)for(int y=0;y<n;y+=step)for(int x=0;x<n;x+=step){
            const int xx=std::min(n,x+step),yy=std::min(n,y+step);
            std::array<render::float3,4> p{point(f,float(x),float(y)),point(f,float(xx),float(y)),point(f,float(xx),float(yy)),point(f,float(x),float(yy))};
            auto normal=render::cross(p[1]-p[0],p[2]-p[0]);
            if(render::dot(normal,p[0])<0)std::swap(p[1],p[3]);
            const auto offset=uint32_t(out.mesh.vertexCount());
            const size_t ci=body.cellIndex(f,x,y);
            const auto color=terrainPreviewColor(ci<body.cells.size()?body.cells[ci]:'.');
            for(const auto& v:p){
                const float length=std::sqrt(render::dot(v,v));
                if(!std::isfinite(length)||length<.01f)return {};
                out.extent=std::max(out.extent,length);
                out.mesh.addVertex(v,v*(1.f/length),color);
            }
            out.mesh.addTriangle(offset,offset+1,offset+2);
            out.mesh.addTriangle(offset,offset+2,offset+3);
        }
        for(size_t i=0;i<doc.data.entities.size()&&out.markers.size()<2000;++i){
            const auto& e=doc.data.entities[i];
            if(e.at.getStr("body")!=body.id)continue;
            const auto face=e.at.getInt("face",-1);
            const double x=e.at["x"].toDouble(NAN),y=e.at["y"].toDouble(NAN);
            if(face<0||face>5||!std::isfinite(x)||!std::isfinite(y)||x<0||x>n||y<0||y>n)continue;
            StudioDocumentProxy marker;
            marker.id=studioDocumentId(doc,{editor::DocumentEntityRef::Source::MapEntity,i});
            const float half=std::clamp(out.radius*.012f,.35f,4.f);
            const auto ground=point(int(face),float(x),float(y));
            const auto up=f3(cube.dirFromFace(int(face),fixed(float(x)),fixed(float(y))));
            marker.center=ground+up*(half*1.8f);marker.half={half,half,half};
            marker.color=e.owner==2?render::float4{.94f,.52f,.42f,1}:render::float4{.44f,.70f,.98f,1};
            marker.calculateBounds();
            out.placements.emplace(marker.id,e.at);
            if(!e.id.empty())out.authoredIds.emplace(e.id,marker.id);
            out.markers.push_back(std::move(marker));
        }
        return out;
    }
    // Ray intersects the exact preview mesh triangles, including occlusion.
    float terrainHit(render::float3 origin,render::float3 direction)const{
        float nearest=std::numeric_limits<float>::infinity();
        const auto& v=mesh.vertices();const auto& idx=mesh.indices();
        for(size_t i=0;i+2<idx.size();i+=3){
            const auto a=v[idx[i]].pos,b=v[idx[i+1]].pos,c=v[idx[i+2]].pos;
            const auto e=b-a,f=c-a,h=render::cross(direction,f);
            const float det=render::dot(e,h);
            if(std::abs(det)<1e-7f)continue;
            const auto s=origin-a;const float u=render::dot(s,h)/det;
            if(u<0||u>1)continue;
            const auto q=render::cross(s,e);const float w=render::dot(direction,q)/det;
            if(w<0||u+w>1)continue;
            const float t=render::dot(f,q)/det;
            if(t>=0&&t<nearest)nearest=t;
        }
        return nearest;
    }
    std::optional<Json> groundPlacement(render::float3 origin,render::float3 direction)const{
        const float t=terrainHit(origin,direction);
        if(!std::isfinite(t))return {};
        const auto p=origin+direction*t;
        if(render::dot(p,p)<1e-6f)return {};
        const auto unit=render::normalize(p);
        game::CubeSphere cube(faceSize);int face;Fixed a,b;
        cube.faceFromDir({fixed(unit.x),fixed(unit.y),fixed(unit.z)},face,a,b);
        Json at=Json::object();at.set("body",Json::string(bodyId));at.set("face",Json::number(int64_t(face)));
        at.set("x",Json::number(std::clamp(a.toDouble(),0.,double(faceSize)),4));
        at.set("y",Json::number(std::clamp(b.toDouble(),0.,double(faceSize)),4));
        return at;
    }
    std::string pick(render::float3 origin,render::float3 direction,
                     const std::set<std::string>& hidden,const std::set<std::string>& locked)const{
        float closest=terrainHit(origin,direction);std::string id;
        for(const auto& p:markers){
            if(hidden.count(p.id)||locked.count(p.id))continue;
            const float distance=studioRayProxyBox(p,origin,direction);
            if(distance<closest){closest=distance;id=p.id;}
        }
        return id;
    }
};
} // namespace aiverse::studio
