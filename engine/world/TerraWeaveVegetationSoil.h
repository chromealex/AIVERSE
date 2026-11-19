// TerraWeave vegetation ecology and soil anchorage (pure integer CPU).
// Tree/shrub roots are optional presentation geometry; grass is processed as
// bounded coverage patches, not one entity or allocation per blade.
#pragma once
#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct PlantEnvironment {
    uint64_t ageTicks=0;           // native game simulation ticks
    uint64_t matureTicks=0;        // 0 => full grown, backward compatible
    uint64_t lifespanTicks=0;      // 0 => no aging senescence
    uint32_t lightPermille=1000;   // 0..1000
    uint32_t waterPermille=1000;
    uint32_t nutrientsPermille=1000;
    constexpr bool operator==(const PlantEnvironment&) const = default;
};

struct PlantAppearance {
    uint32_t sizePermille=1000;
    uint32_t foliagePermille=1000;
    uint32_t vigorPermille=1000;
    uint32_t agingPermille=0;
    constexpr bool operator==(const PlantAppearance&) const = default;
};

inline PlantAppearance plantAppearance(const PlantEnvironment& env) {
    if(env.lightPermille>1000 || env.waterPermille>1000 ||
       env.nutrientsPermille>1000 ||
       env.matureTicks>1000000000000ULL ||
       env.lifespanTicks>1000000000000ULL ||
       env.ageTicks>1000000000000ULL)
        throw std::invalid_argument("TerraWeave plant environment");
    const uint32_t maturity=env.matureTicks==0 ? 1000u :
        uint32_t(std::min<uint64_t>(1000,env.ageTicks*1000/env.matureTicks));
    const uint32_t vigor=std::min({env.lightPermille,env.waterPermille,
                                   env.nutrientsPermille});
    // At the end of life, fade foliage but leave structural silhouettes.
    uint32_t aging=0;
    if(env.lifespanTicks && env.ageTicks>env.lifespanTicks*3/4) {
        const uint64_t span=std::max<uint64_t>(1,env.lifespanTicks/4);
        aging=uint32_t(std::min<uint64_t>(
            1000,(env.ageTicks-env.lifespanTicks*3/4)*1000/span));
    }
    PlantAppearance out;
    out.vigorPermille=vigor;
    out.agingPermille=aging;
    out.sizePermille=uint32_t((uint64_t(250)+750u*maturity/1000u)*
                              (500u+500u*vigor/1000u)/1000u);
    out.foliagePermille=uint32_t(uint64_t(maturity)*vigor*
                                 (1000u-aging)/1000000u);
    return out;
}

struct RootProfile {
    PlantHabit habit=PlantHabit::Tree;
    uint32_t radiusCm=180;         // tree spread; shrub ~45cm; grass 0
    uint32_t depthCm=120;          // buried anchoring depth; shrub ~25cm
    uint32_t exposureThresholdCm=10; // do not allocate mesh before exposed
    uint32_t fallSupportPermille=280; // <= threshold => uprooted
    uint32_t segmentCount=6;       // 0 for grass; tree 6, shrub 3
    constexpr bool operator==(const RootProfile&) const = default;
};

inline RootProfile defaultRootProfile(PlantHabit habit) {
    switch(habit) {
        case PlantHabit::Tree:return {};
        case PlantHabit::Shrub:return {habit,45,25,5,220,3};
        case PlantHabit::Grass:return {habit,0,0,0,0,0};
    }
    throw std::invalid_argument("TerraWeave plant habit");
}

inline void validateRootProfile(const RootProfile& profile) {
    if(profile.habit>PlantHabit::Grass ||
       profile.radiusCm>10000 || profile.depthCm>10000 ||
       profile.exposureThresholdCm>10000 ||
       profile.fallSupportPermille>1000 ||
       profile.segmentCount>16 ||
       (profile.habit==PlantHabit::Grass &&
        (profile.radiusCm || profile.depthCm || profile.segmentCount)) ||
       (profile.habit!=PlantHabit::Grass &&
        (!profile.radiusCm || !profile.depthCm || !profile.segmentCount)))
        throw std::invalid_argument("TerraWeave invalid root profile");
}

