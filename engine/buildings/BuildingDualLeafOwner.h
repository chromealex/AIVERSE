#pragma once
// Independent sliding physical leaves for a generated aperture. Opt-in
// replacement for BuildingApertureOwner; NEVER attach both to one socket.
// Existing SpatialWorld owns collision; game provides swept occupancy test.
// Designed for deterministic world-owner fixed ticks, not render threads.
#include "engine/buildings/BuildingGraphChanges.h"
#include "engine/buildings/BuildingApertureOwner.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingDualLeafIds {uint32_t first=0,second=0;};
struct BuildingDualLeafState {
    BuildingAperturePose pose{};
    uint16_t position=0; // authoritative 0..1000, never visual easing
    bool targetOpen=false;
    bool moving=false;
    BuildingDualLeafIds ids{};
};
struct BuildingLeafMotionCheckpoint {
    std::string socketId;
    uint64_t poseRevision=0;
    uint32_t domains=0;
    bool targetOpen=false,moving=false;
};
struct BuildingDualLeafStep {
    bool committed=false,graphsAcknowledged=false,finished=false;
    uint64_t revision=0;
    std::string error;
};
class BuildingDualLeafOwner {
public:
    using SweptObstruction=std::function<bool(
        const world::Bounds&,const world::Bounds&,uint32_t,uint32_t)>;
    // Boundaries in the generated owner are world-space AABBs. This
    // implementation slides along their dominant X/Z axis (quarter turns).
    // Hinges or curved tracks need a separate mechanism.
    bool attach(const std::vector<BuildingAperturePose>& poses,
                const std::map<std::string,BuildingDualLeafIds>& ids,
                world::SpatialWorld& spatial,uint64_t sceneId,
                SweptObstruction obstruction,std::string* error=nullptr,
                const std::map<std::string,uint16_t>* restoredPositions=nullptr,
                uint64_t restoredRevision=0){
        auto fail=[&](const std::string& why){
            if(error)*error=why;
            return false;
        };
        if(restoredPositions&&restoredPositions->size()!=poses.size())
            return fail("Restored leaf positions must exactly match sockets");
        if(attached_||!sceneId||!obstruction||poses.size()!=ids.size())
            return fail("Invalid dual-leaf scene, collider IDs or sweep probe");
        std::map<std::string,Entry> next;
        std::set<uint32_t> reserved;
        for(const auto& pose:poses){
            auto found=ids.find(pose.socketId);
            if(found==ids.end()||pose.socketId.empty()||
               pose.moving||!poseBoundsValid(pose.bounds)||
               !found->second.first||!found->second.second||
               found->second.first==found->second.second||
               !reserved.insert(found->second.first).second||
               !reserved.insert(found->second.second).second)
                return fail("Duplicate or invalid physical leaf definition");
            Entry e;
            e.state.pose=pose;
            // Preserve a validated authored initial physical position,
            // including a half-open panel; only 1000 unlocks the passage.
            if(pose.physicalPermille>1000)
                return fail("Invalid authored initial leaf progress");
            e.state.position=pose.open?1000:pose.physicalPermille;
            e.state.pose.open=e.state.position==1000;
            if(restoredPositions){
                const auto it=restoredPositions->find(pose.socketId);
                if(it==restoredPositions->end()||it->second>1000)
                    return fail("Missing/invalid restored leaf progress");
                e.state.position=it->second;
                // A partial restored leaf is physically blocked and
                // stationary until the command owner reauthorizes motion.
                e.state.pose.open=e.state.position==1000;
                e.state.pose.moving=false;
                e.state.pose.revision=restoredRevision;
            }
            e.state.pose.physicalPermille=e.state.position;
            e.state.ids=found->second;
            if(!next.emplace(pose.socketId,std::move(e)).second)
                return fail("Duplicate dual-leaf socket ID");
        }
        // SpatialWorld::add replaces an existing ID. Reject rather than
        // borrowing colliders that belong to other scene/physics owners.
        for(uint32_t id:reserved)
            if(spatial.colliders().contains(id))
                return fail("Dual-leaf collider ID is already owned");
        std::vector<uint32_t> added;
        for(const auto& [key,e]:next){
            (void)key;
            if(e.state.position==1000)continue;
            const auto bounds=leafBounds(e.state.pose.bounds,e.state.position);
            if(!spatial.add({e.state.ids.first,bounds.first})){
                for(uint32_t id:added)spatial.remove(id);
                return fail("Could not attach first physical leaf");
            }
            added.push_back(e.state.ids.first);
            if(!spatial.add({e.state.ids.second,bounds.second})){
                for(uint32_t id:added)spatial.remove(id);
                return fail("Could not attach second physical leaf");
            }
            added.push_back(e.state.ids.second);
        }
        entries_=std::move(next);
        probe_=std::move(obstruction);
        sceneId_=sceneId;revision_=restoredRevision;
        attached_=true;faulted_=false;
        if(error)error->clear();
        return true;
    }

