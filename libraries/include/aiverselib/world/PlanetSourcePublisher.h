// Explicitly owned static source collision projection and visual switch.
// Complete desired source set per tick; no arbitrary external body deletion.
#pragma once
#include "aiverselib/world/PlanetSourceIdentity.h"
#include "engine/physics/PhysisBox3D.h"
#include <map>
#include <set>
namespace aiverse::lib::worldprocess {
struct PlanetSourceCollider {
    lib::worldprocess::PlanetSourceKey key;
    physics::BodyDesc desc; // body-local static geometry
};
class PlanetSourcePublisher {
public:
    using Key=lib::worldprocess::PlanetSourceKey;
    size_t count()const{return entries_.size();}
    physics::BodyHandle handle(Key key)const{
        auto it=entries_.find(key);return it==entries_.end()?physics::kNoBody:it->second.handle;
    }
    bool publish(physics::Scene& scene,const World& world,const game::Surface& surface,
                 std::string_view body,std::span<const PlanetSourceCollider> sources,bool replacePlantGeometry=false){
        using namespace lib::worldprocess;
        const int index=surface.findBody(std::string(body));
        if(index<0||(scene_&&scene_!=&scene)||sources.size()>kMaxConsequences)return false;
        const auto anchor=game::Surface::anchor(index);
        std::map<Key,Entry> next;
        std::set<Key> seen;
        std::vector<Key> additions;
        size_t vertices=0,indices=0;
        for(const auto& src:sources){
            if(!seen.insert(src.key).second||!validPlanetSourceBinding(world,surface,src.key,body)||
               src.desc.motion!=physics::Motion::Static||src.desc.sensor||
               src.desc.vertices.size()>262144-vertices||src.desc.indices.size()>1572864-indices)return false;
            vertices+=src.desc.vertices.size();indices+=src.desc.indices.size();
            Vec3 local{};
            if(!physics::physisVec3(src.desc.position,local))return false;
            const auto frame=surfaceFrame(&surface,local+anchor);
            if(!frame||frame->body!=index)return false;
            if(planetSourceRetired(world,src.key,body))continue;
            const auto old=entries_.find(src.key);
            if(old!=entries_.end()){
                if(!scene.valid(old->second.handle))return false;
                if(same(old->second.desc,src.desc))next.emplace(src.key,old->second);
                else{
                    if(!replacePlantGeometry||src.key.kind!=ConsequenceKind::Plant)return false;
                    next.emplace(src.key,Entry{physics::kNoBody,src.desc});additions.push_back(src.key);
                }
            }else{
                next.emplace(src.key,Entry{physics::kNoBody,src.desc});additions.push_back(src.key);
            }
        }
        if(additions.size()>scene.maxBodies()-scene.bodyCount())return false;
        std::vector<physics::BodyHandle> made;made.reserve(additions.size());
        auto discard=[&](){for(auto h:made)scene.destroy(h);};
        try{
            for(const auto& key:additions){
                auto& entry=next.at(key);entry.handle=scene.create(entry.desc);
                if(!entry.handle){discard();return false;}
                made.push_back(entry.handle);
            }
        }catch(...){discard();throw;}
        for(const auto& [key,e]:entries_)if((!next.contains(key)||next.at(key).handle!=e.handle)&&scene.valid(e.handle))scene.destroy(e.handle);
        entries_=std::move(next);scene_=&scene;return true;
    }
    bool reset(physics::Scene& scene){
        if(scene_&&scene_!=&scene)return false;
        for(const auto& [key,e]:entries_)if(scene.valid(e.handle))scene.destroy(e.handle);
        entries_.clear();scene_=nullptr;return true;
    }
private:
    struct Entry{physics::BodyHandle handle;physics::BodyDesc desc;};
    static bool same(const physics::BodyDesc& a,const physics::BodyDesc& b){
        const auto eq=[](b3Vec3 x,b3Vec3 y){return x.x==y.x&&x.y==y.y&&x.z==y.z;};
        if(a.shape!=b.shape||!eq(a.position,b.position)||!eq(a.size,b.size)||!eq(a.rotation.v,b.rotation.v)||
           a.rotation.s!=b.rotation.s||a.radius!=b.radius||a.halfHeight!=b.halfHeight||
           a.friction!=b.friction||a.restitution!=b.restitution||a.category!=b.category||
           a.mask!=b.mask||a.userData!=b.userData||a.indices!=b.indices||a.vertices.size()!=b.vertices.size())return false;
        for(size_t i=0;i<a.vertices.size();++i)if(!eq(a.vertices[i],b.vertices[i]))return false;
        return true;
    }
    std::map<Key,Entry> entries_;
    physics::Scene* scene_=nullptr;
};
} // namespace aiverse::lib::worldprocess
