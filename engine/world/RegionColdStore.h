// Durable hot/cold ECS component parking. This is an opt-in gameplay-tick
// transition, never a callback from asynchronous view/asset streaming.
#pragma once
#include "engine/world/RegionEcsBridge.h"
#include <algorithm>
#include <any>
#include <cstdint>
#include <functional>
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
void registerRegionColdState(SimulationT& sim,
                             RegionPositionReader position=regionTransformPosition) {
    if(!position)throw std::invalid_argument("region cold-state position reader is required");
    sim.template registerPersistentResource<RegionColdStore>(
        "aiverse.world.region_cold_store",RegionColdStore::kSchema);
    sim.template registerPersistentComponent<RegionDormant>(
        "aiverse.world.region_dormant",1);
    sim.addPersistentLoadValidator("aiverse.world.region_cold_consistency",
        [position=std::move(position)](const World& world,std::string& error)->bool {
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
                    if(!world.alive(e) || !position(world,e).has_value() ||
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
        uint32_t type;
        bool (*present)(const World&,Entity);
        bool (*encode)(const World&,Entity,std::string&);
        bool (*decode)(const std::string&,std::any&);
        bool (*encodeValue)(std::any&,std::string&);
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
    RegionPositionReader position_=regionTransformPosition;

    static bool reject(std::string* error,const char* message) {
        if(error)*error=message;
        return false;
    }
    bool identity(const World& world,const RegionEntityLedger& ledger,
                  Entity entity,Cell expected,RegionEntityId& id)const {
        if(!world.alive(entity) || !position_(world,entity).has_value())return false;
        const auto* tag=world.tryGetConst<RegionStableIdentity>(entity);
        if(!tag || !tag->id)return false;
        const auto* record=ledger.find(tag->id);
        if(!record || !record->live || record->owner!=expected)return false;
        id=tag->id;
        return true;
    }

public:
    // Host-owned explicit component policy, configured at setup. Every
    // parked type in a guarded group must be classified: catch up using its
    // portable clock, or explicitly pause. No implicit clock or save field.
    class ColdAdvancePlan {
        struct Entry {
            uint32_t type=0;
            std::function<uint64_t(const std::any&)> tick;
            std::function<bool(std::any&,RegionEntityId,uint64_t,uint64_t)> advance;
        };
        std::map<std::string,Entry> entries_;
        friend class RegionDormancyController;
        bool insert(std::string name,Entry value) {
            if(name.empty() || name.size()>128 ||
               entries_.size()>=RegionColdStore::kMaxComponents)return false;
            for(const auto& [key,entry]:entries_) {
                (void)key;
                if(entry.type==value.type)return false;
            }
            return entries_.emplace(std::move(name),std::move(value)).second;
        }
    public:
        template<class T,class ReadTick,class Advance>
        bool addAdvance(std::string name,ReadTick readTick,Advance advance) {
            return insert(std::move(name),Entry{
                aiverse::detail::componentTypeId<T>(),
                [readTick=std::move(readTick)](const std::any& v)->uint64_t {
                    return readTick(std::any_cast<const T&>(v));
                },
                [advance=std::move(advance)](std::any& v,RegionEntityId id,
                                             uint64_t from,uint64_t to)->bool {
                    return advance(std::any_cast<T&>(v),id,from,to);
                }
            });
        }
        template<class T>
        bool addPaused(std::string name) {
            return insert(std::move(name),Entry{aiverse::detail::componentTypeId<T>(),{},{}});
        }
    };

private:
    bool validatePlan(const ColdAdvancePlan& plan,std::string* error)const {
        if(plan.entries_.empty())return reject(error,"empty cold advancement plan");
        for(const auto& [name,entry]:plan.entries_) {
            const auto it=adapters_.find(name);
            if(it==adapters_.end() || it->second.type!=entry.type)
                return reject(error,"cold policy does not match registered codec");
        }
        return true;
    }

    // Stable-ID order for every guarded operation and eligibility callback.
    // The domain owner still has to verify COMPLETE linked group membership.
    bool canonicalGroup(const World& world,const RegionEntityLedger& ledger,
                        const RegionColdStore& store,std::span<const Entity> entities,
                        Cell owner,bool dormant,std::vector<Entity>& ordered,
                        std::string* error)const {
        if(entities.empty() || entities.size()>RegionColdStore::kMaxEntities)
            return reject(error,"empty or oversized cold group");
        std::map<RegionEntityId,Entity> members;
        for(Entity entity:entities) {
            RegionEntityId id=0;
            if(!identity(world,ledger,entity,owner,id) ||
               !members.emplace(id,entity).second)
                return reject(error,"invalid or duplicate cold group member");
            const auto* marker=world.tryGetConst<RegionDormant>(entity);
            const auto* record=store.find(id);
            if(dormant) {
                if(!marker || marker->marker!=1 || !record ||
                   record->owner!=owner ||
                   record->ownerRevision!=ledger.find(id)->revision)
                    return reject(error,"invalid or stale dormant group member");
            }else if(marker || record) {
                return reject(error,"parking group member is already dormant");
            }
        }
        ordered.reserve(members.size());
        for(const auto& [id,entity]:members) {
            (void)id;
            ordered.push_back(entity);
        }
        return true;
    }

    struct ColdWork {
        RegionEntityId id=0;
        const std::string* name=nullptr;
        const Adapter* codec=nullptr;
        const ColdAdvancePlan::Entry* policy=nullptr;
        std::any value;
        uint64_t from=0;
    };

    // Complete mode accepts heterogeneous entities, but every parked blob
    // must have a matching explicit policy. Legacy single-type mode requires
    // that type on every member and ignores unrelated paused payloads.
    bool prepareAdvance(const World& world,const RegionColdStore& store,
                        std::span<const Entity> ordered,const ColdAdvancePlan& plan,
                        uint64_t targetTick,bool complete,std::vector<ColdWork>& work,
                        std::string* error)const {
        if(!validatePlan(plan,error))return false;
        for(Entity entity:ordered) {
            const auto id=world.tryGetConst<RegionStableIdentity>(entity)->id;
            const auto* record=store.find(id);
            if(!record || record->components.empty())
                return reject(error,"missing cold group payload");
            size_t matched=0;
            for(const auto& [name,blob]:record->components) {
                const auto rule=plan.entries_.find(name);
                if(rule==plan.entries_.end()) {
                    if(complete)return reject(error,"parked component has no explicit policy");
                    continue;
                }
                ++matched;
                const auto& codec=adapters_.at(name);
                if(codec.version!=blob.version || codec.present(world,entity))
                    return reject(error,"stale or already resident cold component");
                std::any decoded;
                if(!codec.decode(blob.bytes,decoded))
                    return reject(error,"corrupt cold component payload");
                if(!rule->second.tick)continue; // explicitly safe to pause
                const uint64_t from=rule->second.tick(decoded);
                if(from>targetTick)return reject(error,"cold advancement cannot rewind a component");
                if(from!=targetTick)
                    work.push_back({id,&name,&codec,&rule->second,std::move(decoded),from});
            }
            if(!matched)return reject(error,"cold group member has no matching policy");
        }
        return true;
    }

    // Work has already been decoded and validated. Apply in stable-ID then
    // component-name order to a private candidate; audit aggregate budget once.
    bool stageAdvance(const RegionColdStore& store,RegionColdStore& candidate,
                      std::vector<ColdWork>& work,uint64_t targetTick,
                      std::string* error)const {
        candidate=store;
        size_t encodedBytes=0;
        for(auto& item:work) {
            if(!item.policy->advance(item.value,item.id,item.from,targetTick) ||
               item.policy->tick(item.value)!=targetTick)
                return reject(error,"cold policy refused or did not record target tick");
            std::string bytes;
            if(!item.codec->encodeValue(item.value,bytes) ||
               bytes.size()>RegionColdStore::kMaxTotalBytes-encodedBytes)
                return reject(error,"advanced cold payload exceeds codec budget");
            encodedBytes+=bytes.size();
            std::any roundTrip;
            if(!item.codec->decode(bytes,roundTrip) ||
               item.policy->tick(roundTrip)!=targetTick)
                return reject(error,"advanced cold timestamp is not persistent");
            candidate.parked_.at(item.id).components.at(*item.name).bytes=std::move(bytes);
        }
        if(work.empty())return true;
        std::string encoded;
        Archive audit(&encoded);
        io(audit,candidate);
        if(!audit.ok())return reject(error,"advanced cold store exceeds persistence limits");
        return true;
    }

    bool advanceWithPlan(const World& world,const RegionEntityLedger& ledger,
                         RegionColdStore& store,std::span<const Entity> entities,
                         Cell owner,uint64_t targetTick,const ColdAdvancePlan& plan,
                         bool complete,std::string* error)const {
        std::vector<Entity> ordered;
        std::vector<ColdWork> work;
        if(!canonicalGroup(world,ledger,store,entities,owner,true,ordered,error) ||
           !prepareAdvance(world,store,ordered,plan,targetTick,complete,work,error))return false;
        if(work.empty())return true;
        RegionColdStore candidate;
        if(!stageAdvance(store,candidate,work,targetTick,error))return false;
        store.parked_.swap(candidate.parked_);
        return true;
    }

    template<class Admit>
    bool advanceAndWakeWithPlan(World& world,const RegionEntityLedger& ledger,
                                RegionColdStore& store,std::span<const Entity> entities,
                                Cell owner,uint64_t targetTick,const ColdAdvancePlan& plan,
                                bool complete,Admit admit,std::string* error)const {
        std::vector<Entity> ordered;
        std::vector<ColdWork> work;
        if(!canonicalGroup(world,ledger,store,entities,owner,true,ordered,error) ||
           !prepareAdvance(world,store,ordered,plan,targetTick,complete,work,error))return false;
        if(!admit(std::as_const(world),owner,std::span<const Entity>(ordered)))
            return reject(error,"cold wake admission refused");
        RegionColdStore candidate;
        if(!stageAdvance(store,candidate,work,targetTick,error))return false;
        if(!wakeGroup(world,ledger,candidate,ordered,owner,error))return false;
        store.parked_.swap(candidate.parked_);
        return true;
    }

public:
    void setPositionReader(RegionPositionReader position) {
        if(!position)throw std::invalid_argument("region position reader is required");
        position_=std::move(position);
    }
    const RegionPositionReader& positionReader()const{return position_;}

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
        const uint32_t type=aiverse::detail::componentTypeId<T>();
        for(const auto& [key,codec]:adapters_) {
            (void)key;
            if(codec.type==type)return false;
        }
        Adapter entry{
            version,type,
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
            +[](std::any& source,std::string& bytes)->bool {
                auto* value=std::any_cast<T>(&source);
                if(!value)return false;
                Archive a(&bytes);
                io(a,*value);
                return a.ok() && bytes.size()<=RegionColdStore::kMaxComponentBytes;
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

    // Explicit opt-in eligibility at a quiescent authoritative tick.
    // Verify complete policy coverage and each active portable clock before
    // removal. The predicate must check pins, external references, occupants,
    // active actions and other owner-specific constraints, without writes.
    template<class Admit>
    bool parkGroupGuarded(World& world,const RegionEntityLedger& ledger,
                          RegionColdStore& store,std::span<const Entity> entities,
                          Cell owner,uint64_t currentTick,const ColdAdvancePlan& plan,
                          Admit admit,std::string* error=nullptr)const {
        std::vector<Entity> ordered;
        if(!canonicalGroup(world,ledger,store,entities,owner,false,ordered,error) ||
           !validatePlan(plan,error))return false;
        if(!admit(std::as_const(world),owner,std::span<const Entity>(ordered)))
            return reject(error,"cold parking eligibility refused");
        for(Entity entity:ordered) {
            size_t matched=0;
            for(const auto& [name,codec]:adapters_) {
                if(!codec.present(world,entity))continue;
                ++matched;
                const auto policy=plan.entries_.find(name);
                if(policy==plan.entries_.end())
                    return reject(error,"live parkable component has no explicit policy");
                if(!policy->second.tick)continue;
                std::string bytes;
                std::any decoded;
                if(!codec.encode(world,entity,bytes) || !codec.decode(bytes,decoded) ||
                   policy->second.tick(decoded)!=currentTick)
                    return reject(error,"parkable component clock is not at current tick");
            }
            if(!matched)return reject(error,"entity has no registered parkable components");
        }
        return parkGroup(world,ledger,store,ordered,owner,error);
    }

    // Explicit complete policy for heterogeneous component groups. Both
    // APIs are trusted native calls; no external callbacks or side effects
    // are permitted from policy functions. All clocked blobs share the
    // authoritative target tick; unrelated non-parked components stay live.
    bool advanceGroup(const World& world,const RegionEntityLedger& ledger,
                      RegionColdStore& store,std::span<const Entity> entities,
                      Cell owner,uint64_t targetTick,const ColdAdvancePlan& plan,
                      std::string* error=nullptr)const {
        return advanceWithPlan(world,ledger,store,entities,owner,targetTick,plan,true,error);
    }

    template<class Admit>
    bool advanceAndWakeGroup(World& world,const RegionEntityLedger& ledger,
                             RegionColdStore& store,std::span<const Entity> entities,
                             Cell owner,uint64_t targetTick,const ColdAdvancePlan& plan,
                             Admit admit,std::string* error=nullptr)const {
        return advanceAndWakeWithPlan(world,ledger,store,entities,owner,targetTick,
                                      plan,true,std::move(admit),error);
    }

    // W1.29a single-type compatibility: every member must own this named
    // component; all other parked types must be safe to pause. Prefer the
    // complete plan overload for new hosts.
    template<class T,class ReadTick,class Advance>
    bool advanceGroup(const World& world,const RegionEntityLedger& ledger,
                      RegionColdStore& store,std::span<const Entity> entities,
                      Cell owner,const std::string& component,uint64_t targetTick,
                      ReadTick readTick,Advance advance,std::string* error=nullptr)const {
        ColdAdvancePlan plan;
        if(!plan.addAdvance<T>(component,std::move(readTick),std::move(advance)))
            return reject(error,"invalid cold advancement component");
        return advanceWithPlan(world,ledger,store,entities,owner,targetTick,plan,false,error);
    }

    template<class T,class ReadTick,class Advance,class Admit>
    bool advanceAndWakeGroup(World& world,const RegionEntityLedger& ledger,
                             RegionColdStore& store,std::span<const Entity> entities,
                             Cell owner,const std::string& component,uint64_t targetTick,
                             ReadTick readTick,Advance advance,Admit admit,
                             std::string* error=nullptr)const {
        ColdAdvancePlan plan;
        if(!plan.addAdvance<T>(component,std::move(readTick),std::move(advance)))
            return reject(error,"invalid cold advancement component");
        return advanceAndWakeWithPlan(world,ledger,store,entities,owner,targetTick,
                                      plan,false,std::move(admit),error);
    }

    bool wakeGroup(World& world,const RegionEntityLedger& ledger,
                   RegionColdStore& store,std::span<const Entity> entities,
                   Cell owner,std::string* error=nullptr)const {
        if(entities.empty() || entities.size()>RegionColdStore::kMaxEntities)
            return reject(error,"empty or oversized region wake group");
        std::set<uint64_t> handles;
        std::set<RegionEntityId> ids;
        std::vector<Decoded> decoded;
        decoded.reserve(entities.size());
        for(Entity entity:entities) {
            RegionEntityId id=0;
            const auto* marker=world.tryGetConst<RegionDormant>(entity);
            if(!handles.insert(entity.packed()).second ||
               !identity(world,ledger,entity,owner,id) ||
               !ids.insert(id).second || !marker || marker->marker!=1)
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
        size_t insertCount=0;
        for(const auto& item:decoded)insertCount+=item.components.size();
        inserted.reserve(insertCount);
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
        if(!RegionEcsBridge::rebuild(world,ledger,live,position_,error))return false;
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
