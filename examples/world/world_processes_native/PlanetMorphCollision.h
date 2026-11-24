// Static deformable mesh projection for the existing Box3D Scene.
// The collision asset must use the same pristine model-local positions,
// indices, bindings and scale as the visual asset if exact parity is desired.
#pragma once
#include "MorphVertexDeformation.h"
#include "engine/physics/PhysisBox3D.h"
#include <map>
#include <set>

namespace aiverse::examples::world_processes_native {
struct PlanetMorphCollisionRequest {
    uint64_t id=0;
    Entity owner=kNullEntity;
    int32_t unitsPerMetre=1000;
    std::span<const render::float3> pristine;
    std::span<const uint32_t> indices;
    std::span<const MorphVertexBinding> bindings;
    MorphModelStreamConfig config{};
    float friction=.5f;
    uint32_t userData=0;
};
struct PlanetMorphPreparedCollider {
    uint64_t id=0;
    physics::BodyDesc desc;
};
inline std::optional<PlanetMorphPreparedCollider> preparePlanetMorphCollider(
        const World& world,const game::Surface& surface,std::string_view body,
        uint32_t tick,const PlanetMorphCollisionRequest& request){
    if(!request.id||request.pristine.empty()||request.pristine.size()>65536||
       request.indices.empty()||request.indices.size()%3||request.indices.size()>393216||
       !std::isfinite(request.config.metresPerUnit)||request.config.metresPerUnit<=0||
       !std::isfinite(request.config.maxDisplacementMetres)||request.config.maxDisplacementMetres<=0||
       request.config.maxDisplacementMetres>1000||!std::isfinite(request.friction)||
       request.friction<0||request.friction>10||
       !validMorphVertexBindings(request.pristine.size(),request.bindings))return {};
    const auto frame=morph::resolvePlanetModelFrame(world,surface,request.owner,body,request.unitsPerMetre);
    if(!frame||std::abs(request.config.metresPerUnit*float(request.unitsPerMetre)-1.f)>1e-4f)return {};
    std::vector<render::float3> positions(request.pristine.begin(),request.pristine.end());
    for(const auto& bind:request.bindings){
        for(size_t i=0;i<4;++i)if(bind.weight[i]&&!morph::planetModelNodeCurrent(world,*frame,bind.node[i],tick))return {};
        render::float3 delta{};
        if(!morphVertexDisplacement(world,bind,request.config,delta))return {};
        positions[bind.vertex]+=delta;
    }
    PlanetMorphPreparedCollider result;result.id=request.id;
    auto& d=result.desc;d.motion=physics::Motion::Static;d.shape=physics::ShapeKind::Mesh;
    d.friction=request.friction;d.userData=request.userData;
    const auto anchor=game::Surface::anchor(frame->frame.body);
    d.position=physics::physisB3(frame->frame.origin-anchor);
    const auto asFloat=[](Vec3 p){return render::float3{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};};
    const auto x=asFloat(frame->frame.east),y=asFloat(frame->frame.up),z=asFloat(frame->frame.south);
    d.vertices.reserve(positions.size());
    for(const auto& v:positions){
        const auto p=x*v.x+y*v.y+z*v.z;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
           std::abs(p.x)>100000||std::abs(p.y)>100000||std::abs(p.z)>100000)return {};
        d.vertices.push_back({p.x,p.y,p.z});
    }
    d.indices.reserve(request.indices.size());
    for(size_t i=0;i<request.indices.size();i+=3){
        const auto a=request.indices[i],b=request.indices[i+1],c=request.indices[i+2];
        if(a>=positions.size()||b>=positions.size()||c>=positions.size()||a==b||a==c||b==c)return {};
        const auto normal=render::cross(positions[b]-positions[a],positions[c]-positions[a]);
        const auto area=render::length(normal);
        if(!std::isfinite(area)||area<=1e-7f)return {};
        d.indices.insert(d.indices.end(),{int32_t(a),int32_t(b),int32_t(c)});
    }
    return result;
}
class PlanetMorphCollisionPublisher {
public:
    size_t count()const{return entries_.size();}
    physics::BodyHandle handle(uint64_t id)const{
        const auto it=entries_.find(id);return it==entries_.end()?physics::kNoBody:it->second.handle;
    }
    bool publish(physics::Scene& scene,std::vector<PlanetMorphPreparedCollider> prepared){
        if(scene_&&scene_!=&scene)return false;
        if(prepared.size()>256)return false;
        for(const auto& [id,e]:entries_)if(!scene.valid(e.handle))return false;
        std::map<uint64_t,Entry> next;
        std::vector<uint64_t> create;
        for(auto& p:prepared){
            if(!p.id||next.contains(p.id))return false;
            const auto old=entries_.find(p.id);
            auto handle=physics::kNoBody;
            if(old!=entries_.end()&&same(old->second.desc,p.desc))handle=old->second.handle;
            else create.push_back(p.id);
            next.emplace(p.id,Entry{handle,std::move(p.desc)});
        }
        if(create.size()>scene.maxBodies()-scene.bodyCount())return false;
        std::vector<physics::BodyHandle> created;created.reserve(create.size());
        const auto discard=[&](){for(auto h:created)scene.destroy(h);};
        try{
            for(auto id:create){
                auto& item=next.at(id);item.handle=scene.create(item.desc);
                if(item.handle==physics::kNoBody){discard();return false;}
                created.push_back(item.handle);
            }
        }catch(...){discard();throw;}
        for(const auto& [id,old]:entries_){
            const auto it=next.find(id);
            if(it==next.end()||it->second.handle!=old.handle)scene.destroy(old.handle);
        }
        entries_=std::move(next);scene_=&scene;return true;
    }
    bool reset(physics::Scene& scene){
        if(scene_&&scene_!=&scene)return false;
        for(const auto& [id,e]:entries_)if(scene.valid(e.handle))scene.destroy(e.handle);
        entries_.clear();scene_=nullptr;return true;
    }
private:
    static bool same(const physics::BodyDesc& a,const physics::BodyDesc& b){
        const auto eq=[](b3Vec3 x,b3Vec3 y){return x.x==y.x&&x.y==y.y&&x.z==y.z;};
        if(!eq(a.position,b.position)||a.friction!=b.friction||a.userData!=b.userData||
           a.indices!=b.indices||a.vertices.size()!=b.vertices.size())return false;
        for(size_t i=0;i<a.vertices.size();++i)if(!eq(a.vertices[i],b.vertices[i]))return false;
        return true;
    }
    struct Entry {physics::BodyHandle handle;physics::BodyDesc desc;};
    std::map<uint64_t,Entry> entries_;
    physics::Scene* scene_=nullptr;
};
} // namespace aiverse::examples::world_processes_native
