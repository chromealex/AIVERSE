#pragma once
// Authoritative binary gate / roof-hatch collision owner for generated
// buildings. Uses the EXISTING SpatialWorld and revisioned graph router.
// Partial opening animation is presentation only: traversal is granted only
// after the blocking collider was removed from the committed physics world.
#include "engine/buildings/BuildingGraphChanges.h"
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingAperturePose {
    std::string socketId;
    uint32_t featureIndex=0;
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    bool open=false;
    uint64_t revision=0;
    world::Bounds bounds{};
};
struct BuildingApertureUpdate {
    bool collisionCommitted=false;
    bool graphsAcknowledged=false;
    uint64_t revision=0;
    std::string error;
};

class BuildingApertureOwner {
public:
    using ObstructionProbe=std::function<bool(const world::Bounds&)>;
    // IDs are game-owned: exactly one unique dynamic collider per operable
    // socket. The static generated model MUST be the runtime variant with
    // DoorLeaf and RoofPanel excluded from static collision and rendering.
    bool attach(const RuntimeBuildingResult& generated,
                world::SpatialWorld& spatial,
                uint64_t sceneId,Vec3 origin,uint32_t yaw,
                const std::map<std::string,uint32_t>& colliderIds,
                ObstructionProbe obstruction,
                std::string* error=nullptr){
        auto fail=[&](const char* msg){if(error)*error=msg;return false;};
        if(attached_||!generated||!generated.structureGraph||sceneId==0||
           yaw>3||!obstruction)return fail("Invalid aperture scene or safety probe");
        const float W=generated.building.constructionSize.width;
        const float H=generated.building.constructionSize.height;
        const float D=generated.building.constructionSize.depth;
        if(!(W>0&&H>0&&D>0))return fail("Invalid construction dimensions");
        std::map<std::string,Entry> staged;
        std::set<uint32_t> seen;
        for(const auto& socket:generated.structureGraph.sockets){
            if(!operable(socket.kind))continue;
            const auto it=colliderIds.find(socket.id);
            if(it==colliderIds.end()||it->second==0||
               !seen.insert(it->second).second||staged.contains(socket.id))
                return fail("Missing or duplicate dynamic aperture collider ID");
            Entry e;
            e.pose.socketId=socket.id;
            e.pose.featureIndex=socket.featureIndex;
            e.pose.kind=socket.kind;
            e.pose.open=socket.open>=0.9999f;
            e.pose.revision=1;
            e.colliderId=it->second;
            // Treat each aperture as one conservative full-width closed
            // barrier. Sliding leaves remain a renderer concern; both
            // halves have identical binary accessibility.
            float x0=0,y0=0,z0=0,x1=0,y1=0,z1=0;
            if(socket.kind==BuildingFeatureKind::SlidingRoof){
                x0=socket.x-socket.width*0.5f;
                x1=socket.x+socket.width*0.5f;
                z0=socket.z-socket.depth*0.5f;
                z1=socket.z+socket.depth*0.5f;
                y0=H-0.28f;y1=H;
            }else{
                y0=0.24f;y1=y0+socket.height;
                // Socket X/Z is one metre OUTSIDE the facade.
                switch(socket.side){
                    case BuildingFeatureSide::Front:
                        x0=socket.x-socket.width*0.5f;
                        x1=socket.x+socket.width*0.5f;
                        z0=0;z1=0.36f;break;
                    case BuildingFeatureSide::Back:
                        x0=socket.x-socket.width*0.5f;
                        x1=socket.x+socket.width*0.5f;
                        z0=D-0.36f;z1=D;break;
                    case BuildingFeatureSide::Left:
                        x0=0;x1=0.36f;
                        z0=socket.z-socket.width*0.5f;
                        z1=socket.z+socket.width*0.5f;break;
                    case BuildingFeatureSide::Right:
                        x0=W-0.36f;x1=W;
                        z0=socket.z-socket.width*0.5f;
                        z1=socket.z+socket.width*0.5f;break;
                }
            }
            const Vec3 a=buildingLocalToWorld(x0,y0,z0,W,D,origin,yaw);
            const Vec3 b=buildingLocalToWorld(x1,y1,z1,W,D,origin,yaw);
            e.pose.bounds={{fxMin(a.x,b.x),fxMin(a.y,b.y),fxMin(a.z,b.z)},
                           {fxMax(a.x,b.x),fxMax(a.y,b.y),fxMax(a.z,b.z)}};
            staged.emplace(socket.id,std::move(e));
        }
        if(staged.size()!=colliderIds.size())
            return fail("Collider IDs contain unknown or unsupported aperture sockets");
        std::vector<uint32_t> inserted;
        for(const auto& [id,e]:staged){
            (void)id;
            if(e.pose.open)continue;
            if(!spatial.add({e.colliderId,e.pose.bounds})){
                for(uint32_t rollback:inserted)spatial.remove(rollback);
                return fail("Could not publish initial aperture collision");
            }
            inserted.push_back(e.colliderId);
        }
        entries_=std::move(staged);
        sceneId_=sceneId;
        obstruction_=std::move(obstruction);
        revision_=0;pendingChange_={};attached_=true;
        if(error)error->clear();
        return true;
    }

