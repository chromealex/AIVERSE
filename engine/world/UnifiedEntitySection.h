#pragma once
#include "UnifiedCheckpointSections.h"
namespace aiverse::world {
class UnifiedEntitySection {
public:
    using State=UnifiedWorldReplay::State;
    using Spawn=std::function<bool(World&,RegionEntityId,const RegionEntityRecord&)>;
    static bool capture(const State& state,std::vector<uint8_t>& out){
        PersistentEntityReferences refs;if(!refs.rebuild(state.world,state.ledger))return false;
        std::vector<uint8_t> b;
        auto put=[&](uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));};
        put(refs.size(),4);
        for(auto id:state.ledger.liveIds()){
            Entity e;if(refs.resolve(state.world,id,e))put(id,8);
        }
        if(b.size()!=4+8*refs.size())return false;
        out.swap(b);return true;
    }
    static bool restore(State& state,uint32_t version,std::span<const uint8_t> b,const Spawn& spawn){
        if(version!=1||!spawn||b.size()<4)return false;
        size_t p=0;auto get=[&](unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return v;};
        auto count=get(4);
        if(count>RegionEntityLedger::kMaxEntities||b.size()!=4+count*8)return false;
        uint64_t previous=0;
        for(uint64_t i=0;i<count;++i){
            auto id=get(8);auto* record=state.ledger.find(id);
            if(id<=previous||!record||!record->live||!spawn(state.world,id,*record))return false;
            previous=id;
        }
        PersistentEntityReferences refs;
        return refs.rebuild(state.world,state.ledger)&&refs.size()==count;
    }
    static UnifiedCheckpointSections::Adapter adapter(Spawn spawn){
        return {1,capture,[spawn=std::move(spawn)](State& s,uint32_t v,std::span<const uint8_t> b){return restore(s,v,b,spawn);}};
    }
};
}
