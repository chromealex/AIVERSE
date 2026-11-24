// Reusable authoring/presentation mirror. A native game can instead feed its
// committed World directly to SpectraPresentationSystem; no GPU handles in ECS.
#pragma once
#include "SpectraSourceDocument.h"
#include <map>
#include <set>
#include <vector>

namespace aiverse::lib::mechanics {
struct SpectraSourceSnapshot {
    std::string identity;
    SpectraSourceComponent source;
    fx::float3 position{},direction{0,1,0};
};
struct SpectraSourceMirrorPose {fx::float3 position{},direction{0,1,0};};

class SpectraSourceMirror {
public:
    void sync(const std::vector<SpectraSourceSnapshot>& rows){
        std::set<std::string> present;
        for(const auto& row:rows){
            if(row.identity.empty()||!present.insert(row.identity).second)continue;
            auto it=entities_.find(row.identity);
            if(it==entities_.end()){
                const Entity entity=world_.create();
                world_.add<SpectraSourceComponent>(entity,row.source);
                world_.add<SpectraSourceMirrorPose>(entity,SpectraSourceMirrorPose{row.position,row.direction});
                entities_.emplace(row.identity,entity);
            }else{
                world_.get<SpectraSourceComponent>(it->second)=row.source;
                world_.get<SpectraSourceMirrorPose>(it->second)={row.position,row.direction};
            }
        }
        for(auto it=entities_.begin();it!=entities_.end();){
            if(present.count(it->first)){++it;continue;}
            world_.destroy(it->second);it=entities_.erase(it);
        }
    }
    void clear(){sync({});}
    const World& world() const{return world_;}
    size_t size() const{return entities_.size();}
    static bool resolve(const World& world,Entity e,fx::float3& pos,fx::float3& dir){
        const auto* pose=world.tryGetConst<SpectraSourceMirrorPose>(e);
        if(!pose)return false;
        pos=pose->position;dir=pose->direction;return true;
    }
private:
    World world_;
    std::map<std::string,Entity> entities_;
};
}