struct RootAnchor {
    uint64_t plantKey=0;
    uint32_t speciesId=0;
    int64_t xCm=0,zCm=0;
    int32_t plantedHeightCm=0;  // historic ground level, not current
    RootProfile roots{};
};
struct RootState {
    uint64_t plantKey=0;
    uint32_t speciesId=0;
    PlantHabit habit=PlantHabit::Tree;
    int32_t plantedHeightCm=0;  // stable planting Y: do not sink tree into an eroded pit
    uint32_t exposedCm=0;        // max positive soil loss among sampled ring
    uint32_t supportPermille=1000;
    bool hasVisibleRoots=false;
    bool uprooted=false;
    int32_t fallDirectionXPermille=0;
    int32_t fallDirectionZPermille=0;
    constexpr bool operator==(const RootState&) const = default;
};

// Immutable terrain snapshot input. Missing samples are treated as *unknown*
// (safe standing/no new exposure), not an arbitrary abyss under the tree.
using SoilHeightSampler=std::function<std::optional<int32_t>(int64_t,int64_t)>;

inline RootState evaluateRootSupport(const RootAnchor& anchor,
                                    const SoilHeightSampler& ground) {
    validateRootProfile(anchor.roots);
    if(!ground || anchor.xCm < -kWorldLimitCm ||
       anchor.xCm > kWorldLimitCm ||
       anchor.zCm < -kWorldLimitCm ||
       anchor.zCm > kWorldLimitCm)
        throw std::invalid_argument("TerraWeave root sampler/coordinates");
    RootState result;
    result.plantKey=anchor.plantKey;
    result.speciesId=anchor.speciesId;
    result.habit=anchor.roots.habit;
    result.plantedHeightCm=anchor.plantedHeightCm;
    if(anchor.roots.habit==PlantHabit::Grass)return result;

    static constexpr std::array<std::array<int32_t,2>,8> directions{{
        {{1000,0}},{{707,707}},{{0,1000}},{{-707,707}},
        {{-1000,0}},{{-707,-707}},{{0,-1000}},{{707,-707}}
    }};
    int64_t totalLost=0,weightedX=0,weightedZ=0;
    uint32_t count=0,maxLost=0;
    const int64_t r=anchor.roots.radiusCm;
    for(const auto& dir:directions) {
        const int64_t dx=r*dir[0]/1000;
        const int64_t dz=r*dir[1]/1000;
        if(anchor.xCm+dx < -kWorldLimitCm ||
           anchor.xCm+dx > kWorldLimitCm ||
           anchor.zCm+dz < -kWorldLimitCm ||
           anchor.zCm+dz > kWorldLimitCm)continue;
        const auto sampled=ground(anchor.xCm+dx,anchor.zCm+dz);
        if(!sampled)continue;
        // A cliff may expose roots at generation time: the current ring can
        // already be lower than the original planted root crown height.
        const int64_t loss=std::max<int64_t>(
            0,int64_t(anchor.plantedHeightCm)-*sampled);
        const uint32_t limited=uint32_t(std::min<int64_t>(
            loss,anchor.roots.depthCm));
        maxLost=std::max(maxLost,limited);
        totalLost+=limited;
        weightedX+=limited*dir[0];
        weightedZ+=limited*dir[1];
        ++count;
    }
    // Require several known positions before concluding the tree fell.
    if(count<4)return result;
    result.exposedCm=maxLost;
    result.supportPermille=uint32_t(1000-
        (totalLost*1000)/(int64_t(anchor.roots.depthCm)*count));
    result.hasVisibleRoots=maxLost>=anchor.roots.exposureThresholdCm &&
        maxLost>0;
    result.uprooted=result.supportPermille<=anchor.roots.fallSupportPermille;
    // Bias a fall toward the eroded, downhill side; never use floating trig.
    if(totalLost) {
        result.fallDirectionXPermille=int32_t(weightedX/totalLost);
        result.fallDirectionZPermille=int32_t(weightedZ/totalLost);
    }
    return result;
}

