#pragma once
// Opt-in, authoritative furniture occupancy adapter. No animation, inventory,
// teleport or additional actor store. Game command handlers call this on the
// existing fixed-tick Simulation owner, after their authorization checks.
#include "engine/buildings/BuildingLiftEcsBridge.h"
#include "engine/sim/Simulation.h"
#include "engine/core/Hash.h"
#include <aiverselib/systems/Reservations.h>
#include <limits>
#include <functional>
#include <set>

namespace aiverse::building {
struct BuildingFurnitureSceneState {
    uint64_t fingerprint=0;
    std::map<int64_t,int64_t> owners; // deterministic slot ordinal -> packed actor
};
inline void io(Archive& a,BuildingFurnitureSceneState& s){
    a(s.fingerprint,s.owners);
    if(!s.fingerprint||s.owners.size()>512)a.fail();
    std::set<int64_t> actors;
    for(const auto& [slot,actor]:s.owners)
        if(slot<=0||slot>512||actor<=0||!actors.insert(actor).second)a.fail();
}
struct BuildingFurnitureWorldState {
    std::map<uint64_t,BuildingFurnitureSceneState> scenes;
};
inline void io(Archive& a,BuildingFurnitureWorldState& s){
    a(s.scenes);if(s.scenes.size()>256)a.fail();
    std::set<int64_t> actors;
    for(const auto& [id,scene]:s.scenes){
        if(!id)a.fail();
        for(const auto& [slot,actor]:scene.owners){(void)slot;if(!actors.insert(actor).second)a.fail();}
    }
}
inline void registerBuildingFurniturePersistence(Simulation& sim){
    sim.registerPersistentResource<BuildingFurnitureWorldState>("building.furniture",1);
    sim.world().setResource<BuildingFurnitureWorldState>();
}
struct BuildingFurnitureWorldSocket {
    std::string id,furnitureId,roomId;
    lib::systems::BuildingFurnitureAction action;
    Vec3 approach,target;
};
class BuildingFurnitureEcs {
public:
    bool bind(World& world,uint64_t sceneId,const lib::systems::RuntimeBuildingResult& generated,
              Vec3 origin,uint32_t yaw,std::string* error=nullptr){
        auto fail=[&](const char* why){if(error)*error=why;return false;};
        const auto* all=world.resourceConst<BuildingFurnitureWorldState>();
        if(!all||!sceneId||!generated||!generated.structureGraph||yaw>3)return fail("Furniture state or generated scene unavailable");
        const float w=generated.building.constructionSize.width,d=generated.building.constructionSize.depth;
        if(!(w>0&&d>0)||generated.structureGraph.furnitureSockets.size()>512)return fail("Invalid furniture socket budget or dimensions");
        std::vector<BuildingFurnitureWorldSocket> next;
        for(const auto& s:generated.structureGraph.furnitureSockets){
            const auto a=buildingLocalToWorld(s.x,s.y,s.z,w,d,origin,yaw);
            const auto b=buildingLocalToWorld(s.targetX,s.targetY,s.targetZ,w,d,origin,yaw);
            next.push_back({s.id,s.furnitureId,s.roomId,s.action,a,b});
        }
        std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        Hasher hash;hash.str("building.furniture.v1");hash.u64(sceneId);
        for(size_t i=0;i<next.size();++i){
            const auto& s=next[i];if(s.id.empty()||(i&&next[i-1].id==s.id))return fail("Duplicate furniture socket");
            hash.str(s.id);hash.str(s.furnitureId);hash.str(s.roomId);hash.u32(uint32_t(s.action));hash.v3(s.approach);hash.v3(s.target);
        }
        const uint64_t identity=hash.value();
        if(!identity)return fail("Invalid furniture fingerprint");
        const auto old=all->scenes.find(sceneId);
        if(old!=all->scenes.end()){
            if(old->second.fingerprint!=identity)return fail("Restored furniture content differs");
            for(const auto& [slot,actor]:old->second.owners)
                if(slot<=0||size_t(slot)>next.size()||actor<=0||!world.alive(Entity::fromPacked(uint64_t(actor))))
                    return fail("Restored furniture owner or slot is invalid");
        }else{
            if(all->scenes.size()>=256)return fail("Furniture scene budget exceeded");
            world.resource<BuildingFurnitureWorldState>()->scenes.emplace(sceneId,BuildingFurnitureSceneState{identity,{}});
        }
        sceneId_=sceneId;fingerprint_=identity;sockets_=std::move(next);
        if(error)error->clear();return true;
    }
    const std::vector<BuildingFurnitureWorldSocket>& sockets()const{return sockets_;}
    // Advisory query, never authorizes a subsequent command.
    bool available(const World& world,const std::string& id)const{
        const auto* scene=state(world);const auto slot=find(id);
        return scene&&slot>0&&!scene->owners.contains(slot);
    }
    template<BuildingPositionBody Body>
    bool claim(World& world,Entity actor,const std::string& id,
               const std::function<bool(Entity,const BuildingFurnitureWorldSocket&)>& reachable,
               std::string* error=nullptr)const{
        auto fail=[&](const char* why){if(error)*error=why;return false;};
        const auto* scene=state(world);const auto slot=find(id);
        if(!scene||slot<=0||!actor.valid()||!world.alive(actor)||actor.packed()>uint64_t(INT64_MAX)||!reachable)
            return fail("Furniture claim requires a live actor, valid socket and reachability guard");
        const auto* body=world.tryGet<Body>(actor);const auto& s=sockets_[size_t(slot-1)];
        if(!body||fxAbs(body->pos.x-s.approach.x)>buildingFixedMm(450)||
           fxAbs(body->pos.z-s.approach.z)>buildingFixedMm(450)||fxAbs(body->pos.y-s.approach.y)>buildingFixedMm(300)||
           !reachable(actor,s))return fail("Actor is not at a reachable interaction point");
        const int64_t owner=int64_t(actor.packed());
        const auto* all=world.resourceConst<BuildingFurnitureWorldState>();
        for(const auto& [key,other]:all->scenes)for(const auto& [n,a]:other.owners)
            if(a==owner&&(key!=sceneId_||n!=slot))return fail("Actor already occupies another furniture socket");
        // Reuse the engine reservation algorithm; ECS map is the canonical
        // serialized representation, the book is only a transient adapter.
        lib::systems::ReservationBook book;
        lib::systems::Quantities stock;
        for(size_t i=0;i<sockets_.size();++i)stock.emplace(int64_t(i+1),1);
        for(const auto& [n,a]:scene->owners)book.tickets.emplace(n,lib::systems::Reservation{a,{{n,1}}});
        const auto status=lib::systems::reserve(book,stock,slot,owner,{{slot,1}});
        if(status!=lib::systems::ReserveResult::Ok&&status!=lib::systems::ReserveResult::Duplicate)
            return fail("Furniture socket already reserved");
        world.resource<BuildingFurnitureWorldState>()->scenes.at(sceneId_).owners.emplace(slot,owner);
        if(error)error->clear();return true;
    }
    bool release(World& world,Entity actor,const std::string& id)const{
        const auto* scene=state(world);const auto slot=find(id);
        if(!scene||slot<=0||!actor.valid()||actor.packed()>uint64_t(INT64_MAX))return false;
        const auto it=scene->owners.find(slot);
        if(it==scene->owners.end()||it->second!=int64_t(actor.packed()))return false;
        lib::systems::ReservationBook book;book.tickets.emplace(slot,lib::systems::Reservation{it->second,{{slot,1}}});
        if(lib::systems::release(book,slot,it->second)!=lib::systems::ReserveResult::Ok)return false;
        world.resource<BuildingFurnitureWorldState>()->scenes.at(sceneId_).owners.erase(slot);return true;
    }
    // Explicit lifecycle cleanup, including actors retired since the last tick.
    size_t removeRetired(World& world)const{
        if(!state(world))return 0;
        auto& owners=world.resource<BuildingFurnitureWorldState>()->scenes.at(sceneId_).owners;
        size_t count=0;for(auto it=owners.begin();it!=owners.end();){
            if(!world.alive(Entity::fromPacked(uint64_t(it->second)))){it=owners.erase(it);++count;}else ++it;
        }return count;
    }
    bool canUnload(const World& world)const{const auto* scene=state(world);return scene&&scene->owners.empty();}
    bool detachVacant(World& world){
        if(!canUnload(world))return false;
        world.resource<BuildingFurnitureWorldState>()->scenes.erase(sceneId_);sceneId_=0;fingerprint_=0;sockets_.clear();return true;
    }
private:
    const BuildingFurnitureSceneState* state(const World& world)const{
        const auto* all=world.resourceConst<BuildingFurnitureWorldState>();if(!all)return nullptr;
        const auto it=all->scenes.find(sceneId_);
        return it!=all->scenes.end()&&it->second.fingerprint==fingerprint_?&it->second:nullptr;
    }
    int64_t find(const std::string& id)const{
        const auto it=std::lower_bound(sockets_.begin(),sockets_.end(),id,[](const auto& s,const auto& key){return s.id<key;});
        return it!=sockets_.end()&&it->id==id?int64_t(it-sockets_.begin()+1):0;
    }
    uint64_t sceneId_=0,fingerprint_=0;
    std::vector<BuildingFurnitureWorldSocket> sockets_;
};
} // namespace aiverse::building
