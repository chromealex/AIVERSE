#pragma once
// Genre-neutral committed-state invalidations for procedural buildings.
// Graph owners subscribe explicitly; this module never mutates an unrelated
// graph's topology or impersonates its authoritative writer.
#include "engine/buildings/BuildingLiftColliderOwner.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

// Independent consumers can coexist. A graph owner registers only the
// domains it actually supports; these are not interchangeable graph APIs.
enum class BuildingGraphDomain : uint32_t {
    Navigation=1u<<0,   // native nav::Mesh, stair/lift landing route cache
    Spatial=1u<<1,      // room/zone graph, area occupancy, reachability
    Hierarchy=1u<<2,    // grid A*, HPA*, flow-field and path clusters
    Transport=1u<<3,    // road/rail/vehicle/cargo network
    Gameplay=1u<<4,     // scripted logic, quest and ability-gated zone graph
    Presentation=1u<<5, // render/material/VFX dependency graph consumers
    Audio=1u<<6,        // sound occlusion/acoustics graph consumers
    Simulation=1u<<7   // power, utility, damage and environmental networks
};
constexpr uint32_t buildingGraphBit(BuildingGraphDomain d) {
    return static_cast<uint32_t>(d);
}
constexpr uint32_t kBuildingGraphAll=
    (1u<<8)-1u;

enum class BuildingGraphChangeKind : uint8_t {
    Created, Removed, GeometryChanged, ApertureChanged, AccessChanged,
    Moved, StructureChanged
};

struct BuildingGraphChange {
    uint64_t sceneId=0;       // caller-assigned stable authoritative scene
    uint64_t revision=0;      // strictly increasing per scene
    uint32_t featureIndex=UINT32_MAX;
    BuildingGraphChangeKind kind=BuildingGraphChangeKind::GeometryChanged;
    uint32_t domains=kBuildingGraphAll;
    world::Bounds bounds{};   // conservative world-space invalidation extent
    // Actual committed state. Clients must never invent this value or
    // interpret invalidation as authorization to open a physical door.
    bool open=false;
};

struct BuildingGraphDispatch {
    bool complete=false;
    uint32_t deliveredDomains=0;
    std::string error;
};

// A callback represents one existing owner (NavMesh, MapGrid/HPA, zone,
// renderer, etc.). It must invalidate its cache / schedule a rebuild, not
// publish an unverified graph as valid. A false result prevents ack.
class BuildingGraphChangeRouter {
public:
    using Consumer=std::function<bool(const BuildingGraphChange&)>;
    bool registerConsumer(BuildingGraphDomain domain,Consumer consumer){
        if(!consumer)return false;
        return consumers_.emplace(buildingGraphBit(domain),
                                  std::move(consumer)).second;
    }
    bool replaceConsumer(BuildingGraphDomain domain,Consumer consumer){
        if(!consumer)return false;
        consumers_[buildingGraphBit(domain)]=std::move(consumer);
        return true;
    }
    void unregisterConsumer(BuildingGraphDomain domain){
        consumers_.erase(buildingGraphBit(domain));
    }
    // Input must originate from a successful authoritative physics/
    // geometry commit, never an untrusted client request or UI preview.
    // Per-domain acks are retained so retry cannot lose an invalidation
    // when one graph owner was temporarily offline.
    BuildingGraphDispatch publish(const BuildingGraphChange& change){
        BuildingGraphDispatch out;
        if(change.sceneId==0||change.revision==0||
           change.domains==0||(change.domains&~kBuildingGraphAll)){
            out.error="Invalid building graph change identity or domain mask";
            return out;
        }
        auto& latest=latest_[change.sceneId];
        if(change.revision<latest){
            out.error="Stale building graph change revision";
            return out;
        }
        if(change.revision>latest){
            // All earlier revisions must already be accepted; if not,
            // advancing would silently discard pending graph updates.
            for(const auto& [key,mask]:pending_)if(key.first==change.sceneId&&mask){
                out.error="Earlier graph invalidation has not been acknowledged";
                return out;
            }
            latest=change.revision;
            pending_[{change.sceneId,change.revision}]=change.domains;
        }
        auto it=pending_.find({change.sceneId,change.revision});
        if(it==pending_.end()){
            out.error="Graph revision already finalized or mismatched";
            return out;
        }
        if((it->second&~change.domains)!=0){
            out.error="Retry cannot reduce original graph domain mask";
            return out;
        }
        for(const auto& [domain,consumer]:consumers_){
            if(!(it->second&domain))continue;
            if(!consumer(change)){
                out.error="A graph owner refused committed building invalidation";
                return out;
            }
            it->second &= ~domain;
            out.deliveredDomains |= domain;
        }
        // Unknown or unregistered domains remain pending (fail closed).
        if(it->second){
            out.error="Graph change has domains without a ready owner";
            return out;
        }
        pending_.erase(it);
        out.complete=true;
        return out;
    }
    uint32_t pendingDomains(uint64_t sceneId,uint64_t revision)const{
        const auto it=pending_.find({sceneId,revision});
        return it==pending_.end()?0:it->second;
    }
    // Only call on authoritative world teardown after owners are detached.
    void forgetScene(uint64_t sceneId){
        latest_.erase(sceneId);
        for(auto it=pending_.begin();it!=pending_.end();){
            if(it->first.first==sceneId)it=pending_.erase(it);
            else ++it;
        }
    }
private:
    std::map<uint32_t,Consumer> consumers_;
    std::map<uint64_t,uint64_t> latest_;
    std::map<std::pair<uint64_t,uint64_t>,uint32_t> pending_;
};

// Existing nav::Mesh integration. This is one registered consumer, not
// a hardwired dependency of the event router itself.
inline BuildingGraphChangeRouter::Consumer buildingNavInvalidator(nav::Mesh& mesh){
    return [&mesh](const BuildingGraphChange& change){
        if(!mesh.built())return false;
        markBuildingNavCollisionChanged(mesh,change.bounds);
        return true;
    };
}
} // namespace aiverse::building