// Geometry data is produced ONLY when a tree/shrub has exposed roots.
// Render as a bark mesh through Arbor; no grass roots are generated.
struct ExposedRootSegment {
    int32_t startXcm=0,startYcm=0,startZcm=0;
    int32_t endXcm=0,endYcm=0,endZcm=0;
    uint32_t radiusCm=1;
};
inline std::vector<ExposedRootSegment> generateExposedRoots(
        const RootAnchor& anchor,const RootState& state) {
    validateRootProfile(anchor.roots);
    if(state.plantKey!=anchor.plantKey ||
       state.speciesId!=anchor.speciesId ||
       state.habit!=anchor.roots.habit)
        throw std::invalid_argument("TerraWeave mismatched roots");
    if(!state.hasVisibleRoots || anchor.roots.habit==PlantHabit::Grass)
        return {};
    const uint32_t count=anchor.roots.segmentCount;
    std::vector<ExposedRootSegment> roots;
    roots.reserve(count);
    const uint32_t exposure=std::min(
        state.exposedCm,anchor.roots.depthCm);
    for(uint32_t i=0;i<count;++i) {
        const uint64_t h=detail::mix64(
            anchor.plantKey ^ (uint64_t(anchor.speciesId)<<32) ^
            (uint64_t(i)*0x9e3779b97f4a7c15ULL));
        int32_t x=int32_t(h&0xffff)-32768;
        int32_t z=int32_t((h>>16)&0xffff)-32768;
        const int32_t div=std::max({int32_t(1),std::abs(x),std::abs(z)});
        const int32_t spread=int32_t(anchor.roots.radiusCm)*
            (60+int32_t((h>>32)%41))/100;
        roots.push_back({0,-int32_t(exposure)/3,0,
            int32_t(int64_t(x)*spread/div),
            -int32_t(exposure),int32_t(int64_t(z)*spread/div),
            std::max(1u,anchor.roots.depthCm/20u)});
    }
    return roots;
}

// One grass patch represents many blades; its immutable key & environment
// determines all samples. Digging and erosion reduce coverage once per area,
// not one grass ECS component per individual plant.
struct GrassPatch {
    uint64_t key=0;
    RectCm bounds{};
    uint32_t maximumBlades=0;        // up to 262144 in this patch
    uint32_t coveragePermille=1000;  // 0..1000
    PlantEnvironment environment{};
    uint64_t seed=0;
};
struct GrassPatchDraw {
    uint64_t key=0;
    RectCm bounds{};
    uint32_t bladeCount=0;
    uint32_t meanLightPermille=1000;
    uint32_t meanAgePermille=1000;
    uint64_t seed=0;
};

inline GrassPatchDraw prepareGrassPatch(const GrassPatch& patch) {
    if(patch.maximumBlades>262144 || patch.coveragePermille>1000 ||
       patch.bounds.minX>=patch.bounds.maxX ||
       patch.bounds.minZ>=patch.bounds.maxZ ||
       patch.bounds.minX<-kWorldLimitCm ||
       patch.bounds.minZ<-kWorldLimitCm ||
       patch.bounds.maxX>kWorldLimitCm ||
       patch.bounds.maxZ>kWorldLimitCm ||
       patch.bounds.maxX-patch.bounds.minX>6400 ||
       patch.bounds.maxZ-patch.bounds.minZ>6400)
        throw std::invalid_argument("TerraWeave grass patch limits");
    const auto appearance=plantAppearance(patch.environment);
    const uint32_t count=uint32_t(
        uint64_t(patch.maximumBlades)*patch.coveragePermille*
        appearance.foliagePermille/1000000ull);
    return {patch.key,patch.bounds,count,
            patch.environment.lightPermille,
            appearance.sizePermille,patch.seed};
}

inline void erodeGrassCoverage(GrassPatch& patch,uint32_t disruptedPermille) {
    if(disruptedPermille>1000)
        throw std::invalid_argument("TerraWeave grass disturbance");
    patch.coveragePermille=uint32_t(
        uint64_t(patch.coveragePermille)*(1000u-disruptedPermille)/1000u);
}

// Sparse deterministic per-blade attributes generated on demand by GPU/CPU
// instance preparation. NOT retained as ECS state or a giant vector.
struct GrassBladeAttributes {
    int64_t xCm=0,zCm=0;
    uint32_t agePermille=0;
    uint32_t lightPermille=1000;
    uint32_t visualScalePermille=1000;
};
inline GrassBladeAttributes sampleGrassBlade(const GrassPatch& patch,
                                             uint32_t bladeIndex) {
    const auto draw=prepareGrassPatch(patch);
    if(bladeIndex>=draw.bladeCount)
        throw std::out_of_range("TerraWeave grass blade index");
    const uint64_t h=detail::mix64(patch.seed ^ patch.key ^
        (uint64_t(bladeIndex)*0x9e3779b97f4a7c15ULL));
    const int64_t width=patch.bounds.maxX-patch.bounds.minX;
    const int64_t height=patch.bounds.maxZ-patch.bounds.minZ;
    const uint32_t jitter=uint32_t((h>>20)%501);
    const uint32_t age=uint32_t((h>>40)%1001);
    return {patch.bounds.minX+int64_t(h%uint64_t(width)),
            patch.bounds.minZ+int64_t((h>>21)%uint64_t(height)),
            age,patch.environment.lightPermille,
            uint32_t(uint64_t(draw.meanAgePermille)*
                     (750u+jitter)/1000u)};
}

} // namespace aiverse::terraweave
