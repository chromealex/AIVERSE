// TerraWeave: compact authoritative terrain / rooted-plant / grass deltas.
// Network hosts transport these bytes; no sockets, transport-specific protocol,
// GPU, physics or per-blade ECS allocations are owned here.
// All mutations are revision-guarded and validated on copies before swap.
#pragma once
#include "engine/world/TerraWeavePersistence.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

// Additional POD Archive mappings live in TerraWeave namespace so ADL finds
// them without changing shared generic Archive or NEREID ECS serializers.
inline void io(Archive& a,EarthVertexEdit& v){
    a(v.vertex,v.beforeCm,v.afterCm);
}
inline void io(Archive& a,AnchoredPlantId& id){
    a(id.speciesId,id.key);
}
inline void io(Archive& a,PlantSupportChange& change){
    a(change.id,change.support,change.fallen);
}
inline void io(Archive& a,GrassCoverChange& change){
    a(change.key,change.beforeCoveragePermille,
      change.afterCoveragePermille,change.earthRevision);
}
inline void io(Archive& a,PlantFallEvent& event){
    a(event.id,event.habit,event.xCm,event.zCm,
      event.plantedHeightCm,event.fallDirectionXPermille,
      event.fallDirectionZPermille,event.supportPermille,
      event.exposedCm,event.sourceSoilRevision,event.tick);
}

struct TerraWeaveStateDelta {
    uint64_t tick=0;
    uint64_t beforeSoilRevision=0,afterSoilRevision=0;
    std::vector<EarthVertexEdit> terrain;
    std::vector<PlantSupportChange> rooted;
    std::vector<GrassCoverChange> grass;
    std::vector<PlantFallEvent> fell; // gameplay/VFX trigger only; not a Box3D trajectory
};

inline TerraWeaveStateDelta makeTerraWeaveStateDelta(
        uint64_t tick,uint64_t beforeRevision,
        const EarthworkChange& earth,
        const PlantSupportUpdate& rooted,
        const std::vector<GrassCoverChange>& grass){
    TerraWeaveStateDelta out;
    out.tick=tick;
    out.beforeSoilRevision=beforeRevision;
    out.afterSoilRevision=earth.vertices.empty() ?
        beforeRevision:earth.revision;
    out.terrain=earth.vertices;
    out.rooted=rooted.changed;
    out.grass=grass;
    out.fell=rooted.fell;
    return out;
}

inline constexpr uint32_t kTerraWeaveDeltaMagic=0x5457444cu; // TWDL
inline constexpr uint32_t kTerraWeaveDeltaVersion=1;
inline constexpr size_t kTerraWeaveDeltaMaxBytes=8u*1024u*1024u;

inline void validateTerraWeaveStateDelta(const TerraWeaveStateDelta& delta){
    if(delta.terrain.size()>65536u ||
       delta.rooted.size()>65536u ||
       delta.grass.size()>GrassCoverRegistry::kMaxPatches ||
       delta.fell.size()>65536u ||
       delta.beforeSoilRevision==UINT64_MAX)
        throw std::invalid_argument("TerraWeave delta budget/revision");
    if(delta.afterSoilRevision!=
        delta.beforeSoilRevision+(delta.terrain.empty()?0ull:1ull))
        throw std::invalid_argument("TerraWeave delta has invalid soil revision");
    for(size_t i=1;i<delta.terrain.size();++i)
        if(!(delta.terrain[i-1].vertex<delta.terrain[i].vertex))
            throw std::invalid_argument("TerraWeave unsorted/duplicate earth vertices");
    for(const auto& edit:delta.terrain)
        if(edit.beforeCm==edit.afterCm)
            throw std::invalid_argument("TerraWeave no-op earth delta");
    for(size_t i=1;i<delta.rooted.size();++i)
        if(!(delta.rooted[i-1].id<delta.rooted[i].id))
            throw std::invalid_argument("TerraWeave unsorted/duplicate rooted changes");
    for(size_t i=1;i<delta.grass.size();++i)
        if(delta.grass[i-1].key>=delta.grass[i].key)
            throw std::invalid_argument("TerraWeave unsorted/duplicate grass changes");
    for(const auto& change:delta.grass)
        if(!change.key || change.earthRevision!=delta.afterSoilRevision)
            throw std::invalid_argument("TerraWeave invalid grass delta revision");
    for(size_t i=1;i<delta.fell.size();++i)
        if(!(delta.fell[i-1].id<delta.fell[i].id))
            throw std::invalid_argument("TerraWeave unsorted/duplicate fallen changes");
    for(const auto& fall:delta.fell) {
        if(fall.sourceSoilRevision!=delta.afterSoilRevision ||
           fall.tick!=delta.tick || !fall.id.key || !fall.id.speciesId ||
           fall.habit==PlantHabit::Grass ||
           fall.supportPermille>1000u)
            throw std::invalid_argument("TerraWeave invalid falling plant delta");
        const auto it=std::lower_bound(delta.rooted.begin(),delta.rooted.end(),
            fall.id,[](const PlantSupportChange& a,AnchoredPlantId id){
                return a.id<id;
            });
        if(it==delta.rooted.end() || !(it->id==fall.id) ||
           !it->fallen || !it->support.uprooted)
            throw std::invalid_argument(
                "TerraWeave fall event must accompany uprooted state change");
    }
}

