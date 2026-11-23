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
                SweptObstruction obstruction,std::string* error=nullptr){
        auto fail=[&](const std::string& why){
            if(error)*error=why;
            return false;
        };
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
            e.state.position=pose.open?1000:0;
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
        sceneId_=sceneId;revision_=0;attached_=true;faulted_=false;
        if(error)error->clear();
        return true;
    }

    bool begin(const std::string& socketId,bool targetOpen,uint32_t domains,
               std::string* error=nullptr){
        if(!attached_||faulted_||pending_||moving()||!domains||
           (domains&~kBuildingGraphAll)){
            if(error)*error="Dual-leaf motion busy or invalid domains";return false;
        }
        auto it=entries_.find(socketId);
        if(it==entries_.end()||it->second.state.pose.open==targetOpen){
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
        if(!attached_||faulted_||pending_||!step||step>250){
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

    BuildingDualLeafStep retry(BuildingGraphChangeRouter& router){
        BuildingDualLeafStep result;
        if(!pending_){result.error="No pending graph event";return result;}
        result.committed=true;result.revision=pending_->revision;
        auto ack=router.publish(*pending_);
        result.graphsAcknowledged=ack.complete;result.error=ack.error;
        if(ack.complete)pending_.reset();
        return result;
    }

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
