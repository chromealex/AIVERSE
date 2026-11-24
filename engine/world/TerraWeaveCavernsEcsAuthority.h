#pragma once
// Stage50: authored cave density inputs are canonical ECS state.
// Meshes, GPU, SpatialWorld, Box3D and navigation are derived owners.
// All edits run on the authoritative fixed-tick command path, never a worker.
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include "engine/sim/Simulation.h"
#include "engine/core/Archive.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveAuthorityResource {
    uint32_t version=1;
    std::string recipeId;
    CaveVolumeSnapshot volume;
    uint64_t nextStampId=1;
};
struct CaveAuthorityConfig {
    std::string recipeId;
    uint64_t generatorVersion=1;
    uint32_t resolution=16;
    int32_t spacingCm=100;
    uint32_t maxDirtyChunks=1024;
    uint32_t maxEditsPerCommand=64;
};
inline void validateCaveAuthorityConfig(const CaveAuthorityConfig& cfg) {
    if(cfg.recipeId.empty()||cfg.recipeId.size()>128||!cfg.generatorVersion||
       !cfg.resolution||cfg.resolution>64||cfg.spacingCm<1||cfg.spacingCm>10000||
       !cfg.maxDirtyChunks||cfg.maxDirtyChunks>1024||
       !cfg.maxEditsPerCommand||cfg.maxEditsPerCommand>1024)
        throw std::invalid_argument("Caverns ECS authority config");
}
inline void validateCaveAuthorityState(const CaveAuthorityResource& state) {
    if(state.version!=1||state.recipeId.empty()||state.recipeId.size()>128||
       !state.volume.generatorVersion||!state.volume.sourceRevision||
       !state.nextStampId||state.volume.stamps.size()>16384||
       state.volume.entrances.size()>4096)
        throw std::invalid_argument("Caverns ECS authority state");
    validate(state.volume.settings);
    uint64_t previous=0;
    for(const auto& stamp:state.volume.stamps) {
        validatePoint(stamp.center);
        if(!stamp.id||stamp.id<=previous||stamp.id>=state.nextStampId||
           stamp.radiusCm<1||stamp.radiusCm>1000000||
           (stamp.mode!=CaveStampMode::Carve&&stamp.mode!=CaveStampMode::Fill))
            throw std::invalid_argument("Caverns noncanonical stamp state");
        previous=stamp.id;
    }
    bool first=true;
    previous=0;
    for(const auto& entrance:state.volume.entrances) {
        validatePoint({entrance.xCm,entrance.caveYcm,entrance.zCm});
        validatePoint({entrance.xCm,entrance.surfaceYcm,entrance.zCm});
        if((!first&&entrance.id<=previous)||entrance.radiusCm<1||
           entrance.radiusCm>1000000||entrance.surfaceYcm<entrance.caveYcm)
            throw std::invalid_argument("Caverns noncanonical entrance state");
        previous=entrance.id;first=false;
    }
}
namespace authority_detail {
template<class T,class Transfer>
inline void records(Archive& a,std::vector<T>& values,uint32_t cap,
                    size_t bytes,Transfer transfer) {
    if(a.writing()&&values.size()>cap){a.fail();return;}
    const auto count=a.count(values.size(),bytes);
    if(!a.ok()||count>cap||(!a.writing()&&count>a.remaining()/bytes)){
        a.fail();return;
    }
    if(!a.writing())values.resize(count);
    for(auto& value:values){if(!a.ok())break;transfer(a,value);}
}
inline void recipe(Archive& a,std::string& value) {
    if(a.writing()&&(value.empty()||value.size()>128)){a.fail();return;}
    uint32_t size=uint32_t(value.size());a(size);
    if(!a.ok()||!size||size>128||(!a.writing()&&size>a.remaining())){
        a.fail();return;
    }
    if(!a.writing())value.resize(size);
    a.block(value.data(),size);
}
inline void point(Archive& a,PointCm& p){a(p.x,p.y,p.z);}
inline void stamp(Archive& a,CaveSphereStamp& s){
    a(s.id);point(a,s.center);a(s.radiusCm,s.mode);
}
inline void entrance(Archive& a,Entrance& e){
    a(e.id,e.xCm,e.zCm,e.surfaceYcm,e.caveYcm,e.radiusCm);
}
inline void settings(Archive& a,Settings& s){
    a(s.seed,s.cellCm,s.tunnelRadiusCm,s.chamberRadiusCm,
      s.tunnelChance,s.chamberChance,s.minYcm,s.maxYcm);
}
inline bool sameStamp(const CaveSphereStamp& a,const CaveSphereStamp& b){
    return a.id==b.id&&a.center.x==b.center.x&&a.center.y==b.center.y&&
           a.center.z==b.center.z&&a.radiusCm==b.radiusCm&&a.mode==b.mode;
}
inline void mergeBounds(CaveDirtyBounds& a,const CaveDirtyBounds& b){
    a.minX=std::min(a.minX,b.minX);a.minY=std::min(a.minY,b.minY);
    a.minZ=std::min(a.minZ,b.minZ);a.maxX=std::max(a.maxX,b.maxX);
    a.maxY=std::max(a.maxY,b.maxY);a.maxZ=std::max(a.maxZ,b.maxZ);
}
}
inline void io(Archive& a,CaveAuthorityResource& s) {
    if(a.writing()){
        try{validateCaveAuthorityState(s);}catch(const std::exception&){a.fail();return;}
    }
    a(s.version);
    if(!a.ok()||s.version!=1){a.fail();return;}
    authority_detail::recipe(a,s.recipeId);
    a(s.volume.generatorVersion,s.volume.sourceRevision,s.nextStampId);
    authority_detail::settings(a,s.volume.settings);
    authority_detail::records(a,s.volume.entrances,4096,44,authority_detail::entrance);
    authority_detail::records(a,s.volume.stamps,16384,37,authority_detail::stamp);
    if(!a.ok())return;
    if(!a.writing()){
        try{validateCaveAuthorityState(s);}catch(const std::exception&){a.fail();}
    }
}
inline void requireCaveRecipe(const CaveAuthorityResource& s,const CaveAuthorityConfig& cfg){
    validateCaveAuthorityConfig(cfg);
    validateCaveAuthorityState(s);
    if(s.recipeId!=cfg.recipeId||s.volume.generatorVersion!=cfg.generatorVersion)
        throw std::invalid_argument("Caverns ECS immutable recipe mismatch");
}
inline void registerCaveAuthorityResource(Simulation& sim,const CaveAuthorityConfig& cfg,
                                          CaveVolumeSnapshot initial) {
    validateCaveAuthorityConfig(cfg);
    canonicalizeCaveSnapshot(initial);
    CaveAuthorityResource resource;
    resource.recipeId=cfg.recipeId;
    resource.volume=std::move(initial);
    if(!resource.volume.stamps.empty()){
        const auto last=resource.volume.stamps.back().id;
        if(last==UINT64_MAX)throw std::overflow_error("Caverns stamp ID exhausted");
        resource.nextStampId=last+1;
    }
    requireCaveRecipe(resource,cfg);
    sim.registerPersistentResource<CaveAuthorityResource>("terraweave.caverns.authority",1);
    sim.world().setResource<CaveAuthorityResource>(std::move(resource));
}
enum class CaveStampEditKind:uint8_t { Add,Replace,Remove };
struct CaveStampEdit {
    CaveStampEditKind kind=CaveStampEditKind::Add;
    CaveSphereStamp stamp{}; // Add requires id=0; Replace/Remove require existing ID.
};
struct CaveStampCommand {
    uint64_t expectedRevision=0;
    std::vector<CaveStampEdit> edits;
};
inline void io(Archive& a,CaveStampCommand& c){
    a(c.expectedRevision);
    authority_detail::records(a,c.edits,1024,38,[](Archive& ar,CaveStampEdit& edit){
        ar(edit.kind);authority_detail::stamp(ar,edit.stamp);
        if(edit.kind!=CaveStampEditKind::Add&&edit.kind!=CaveStampEditKind::Replace&&
           edit.kind!=CaveStampEditKind::Remove)ar.fail();
    });
}
struct CaveAuthorityChange {
    uint64_t oldRevision=0,newRevision=0;
    uint32_t tick=0;
    bool changed=false;
    CaveDirtyBounds bounds{};
    std::vector<uint64_t> createdIds;
};
inline std::shared_ptr<const CaveVolumeSnapshot> freezeCaveAuthority(
    const World& world,const CaveAuthorityConfig& cfg) {
    const auto* state=world.resourceConst<CaveAuthorityResource>();
    if(!state)throw std::logic_error("Caverns authority not initialized");
    requireCaveRecipe(*state,cfg);
    return std::make_shared<const CaveVolumeSnapshot>(state->volume);
}
// Only call for commands already authorized by the game (permissions,
// ownership, tool reach, costs, rate limits). A rejected command mutates
// no ECS state. The persistent stamp allocator never reuses removed IDs.
inline CaveAuthorityChange applyAuthorizedCaveStampCommand(
    World& world,const CaveAuthorityConfig& cfg,const CaveStampCommand& command,
    uint32_t tick) {
    const auto* old=world.resourceConst<CaveAuthorityResource>();
    if(!old)throw std::logic_error("Caverns authority not initialized");
    requireCaveRecipe(*old,cfg);
    if(command.expectedRevision!=old->volume.sourceRevision||
       command.edits.empty()||command.edits.size()>cfg.maxEditsPerCommand)
        throw std::invalid_argument("Caverns stale or oversized command");
    CaveAuthorityResource next=*old;
    CaveAuthorityChange change;
    change.tick=tick;change.oldRevision=change.newRevision=old->volume.sourceRevision;
    const auto dirty=[&](const CaveSphereStamp& stamp){
        const auto b=dirtyBoundsForStamp(stamp,cfg.spacingCm);
        if(!change.changed)change.bounds=b;
        else authority_detail::mergeBounds(change.bounds,b);
        change.changed=true;
    };
    std::set<uint64_t> touched;
    auto& stamps=next.volume.stamps;
    for(const auto& edit:command.edits){
        auto stamp=edit.stamp;
        if(edit.kind==CaveStampEditKind::Add){
            if(stamp.id||stamps.size()>=16384||next.nextStampId==UINT64_MAX)
                throw std::invalid_argument("Caverns stamp allocation");
            stamp.id=next.nextStampId++;
            validatePoint(stamp.center);
            if(stamp.radiusCm<1||stamp.radiusCm>1000000||
               (stamp.mode!=CaveStampMode::Carve&&stamp.mode!=CaveStampMode::Fill))
                throw std::invalid_argument("Caverns invalid brush");
            touched.insert(stamp.id);
            dirty(stamp);stamps.push_back(stamp);change.createdIds.push_back(stamp.id);
            continue;
        }
        if(edit.kind!=CaveStampEditKind::Replace&&edit.kind!=CaveStampEditKind::Remove)
            throw std::invalid_argument("Caverns invalid edit kind");
        if(!stamp.id||!touched.insert(stamp.id).second)
            throw std::invalid_argument("Caverns duplicate edit ID");
        auto it=std::lower_bound(stamps.begin(),stamps.end(),stamp.id,
            [](const CaveSphereStamp& s,uint64_t id){return s.id<id;});
        if(it==stamps.end()||it->id!=stamp.id)
            throw std::invalid_argument("Caverns unknown stamp ID");
        if(edit.kind==CaveStampEditKind::Remove){
            dirty(*it);stamps.erase(it);continue;
        }
        validatePoint(stamp.center);
        if(stamp.radiusCm<1||stamp.radiusCm>1000000||
           (stamp.mode!=CaveStampMode::Carve&&stamp.mode!=CaveStampMode::Fill))
            throw std::invalid_argument("Caverns invalid replacement brush");
        if(authority_detail::sameStamp(*it,stamp))continue;
        dirty(*it);dirty(stamp);*it=stamp;
    }
    if(!change.changed)return change;
    if(next.volume.sourceRevision==UINT64_MAX)
        throw std::overflow_error("Caverns authority revision exhausted");
    // Reject unbounded regeneration before replacing canonical ECS state.
    (void)dirtyCaveChunks(change.bounds,cfg.resolution,cfg.spacingCm,cfg.maxDirtyChunks);
    ++next.volume.sourceRevision;change.newRevision=next.volume.sourceRevision;
    validateCaveAuthorityState(next);
    world.setResource<CaveAuthorityResource>(std::move(next));
    return change;
}
} // namespace aiverse::terraweave::caverns
