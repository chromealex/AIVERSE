// Durable hot/cold ECS component parking. This is an opt-in gameplay-tick
// transition, never a callback from asynchronous view/asset streaming.
#pragma once
#include "engine/world/RegionEcsBridge.h"
#include <algorithm>
#include <any>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse::world {

struct RegionColdBlob {
    uint32_t version=1;
    std::string bytes;
};
struct RegionColdRecord {
    Cell owner{};
    uint64_t ownerRevision=0;
    std::map<std::string,RegionColdBlob> components;
};

class RegionColdStore {
public:
    static constexpr uint32_t kSchema=1;
    static constexpr uint32_t kMaxEntities=65536;
    static constexpr uint32_t kMaxComponents=64;
    static constexpr uint32_t kMaxComponentBytes=1024*1024;
    static constexpr uint32_t kMaxTotalBytes=16*1024*1024;
    const RegionColdRecord* find(RegionEntityId id)const {
        auto it=parked_.find(id);
        return it==parked_.end()?nullptr:&it->second;
    }
    size_t size()const {return parked_.size();}
private:
    std::map<RegionEntityId,RegionColdRecord> parked_;
    friend void io(Archive&,RegionColdStore&);
    friend class RegionDormancyController;
};

inline void regionColdBytes(Archive& a,std::string& bytes,uint32_t max) {
    uint32_t n=0;
    if(a.writing()){
        if(bytes.size()>max){a.fail();return;}
        n=uint32_t(bytes.size());
    }
    a(n);
    if(!a.ok() || n>max){a.fail();return;}
    if(!a.writing())bytes.assign(n,'\0');
    if(n)a.block(bytes.data(),n);
}

// Portable field-by-field, deterministic and failure-atomic decode.
// Bytes held here contain game-defined component payloads. Entity references
// inside those payloads remain the host's responsibility (their handles are
// retained in this stage); never store GPU or other presentation resources.
// Native ECS v10 persists this resource with its schema.
inline void io(Archive& a,RegionColdStore& store) {
    uint32_t schema=RegionColdStore::kSchema;
    a(schema);
    if(!a.ok() || schema!=RegionColdStore::kSchema){a.fail();return;}
    uint32_t count=0;
    if(a.writing()) {
        if(store.parked_.size()>RegionColdStore::kMaxEntities){a.fail();return;}
        count=uint32_t(store.parked_.size());
    }
    a(count);
    if(!a.ok() || count>RegionColdStore::kMaxEntities){a.fail();return;}
    std::map<RegionEntityId,RegionColdRecord> decoded;
    // This total includes metadata, not only opaque component payloads.
    // It must remain below the native ECS v10 per-resource byte budget.
    size_t totalBytes=8;
    auto readOne=[&](RegionEntityId& id,RegionColdRecord& record){
        a(id,record.owner.x,record.owner.y,record.owner.z,record.ownerRevision);
        totalBytes+=sizeof(id)+3*sizeof(int32_t)+sizeof(uint64_t)+sizeof(uint32_t);
        if(!a.ok() || !id || !record.ownerRevision ||
           totalBytes>RegionColdStore::kMaxTotalBytes){a.fail();return;}
        uint32_t components=0;
        if(a.writing()) {
            if(record.components.empty() || record.components.size()>RegionColdStore::kMaxComponents)
                {a.fail();return;}
            components=uint32_t(record.components.size());
        }
        a(components);
        if(!a.ok() || !components || components>RegionColdStore::kMaxComponents){a.fail();return;}
        std::map<std::string,RegionColdBlob> restored;
        for(uint32_t i=0;i<components && a.ok();++i) {
            std::string name;
            RegionColdBlob blob;
            if(a.writing()){
                auto it=record.components.begin();
                std::advance(it,i);
                name=it->first;
                blob=it->second;
            }
            regionColdBytes(a,name,128);
            a(blob.version);
            if(!a.ok() || name.empty() || blob.version==0){a.fail();return;}
            regionColdBytes(a,blob.bytes,RegionColdStore::kMaxComponentBytes);
            totalBytes+=2*sizeof(uint32_t)+name.size()+sizeof(uint32_t)+blob.bytes.size();
            if(!a.ok() || totalBytes>RegionColdStore::kMaxTotalBytes){a.fail();return;}
            if(!a.writing() && !restored.emplace(std::move(name),std::move(blob)).second)
                {a.fail();return;}
        }
        if(!a.writing() && a.ok())record.components=std::move(restored);
    };
    if(a.writing()) {
        for(const auto& [id,item]:store.parked_) {
            RegionEntityId key=id;
            RegionColdRecord record=item;
            readOne(key,record);
            if(!a.ok())return;
        }
    }else{
        for(uint32_t i=0;i<count && a.ok();++i) {
            RegionEntityId key=0;
            RegionColdRecord record;
            readOne(key,record);
            if(!a.ok() || !decoded.emplace(key,std::move(record)).second){a.fail();return;}
        }
        if(a.ok())store.parked_.swap(decoded);
    }
}