    // Restore a brand-new physical owner from the persisted ECS
    // canonical positions. Never operate on an attached instance: first
    // retire the old renderer/graph/Spatial owners and their ID leases.
    // Caller MUST invalidate and rebuild graph overlays before commands.
    bool attachRestored(
        const std::vector<BuildingAperturePose>& generated,
        const std::map<std::string,BuildingDualLeafIds>& ids,
        const std::map<std::string,uint16_t>& positions,
        uint64_t savedRevision,
        world::SpatialWorld& spatial,uint64_t sceneId,
        SweptObstruction probe,std::string* error=nullptr){
        // Revision zero is a valid snapshot before the first door motion.
        // The graph reconciliation layer publishes a nonzero invalidation
        // revision before the scene may resume handling interactions.
        if(attached_){
            if(error)*error="Restored dual leaf owner must be detached";
            return false;
        }
        return attach(generated,ids,spatial,sceneId,std::move(probe),
                      error,&positions,savedRevision);
    }

    bool begin(const std::string& socketId,bool targetOpen,uint32_t domains,
               std::string* error=nullptr){
        if(!attached_||faulted_||pending_||moving()||!domains||
           (domains&~kBuildingGraphAll)){
            if(error)*error="Dual-leaf motion busy or invalid domains";return false;
        }
        auto it=entries_.find(socketId);
        if(it==entries_.end()||
           it->second.state.position==(targetOpen?1000:0)){
            if(error)*error="Unknown aperture or already at target";return false;
        }
        it->second.state.targetOpen=targetOpen;
        it->second.state.moving=true;
        it->second.state.pose.moving=true;
        it->second.domains=domains;
        if(error)error->clear();
        return true;
    }

    BuildingDualLeafStep advance(world::SpatialWorld& spatial,
                                 BuildingGraphChangeRouter& router,
                                 uint16_t step=100){
        BuildingDualLeafStep result;
        if(!attached_||faulted_||pending_||!step||step>250||revision_==UINT64_MAX){
            result.error="Dual-leaf scene unavailable or graph ACK pending";
            return result;
        }
        auto it=std::find_if(entries_.begin(),entries_.end(),
            [](const auto& p){return p.second.state.moving;});
        if(it==entries_.end()){result.error="No active dual-leaf motion";return result;}
        auto& e=it->second;
        const auto previous=e.state.position;
        const uint16_t next=e.state.targetOpen?
            uint16_t(std::min<int>(1000,previous+step)):
            uint16_t(std::max<int>(0,int(previous)-step));
        const auto old=leafBounds(e.state.pose.bounds,previous);
        const auto proposed=leafBounds(e.state.pose.bounds,next);
        const auto sweepA=merge(old.first,proposed.first);
        const auto sweepB=merge(old.second,proposed.second);
        if(probe_(sweepA,sweepB,e.state.ids.first,e.state.ids.second)){
            result.error="Swept actor or cargo obstructs moving leaves";return result;
        }
        const auto& registered=spatial.colliders();
        if(!colliderEquals(registered,e.state.ids.first,old.first,previous==1000)||
           !colliderEquals(registered,e.state.ids.second,old.second,previous==1000)){
            result.error="Authoritative leaf collider ownership changed";
            return result;
        }
        // Stage both updates, and restore both old shapes if a publish
        // fails. Neither graph state nor progress advances on rejection.
        if(previous!=1000){
            spatial.remove(e.state.ids.first);
            spatial.remove(e.state.ids.second);
        }
        const bool fullyOpen=(next==1000);
        bool firstOk=true,secondOk=true;
        if(!fullyOpen){
            firstOk=spatial.add({e.state.ids.first,proposed.first});
            if(firstOk)secondOk=spatial.add({e.state.ids.second,proposed.second});
        }
        if(!firstOk||!secondOk){
            spatial.remove(e.state.ids.first);
            spatial.remove(e.state.ids.second);
            if(previous!=1000&&
               (!spatial.add({e.state.ids.first,old.first})||
                !spatial.add({e.state.ids.second,old.second}))){
                faulted_=true;
                result.error="Dual-leaf rollback failed; scene requires reconciliation";
            }else result.error="Dual-leaf collision commit rejected; old state restored";
            return result;
        }
        e.state.position=next;
        e.state.pose.physicalPermille=next;
        e.state.pose.open=fullyOpen;
        e.state.moving=next!=(e.state.targetOpen?1000:0);
        e.state.pose.moving=e.state.moving;
        e.state.pose.revision=++revision_;
        pending_=BuildingGraphChange{
            sceneId_,revision_,e.state.pose.featureIndex,
            BuildingGraphChangeKind::Moved,e.domains,
            merge(sweepA,sweepB),fullyOpen};
        result.committed=true;result.revision=revision_;
        result.finished=!e.state.moving;
        const auto ack=router.publish(*pending_);
        result.graphsAcknowledged=ack.complete;result.error=ack.error;
        if(ack.complete)pending_.reset();
        return result;
    }

