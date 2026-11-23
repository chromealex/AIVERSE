// ECS binding for the deterministic region ledger. Never couples entity ownership
// to camera position or asynchronous resource-residency decisions.
#pragma once
#include "engine/world/RegionEntityLedger.h"
#include "engine/sim/CommonComponents.h"
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::world {

struct RegionStableIdentity {
    RegionEntityId id=0;
};
inline void io(Archive& a,RegionStableIdentity& value) {a(value.id);}

// The module registers the ledger resource separately via registerRegionEntityLedger.
// Both registrations must occur at setup; no extra state is created by this helper.
template<class SimulationT>
void registerRegionStableIdentity(SimulationT& sim) {
    sim.template registerPersistentComponent<RegionStableIdentity>(
        "aiverse.world.region_stable_identity",1);
}

class RegionEcsBridge {
    static bool reject(std::string* error,const char* why) {
        if(error)*error=why;
        return false;
    }
    static bool available(const World& world,RegionEntityId id) {
        bool seen=false;
        world.eachExistingConst<RegionStableIdentity>(
            [&](Entity,const RegionStableIdentity& identity) {
                if(identity.id==id)seen=true;
            });
        return !seen;
    }
    static bool eligible(const World& world,Entity e,RegionEntityId id,
                         std::string* error) {
        if(!id)return reject(error,"stable region ID must be nonzero");
        if(!world.alive(e) || !world.has<Transform>(e))
            return reject(error,"entity must be alive and have a Transform");
        if(world.has<RegionStableIdentity>(e))
            return reject(error,"entity already has a region identity");
        if(!available(world,id))
            return reject(error,"stable ID is already bound to an ECS entity");
        return true;
    }

public:
    // On a deterministic gameplay tick, tag an already-created live ECS entity.
    // Components are installed before updating the ledger so allocation failure
    // cannot create an unreachable authoritative identity.
    static bool bindNew(World& world,RegionEntityLedger& ledger,Entity e,
                        RegionEntityId id,Cell owner,std::string* error=nullptr) {
        if(!eligible(world,e,id,error))return false;
        if(ledger.find(id))return reject(error,"stable ID already exists (possibly tombstoned)");
        world.add<RegionStableIdentity>(e,RegionStableIdentity{id});
        if(ledger.spawn(id,owner))return true;
        world.remove<RegionStableIdentity>(e);
        return reject(error,"could not allocate a persistent region identity");
    }

    // Attach a live materialization to an existing, non-deleted ledger record.
    // This does not spawn or duplicate the authoritative record.
    static bool bindExisting(World& world,const RegionEntityLedger& ledger,Entity e,
                             RegionEntityId id,Cell expected,std::string* error=nullptr) {
        if(!eligible(world,e,id,error))return false;
        const auto* record=ledger.find(id);
        if(!record || !record->live || record->owner!=expected)
            return reject(error,"missing, tombstoned or wrong-owner region record");
        world.add<RegionStableIdentity>(e,RegionStableIdentity{id});
        return true;
    }

    // Atomic transfer of a caller-declared group: vehicle, occupants, cargo,
    // or hierarchy. A transfer does not mutate Transform; gameplay movement
    // owns positions. All entities must already be live and attached.
    static bool transferGroup(const World& world,RegionEntityLedger& ledger,
                              std::span<const Entity> entities,Cell expected,
                              Cell destination,std::string* error=nullptr) {
        if(entities.empty())return reject(error,"empty region transfer group");
        std::vector<RegionEntityId> ids;
        ids.reserve(entities.size());
        for(Entity e:entities) {
            if(!world.alive(e) || !world.has<Transform>(e))
                return reject(error,"group contains a dead or unpositioned entity");
            const auto* id=world.tryGetConst<RegionStableIdentity>(e);
            if(!id || !id->id)
                return reject(error,"group contains an entity without a stable ID");
            ids.push_back(id->id);
        }
        if(!ledger.transfer(ids,expected,destination))
            return reject(error,"transfer failed ledger validation; no owner changed");
        return true;
    }

    // Gameplay destruction; never call during an active ECS pool iteration.
    // A renderer or chunk streamer must never invoke this method.
    static bool retire(World& world,RegionEntityLedger& ledger,Entity entity,
                       Cell expected,std::string* error=nullptr) {
        if(!world.alive(entity))
            return reject(error,"entity is not alive");
        const auto* identity=world.tryGetConst<RegionStableIdentity>(entity);
        if(!identity || !identity->id)
            return reject(error,"entity is not bound to a stable ID");
        if(!ledger.despawn(identity->id,expected))
            return reject(error,"retire failed ledger validation");
        world.destroy(entity);
        return true;
    }

    // Derived, non-authoritative cache. Rebuild after native save/restore,
    // COW rollback or ECS structural changes; never persist Entity handles
    // as IDs and never cache this map across rollback without rebuilding.
    static bool rebuild(const World& world,const RegionEntityLedger& ledger,
                        std::map<RegionEntityId,Entity>& out,
                        std::string* error=nullptr) {
        std::map<RegionEntityId,Entity> next;
        bool valid=true;
        world.eachExistingConst<RegionStableIdentity>(
            [&](Entity e,const RegionStableIdentity& identity) {
                const auto* record=ledger.find(identity.id);
                if(!world.alive(e) || !world.has<Transform>(e) ||
                   !identity.id || !record || !record->live ||
                   !next.emplace(identity.id,e).second)valid=false;
            });
        if(!valid)return reject(error,"inconsistent live ECS identity binding");
        out.swap(next);
        return true;
    }

    // Read-only diagnostic. Detect movement in authoritative fixed-point
    // coordinates. The caller groups occupants with the vehicle before
    // committing a transfer, never one entity at a time implicitly.
    struct Crossing {RegionEntityId id=0;Entity entity{};Cell from{},to{};};
    static bool crossings(const World& world,const RegionEntityLedger& ledger,
                          const SpatialWorld& spatial,std::vector<Crossing>& out,
                          std::string* error=nullptr) {
        std::map<RegionEntityId,Entity> current;
        if(!rebuild(world,ledger,current,error))return false;
        std::vector<Crossing> detected;
        for(const auto& [id,e]:current) {
            const auto* transform=world.tryGetConst<Transform>(e);
            const auto* record=ledger.find(id);
            if(!transform || !record)return reject(error,"stale live region binding");
            const auto destination=spatial.cell(transform->pos);
            if(destination!=record->owner)
                detected.push_back({id,e,record->owner,destination});
        }
        out.swap(detected);
        return true;
    }
};

} // namespace aiverse::world