template<class SimulationT>
void registerRegionColdState(SimulationT& sim) {
    sim.template registerPersistentResource<RegionColdStore>(
        "aiverse.world.region_cold_store",RegionColdStore::kSchema);
    sim.template registerPersistentComponent<RegionDormant>(
        "aiverse.world.region_dormant",1);
    sim.addPersistentLoadValidator("aiverse.world.region_cold_consistency",
        [](const World& world,std::string& error)->bool {
            const auto* cold=world.resourceConst<RegionColdStore>();
            const auto* ledger=world.resourceConst<RegionEntityLedger>();
            bool valid=true;
            size_t dormantCount=0;
            std::set<RegionEntityId> seen;
            world.eachExistingConst<RegionDormant>(
                [&](Entity e,const RegionDormant& dormant) {
                    ++dormantCount;
                    const auto* tag=world.tryGetConst<RegionStableIdentity>(e);
                    if(!tag || !seen.insert(tag->id).second)valid=false;
                    const auto* stored=cold && tag?cold->find(tag->id):nullptr;
                    const auto* record=ledger && tag?ledger->find(tag->id):nullptr;
                    if(!world.alive(e) || !world.has<Transform>(e) ||
                       dormant.marker!=1 || !tag || !tag->id ||
                       !stored || !record || !record->live ||
                       stored->owner!=record->owner ||
                       stored->ownerRevision!=record->revision)
                        valid=false;
                });
            if(!valid || (cold?cold->size():0)!=dormantCount) {
                error="dormant tag, region ledger and parked payload disagree";
                return false;
            }
            return true;
        });
}

// Register only components whose *entire* state has a portable Archive codec
// and whose host systems explicitly skip dormant entities. Transform and the
// stable identity must stay present; those ensure Entity handle continuity.
class RegionDormancyController {
    struct Adapter {
        uint32_t version;
        bool (*present)(const World&,Entity);
        bool (*encode)(const World&,Entity,std::string&);
        bool (*decode)(const std::string&,std::any&);
        void (*remove)(World&,Entity);
        void (*restore)(World&,Entity,const std::any&);
    };
    struct Prepared {
        Entity entity{};
        RegionEntityId id=0;
        RegionColdRecord record;
    };
    struct Decoded {
        Entity entity{};
        RegionEntityId id=0;
        std::vector<std::pair<const Adapter*,std::any>> components;
    };
    std::map<std::string,Adapter> adapters_;

    static bool reject(std::string* error,const char* message) {
        if(error)*error=message;
        return false;
    }
    static bool identity(const World& world,const RegionEntityLedger& ledger,
                         Entity entity,Cell expected,RegionEntityId& id) {
        if(!world.alive(entity) || !world.has<Transform>(entity))return false;
        const auto* tag=world.tryGetConst<RegionStableIdentity>(entity);
        if(!tag || !tag->id)return false;
        const auto* record=ledger.find(tag->id);
        if(!record || !record->live || record->owner!=expected)return false;
        id=tag->id;
        return true;
    }

public:
    template<class T>
    bool registerComponent(std::string name,uint32_t version=1) {
        static_assert(std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>,
            "Parked ECS components must be default and copy constructible");
        static_assert(!std::is_same_v<T,Transform> &&
                      !std::is_same_v<T,RegionStableIdentity> &&
                      !std::is_same_v<T,RegionDormant>,
            "Core identity, position and dormant marker must remain live in ECS");
        if(name.empty() || name.size()>128 || !version ||
           adapters_.size()>=RegionColdStore::kMaxComponents)return false;
        Adapter entry{
            version,
            +[](const World& w,Entity entity)->bool {return w.has<T>(entity);},
            +[](const World& w,Entity entity,std::string& bytes)->bool {
                const auto* source=w.tryGetConst<T>(entity);
                if(!source)return false;
                T value=*source;
                Archive a(&bytes);
                io(a,value);
                return a.ok() && bytes.size()<=RegionColdStore::kMaxComponentBytes;
            },
            +[](const std::string& bytes,std::any& target)->bool {
                T value{};
                Archive a(bytes);
                io(a,value);
                if(!a.ok() || a.remaining()!=0)return false;
                target=std::move(value);
                return true;
            },
            +[](World& w,Entity entity){w.remove<T>(entity);},
            +[](World& w,Entity entity,const std::any& item){
                w.add<T>(entity,std::any_cast<const T&>(item));
            }
        };
        return adapters_.emplace(std::move(name),entry).second;
    }

