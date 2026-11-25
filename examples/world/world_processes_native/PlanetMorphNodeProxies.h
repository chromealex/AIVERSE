// Native kinetic projection of MORPH nodes, not a second soft-body solver.
// Each sphere is a separate kinematic contact proxy (not a compound rigid
// body). The actual moving owner pose must be supplied by the game.
#pragma once
#include "MorphVertexDeformation.h"
#include "engine/physics/PhysisBox3D.h"
#include <map>
#include <set>
#include <tuple>

namespace aiverse::examples::world_processes_native {
struct PlanetMorphNodeProxyRequest {
    uint64_t instance=0;
    Entity owner=kNullEntity,node=kNullEntity;
    int32_t unitsPerMetre=1000;
    float radius=.25f,friction=.5f;
    uint32_t userData=0;
};
struct PlanetMorphNodeProxy {
    PlanetMorphNodeProxyRequest request;
    b3Vec3 target{};
};
inline std::optional<PlanetMorphNodeProxy> preparePlanetMorphNodeProxy(
        const World& world,const game::Surface& surface,std::string_view body,uint32_t tick,
        const PlanetMorphNodeProxyRequest& request){
    if(!request.instance||!std::isfinite(request.radius)||request.radius<.005f||request.radius>1000.f||
       !std::isfinite(request.friction)||request.friction<0||request.friction>10)return {};
    const auto frame=morph::resolvePlanetModelFrame(world,surface,request.owner,body,request.unitsPerMetre);
    if(!frame||!morph::planetModelNodeCurrent(world,*frame,request.node,tick))return {};
    const auto* node=world.tryGetConst<morph::Node>(request.node);
    const auto local=Vec3{Fixed::fromRatio(node->x,request.unitsPerMetre),
        Fixed::fromRatio(node->y,request.unitsPerMetre),Fixed::fromRatio(node->z,request.unitsPerMetre)};
    const auto position=frame->frame.origin+frame->frame.simulationVector(local);
    const auto resolved=lib::worldprocess::surfaceFrame(&surface,position);
    if(!resolved||resolved->body!=frame->frame.body)return {};
    return PlanetMorphNodeProxy{request,physics::physisB3(position-game::Surface::anchor(resolved->body))};
}
class PlanetMorphNodePublisher {
public:
    using Key=std::pair<uint64_t,uint64_t>; // instance, packed node
    size_t count()const{return entries_.size();}
    physics::BodyHandle handle(uint64_t instance,Entity node)const{
        const auto it=entries_.find({instance,node.packed()});
        return it==entries_.end()?physics::kNoBody:it->second.handle;
    }
    // Shape and identity immutable during residency; changed radius/owner
    // requires explicit removal/reset. Moving targets keep the same handle.
    bool publish(physics::Scene& scene,const std::vector<PlanetMorphNodeProxy>& desired){
        if((scene_&&scene_!=&scene)||desired.size()>1024)return false;
        for(const auto& [key,e]:entries_)if(!scene.valid(e.handle))return false;
        std::map<Key,Entry> next;
        std::vector<Key> create;
        for(const auto& proxy:desired){
            const auto& r=proxy.request;const Key key{r.instance,r.node.packed()};
            if(!r.instance||!r.owner.valid()||!r.node.valid()||next.contains(key)||
               !std::isfinite(r.radius)||r.radius<.005f||r.radius>1000.f||
               !std::isfinite(r.friction)||r.friction<0||r.friction>10||
               !std::isfinite(proxy.target.x)||!std::isfinite(proxy.target.y)||!std::isfinite(proxy.target.z)||
               std::abs(proxy.target.x)>100000||std::abs(proxy.target.y)>100000||std::abs(proxy.target.z)>100000)return false;
            const auto old=entries_.find(key);
            if(old!=entries_.end()){
                const auto& prev=old->second.proxy.request;
                if(prev.owner!=r.owner||prev.radius!=r.radius||prev.friction!=r.friction||
                   prev.unitsPerMetre!=r.unitsPerMetre||prev.userData!=r.userData)return false;
                const auto p=scene.position(old->second.handle);
                const auto maxMove=scene.dt()*1000.f;
                const auto dx=proxy.target.x-p.x,dy=proxy.target.y-p.y,dz=proxy.target.z-p.z;
                if(dx*dx+dy*dy+dz*dz>maxMove*maxMove)return false;
                next.emplace(key,Entry{old->second.handle,proxy});
            }else{
                next.emplace(key,Entry{physics::kNoBody,proxy});create.push_back(key);
            }
        }
        if(create.size()>scene.maxBodies()-scene.bodyCount())return false;
        std::vector<physics::BodyHandle> created;created.reserve(create.size());
        auto discard=[&](){for(auto handle:created)scene.destroy(handle);};
        try{
            for(const auto& key:create){
                auto& entry=next.at(key);const auto& r=entry.proxy.request;
                physics::BodyDesc d;d.motion=physics::Motion::Kinematic;d.shape=physics::ShapeKind::Sphere;
                d.position=entry.proxy.target;d.radius=r.radius;d.friction=r.friction;
                d.userData=r.userData;d.gravityScale=0;
                // Node proxies never collide with one another. Only real
                // dynamic bodies produce physical reaction/contact input.
                d.category=physics::kCategoryKinematic;d.mask=physics::kCategoryDynamic|physics::kCategoryQuery;
                entry.handle=scene.create(d);
                if(entry.handle==physics::kNoBody){discard();return false;}
                created.push_back(entry.handle);
            }
        }catch(...){discard();throw;}
        // Scene owner is serialized. All targets validated above.
        for(const auto& [key,e]:next)if(!scene.moveKinematic(e.handle,e.proxy.target,b3Quat_identity)){
            discard();return false;
        }
        for(const auto& [key,e]:entries_)if(!next.contains(key))scene.destroy(e.handle);
        entries_=std::move(next);scene_=&scene;return true;
    }
    bool appendContactBindings(std::map<physics::BodyHandle,Entity>& nodes,
                               std::map<uint64_t,Entity>& owners)const{
        for(const auto& [key,e]:entries_){
            const auto& r=e.proxy.request;
            const auto [n,newNode]=nodes.emplace(e.handle,r.node);
            if(!newNode&&n->second!=r.node)return false;
            const auto [o,newOwner]=owners.emplace(r.node.packed(),r.owner);
            if(!newOwner&&o->second!=r.owner)return false;
        }
        return true;
    }
    bool reset(physics::Scene& scene){
        if(scene_&&scene_!=&scene)return false;
        for(const auto& [key,e]:entries_)if(scene.valid(e.handle))scene.destroy(e.handle);
        entries_.clear();scene_=nullptr;return true;
    }
private:
    struct Entry {physics::BodyHandle handle;PlanetMorphNodeProxy proxy;};
    std::map<Key,Entry> entries_;
    physics::Scene* scene_=nullptr;
};
} // namespace aiverse::examples::world_processes_native
