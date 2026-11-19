// TerraWeave + NEREID: portable, bounded, versioned save of authoritative
// sparse terrain/grass/root/erosion state. Uses the existing AIVERSE Archive
// (explicit little-endian fields, no struct memcpy or float).
// Game must persist same exact procedural base recipe/asset digest separately:
// terrain overrides are only meaningful relative to THAT baseline.
// The tail checksum detects accidental corruption, NOT malicious tampering.
#pragma once
#include "engine/core/Archive.h"
#include "engine/world/TerraWeaveNereidErosionBridge.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

inline void io(Archive& a,EarthVertex& v){a(v.x,v.z);}
inline void io(Archive& a,RootProfile& p){
    a(p.habit,p.radiusCm,p.depthCm,p.exposureThresholdCm,
      p.fallSupportPermille,p.segmentCount);
}
inline void io(Archive& a,PlantEnvironment& e){
    a(e.ageTicks,e.matureTicks,e.lifespanTicks,
      e.lightPermille,e.waterPermille,e.nutrientsPermille);
}
inline void io(Archive& a,RootAnchor& p){
    a(p.plantKey,p.speciesId,p.xCm,p.zCm,p.plantedHeightCm,p.roots);
}
inline void io(Archive& a,RootState& s){
    a(s.plantKey,s.speciesId,s.habit,s.exposedCm,s.supportPermille,
      s.hasVisibleRoots,s.uprooted,
      s.fallDirectionXPermille,s.fallDirectionZPermille);
}
inline void io(Archive& a,AnchoredPlant& p){
    a(p.anchor,p.support,p.environment,p.fallen);
}
inline void io(Archive& a,GrassPatch& p){
    a(p.key,p.bounds.minX,p.bounds.minZ,p.bounds.maxX,p.bounds.maxZ,
      p.maximumBlades,p.coveragePermille,p.environment,p.seed);
}
inline void io(Archive& a,NereidBankSampleState& p){
    // Field-wise transfer: do not redefine ShoreErosionCell::io from
    // NereidShorelineSystems.h (native water modules may include both).
    a(p.vertex,p.soil.soil,p.soil.groundCm,p.soil.bedrockCm,
      p.soil.suspendedCm,p.soil.erosionCarryMilliCm,
      p.soil.depositionCarryMilliCm,p.soil.rootBindingPermille);
}

inline constexpr uint32_t kTerraWeaveSaveMagic=0x54575353u; // 'TWSS'
inline constexpr uint32_t kTerraWeaveSaveVersion=1;
inline constexpr size_t kTerraWeaveSaveMaxBytes=64u*1024u*1024u;
inline constexpr size_t kTerraWeaveRecipeIdMax=128;

namespace save_detail {

inline uint64_t digest(const char* bytes,size_t length){
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<length;++i){
        hash^=static_cast<uint8_t>(bytes[i]);
        hash*=1099511628211ull;
    }
    return hash;
}

// Archive.count is deliberately NOT used to allocate vector lengths before
// checking subsystem budgets. A malformed length cannot allocate huge arrays.
template<class T> void array(Archive& a,std::vector<T>& values,
                             uint32_t limit,size_t minBytesEach){
    uint32_t n=a.count(values.size(),minBytesEach);
    if(!a.ok() || n>limit){a.fail();return;}
    if(!a.writing())values.resize(n);
    for(auto& value:values){
        if(!a.ok())break;
        a(value);
    }
}

inline void recipe(Archive& a,std::string& value){
    uint32_t length=static_cast<uint32_t>(value.size());
    a(length);
    if(!a.ok() || length==0 || length>kTerraWeaveRecipeIdMax){
        a.fail();return;
    }
    if(!a.writing())value.assign(length,'\0');
    a.block(value.data(),length);
}

struct Payload {
    std::string recipeId;
    EarthworksSnapshot earth{};
    std::vector<AnchoredPlant> roots;
    std::vector<GrassPatch> grass;
    std::vector<NereidBankSampleState> banks;
    uint64_t lastWaterTick=0;
};

inline void transfer(Archive& a,Payload& v){
    recipe(a,v.recipeId);
    if(!a.ok())return;
    a(v.earth.spacingCm,v.earth.bedrockCm,v.earth.revision);
    array(a,v.earth.heights,1000000u,20);
    array(a,v.roots,1000000u,90);
    array(a,v.grass,GrassCoverRegistry::kMaxPatches,75);
    array(a,v.banks,NereidEarthworksBridge::kMaxSampleCount,40);
    a(v.lastWaterTick);
}

} // namespace save_detail

