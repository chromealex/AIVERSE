// Seeded cosmetic geometry in metres. Not a deterministic collision shape.
// Reuse one model per variant; existing Renderer3D owns upload, culling and LOD.
#pragma once
#include "engine/world/TerraWeaveRocks.h"
#include "engine/render/Model.h"
#include <map>
#include <memory>
#include <cmath>

namespace aiverse::terraweave {
enum class RockShape : uint8_t { Rounded, Boulder, Slab, Shard };
struct RockProfile {
    RockShape shape=RockShape::Boulder;
    int32_t widthCm=240,heightCm=190,depthCm=210;
    uint32_t weatheringPermille=400;
    int32_t textureRepeatCm=200;
};
namespace rock_detail {
using render::float3;
inline float rdot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float3 rcross(float3 a,float3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float3 runit(float3 a){const float q=std::sqrt(rdot(a,a));return a*(1.f/q);}
inline float random(uint64_t seed,uint64_t n){return float(detail::mix64(seed+n*0x9e3779b97f4a7c15ULL)>>40)/16777215.f;}
struct Surface {
    RockProfile p;uint64_t seed;
    std::array<float3,14> cuts;
    std::array<float,14> depths;
    Surface(RockProfile profile,uint64_t value):p(profile),seed(value) {
        for(size_t i=0;i<cuts.size();++i){
            cuts[i]=runit(float3{random(seed,i*4)*2-1,random(seed,i*4+1)*2-1,random(seed,i*4+2)*2-1});
            depths[i]=.54f+.32f*random(seed,i*4+3);
            if(i<6){cuts[i]={0,0,0};const float sign=(i%2)?-1.f:1.f;if(i<2)cuts[i].x=sign;else if(i<4)cuts[i].y=sign;else cuts[i].z=sign;
                depths[i]=.65f+.22f*random(seed,i*4+3);}
        }
    }
    float3 at(float3 n)const {
        float radius=1;
        for(size_t i=0;i<cuts.size();++i){const float d=rdot(n,cuts[i]);if(d>.001f)radius=std::min(radius,depths[i]/d);}
        const float smooth=float(p.weatheringPermille)/1000;
        radius=radius*(1-smooth*.65f)+smooth*.65f;
        if(p.shape==RockShape::Rounded)radius=.4f*radius+.6f;
        const float phase=random(seed,100)*6.2831853f;
        // Continuous seed-owned displacement, independent of tessellation/LOD.
        radius*=1+.09f*std::sin(3*n.x+phase)*std::sin(4*n.z-phase)+.02f*std::sin(9*n.y+3*n.x+phase);
        float3 v=n*radius;
        if(p.shape==RockShape::Slab){v.y*=.40f;v.x+=v.y*.18f;}
        if(p.shape==RockShape::Shard){const float taper=.72f-.25f*v.y;v.x*=.85f*taper;v.z*=taper;v.y*=1.45f;v.x+=v.y*.24f;}
        v.x+=.10f*n.y*n.y*(random(seed,200)-.5f);
        return {v.x*float(p.widthCm)*.005f*(.8f+.4f*random(seed,301)),v.y*float(p.heightCm)*.005f*(.8f+.4f*random(seed,302)),v.z*float(p.depthCm)*.005f*(.8f+.4f*random(seed,303))};
    }
};
struct Sphere {std::vector<float3> points;std::vector<std::array<uint32_t,3>> faces;};
inline Sphere sphere(uint32_t divisions) {
    const float t=(1+std::sqrt(5.f))/2;
    Sphere s{{{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}},
        {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}}};
    for(auto& p:s.points)p=runit(p);
    for(uint32_t d=0;d<divisions;++d){
        std::map<std::pair<uint32_t,uint32_t>,uint32_t> edges;std::vector<std::array<uint32_t,3>> next;
        auto mid=[&](uint32_t a,uint32_t b){auto key=std::minmax(a,b);auto it=edges.find(key);if(it!=edges.end())return it->second;
            const auto index=uint32_t(s.points.size());s.points.push_back(runit(s.points[a]+s.points[b]));edges.emplace(key,index);return index;};
        for(auto f:s.faces){auto a=mid(f[0],f[1]),b=mid(f[1],f[2]),c=mid(f[2],f[0]);next.push_back({f[0],a,c});next.push_back({f[1],b,a});next.push_back({f[2],c,b});next.push_back({a,b,c});}
        s.faces=std::move(next);
    }
    return s;
}
} // namespace rock_detail
inline std::unique_ptr<render::Model> makeRockModel(const RockProfile& profile,uint64_t seed) {
    if(profile.shape>RockShape::Shard || profile.widthCm<1 || profile.widthCm>100000 ||
       profile.heightCm<1 || profile.heightCm>100000 || profile.depthCm<1 || profile.depthCm>100000 ||
       profile.weatheringPermille>1000 || profile.textureRepeatCm<1 || profile.textureRepeatCm>100000)
        throw std::invalid_argument("TerraWeave rock profile");
    using namespace rock_detail;
    auto model=std::make_unique<render::Model>();model->path="terraweave/rocks/"+std::to_string(seed);
    model->batching.autoLod=false;model->batching.independentShadowLod=true;
    render::Material material;material.name="TerraWeave Rock";material.baseColor={.65f,.65f,.65f,1};material.metallic=0;material.roughness=.88f;
    model->materials.push_back(material);
    Surface surface(profile,seed);
    // Box mapping: each triangle takes the planar projection of its dominant axis, in metres / repeat.
    // Uniform texel density everywhere; a latitude/longitude mapping pinched the texture into a swirl at the
    // top and bottom poles. Vertices are per face already, so the projection switch needs no extra seams.
    const float perMetre=100.f/float(profile.textureRepeatCm);
    for(uint32_t division:{3u,2u,1u}) {
        const auto sphere=rock_detail::sphere(division);
        render::Primitive prim;prim.material=0;prim.firstVertex=uint32_t(model->vertices.size());prim.firstIndex=uint32_t(model->indices.size());
        for(auto face:sphere.faces){
            std::array<render::ModelVertex,3> vs;
            for(size_t k=0;k<3;++k){
                const auto n=sphere.points[face[k]];auto& v=vs[k];v.pos=surface.at(n);
                const auto helper=std::abs(n.y)<.9f?float3{0,1,0}:float3{1,0,0};
                const auto a=runit(rcross(helper,n)),b=rcross(n,a);constexpr float e=.001f;
                v.nrm=runit(rcross(surface.at(runit(n+a*e))-surface.at(runit(n-a*e)),surface.at(runit(n+b*e))-surface.at(runit(n-b*e))));
            }
            const auto fn=rcross(vs[1].pos-vs[0].pos,vs[2].pos-vs[0].pos);
            const float ax=std::abs(fn.x),ay=std::abs(fn.y),az=std::abs(fn.z);
            for(size_t k=0;k<3;++k){
                const auto q=vs[k].pos*perMetre;
                // sign flips keep each projection unmirrored seen from outside
                if(ay>=ax && ay>=az)vs[k].uv={fn.y>0?q.x:-q.x,q.z};
                else if(ax>=az)vs[k].uv={fn.x>0?-q.z:q.z,-q.y};
                else vs[k].uv={fn.z>0?q.x:-q.x,-q.y};
                model->indices.push_back(uint32_t(model->vertices.size()));model->vertices.push_back(vs[k]);
            }
        }
        prim.vertexCount=uint32_t(model->vertices.size())-prim.firstVertex;prim.indexCount=uint32_t(model->indices.size())-prim.firstIndex;
        render::MeshDef mesh;mesh.name="Rock LOD"+std::to_string(model->meshes.size());mesh.primitives.push_back(prim);model->meshes.push_back(std::move(mesh));
    }
    model->meshes[0].lods={{1,100.f},{2,28.f}};
    render::Node node;node.name="TerraWeave Rock";node.mesh=0;model->nodes.push_back(node);
    render::generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
    model->finalize();return model;
}
struct RockPose {render::float3 position;float scale=1,yawRadians=0;};
// Provide a local render origin to preserve precision in large worlds.
inline RockPose rockPose(const render::Model& model,const RockInstance& at,int64_t originXcm=0,int64_t originZcm=0) {
    if(at.radiusCm<1 || at.radiusCm>10000 || at.burialPermille>750 || at.yawMilliDegrees>=360000 ||
       at.xCm < -kWorldLimitCm || at.xCm>kWorldLimitCm || at.zCm < -kWorldLimitCm || at.zCm>kWorldLimitCm ||
       originXcm < -kWorldLimitCm || originXcm>kWorldLimitCm || originZcm < -kWorldLimitCm || originZcm>kWorldLimitCm)
        throw std::invalid_argument("TerraWeave rock pose");
    const float radius=std::sqrt(std::pow(std::max(std::abs(model.boundsMin.x),std::abs(model.boundsMax.x)),2)+
                                 std::pow(std::max(std::abs(model.boundsMin.z),std::abs(model.boundsMax.z)),2));
    if(!std::isfinite(radius) || radius<=0)throw std::invalid_argument("TerraWeave rock model bounds");
    const float scale=float(at.radiusCm)*.01f/radius;
    const float y=float(at.groundCm)*.01f-(model.boundsMin.y+(model.boundsMax.y-model.boundsMin.y)*float(at.burialPermille)/1000)*scale;
    return {{float(at.xCm-originXcm)*.01f,y,float(at.zCm-originZcm)*.01f},scale,float(at.yawMilliDegrees)*.00001745329252f};
}
} // namespace aiverse::terraweave
