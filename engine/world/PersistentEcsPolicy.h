#pragma once
#include "engine/sim/Simulation.h"
#include <map>
namespace aiverse::world {
enum class PersistentStorageMode { Transient, Checkpoint, Journal };
class PersistentEcsPolicy {
    std::map<std::string,PersistentStorageMode> modes_;
public:
    bool set(const Simulation& sim,const std::string& id,PersistentStorageMode mode){
        if(mode!=PersistentStorageMode::Transient&&mode!=PersistentStorageMode::Checkpoint&&mode!=PersistentStorageMode::Journal)return false;
        const auto schemas=sim.persistentSchemas();
        if(std::none_of(schemas.begin(),schemas.end(),[&](const auto& s){return s.id==id;}))return false;
        if(mode==PersistentStorageMode::Transient)modes_.erase(id);
        else modes_[id]=mode;
        return true;
    }
    PersistentStorageMode mode(const std::string& id)const{
        auto it=modes_.find(id);return it==modes_.end()?PersistentStorageMode::Transient:it->second;
    }
    // Journal schemas also need a checkpoint baseline. Capture at a
    // quiescent finalized barrier; output is sorted by stable schema ID.
    bool capture(const Simulation& sim,bool journalOnly,
        std::vector<PersistentEcsRegistry::LoadedHash::Chunk>& out)const{
        std::vector<PersistentEcsRegistry::LoadedHash::Chunk> staged;
        for(const auto& [id,mode]:modes_){
            if(journalOnly&&mode!=PersistentStorageMode::Journal)continue;
            PersistentEcsRegistry::LoadedHash::Chunk chunk;
            if(!sim.capturePersistentSchema(id,chunk))return false;
            staged.push_back(std::move(chunk));
        }
        out.swap(staged);return true;
    }
};
}