// Format: LE magic, version, recipe length+bytes, deterministic component
// streams in stable map order, lastWaterTick, FNV64 footer. Never save the
// transient GPU, Box3D or NavMesh object pointer/handle as game authority.
inline std::string saveTerraWeaveState(
        const EarthworksLayer& earth,
        const RootSupportRegistry& roots,
        const GrassCoverRegistry& grass,
        const NereidEarthworksBridge& water,
        std::string_view immutableRecipeId){
    if(immutableRecipeId.empty() ||
       immutableRecipeId.size()>kTerraWeaveRecipeIdMax)
        throw std::invalid_argument("TerraWeave save requires exact base recipe identity");
    save_detail::Payload payload;
    payload.recipeId=std::string(immutableRecipeId);
    payload.earth=earth.snapshot();
    payload.roots=roots.snapshot();
    payload.grass=grass.snapshot();
    payload.banks=water.snapshot();
    payload.lastWaterTick=water.lastTick();
    std::string bytes;
    Archive out(&bytes);
    uint32_t magic=kTerraWeaveSaveMagic,version=kTerraWeaveSaveVersion;
    out(magic,version);
    save_detail::transfer(out,payload);
    if(!out.ok() || bytes.size()>kTerraWeaveSaveMaxBytes-8u)
        throw std::length_error("TerraWeave authoritative save exceeds byte budget");
    uint64_t checksum=save_detail::digest(bytes.data(),bytes.size());
    out(checksum);
    return bytes;
}

// Validate the ENTIRE archive into staged registries. Only once EVERY
// record, recipe, budget and soil/erosion consistency check succeeds are
// the four maps swapped on a single authoritative owner-thread barrier.
// The caller must immediately invalidate/rebuild resident terrain, navmesh,
// grass GPU and near/far foliage after a successful restore.
inline void restoreTerraWeaveState(
        const std::string& bytes,std::string_view expectedRecipeId,
        EarthworksLayer& earth,RootSupportRegistry& roots,
        GrassCoverRegistry& grass,NereidEarthworksBridge& water){
    if(expectedRecipeId.empty() ||
       expectedRecipeId.size()>kTerraWeaveRecipeIdMax ||
       bytes.size()<32u || bytes.size()>kTerraWeaveSaveMaxBytes)
        throw std::invalid_argument("TerraWeave saved state size/recipe mismatch");

    Archive in(bytes);
    uint32_t magic=0,version=0;
    in(magic,version);
    if(!in.ok() || magic!=kTerraWeaveSaveMagic ||
       version!=kTerraWeaveSaveVersion)
        throw std::invalid_argument("TerraWeave unknown save version/magic");
    save_detail::Payload payload;
    save_detail::transfer(in,payload);
    uint64_t checksum=0;
    in(checksum);
    if(!in.ok() || in.remaining()!=0 ||
       checksum!=save_detail::digest(bytes.data(),bytes.size()-8u))
        throw std::invalid_argument("TerraWeave corrupt/truncated save");
    if(payload.recipeId!=expectedRecipeId)
        throw std::invalid_argument("TerraWeave procedural base recipe mismatch");

    // Mutate only private copies during validation. Each registry's
    // setSnapshot rejects duplicate identities, invalid bounds and budgets.
    EarthworksLayer stagedEarth=earth;
    stagedEarth.setSnapshot(payload.earth);
    RootSupportRegistry stagedRoots=roots;
    stagedRoots.setSnapshot(payload.roots);
    GrassCoverRegistry stagedGrass=grass;
    stagedGrass.setSnapshot(payload.grass);
    NereidEarthworksBridge stagedWater(stagedEarth);
    stagedWater.setSnapshot(payload.banks,payload.lastWaterTick);

    // exchangeSnapshotState is allocation-free for compatible instances.
    earth.exchangeSnapshotState(stagedEarth);
    roots.exchangeSnapshotState(stagedRoots);
    grass.exchangeSnapshotState(stagedGrass);
    water.exchangeSnapshotState(stagedWater);
}

} // namespace aiverse::terraweave