    // One revision stream for all physical owners of this building.
    // A committed lift change uses the same ACK gate as a leaf movement.
    BuildingDualLeafStep publishExternalCommit(BuildingGraphChangeRouter& router,
                    world::Bounds affected,uint32_t domains){
        BuildingDualLeafStep result;
        if(!attached_||faulted_||pending_||!domains||(domains&~kBuildingGraphAll)||
           revision_==UINT64_MAX){
            result.error="External graph commit unavailable";return result;
        }
        pending_=BuildingGraphChange{sceneId_,++revision_,UINT32_MAX,
            BuildingGraphChangeKind::Moved,domains,affected,false};
        result.committed=true;result.revision=revision_;
        const auto ack=router.publish(*pending_);
        result.graphsAcknowledged=ack.complete;result.error=ack.error;
        if(ack.complete)pending_.reset();
        return result;
    }
    BuildingDualLeafStep retry(BuildingGraphChangeRouter& router){
        BuildingDualLeafStep result;
        if(!pending_){result.error="No pending graph event";return result;}
        result.committed=true;result.revision=pending_->revision;
        auto ack=router.publish(*pending_);
        result.graphsAcknowledged=ack.complete;result.error=ack.error;
        if(ack.complete)pending_.reset();
        return result;
    }

    bool captureMotionCheckpoint(std::vector<BuildingLeafMotionCheckpoint>& out)const{
        if(!attached_||faulted_||pending_)return false;
        std::vector<BuildingLeafMotionCheckpoint> next;
        for(const auto& [id,e]:entries_)
            next.push_back({id,e.state.pose.revision,e.domains,e.state.targetOpen,e.state.moving});
        out=std::move(next);return true;
    }
    // The caller has already restored exact physical positions and
    // reserved IDs. Rehydrate command target and per-leaf revision without
    // moving a collider, advancing time, or allocating a model.
    bool restoreMotionCheckpoint(const std::vector<BuildingLeafMotionCheckpoint>& saved){
        if(!attached_||faulted_||pending_||saved.size()!=entries_.size())return false;
        std::set<std::string> seen;size_t moving=0;
        for(const auto& item:saved){
            const auto it=entries_.find(item.socketId);
            if(it==entries_.end()||!seen.insert(item.socketId).second||
               item.poseRevision>revision_||(item.domains&~kBuildingGraphAll)||
               (item.moving&&(!item.domains||++moving>1||
                    it->second.state.position==(item.targetOpen?1000:0))))return false;
        }
        for(const auto& item:saved){
            auto& e=entries_.at(item.socketId);
            e.state.pose.revision=item.poseRevision;e.domains=item.domains;
            e.state.targetOpen=item.targetOpen;e.state.moving=item.moving;
            e.state.pose.moving=item.moving;
        }
        return true;
    }
    uint64_t sceneId()const noexcept{return sceneId_;}
    bool attached()const noexcept{return attached_;}
    bool moving()const{
        for(const auto& [id,e]:entries_){
            (void)id;
            if(e.state.moving)return true;
        }
        return false;
    }
    bool pendingGraphs()const{return pending_.has_value();}
    bool faulted()const{return faulted_;}
    uint64_t revision()const{return revision_;}
    std::vector<BuildingAperturePose> poses()const{
        std::vector<BuildingAperturePose> out;
        for(const auto& [id,e]:entries_){(void)id;out.push_back(e.state.pose);}
        return out;
    }
    std::set<uint32_t> ownedColliderIds()const{
        std::set<uint32_t> ids;
        for(const auto& [key,e]:entries_){
            (void)key;ids.insert(e.state.ids.first);ids.insert(e.state.ids.second);
        }
        return ids;
    }
    std::map<uint32_t,world::Bounds> closedColliderBounds()const{
        std::map<uint32_t,world::Bounds> out;
        for(const auto& [id,e]:entries_){
            (void)id;
            if(e.state.position==1000)continue;
            const auto pair=leafBounds(e.state.pose.bounds,e.state.position);
            out.emplace(e.state.ids.first,pair.first);
            out.emplace(e.state.ids.second,pair.second);
        }
        return out;
    }
    void detach(world::SpatialWorld& spatial){
        for(const auto& [id,e]:entries_){
            (void)id;
            spatial.remove(e.state.ids.first);
            spatial.remove(e.state.ids.second);
        }
        entries_.clear();probe_={};pending_.reset();
        sceneId_=revision_=0;attached_=faulted_=false;
    }
private:
    struct Entry{BuildingDualLeafState state;uint32_t domains=0;};
    static bool poseBoundsValid(const world::Bounds& b){
        return b.min.x<b.max.x&&b.min.y<b.max.y&&b.min.z<b.max.z;
    }
    static bool equals(const world::Bounds& a,const world::Bounds& b){
        return a.min.x==b.min.x&&a.min.y==b.min.y&&a.min.z==b.min.z&&
               a.max.x==b.max.x&&a.max.y==b.max.y&&a.max.z==b.max.z;
    }
    static bool colliderEquals(const std::map<uint32_t,world::Collider>& colliders,
                               uint32_t id,const world::Bounds& b,bool absent){
        const auto it=colliders.find(id);
        return absent?it==colliders.end():
            it!=colliders.end()&&equals(it->second.bounds,b);
    }
    static world::Bounds merge(const world::Bounds& a,const world::Bounds& b){
        return {{fxMin(a.min.x,b.min.x),fxMin(a.min.y,b.min.y),
                 fxMin(a.min.z,b.min.z)},
                {fxMax(a.max.x,b.max.x),fxMax(a.max.y,b.max.y),
                 fxMax(a.max.z,b.max.z)}};
    }
    static std::pair<world::Bounds,world::Bounds> leafBounds(
        const world::Bounds& b,uint16_t value){
        const Fixed width=b.max.x-b.min.x,depth=b.max.z-b.min.z;
        const bool alongX=width>=depth;
        const Fixed half=(alongX?width:depth)/2;
        const Fixed shift=half*Fixed::fromRatio(value,1000);
        world::Bounds first=b,second=b;
        if(alongX){
            first.max.x=b.min.x+half-shift;
            first.min.x=b.min.x-shift;
            second.min.x=b.min.x+half+shift;
            second.max.x=b.max.x+shift;
        }else{
            first.max.z=b.min.z+half-shift;
            first.min.z=b.min.z-shift;
            second.min.z=b.min.z+half+shift;
            second.max.z=b.max.z+shift;
        }
        return {first,second};
    }
    std::map<std::string,Entry> entries_;
    SweptObstruction probe_;
    std::optional<BuildingGraphChange> pending_;
    uint64_t sceneId_=0,revision_=0;
    bool attached_=false,faulted_=false;
};

} // namespace aiverse::building