    // Every member of a group is validated and serialized before any live
    // component is removed. On success, stable identity + Transform stay live,
    // and all declared heavy components move to the persistent cold resource.
    // Call only at a safe ECS structural mutation point, inside a game tick.
    bool parkGroup(World& world,const RegionEntityLedger& ledger,
                   RegionColdStore& store,std::span<const Entity> entities,
                   Cell owner,std::string* error=nullptr)const {
        if(entities.empty() || adapters_.empty())
            return reject(error,"empty region group or parking codec registry");
        if(entities.size()>RegionColdStore::kMaxEntities ||
           entities.size()>RegionColdStore::kMaxEntities-store.size())
            return reject(error,"region cold entity budget exceeded");
        std::set<uint64_t> handles;
        std::set<RegionEntityId> stableIds;
        std::vector<Prepared> prepared;
        prepared.reserve(entities.size());
        size_t newBytes=0;
        for(Entity entity:entities) {
            RegionEntityId id=0;
            if(!handles.insert(entity.packed()).second ||
               !identity(world,ledger,entity,owner,id) ||
               !stableIds.insert(id).second ||
               world.has<RegionDormant>(entity) || store.find(id))
                return reject(error,"invalid, duplicated or already dormant entity");
            Prepared item;
            item.entity=entity;
            item.id=id;
            item.record.owner=owner;
            item.record.ownerRevision=ledger.find(id)->revision;
            for(const auto& [name,codec]:adapters_) {
                if(!codec.present(world,entity))continue;
                RegionColdBlob blob;
                blob.version=codec.version;
                if(!codec.encode(world,entity,blob.bytes))
                    return reject(error,"cannot serialize a parked ECS component");
                newBytes+=blob.bytes.size();
                if(newBytes>RegionColdStore::kMaxTotalBytes)
                    return reject(error,"region cold byte budget exceeded");
                item.record.components.emplace(name,std::move(blob));
            }
            if(item.record.components.empty())
                return reject(error,"entity has no registered parkable components");
            prepared.emplace_back(std::move(item));
        }
        // Preserve the canonical byte and entity budgets even for a long-lived
        // resource; prepare a new map before touching live ECS component pools.
        auto next=store.parked_;
        for(const auto& item:prepared)
            if(!next.emplace(item.id,item.record).second)
                return reject(error,"duplicate parked region identity");
        RegionColdStore candidate;
        candidate.parked_=std::move(next);
        std::string encoded;
        Archive audit(&encoded);
        io(audit,candidate);
        if(!audit.ok())
            return reject(error,"parked resource exceeds persistence limits");

        // Structural changes are committed after every source has validated.
        // Low-memory exceptions from ECS pool cloning remain a host exception
        // boundary; call only at tick barriers when no readers hold references.
        for(const auto& item:prepared) {
            for(const auto& [name,blob]:item.record.components) {
                (void)blob;
                adapters_.at(name).remove(world,item.entity);
            }
            world.add<RegionDormant>(item.entity,RegionDormant{});
        }
        store.parked_.swap(candidate.parked_);
        return true;
    }