    // The caller must authenticate the actor and access policy before use.
    // A close is rejected if live actors/cargo/vehicles obstruct the aperture.
    // On publish failure, physical state stays authoritative, and the
    // router retains pending notifications for an explicit retry.
    BuildingApertureUpdate setOpen(
        const std::string& socketId,bool open,
        world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,
        uint32_t domains) {
        BuildingApertureUpdate out;
        if(!attached_||pendingChange_){
            out.error="Scene inactive or prior graph event awaiting acknowledgement";
            return out;
        }
        auto it=entries_.find(socketId);
        if(it==entries_.end()||domains==0||(domains&~kBuildingGraphAll)){
            out.error="Invalid aperture or graph domains";return out;
        }
        auto& e=it->second;
        if(e.pose.open==open){
            out.collisionCommitted=true;out.graphsAcknowledged=true;
            out.revision=revision_;return out;
        }
        if(!open&&obstruction_(e.pose.bounds)){
            out.error="Authoritative obstruction sensor blocks closure";return out;
        }
        if(open){
            // The entry was registered by this owner. Refuse publication if
            // another subsystem has externally removed its collider.
            if(!spatial.colliders().contains(e.colliderId)){
                out.error="Aperture collider lost ownership";return out;
            }
            spatial.remove(e.colliderId);
        }else if(!spatial.add({e.colliderId,e.pose.bounds})){
            out.error="Could not install closed aperture collider";return out;
        }
        e.pose.open=open;
        e.pose.revision=++revision_;
        pendingChange_=BuildingGraphChange{
            sceneId_,revision_,e.pose.featureIndex,
            BuildingGraphChangeKind::ApertureChanged,
            domains,e.pose.bounds,open};
        out.collisionCommitted=true;
        out.revision=revision_;
        const auto dispatch=router.publish(*pendingChange_);
        out.graphsAcknowledged=dispatch.complete;
        out.error=dispatch.error;
        if(dispatch.complete)pendingChange_.reset();
        return out;
    }

    BuildingApertureUpdate retryGraphPublication(BuildingGraphChangeRouter& router){
        BuildingApertureUpdate out;
        if(!pendingChange_){out.error="No pending graph publication";return out;}
        out.collisionCommitted=true;
        out.revision=pendingChange_->revision;
        const auto dispatch=router.publish(*pendingChange_);
        out.graphsAcknowledged=dispatch.complete;out.error=dispatch.error;
        if(dispatch.complete)pendingChange_.reset();
        return out;
    }

    std::vector<BuildingAperturePose> poses()const{
        std::vector<BuildingAperturePose> out;
        for(const auto& [id,e]:entries_){(void)id;out.push_back(e.pose);}
        return out;
    }
    bool attached()const noexcept{return attached_;}
    bool hasPendingGraphs()const noexcept{return pendingChange_.has_value();}
    uint64_t revision()const noexcept{return revision_;}
    // Only ordinary scene shutdown after all entities have reconciled.
    // Existing game owner retires render resources separately.
    void detach(world::SpatialWorld& spatial){
        for(const auto& [id,e]:entries_){
            (void)id;
            if(!e.pose.open)spatial.remove(e.colliderId);
        }
        entries_.clear();obstruction_={};pendingChange_.reset();
        sceneId_=revision_=0;attached_=false;
    }
private:
    static bool operable(BuildingFeatureKind kind){
        return kind==BuildingFeatureKind::PersonnelDoor||
               kind==BuildingFeatureKind::LargeGate||
               kind==BuildingFeatureKind::SmallGate||
               kind==BuildingFeatureKind::SlidingRoof;
    }
    struct Entry{
        BuildingAperturePose pose;
        uint32_t colliderId=0;
    };
    std::map<std::string,Entry> entries_;
    ObstructionProbe obstruction_;
    std::optional<BuildingGraphChange> pendingChange_;
    uint64_t sceneId_=0,revision_=0;
    bool attached_=false;
};
} // namespace aiverse::building
