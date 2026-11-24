#pragma once
// Physical lift landing doors on the existing SpatialWorld, independent of
// genre and architectural palette. Fully open only when cabin is physically
// at that floor AND the authoritative lift phase is Open. The game assigns
// collider IDs; no new physics world or ECS entity namespace is invented.
//
// Geometry: two 0.12m-thick sliding leaves mounted at the local shaft's
// front (-Z) edge. A normal static building has no opaque facade across the
// lift core; these colliders are the sole gameplay barrier at the landing.
#include "engine/buildings/BuildingLiftColliderOwner.h"
#include <aiverselib/systems/BuildingCirculationRuntime.h>
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
struct BuildingLiftDoorKey {
    uint32_t feature=0,floor=0;
    auto operator<=>(const BuildingLiftDoorKey&) const = default;
};
struct BuildingLiftDoorIds {
    uint32_t left=0,right=0;
};
struct BuildingLiftDoorPose {
    BuildingLiftDoorKey key;
    bool opened=false;
    float x0=0,z=0,x1=0;
    int64_t yMm=0;
    uint64_t revision=0;
};

class BuildingLiftDoorOwner {
public:
    bool attach(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                const BuildingStructureGraph& graph,
                const std::map<uint32_t,BuildingLiftController>& controllers,
                const std::map<BuildingLiftDoorKey,BuildingLiftDoorIds>& ids,
                float width,float depth,Vec3 origin,uint32_t yaw,
                std::string* error=nullptr) {
        auto fail=[&](const std::string& msg){if(error)*error=msg;return false;};
        if(!entries_.empty()||yaw>3||width<=0||depth<=0)
            return fail("Door owner already attached or invalid building transform");
        std::set<uint32_t> allIds;
        std::map<BuildingLiftDoorKey,Door> staged;
        for(const auto& [feature,lift]:controllers) {
            const auto& pose=lift.platformPose();
            for(uint32_t floor=0;floor<lift.floorCount();++floor) {
                const BuildingLiftDoorKey key{feature,floor};
                auto entry=ids.find(key);
                if(entry==ids.end()||!entry->second.left||!entry->second.right||
                   entry->second.left==entry->second.right||
                   !allIds.insert(entry->second.left).second||
                   !allIds.insert(entry->second.right).second)
                    return fail("Missing/duplicate landing door collider IDs");
                // Graph stores the lower landing on each vertical link
                // and highest room floor height as its final destination.
                int64_t mm=0;
                bool found=false;
                for(const auto& socket:graph.sockets)
                    if(socket.kind==BuildingFeatureKind::Elevator&&
                       socket.featureIndex==feature&&socket.repeatIndex==floor){
                        mm=std::llround(double(socket.y)*1000.);found=true;break;
                    }
                if(!found){
                    for(const auto& room:graph.rooms)
                        if(room.floor==floor){
                            mm=std::llround(double(room.y0)*1000.);
                            found=true;break;
                        }
                }
                if(!found)return fail("Cannot resolve lift door landing height");
                Door door;
                door.ids=entry->second;door.feature=feature;door.floor=floor;
                door.x0=pose.x0;door.x1=pose.x1;door.frontZ=pose.z0;
                door.yMm=mm;door.width=width;door.depth=depth;
                door.origin=origin;door.yaw=yaw;
                door.open=lift.readyAt(floor);
                door.revision=lift.snapshot().revision;
                staged.emplace(key,door);
            }
        }
        if(staged.size()!=ids.size())
            return fail("Unexpected door IDs without matching lift");
        for(uint32_t id:allIds)
            if(spatial.colliders().contains(id))return fail("Landing door collider ID occupied");
        // Register closed leaves; open landing doors are absent from
        // SpatialWorld. Roll back ALL new colliders on any failure.
        std::vector<uint32_t> added;
        for(auto& [key,door]:staged){
            (void)key;
            if(door.open)continue;
            const auto bounds=leafBounds(door);
            if(!spatial.add({door.ids.left,bounds.first})){
                rollback(spatial,added);return fail("Could not register left lift door");
            }
            added.push_back(door.ids.left);
            if(!spatial.add({door.ids.right,bounds.second})){
                rollback(spatial,added);return fail("Could not register right lift door");
            }
            added.push_back(door.ids.right);
            if(navmesh){
                markBuildingNavCollisionChanged(*navmesh,bounds.first);
                markBuildingNavCollisionChanged(*navmesh,bounds.second);
            }
        }
        entries_=std::move(staged);
        if(error)error->clear();
        return true;
    }