    bool wakeGroup(World& world,const RegionEntityLedger& ledger,
                   RegionColdStore& store,std::span<const Entity> entities,
                   Cell owner,std::string* error=nullptr)const {
        if(entities.empty())return reject(error,"empty region wake group");
        std::set<uint64_t> handles;
        std::set<RegionEntityId> ids;
        std::vector<Decoded> decoded;
        decoded.reserve(entities.size());
        for(Entity entity:entities) {
            RegionEntityId id=0;
            if(!handles.insert(entity.packed()).second ||
               !identity(world,ledger,entity,owner,id) ||
               !ids.insert(id).second || !world.has<RegionDormant>(entity))
                return reject(error,"invalid or non-dormant ECS entity");
            const auto* record=store.find(id);
            if(!record || record->owner!=owner ||
               record->ownerRevision!=ledger.find(id)->revision)
                return reject(error,"stale cold record or changed region owner");
            Decoded staged;
            staged.entity=entity;staged.id=id;
            if(record->components.empty())return reject(error,"empty cold component record");
            for(const auto& [name,blob]:record->components) {
                const auto it=adapters_.find(name);
                if(it==adapters_.end() || it->second.version!=blob.version ||
                   it->second.present(world,entity))
                    return reject(error,"unknown, mismatched or already resident component");
                std::any value;
                if(!it->second.decode(blob.bytes,value))
                    return reject(error,"corrupt cold component payload");
                staged.components.emplace_back(&it->second,std::move(value));
            }
            decoded.emplace_back(std::move(staged));
        }

        // All decodes and membership checks succeeded: publish as one batch.
        // Component add may throw on allocation; roll back additions when
        // possible. Failure before this point never mutates the live ECS.
        std::vector<std::pair<const Adapter*,Entity>> inserted;
        try {
            for(const auto& item:decoded)
                for(const auto& [codec,value]:item.components) {
                    codec->restore(world,item.entity,value);
                    inserted.emplace_back(codec,item.entity);
                }
        }catch(...) {
            for(auto it=inserted.rbegin();it!=inserted.rend();++it)
                it->first->remove(world,it->second);
            return reject(error,"could not restore the cold ECS components");
        }
        for(const auto& item:decoded) {
            world.remove<RegionDormant>(item.entity);
            store.parked_.erase(item.id);
        }
        return true;
    }

    // Deterministic region-wide transition, called by the authoritative game
    // tick when gameplay decides which regions run hot. It is intentionally
    // independent of visual observers/ChunkStreamer budgets.
    // The first W1 implementation retains Entity and Transform in both states;
    // if a ledger ID has no materialized Entity, fail rather than silently lose it.
    // A mixed hot/cold region is rejected instead of partially mutating it.
    bool setRegionActive(World& world,const RegionEntityLedger& ledger,
                         RegionColdStore& store,Cell region,bool active,
                         std::string* error=nullptr)const {
        std::map<RegionEntityId,Entity> live;
        if(!RegionEcsBridge::rebuild(world,ledger,live,error))return false;
        const auto ids=ledger.activeIn(region);
        if(ids.empty())return true;
        std::vector<Entity> entities;
        entities.reserve(ids.size());
        size_t dormant=0;
        for(RegionEntityId id:ids) {
            const auto it=live.find(id);
            if(it==live.end())
                return reject(error,"region entity is not materialized");
            const bool isDormant=world.has<RegionDormant>(it->second);
            const auto* record=store.find(id);
            if(isDormant) {
                ++dormant;
                if(!record || record->owner!=region ||
                   record->ownerRevision!=ledger.find(id)->revision)
                    return reject(error,"dormant region record is missing or stale");
            }else if(record) {
                return reject(error,"hot entity unexpectedly has a cold payload");
            }
            entities.push_back(it->second);
        }
        if(dormant && dormant!=ids.size())
            return reject(error,"mixed active and dormant entities in one region");
        if(active) {
            if(!dormant)return true; // already hot, no writes
            return wakeGroup(world,ledger,store,entities,region,error);
        }
        if(dormant==ids.size())return true; // already cold, no writes
        return parkGroup(world,ledger,store,entities,region,error);
    }

    // A dormant entity cannot pass through RegionEcsBridge::retire because its
    // payload must be removed together with the permanent ledger tombstone.
    bool retireDormant(World& world,RegionEntityLedger& ledger,
                       RegionColdStore& store,Entity entity,Cell owner,
                       std::string* error=nullptr)const {
        RegionEntityId id=0;
        if(!identity(world,ledger,entity,owner,id) ||
           !world.has<RegionDormant>(entity))
            return reject(error,"retiring entity is not valid and dormant");
        const auto* record=store.find(id);
        if(!record || record->owner!=owner ||
           record->ownerRevision!=ledger.find(id)->revision)
            return reject(error,"cannot retire stale cold region payload");
        if(!ledger.despawn(id,owner))
            return reject(error,"ledger refused dormant retirement");
        store.parked_.erase(id);
        world.destroy(entity);
        return true;
    }
};

} // namespace aiverse::world
