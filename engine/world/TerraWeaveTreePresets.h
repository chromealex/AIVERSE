// Botanical authoring recipes over the SAME deterministic Arbor generator.
// These are morphology/material choices, not ten independent tree solvers.
#pragma once
#include "engine/world/TerraWeaveArbor.h"
#include "engine/world/TerraWeaveVegetationSoil.h"
#include "engine/world/TerraWeavePhenology.h"
#include "engine/world/TerraWeaveRocks.h"
#include <array>
#include <string_view>

namespace aiverse::terraweave {
struct TreePreset {
    std::string_view name;
    TreeProfile profile;
    std::string_view barkMaterial, leafMaterial;
};

// Palette index only: reuse the global four-color scatter lattice, with no
// mutable neighborhood cache or second placement generator. Eight neighboring
// cells differ in species (>=4 kinds) or seed variant (single-kind banks).
inline uint32_t treePrototypeVariant(int64_t cellX,int64_t cellZ,uint64_t seed,
        uint32_t speciesCount,uint32_t variantsPerSpecies) {
    if(speciesCount<1||speciesCount>16||variantsPerSpecies<1||variantsPerSpecies>8||
       (speciesCount<4&&variantsPerSpecies<4))
        throw std::invalid_argument("tree palette needs four species or four variants");
    const uint32_t color=rockVariant(cellX,cellZ,seed,4);
    const uint64_t hash=detail::hash2(cellX,cellZ,seed^0xd1b54a32d192ed03ULL);
    const uint32_t species=speciesCount>=4?
        color+4*uint32_t(hash%((speciesCount-1-color)/4+1)):
        uint32_t(hash%speciesCount);
    const uint32_t variant=speciesCount>=4?uint32_t((hash>>32)%variantsPerSpecies):
        color+4*uint32_t((hash>>32)%((variantsPerSpecies-1-color)/4+1));
    return species*variantsPerSpecies+variant;
}

inline const std::array<TreePreset, 10>& botanicalTreePresets() {
    static const auto values=[] {
        std::array<TreePreset,10> out{};
        const auto add=[&](size_t slot,std::string_view name,CrownShape shape,
                int lo,int hi,int radius,uint32_t base,uint32_t limbs,
                uint32_t reach,uint32_t rise,uint32_t leaf,bool evergreen,
                std::string_view bark,std::string_view foliage,uint32_t whorl=0) {
            TreeProfile p;
            p.speciesId=uint32_t(slot+1);p.shape=shape;
            p.minHeightCm=lo;p.maxHeightCm=hi;p.trunkRadiusCm=radius;
            p.crownStartPermille=base;p.primaryBranches=limbs;
            p.branchLengthPermille=reach;p.branchRisePermille=rise;
            p.leafRadiusCm=leaf;p.whorlSize=whorl;
            p.canopyHabit=evergreen?CanopyHabit::Evergreen:CanopyHabit::Deciduous;
            out[slot]={name,p,bark,foliage};
        };
        add(0,"spruce",CrownShape::Conifer,1100,2100,30,90,95,470,35,45,true,"spruce_bark","spruce_leaves",5);
        add(1,"pine",CrownShape::Conifer,1400,2400,34,480,60,690,100,50,true,"pine_bark","pine_leaves",3);
        out[1].profile.roundedConiferCrown=true;
        add(2,"fir",CrownShape::Conifer,1400,2600,35,70,110,390,90,40,true,"spruce_bark","spruce_leaves",6);
        add(3,"larch",CrownShape::Conifer,1200,2200,26,220,75,430,50,38,false,"pine_bark","pine_leaves",4);
        add(4,"oak",CrownShape::Broadleaf,900,1700,48,220,32,650,390,125,false,"oak_bark","oak_leaves");
        add(5,"birch",CrownShape::Broadleaf,1000,1900,18,250,38,350,270,65,false,"birch_bark","birch_leaves");
        add(6,"maple",CrownShape::Broadleaf,800,1600,32,220,38,550,530,105,false,"oak_bark","maple_leaves");
        add(7,"apple",CrownShape::Broadleaf,320,650,24,180,24,800,310,65,false,"oak_bark","apple_leaves");
        add(8,"willow",CrownShape::Broadleaf,700,1400,38,220,44,750,500,120,false,"oak_bark","willow_leaves");
        add(9,"poplar",CrownShape::Columnar,1600,2800,28,180,48,320,1150,70,false,"birch_bark","shrub_leaves");
        out[8].profile.branchDroopPermille=510;
        for(size_t i=4;i<out.size();++i) {
            out[i].profile.primaryBranches=i==5?24u:i==9?26u:i==7?16u:22u;
            out[i].profile.secondaryBranches=3;
            out[i].profile.tertiaryBranches=2;
            out[i].profile.roundedBroadleafCrown=true;
            out[i].profile.leafRadiusCm=std::min(out[i].profile.leafRadiusCm,55u);
        }
        return out;
    }();
    return values;
}

// Offline/presentation recipe only. Live ForestScene already applies this
// ecology policy at instance submission: do not apply the size twice there.
inline TreeProfile treeProfileAtAge(const TreePreset& preset,const PlantEnvironment& environment) {
    const auto appearance=plantAppearance(environment);
    TreeProfile profile=preset.profile;
    profile.minHeightCm=std::max<int32_t>(100,int32_t(int64_t(profile.minHeightCm)*appearance.sizePermille/1000));
    profile.maxHeightCm=std::max(profile.minHeightCm,int32_t(int64_t(profile.maxHeightCm)*appearance.sizePermille/1000));
    profile.trunkRadiusCm=std::max<int32_t>(1,int32_t(int64_t(profile.trunkRadiusCm)*appearance.sizePermille/1000));
    profile.leafRadiusCm=std::max<uint32_t>(8,uint32_t(uint64_t(profile.leafRadiusCm)*appearance.sizePermille/1000));
    return profile;
}

inline uint32_t treeCanopyAtAge(const TreePreset& preset,const PlantEnvironment& environment,
                               const CanopyPhenology& season) {
    validateCanopyPhenology(season);
    return uint32_t(uint64_t(plantAppearance(environment).foliagePermille)*
                    canopyCoverPermille(season,treeIsEvergreen(preset.profile))/1000);
}
} // namespace aiverse::terraweave