    // Call after lift phase transitions, before agents are allowed to board.
    // Partial failure is reported and its owner must not publish a new
    // navmesh revision. Scene integration marks all navigation dirty.
    bool synchronize(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                     const std::map<uint32_t,BuildingLiftController>& controllers) {
        for(auto& [key,door]:entries_){
            auto it=controllers.find(key.feature);
            if(it==controllers.end())return false;
            const auto& lift=it->second;
            const bool open=lift.readyAt(key.floor);
            if(open==door.open)continue;
            const auto bounds=leafBounds(door);
            if(open){
                spatial.remove(door.ids.left);
                spatial.remove(door.ids.right);
            }else{
                if(!spatial.add({door.ids.left,bounds.first}))return false;
                if(!spatial.add({door.ids.right,bounds.second})){
                    spatial.remove(door.ids.left);
                    return false;
                }
            }
            door.open=open;
            door.revision=lift.snapshot().revision;
            if(navmesh){
                markBuildingNavCollisionChanged(*navmesh,bounds.first);
                markBuildingNavCollisionChanged(*navmesh,bounds.second);
            }
        }
        return true;
    }

    bool openAt(uint32_t feature,uint32_t floor)const {
        auto it=entries_.find({feature,floor});
        return it!=entries_.end()&&it->second.open;
    }
    bool enabled()const noexcept{return !entries_.empty();}
    std::vector<BuildingLiftDoorPose> poses()const {
        std::vector<BuildingLiftDoorPose> result;
        for(const auto& [key,door]:entries_)
            result.push_back({key,door.open,door.x0,door.frontZ,
                              door.x1,door.yMm,door.revision});
        return result;
    }
    void detach(world::SpatialWorld& spatial,nav::Mesh* navmesh){
        for(const auto& [key,door]:entries_){
            (void)key;
            if(door.open)continue;
            const auto b=leafBounds(door);
            spatial.remove(door.ids.left);
            spatial.remove(door.ids.right);
            if(navmesh){
                markBuildingNavCollisionChanged(*navmesh,b.first);
                markBuildingNavCollisionChanged(*navmesh,b.second);
            }
        }
        entries_.clear();
    }
private:
    struct Door {
        BuildingLiftDoorIds ids;
        uint32_t feature=0,floor=0;
        float x0=0,x1=0,frontZ=0,width=0,depth=0;
        int64_t yMm=0;
        Vec3 origin{};
        uint32_t yaw=0;
        bool open=false;
        uint64_t revision=0;
    };
    static void rollback(world::SpatialWorld& spatial,
                         const std::vector<uint32_t>& ids) {
        for(const auto id:ids)spatial.remove(id);
    }
    static std::pair<world::Bounds,world::Bounds> leafBounds(const Door& door){
        const float mid=(door.x0+door.x1)*0.5f;
        const float z=door.frontZ-0.06f;
        const float h=2.3f;
        const auto p=[&](float x,float y,float zz){
            return buildingLocalToWorld(x,y,zz,door.width,door.depth,
                                        door.origin,door.yaw);
        };
        const float y=float(door.yMm)/1000.f;
        const auto l0=p(door.x0,y,z),l1=p(mid,y+h,z+0.12f);
        const auto r0=p(mid,y,z),r1=p(door.x1,y+h,z+0.12f);
        auto box=[](Vec3 a,Vec3 b)->world::Bounds{
            return {{fxMin(a.x,b.x),fxMin(a.y,b.y),fxMin(a.z,b.z)},
                    {fxMax(a.x,b.x),fxMax(a.y,b.y),fxMax(a.z,b.z)}};
        };
        return {box(l0,l1),box(r0,r1)};
    }
    std::map<BuildingLiftDoorKey,Door> entries_;
};
} // namespace aiverse::building
