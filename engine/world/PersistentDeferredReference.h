#pragma once
#include "PersistentEntityReferences.h"
namespace aiverse::world {
// Durable value: never caches a runtime handle across materialization or rollback.
struct PersistentDeferredReference {
    RegionEntityId id=0;
    bool operator==(const PersistentDeferredReference&)const=default;
    using Resolution=PersistentEntityReferences::Resolution;
    Resolution resolve(const World& world,const RegionEntityLedger& ledger,
        const PersistentEntityReferences& refs,Entity& out)const {
        out=kNullEntity;
        return refs.classify(world,ledger,id,out);
    }
    bool bind(const World& world,const RegionEntityLedger& ledger,
        const PersistentEntityReferences& refs,Entity target){
        RegionEntityId candidate=0;
        if(!refs.encode(world,target,candidate))return false;
        Entity resolved=kNullEntity;
        const auto status=refs.classify(world,ledger,candidate,resolved);
        if(status!=Resolution::Null && (status!=Resolution::Materialized||resolved!=target))return false;
        id=candidate;return true;
    }
    void clear(){id=0;}
};
inline void io(Archive& a,PersistentDeferredReference& value){a(value.id);}
}
