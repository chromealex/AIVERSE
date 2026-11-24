#pragma once
// Editor-only body overview. The draw and hit paths share one immutable
// triangle source; no new planet generator, physics or runtime renderer.
#include "apps/editor/core/TerraWeavePlanetCavernsDoc.h"
#include "engine/render/Renderer3D.h"
#include <cmath>
#include <limits>
#include <optional>

namespace aiverse::studio {
struct StudioGlobeMarker {
    std::string id;
    render::float3 position{};
    Vec3 origin{},east{},up{},south{};
    Fixed extent{};
    float size=0;
};
struct StudioGlobeHit {
    int face=0;
    Fixed a{},b{};
    std::string regionId;
};
struct StudioPlanetGlobe {
    std::string body;
    game::CubeSphere cube;
    float scale=1;
    int divisions=0;
    std::vector<std::array<render::float3,3>> triangles;
    std::vector<StudioGlobeMarker> markers;
    static constexpr float previewRadius=10.f;

    static StudioPlanetGlobe capture(const editor::MapData& map,const std::string& body){
        namespace pc=editor::planet_caves;
        using namespace terraweave::caverns;
        const CavePlanetAnchor reference{body,0,{}, {},100};
        const auto frame=pc::frameForDocument(map,reference);
        const auto& surface=frame.surface();
        const auto& def=surface.bodies[size_t(frame.body())];
        StudioPlanetGlobe out;out.body=body;out.cube=surface.cubeOf(frame.body());
        out.scale=previewRadius/float(def.radius.toDouble());
        out.divisions=std::min(def.faceSize,64);
        const auto point=[&](Vec3 p){return render::float3{
            float(p.x.toDouble())*out.scale,float(p.y.toDouble())*out.scale,float(p.z.toDouble())*out.scale};};
        const auto coord=[&](int i){return Fixed::fromRatio(int64_t(i)*def.faceSize,out.divisions);};
        out.triangles.reserve(size_t(12)*out.divisions*out.divisions);
        for(int f=0;f<6;++f){
            const int n=out.divisions;
            std::vector<Vec3> corners(size_t(n+1)*(n+1));
            for(int z=0;z<=n;++z)for(int x=0;x<=n;++x)
                corners[size_t(z)*(n+1)+x]=surface.facePoint(frame.body(),f,coord(x),coord(z));
            const auto at=[&](int x,int z){return corners[size_t(z)*(n+1)+x];};
            const auto append=[&](Vec3 a,Vec3 b,Vec3 c){
                auto p=point(a),q=point(b),r=point(c);
                if(render::dot(render::cross(q-p,r-p),p)<0)std::swap(q,r);
                out.triangles.push_back({p,q,r});
            };
            for(int z=0;z<n;++z)for(int x=0;x<n;++x){
                const auto a=at(x,z),b=at(x+1,z),c=at(x+1,z+1),d=at(x,z+1);
                append(a,c,b);append(a,d,c);
            }
        }
        const auto regions=pc::readRegions(map);
        for(const auto& [id,region]:regions.regions){
            if(region.anchor.bodyId!=body)continue;
            const auto chart=pc::frameForDocument(map,region.anchor);
            const auto onSurface=surface.facePoint(frame.body(),region.anchor.face,region.anchor.a,region.anchor.b);
            const auto extent=Fixed::fromRatio(region.anchor.halfExtentCm,100);
            const auto size=std::clamp(float(extent.toDouble())*out.scale*.18f,.07f,.24f);
            out.markers.push_back({std::to_string(id),point(onSurface)+render::normalize(point(onSurface))*size,
                chart.originBody(),chart.east(),chart.up(),chart.south(),extent,size});
        }
        return out;
    }
    render::MeshBuilder mesh()const{
        render::MeshBuilder out;
        for(const auto& t:triangles){
            const auto n=render::normalize(render::cross(t[1]-t[0],t[2]-t[0]));
            const auto color=render::rgba(91,132,111);
            const auto a=out.addVertex(t[0],n,color),b=out.addVertex(t[1],n,color),c=out.addVertex(t[2],n,color);
            out.addTriangle(a,b,c);
        }
        return out;
    }
    // Nearest positive intersection on exactly the displayed overview
    // triangles. CubeSphere provides the canonical face tie-break at seams.
    std::optional<StudioGlobeHit> pick(render::float3 origin,render::float3 direction)const{
        const auto finite=[](render::float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        if(!finite(origin)||!finite(direction)||!std::isfinite(scale)||scale<=0||
           std::abs(render::dot(direction,direction)-1.f)>.01f)return {};
        float best=std::numeric_limits<float>::infinity();
        for(const auto& t:triangles){
            const auto u=t[1]-t[0],v=t[2]-t[0],h=render::cross(direction,v);
            const auto det=render::dot(u,h);
            if(std::abs(det)<1e-8f)continue;
            const auto s=origin-t[0];const auto b=render::dot(s,h)/det;
            if(b<0||b>1)continue;
            const auto q=render::cross(s,u);const auto c=render::dot(direction,q)/det;
            if(c<0||b+c>1)continue;
            const auto d=render::dot(v,q)/det;
            if(d>=0&&d<best)best=d;
        }
        if(!std::isfinite(best))return {};
        const auto p=origin+direction*best;
        const auto fixed=[](float x){return Fixed::fromRaw(int64_t(std::llround(double(x)*Fixed::kOne)));};
        const Vec3 bodyPoint{fixed(p.x/scale),fixed(p.y/scale),fixed(p.z/scale)};
        StudioGlobeHit hit;cube.faceFromDir(bodyPoint,hit.face,hit.a,hit.b);
        // Region selection is a chart footprint test on the reference sphere,
        // not a claim that the overview represents its actual cavern volume.
        const auto dir=fxNormalize(bodyPoint);
        for(const auto& marker:markers){
            if(dir.dot(marker.up)<=Fixed{})continue;
            const auto relative=dir*fxSqrt(marker.origin.lengthSq())-marker.origin;
            if(fxAbs(relative.dot(marker.east))<=marker.extent&&
               fxAbs(relative.dot(marker.south))<=marker.extent){
                hit.regionId=marker.id;break;
            }
        }
        return hit;
    }
};
} // namespace aiverse::studio
