#pragma once
// Native GI projection uses the same body centers and LENS view frame as
// PlanetRenderer::draw, not the deterministic physics namespace anchors.
#include "engine/world/TerraWeaveCavernsPlanetGiPublication.h"
#include "engine/view/game/GamePlanet.h"
#include "engine/render/CameraSphereOrbit.h"
#include <map>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace aiverse::terraweave::caverns {
struct CavePlanetGiViewFrame {
    render::SphereViewFrame frame;
    std::map<std::string,render::float3> centers; // already in view space

    static CavePlanetGiViewFrame capture(const game::PlanetWorld& world,
            const render::SphereViewFrame& frame,double seconds){
        if(!std::isfinite(seconds))throw std::invalid_argument("Caverns GI frame time");
        const auto finite=[](render::float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        if(!finite(frame.origin)||!finite(frame.right)||!finite(frame.up)||!finite(frame.back)||
           std::abs(render::dot(frame.right,frame.right)-1.f)>.001f||
           std::abs(render::dot(frame.up,frame.up)-1.f)>.001f||
           std::abs(render::dot(frame.back,frame.back)-1.f)>.001f||
           std::abs(render::dot(frame.right,frame.up))>.001f||
           std::abs(render::dot(frame.right,frame.back))>.001f||
           std::abs(render::dot(frame.up,frame.back))>.001f||
           render::dot(render::cross(frame.right,frame.up),frame.back)<.999f)
            throw std::invalid_argument("Caverns GI invalid view basis");
        CavePlanetGiViewFrame out;out.frame=frame;
        for(size_t i=0;i<world.bodies.size();++i){
            const auto& id=world.bodies[i].id;
            const auto center=frame.toView(world.center(int(i),seconds));
            if(id.empty()||!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(center.z)||
               !out.centers.emplace(id,center).second)throw std::invalid_argument("Caverns GI body center");
        }
        return out;
    }
    CavePlanetGiProject projector()const{
        return [snapshot=*this](const std::string& body,Vec3 p){
            const render::float3 local{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};
            return snapshot.centers.at(body)+snapshot.frame.dirToView(local);
        };
    }
    // Host supplies stable chunk ownership (nullopt = static world-space
    // geometry). Other moving producers require their own transform mapping.
    using BodyForChunk=std::function<std::optional<std::string>(uint64_t)>;
    CavePlanetGiOtherProject remapFrom(const CavePlanetGiViewFrame& previous,
            BodyForChunk bodyForChunk)const{
        if(!bodyForChunk)throw std::invalid_argument("Caverns GI chunk coordinate ownership");
        return [next=*this,previous,bodyForChunk=std::move(bodyForChunk)](uint64_t id,render::float3 p){
            const auto body=bodyForChunk(id);
            if(body){
                const auto local=previous.frame.dirToWorld(p-previous.centers.at(*body));
                return next.centers.at(*body)+next.frame.dirToView(local);
            }
            const auto world=previous.frame.origin+previous.frame.dirToWorld(p);
            return next.frame.toView(world);
        };
    }
};
// Exact host-owned baseline reprojection: the current projected vertices
// are matched to the original fixed body vertices, never inverse-transformed
// from float. This avoids accumulated drift before a later cave lease.
struct CavePlanetGiCanonicalChunk {
    std::string body;
    std::vector<world::Triangle> triangles;
};
inline CavePlanetGiOtherProject exactCavePlanetGiBaselineRemap(
        const CavePlanetGiProject& previous,const CavePlanetGiProject& next,
        const std::map<uint64_t,CavePlanetGiCanonicalChunk>& chunks){
    if(!previous||!next||chunks.empty())throw std::invalid_argument("Caverns exact GI baseline source");
    using Key=std::array<float,3>;
    struct Vertex {Vec3 source;render::float3 target;};
    std::map<uint64_t,std::map<Key,Vertex>> mapping;
    const auto key=[](render::float3 p){return Key{p.x,p.y,p.z};};
    const auto finite=[](render::float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    size_t count=0;
    for(const auto& [id,chunk]:chunks){
        if(!id||chunk.body.empty()||chunk.triangles.empty()||chunk.triangles.size()>262144-count)
            throw std::invalid_argument("Caverns exact GI baseline budget");
        count+=chunk.triangles.size();auto& vertices=mapping[id];
        for(const auto& triangle:chunk.triangles)for(const auto p:{triangle.a,triangle.b,triangle.c}){
            const auto a=previous(chunk.body,p),b=next(chunk.body,p);
            if(!finite(a)||!finite(b))throw std::invalid_argument("Caverns exact GI nonfinite projection");
            const auto [it,added]=vertices.emplace(key(a),Vertex{p,b});
            if(!added&&it->second.source!=p)throw std::invalid_argument("Caverns exact GI ambiguous vertex");
        }
    }
    return [mapping=std::move(mapping),key,finite](uint64_t id,render::float3 p){
        if(!finite(p))throw std::invalid_argument("Caverns exact GI invalid vertex");
        return mapping.at(id).at(key(p)).target;
    };
}
} // namespace aiverse::terraweave::caverns
