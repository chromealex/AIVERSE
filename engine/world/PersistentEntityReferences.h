#pragma once
#include "RegionEntityLedger.h"
#include "engine/core/ECS.h"
#include <map>
namespace aiverse::world {
// Derived mapping scoped to one materialized World. Never serialize runtime
// handles as durable identities. Rebuild after restore or allocator changes.
class PersistentEntityReferences {
    std::map<RegionEntityId,Entity> byId_;
    std::map<uint64_t,RegionEntityId> byHandle_;
public:
    bool bind(const World& world,RegionEntityId id,Entity entity){
        if(!id||!world.alive(entity))return false;
        auto a=byId_.find(id);auto b=byHandle_.find(entity.packed());
        if(a!=byId_.end()||b!=byHandle_.end())
            return a!=byId_.end()&&b!=byHandle_.end()&&a->second==entity&&b->second==id;
        byId_.emplace(id,entity);
        try{byHandle_.emplace(entity.packed(),id);}catch(...){byId_.erase(id);throw;}
        return true;
    }
    bool unbind(RegionEntityId id){
        auto it=byId_.find(id);if(it==byId_.end())return false;
        byHandle_.erase(it->second.packed());byId_.erase(it);return true;
    }
    bool encode(const World& world,Entity entity,RegionEntityId& out)const{
        if(entity==kNullEntity){out=0;return true;}
        if(!world.alive(entity))return false;
        auto it=byHandle_.find(entity.packed());if(it==byHandle_.end())return false;
        out=it->second;return true;
    }
    bool resolve(const World& world,RegionEntityId id,Entity& out)const{
        if(!id){out=kNullEntity;return true;}
        auto it=byId_.find(id);
        if(it==byId_.end()||!world.alive(it->second))return false;
        out=it->second;return true;
    }
    void clear(){byId_.clear();byHandle_.clear();}
    size_t size()const{return byId_.size();}
};
// Explicit adapter helper: use in durable component codecs, not native
// rollback Archive Entity IO. Missing references fail closed.
inline bool ioPersistentEntity(Archive& archive,const World& world,
    const PersistentEntityReferences& refs,Entity& entity){
    RegionEntityId id=0;
    if(archive.writing()&&!refs.encode(world,entity,id)){archive.fail();return false;}
    archive(id);
    if(!archive.ok())return false;
    if(!archive.writing()&&!refs.resolve(world,id,entity)){archive.fail();return false;}
    return true;
}
}
