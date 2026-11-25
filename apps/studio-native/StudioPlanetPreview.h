// Bounded authoring preview using the canonical CubeSphere and MapDoc.
// Body-local coordinates (not simulation anchors or orbital render space).
#pragma once
#include "apps/studio-native/StudioVisualPicking.h"
#include "engine/game/Sphere.h"
#include "apps/studio-native/StudioModelPicking.h"
#include <optional>
#include <array>
#include <map>
#include <set>
#include <limits>

namespace aiverse::studio {
struct StudioPlanetPreview {
    std::string bodyId;
    int faceSize=0;
    float radius=0,extent=0;
    render::MeshBuilder mesh;
    std::vector<StudioDocumentProxy> markers;
    std::map<std::string,Json> placements;
    std::map<std::string,render::mat4> modelFrames;
    std::map<std::string,std::string> authoredIds;
    struct Link { std::string from,to; uint32_t color=0; };
    std::vector<Link> links;
    static render::float3 f3(Vec3 v){return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};}
    static Fixed fixed(float v){return Fixed::fromRaw(int64_t(std::llround(double(v)*Fixed::kOne)));}
    static float height(const editor::DocBody& body,int face,float a,float b){
        const int n=body.faceSize;
        const int x=std::min(n-1,int(std::floor(a))),y=std::min(n-1,int(std::floor(b)));
        const float u=a-x,v=b-y;
        const auto h=[&](int i,int j){return float(body.corners[body.cornerIndex(face,i,j)])/16.f;};
        return (h(x,y)*(1-u)+h(x+1,y)*u)*(1-v)+(h(x,y+1)*(1-u)+h(x+1,y+1)*u)*v;
    }
    // Surface yaw about radial up. The model basis must be orthonormal:
    // projected cube face tangents themselves are not orthogonal at corners.
    static render::mat4 modelFrame(const game::CubeSphere& cube,int face,
            Fixed a,Fixed b,render::float3 ground,const editor::DocEntity& entity){
        const auto direction=cube.dirFromFace(face,a,b);
        int canonical;Fixed ca,cb;cube.faceFromDir(direction,canonical,ca,cb);
        Vec3 east,south;game::CubeSphere::faceTangents(canonical,direction,east,south);
        const auto up=render::normalize(f3(direction));
        const auto right=render::normalize(f3(east));
        const auto forward=render::normalize(render::cross(right,up));
        render::mat4 basis;
        const render::float3 axes[3]={right,up,forward};
        for(int i=0;i<3;++i){
            basis.at(0,i)=axes[i].x;basis.at(1,i)=axes[i].y;basis.at(2,i)=axes[i].z;
        }
        render::mat4 local;
        if(editor::authoredEntity(entity)){
            // Existing authored rotation/scale. Never interpret mirrored
            // face x/y as Cartesian translation; at owns the ground anchor.
            local=editor::authoredModelMatrix(entity);
            local.at(0,3)=0;local.at(1,3)=0;local.at(2,3)=0;
        }else{
            const double facing=entity.extra["facing"].toDouble(0);
            if(std::isfinite(facing))local=render::mat4::rotateY(float(-facing*3.141592653589793/180.));
        }
        return render::mat4::translate(ground)*basis*local;
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
            try{out.modelFrames.emplace(marker.id,modelFrame(cube,int(face),fixed(float(x)),fixed(float(y)),ground,e));}
            catch(const std::exception&){/* Invalid authored transform keeps the marker. */}
            if(!e.id.empty())out.authoredIds.emplace(e.id,marker.id);
            out.markers.push_back(std::move(marker));
        }
        // Authored stable references only, restricted to this body.
        const auto link=[&](const Json& c,const char* a,const char* b,uint32_t color){
            if(!c.isObject()||!c.getBool("enabled",true)||out.links.size()>=4096)return;
            const auto first=out.authoredIds.find(c.getStr(a)),second=out.authoredIds.find(c.getStr(b));
            if(first==out.authoredIds.end()||second==out.authoredIds.end()||first->second==second->second)return;
            out.links.push_back({first->second,second->second,color});
        };
        for(const auto& e:doc.data.entities){
            if(e.at.getStr("body")!=body.id)continue;
            const auto& c=e.extra["components"];
            link(c["CircuitWire"],"from","to",render::rgba(255,210,70));
            link(c["AetherDuct"],"from","to",render::rgba(115,210,255));
            link(c["FractureBond"],"first","second",render::rgba(255,130,90));
            link(c["EchoPortal"],"from","to",render::rgba(180,140,255));
            link(c["MorphSpring"],"a","b",render::rgba(60,210,245));
            link(c["ImprintLink"],"a","b",render::rgba(210,160,90));
            link(c["SocietyContact"],"source","target",render::rgba(100,240,155));
        }
        return out;
    }
    // Ray intersects the exact preview mesh triangles, including occlusion.
    float terrainHit(render::float3 origin,render::float3 direction,const render::MeshBuilder* renderedTerrain=nullptr)const{
        return studioRayMesh(origin,direction,renderedTerrain?*renderedTerrain:mesh);
    }
    std::optional<Json> groundPlacement(render::float3 origin,render::float3 direction,const render::MeshBuilder* renderedTerrain=nullptr)const{
        const float t=terrainHit(origin,direction,renderedTerrain);
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
                     const std::set<std::string>& hidden,const std::set<std::string>& locked,
                     const std::vector<StudioModelPickInstance>& models={},const render::MeshBuilder* renderedTerrain=nullptr)const{
        float closest=terrainHit(origin,direction,renderedTerrain);std::string id;
        std::set<std::string> replaced;
        for(const auto& model:models){
            if(!model.model)continue;
            replaced.insert(model.id);
            if(hidden.count(model.id)||locked.count(model.id))continue;
            const auto hit=studioPickModelSurface(*model.model,model.world,origin,direction);
            if(hit.distance<closest){closest=hit.distance;id=model.id;}
        }
        for(const auto& p:markers){
            if(hidden.count(p.id)||locked.count(p.id)||replaced.count(p.id))continue;
            const float distance=studioRayProxyBox(p,origin,direction);
            if(distance<closest){closest=distance;id=p.id;}
        }
        return id;
    }
};
} // namespace aiverse::studio