namespace delta_detail {
inline void transfer(Archive& a,TerraWeaveStateDelta& delta){
    a(delta.tick,delta.beforeSoilRevision,delta.afterSoilRevision);
    save_detail::array(a,delta.terrain,65536u,24);
    save_detail::array(a,delta.rooted,65536u,40);
    save_detail::array(a,delta.grass,GrassCoverRegistry::kMaxPatches,24);
    save_detail::array(a,delta.fell,65536u,61);
}
} // namespace delta_detail

// Server/replay writer. Canonicalize input ordering (physical simulation
// and ECS pool iteration order must not change bytes or result hash).
inline std::string encodeTerraWeaveStateDelta(TerraWeaveStateDelta delta){
    std::sort(delta.terrain.begin(),delta.terrain.end(),
        [](const auto& a,const auto& b){return a.vertex<b.vertex;});
    std::sort(delta.rooted.begin(),delta.rooted.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    std::sort(delta.grass.begin(),delta.grass.end(),
        [](const auto& a,const auto& b){return a.key<b.key;});
    std::sort(delta.fell.begin(),delta.fell.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    validateTerraWeaveStateDelta(delta);
    std::string bytes;
    Archive out(&bytes);
    uint32_t magic=kTerraWeaveDeltaMagic,version=kTerraWeaveDeltaVersion;
    out(magic,version);
    delta_detail::transfer(out,delta);
    if(!out.ok() || bytes.size()>kTerraWeaveDeltaMaxBytes-8u)
        throw std::length_error("TerraWeave network delta exceeds byte limit");
    uint64_t footer=save_detail::digest(bytes.data(),bytes.size());
    out(footer);
    return bytes;
}

// Decode only; cannot change gameplay state even when packet is corrupt.
// For hostile internet peers this noncryptographic checksum is NOT an
// authentication boundary; the server validates allowed commands separately.
inline TerraWeaveStateDelta decodeTerraWeaveStateDelta(
        const std::string& bytes){
    if(bytes.size()<36u || bytes.size()>kTerraWeaveDeltaMaxBytes)
        throw std::invalid_argument("TerraWeave network delta byte budget");
    Archive in(bytes);
    uint32_t magic=0,version=0;
    in(magic,version);
    if(!in.ok() || magic!=kTerraWeaveDeltaMagic ||
       version!=kTerraWeaveDeltaVersion)
        throw std::invalid_argument("TerraWeave unsupported network delta");
    TerraWeaveStateDelta delta;
    delta_detail::transfer(in,delta);
    uint64_t footer=0;
    in(footer);
    if(!in.ok() || in.remaining()!=0 ||
       footer!=save_detail::digest(bytes.data(),bytes.size()-8u))
        throw std::invalid_argument("TerraWeave corrupted network delta");
    validateTerraWeaveStateDelta(delta);
    return delta;
}

struct AppliedTerraWeaveDelta {
    EarthworkChange earth{};
    std::vector<PlantFallEvent> fell;
    uint64_t tick=0;
    uint64_t soilRevision=0;
};

// Client or replay side: only apply the precomputed server outcomes;
// NEVER call erosion, plant growth or Box3D as if locally authoritative.
inline AppliedTerraWeaveDelta applyTerraWeaveStateDelta(
        const TerraWeaveStateDelta& delta,EarthworksLayer& earth,
        RootSupportRegistry& roots,GrassCoverRegistry& grass) {
    validateTerraWeaveStateDelta(delta);
    if(earth.revision()!=delta.beforeSoilRevision)
        throw std::invalid_argument("TerraWeave delta stale soil revision");

    EarthworksLayer stagedEarth=earth;
    RootSupportRegistry stagedRoots=roots;
    GrassCoverRegistry stagedGrass=grass;
    AppliedTerraWeaveDelta out;
    out.tick=delta.tick;
    out.soilRevision=delta.afterSoilRevision;
    out.fell=delta.fell;
    if(!delta.terrain.empty())
        out.earth=stagedEarth.applyEdits(
            delta.terrain,delta.beforeSoilRevision);
    if(stagedEarth.revision()!=delta.afterSoilRevision)
        throw std::invalid_argument("TerraWeave delta soil application failed");
    stagedRoots.applyAuthoritativeSupportChanges(delta.rooted);
    stagedGrass.applyAuthoritativeCoverageChanges(
        delta.grass,delta.afterSoilRevision);

    earth.exchangeSnapshotState(stagedEarth);
    roots.exchangeSnapshotState(stagedRoots);
    grass.exchangeSnapshotState(stagedGrass);
    return out;
}

// Tick guard is separate from the soil revision: an NEREID bank tick can
// change sediment carry without changing height, and still must not be
// applied twice. Native game persists this watermark beside its ECS tick.
class TerraWeaveDeltaReceiver {
public:
    void setWatermark(uint64_t lastTick,bool initialized=true){
        lastTick_=lastTick;initialized_=initialized;
    }
    uint64_t watermark()const{return lastTick_;}
    bool initialized()const{return initialized_;}
    AppliedTerraWeaveDelta consume(
            const TerraWeaveStateDelta& delta,EarthworksLayer& earth,
            RootSupportRegistry& roots,GrassCoverRegistry& grass){
        if(initialized_ && delta.tick<=lastTick_)
            throw std::invalid_argument("TerraWeave repeated/out-of-order delta tick");
        auto result=applyTerraWeaveStateDelta(delta,earth,roots,grass);
        lastTick_=delta.tick;
        initialized_=true;
        return result;
    }
private:
    uint64_t lastTick_=0;
    bool initialized_=false;
};

} // namespace aiverse::terraweave
