// TerraWeave meadow grass: species presets, clumps and macro fields.
//
// Pure CPU, render-agnostic and deterministic (integer hashing only). The
// render adapter (engine/render/TerraWeaveGrassRender.h) turns a placed blade
// into geometry. An empty GrassMeadow (no species) keeps the legacy grass
// generator byte-for-byte; projects opt in through JSON (see
// TerraWeaveGrassSpeciesJson.h) or the C++ presets below.
//
// Placement is anchored to WORLD centimetres (clump cells, field noise), not to
// the patch, so clumps and dry/lush areas continue across patch borders and
// do not repeat per patch. Every blade still comes from GrassPatch's bounded
// stratified sample: no per-blade entities, no persistent blade arrays.
#pragma once
#include "engine/world/TerraWeave.h"
#include "engine/world/TerraWeaveVegetationSoil.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::terraweave {

struct GrassRgb {
    uint8_t r=0,g=0,b=0;
    constexpr bool operator==(const GrassRgb&) const = default;
};

enum class GrassHead : uint8_t {
    None=0,    // leaf blades only
    Panicle=1, // loose, drooping seed cluster on a thin stem (bent/timothy/oat grass)
    Spike=2,   // compact upright seed spike (timothy, plantain)
    Flower=3,  // small open flower facing up (daisy, buttercup, clover)
};

// One grass species. All values are bounded integers so presets diff/hash
// cleanly and JSON authoring stays exact. Units in field names.
struct GrassSpecies {
    std::string name="grass";
    uint32_t weightPermille=1000;          // share in the mix (relative), 0..1000
    uint32_t heightCm=40;                  // mean leaf height 3..150
    uint32_t heightVariationPermille=350;  // +- relative, 0..800
    uint32_t halfWidthMm=5;                // blade half width 1..120 mm
    uint32_t widthVariationPermille=300;   // +- relative, 0..800
    uint32_t curvaturePermille=350;        // tip sweep as fraction of height, 0..1000
    uint32_t droopPermille=0;              // tip falls back down (long soft blades), 0..1000
    uint32_t leanPermille=180;             // lean away from the clump centre, 0..1000 (~0..50 deg)
    uint32_t twistPermille=0;              // twist along the blade, 0..1000 (= 0..180 deg)
    uint32_t taperPermille=500;            // 0 strap with blunt tip .. 1000 needle
    uint32_t shoulderPermille=200;         // where the blade reaches full width, 0..900
    uint32_t segments=3;                   // 1..4 ribbon segments (host cap: quality.bladeSegments)
    GrassRgb rootColor{46,62,22};
    GrassRgb midColor{92,128,44};
    GrassRgb tipColor{150,170,78};
    uint32_t colorJitterPermille=120;      // per blade brightness/hue jitter, 0..500
    uint32_t dryPermille=60;               // share of dry (straw) blades, 0..1000
    GrassRgb dryColor{176,152,92};
    GrassHead head=GrassHead::None;
    uint32_t headPermille=0;               // share of blades that are flowering stems, 0..1000
    uint32_t stemHeightPermille=1300;      // stem height relative to the leaf height, 500..2500
    uint32_t headLengthMm=60;              // 4..300
    uint32_t headHalfWidthMm=8;            // 1..80
    GrassRgb headColor{168,150,100};
    uint32_t fieldAffinityPermille=500;    // where in the macro field (0..1000) it dominates
    uint32_t fieldTolerancePermille=1000;  // 1000 = everywhere; smaller = patchier species
    uint32_t flexPermille=1000;            // wind response scale for hosts with sway, 100..2000
    bool operator==(const GrassSpecies&) const = default;
};

struct GrassClumpSettings {
    uint32_t spacingCm=60;                 // jittered-grid clump cell 10..600 (blue-noise-like)
    uint32_t radiusCm=20;                  // mean clump radius 2..200
    uint32_t radiusVariationPermille=450;  // 0..900
    uint32_t clumpedPermille=820;          // share of blades in clumps; rest scattered fill, 0..1000
    uint32_t gapPermille=200;              // share of empty clump cells (bare gaps), 0..950
    uint32_t mixPermille=120;              // blades of another species inside a clump, 0..1000
    uint32_t centreDensityPermille=600;    // 0 even disc .. 1000 dense centre / sparse rim
    uint32_t fillHeightPermille=620;       // scattered fill blade height factor, 200..1000
    bool operator==(const GrassClumpSettings&) const = default;
};

struct GrassFieldSettings {
    uint32_t periodCm=2200;                // macro noise period 200..50000
    uint32_t densityContrastPermille=380;  // thin/lush areas, 0..1000
    uint32_t heightContrastPermille=250;   // short/tall areas, 0..800
    uint32_t dryPatchPermille=260;         // dry/yellow areas, 0..1000
    bool operator==(const GrassFieldSettings&) const = default;
};

struct GrassMeadow {
    std::vector<GrassSpecies> species;     // empty: legacy uniform grass
    GrassClumpSettings clumps{};
    GrassFieldSettings field{};
    uint64_t worldSeed=0x4d454144ull;      // world-anchored clumps and fields
    bool enabled() const {return !species.empty();}
    bool operator==(const GrassMeadow&) const = default;
};

inline constexpr size_t kMaxGrassSpecies=8;

inline void validateGrassMeadow(const GrassMeadow& m) {
    if(m.species.empty())return;
    if(m.species.size()>kMaxGrassSpecies)
        throw std::invalid_argument("TerraWeave grass meadow: at most 8 species");
    uint32_t total=0;
    for(const auto& s:m.species) {
        const auto bad=[&](const char* field){
            throw std::invalid_argument("TerraWeave grass species '"+s.name+"': "+field+" out of range");
        };
        if(s.weightPermille>1000)bad("weightPermille");
        if(s.heightCm<3||s.heightCm>150)bad("heightCm");
        if(s.heightVariationPermille>800)bad("heightVariationPermille");
        if(s.halfWidthMm<1||s.halfWidthMm>120)bad("halfWidthMm");
        if(s.widthVariationPermille>800)bad("widthVariationPermille");
        if(s.curvaturePermille>1000)bad("curvaturePermille");
        if(s.droopPermille>1000)bad("droopPermille");
        if(s.leanPermille>1000)bad("leanPermille");
        if(s.twistPermille>1000)bad("twistPermille");
        if(s.taperPermille>1000)bad("taperPermille");
        if(s.shoulderPermille>900)bad("shoulderPermille");
        if(s.segments<1||s.segments>4)bad("segments");
        if(s.colorJitterPermille>500)bad("colorJitterPermille");
        if(s.dryPermille>1000)bad("dryPermille");
        if(uint8_t(s.head)>3)bad("head");
        if(s.headPermille>1000)bad("headPermille");
        if(s.stemHeightPermille<500||s.stemHeightPermille>2500)bad("stemHeightPermille");
        if(s.headLengthMm<4||s.headLengthMm>300)bad("headLengthMm");
        if(s.headHalfWidthMm<1||s.headHalfWidthMm>80)bad("headHalfWidthMm");
        if(s.fieldAffinityPermille>1000)bad("fieldAffinityPermille");
        if(s.fieldTolerancePermille<50||s.fieldTolerancePermille>1000)bad("fieldTolerancePermille");
        if(s.flexPermille<100||s.flexPermille>2000)bad("flexPermille");
        total+=s.weightPermille;
    }
    if(!total)throw std::invalid_argument("TerraWeave grass meadow: all species weights are 0");
    const auto& c=m.clumps;
    if(c.spacingCm<10||c.spacingCm>600||c.radiusCm<2||c.radiusCm>200||
       c.radiusVariationPermille>900||c.clumpedPermille>1000||c.gapPermille>950||
       c.mixPermille>1000||c.centreDensityPermille>1000||
       c.fillHeightPermille<200||c.fillHeightPermille>1000)
        throw std::invalid_argument("TerraWeave grass meadow: clumps out of range");
    const auto& f=m.field;
    if(f.periodCm<200||f.periodCm>50000||f.densityContrastPermille>1000||
       f.heightContrastPermille>800||f.dryPatchPermille>1000)
        throw std::invalid_argument("TerraWeave grass meadow: field out of range");
}

// ---------------------------------------------------------------- presets

// Species presets. Colours are linear-ish sRGB vertex colours multiplied by a
// neutral white material, so they read directly as the blade colour.
inline std::optional<GrassSpecies> grassSpeciesPreset(std::string_view name) {
    GrassSpecies s;
    s.name=std::string(name);
    if(name=="meadowFine") {          // fescue/bent: many thin soft blades
        s.heightCm=38;s.heightVariationPermille=380;s.halfWidthMm=3;s.widthVariationPermille=350;
        s.curvaturePermille=420;s.droopPermille=120;s.leanPermille=220;s.twistPermille=120;
        s.taperPermille=720;s.shoulderPermille=120;s.segments=3;
        s.rootColor={40,58,20};s.midColor={86,124,40};s.tipColor={150,166,74};
        s.dryPermille=70;s.dryColor={182,160,98};s.fieldAffinityPermille=450;s.flexPermille=1150;
    } else if(name=="broadBlade") {   // cocksfoot/ryegrass: wider, stiffer, deeper green
        s.heightCm=46;s.heightVariationPermille=320;s.halfWidthMm=8;s.widthVariationPermille=300;
        s.curvaturePermille=520;s.droopPermille=260;s.leanPermille=260;s.twistPermille=260;
        s.taperPermille=380;s.shoulderPermille=260;s.segments=4;
        s.rootColor={34,56,18};s.midColor={70,118,36};s.tipColor={118,150,58};
        s.dryPermille=40;s.dryColor={170,150,90};s.fieldAffinityPermille=300;s.fieldTolerancePermille=700;
        s.flexPermille=850;
    } else if(name=="tallDry") {      // tall seeding grass with panicles, straw tips
        s.heightCm=72;s.heightVariationPermille=420;s.halfWidthMm=4;s.widthVariationPermille=300;
        s.curvaturePermille=300;s.droopPermille=180;s.leanPermille=160;s.twistPermille=180;
        s.taperPermille=640;s.shoulderPermille=150;s.segments=4;
        s.rootColor={60,70,26};s.midColor={128,138,60};s.tipColor={196,180,110};
        s.dryPermille=260;s.dryColor={198,176,116};
        s.head=GrassHead::Panicle;s.headPermille=260;s.stemHeightPermille=1350;
        s.headLengthMm=110;s.headHalfWidthMm=14;s.headColor={190,170,118};
        s.fieldAffinityPermille=780;s.fieldTolerancePermille=650;s.flexPermille=1300;
    } else if(name=="timothy") {      // upright stems with cylindrical spikes
        s.heightCm=55;s.heightVariationPermille=350;s.halfWidthMm=4;
        s.curvaturePermille=260;s.droopPermille=60;s.leanPermille=120;s.twistPermille=100;
        s.taperPermille=560;s.shoulderPermille=160;s.segments=3;
        s.rootColor={42,60,22};s.midColor={90,126,44};s.tipColor={146,160,72};
        s.dryPermille=90;s.head=GrassHead::Spike;s.headPermille=200;s.stemHeightPermille=1500;
        s.headLengthMm=70;s.headHalfWidthMm=5;s.headColor={150,150,104};
        s.fieldAffinityPermille=620;s.fieldTolerancePermille=800;s.flexPermille=1000;
    } else if(name=="lawn") {         // short dense mown grass
        s.heightCm=9;s.heightVariationPermille=250;s.halfWidthMm=3;s.widthVariationPermille=250;
        s.curvaturePermille=200;s.droopPermille=0;s.leanPermille=150;s.twistPermille=80;
        s.taperPermille=520;s.shoulderPermille=120;s.segments=2;
        s.rootColor={36,58,20};s.midColor={74,122,38};s.tipColor={112,156,58};
        s.dryPermille=20;s.fieldAffinityPermille=500;s.flexPermille=500;
    } else if(name=="clover") {       // low broad leaves with white flower heads
        s.heightCm=12;s.heightVariationPermille=300;s.halfWidthMm=9;s.widthVariationPermille=250;
        s.curvaturePermille=550;s.droopPermille=350;s.leanPermille=450;s.twistPermille=300;
        s.taperPermille=150;s.shoulderPermille=400;s.segments=2;
        s.rootColor={32,58,22};s.midColor={58,104,40};s.tipColor={78,124,52};
        s.dryPermille=10;s.head=GrassHead::Flower;s.headPermille=140;s.stemHeightPermille=1500;
        s.headLengthMm=16;s.headHalfWidthMm=11;s.headColor={236,232,214};
        s.fieldAffinityPermille=250;s.fieldTolerancePermille=500;s.flexPermille=600;
    } else if(name=="wildflower") {   // yellow buttercup-like flowers above the grass
        s.heightCm=34;s.heightVariationPermille=400;s.halfWidthMm=2;s.widthVariationPermille=200;
        s.curvaturePermille=200;s.droopPermille=50;s.leanPermille=150;s.twistPermille=60;
        s.taperPermille=600;s.shoulderPermille=100;s.segments=3;
        s.rootColor={42,64,24};s.midColor={80,120,44};s.tipColor={110,146,58};
        s.dryPermille=0;s.head=GrassHead::Flower;s.headPermille=900;s.stemHeightPermille=1150;
        s.headLengthMm=14;s.headHalfWidthMm=13;s.headColor={238,200,40};
        s.fieldAffinityPermille=350;s.fieldTolerancePermille=450;s.flexPermille=1100;
    } else if(name=="sedge") {        // stiff, wide, dark wet-ground blades
        s.heightCm=52;s.heightVariationPermille=300;s.halfWidthMm=6;s.widthVariationPermille=200;
        s.curvaturePermille=220;s.droopPermille=120;s.leanPermille=300;s.twistPermille=200;
        s.taperPermille=800;s.shoulderPermille=200;s.segments=3;
        s.rootColor={30,48,22};s.midColor={58,92,40};s.tipColor={104,124,60};
        s.dryPermille=120;s.dryColor={150,136,86};s.fieldAffinityPermille=150;s.fieldTolerancePermille=500;
        s.flexPermille=700;
    } else return std::nullopt;
    return s;
}

inline std::vector<std::string_view> grassSpeciesPresetNames() {
    return {"meadowFine","broadBlade","tallDry","timothy","lawn","clover","wildflower","sedge"};
}

// Whole-meadow presets: species mix + clumps + field.
inline std::optional<GrassMeadow> grassMeadowPreset(std::string_view name) {
    GrassMeadow m;
    const auto add=[&](std::string_view preset,uint32_t weight){
        auto s=*grassSpeciesPreset(preset);s.weightPermille=weight;m.species.push_back(std::move(s));
    };
    if(name=="legacy") return m; // empty = legacy uniform generator
    if(name=="meadow") {
        add("meadowFine",520);add("broadBlade",220);add("tallDry",140);add("timothy",70);
        add("wildflower",30);add("clover",20);
    } else if(name=="lushMeadow") {
        add("broadBlade",420);add("meadowFine",380);add("timothy",80);add("clover",80);add("wildflower",40);
        m.field.dryPatchPermille=120;m.clumps.gapPermille=120;m.clumps.clumpedPermille=760;
    } else if(name=="dryMeadow") {
        add("tallDry",450);add("meadowFine",380);add("timothy",120);add("wildflower",50);
        m.field.dryPatchPermille=520;m.clumps.gapPermille=300;m.clumps.radiusCm=24;
    } else if(name=="pasture") {     // grazed: shorter, more clover, small tufts
        add("meadowFine",520);add("lawn",200);add("clover",180);add("broadBlade",100);
        m.species[0].heightCm=22;m.species[3].heightCm=26;
        m.clumps.spacingCm=40;m.clumps.radiusCm=14;m.clumps.gapPermille=140;
    } else if(name=="lawn") {
        add("lawn",900);add("clover",100);
        m.clumps.spacingCm=24;m.clumps.radiusCm=12;m.clumps.gapPermille=20;m.clumps.clumpedPermille=450;
        m.field.densityContrastPermille=150;m.field.dryPatchPermille=90;m.field.heightContrastPermille=120;
    } else if(name=="steppe") {      // sparse dry bunch grass
        add("tallDry",600);add("meadowFine",300);add("sedge",100);
        m.species[1].dryPermille=300;
        m.clumps.spacingCm=70;m.clumps.radiusCm=16;m.clumps.gapPermille=420;m.clumps.clumpedPermille=930;
        m.clumps.centreDensityPermille=800;m.field.dryPatchPermille=700;m.field.densityContrastPermille=550;
    } else if(name=="wetland") {
        add("sedge",600);add("broadBlade",250);add("meadowFine",150);
        m.field.dryPatchPermille=80;m.clumps.radiusCm=26;m.clumps.gapPermille=150;
    } else return std::nullopt;
    return m;
}

inline std::vector<std::string_view> grassMeadowPresetNames() {
    return {"legacy","meadow","lushMeadow","dryMeadow","pasture","lawn","steppe","wetland"};
}

// ---------------------------------------------------------------- fields

namespace grassdetail {
inline uint64_t cellHash(uint64_t seed,int64_t x,int64_t z,uint64_t salt) {
    return detail::mix64(seed^salt^(uint64_t(x)*0x9e3779b97f4a7c15ull)^
                         detail::mix64(uint64_t(z)+0x632be59bd9b4e019ull));
}
inline int64_t floorDivCm(int64_t a,int64_t b) {
    const int64_t q=a/b;return (a%b!=0 && ((a<0)!=(b<0)))?q-1:q;
}
// 0..1000 smooth value noise, integer only (deterministic on every platform).
inline uint32_t valueNoise(uint64_t seed,int64_t x,int64_t z,int64_t period,uint64_t salt) {
    const int64_t ix=floorDivCm(x,period),iz=floorDivCm(z,period);
    const int64_t fx=(x-ix*period)*1024/period,fz=(z-iz*period)*1024/period; // 0..1023
    const auto corner=[&](int64_t a,int64_t b){return int64_t(cellHash(seed,a,b,salt)%1001u);};
    // smoothstep weights in 1/1024 units
    const int64_t sx=fx*fx*(3*1024-2*fx)/(1024*1024),sz=fz*fz*(3*1024-2*fz)/(1024*1024);
    const int64_t a=corner(ix,iz)*(1024-sx)+corner(ix+1,iz)*sx;
    const int64_t b=corner(ix,iz+1)*(1024-sx)+corner(ix+1,iz+1)*sx;
    return uint32_t((a*(1024-sz)+b*sz)/(1024*1024));
}
}

struct GrassFieldSample {
    uint32_t speciesField=500;  // 0..1000 picks which species dominate
    uint32_t density=1000;      // 0..1000 local density multiplier
    uint32_t height=1000;       // permille height multiplier
    uint32_t dry=0;             // 0..1000 dryness tint
};

// Two octaves of world-anchored value noise.
inline GrassFieldSample sampleGrassField(const GrassMeadow& m,int64_t x,int64_t z) {
    const int64_t p=m.field.periodCm;
    const auto octave=[&](uint64_t salt){
        return (grassdetail::valueNoise(m.worldSeed,x,z,p,salt)*2+
                grassdetail::valueNoise(m.worldSeed,x,z,std::max<int64_t>(100,p*2/5),salt^0x55))/3;
    };
    GrassFieldSample s;
    s.speciesField=octave(0x5bd1e995ull);
    const uint32_t d=octave(0x27d4eb2full);
    s.density=1000-m.field.densityContrastPermille*(1000-std::min<uint32_t>(1000,d*14/10))/1000;
    const uint32_t hgt=octave(0x165667b1ull);
    s.height=1000-m.field.heightContrastPermille/2+m.field.heightContrastPermille*hgt/1000;
    const uint32_t dry=octave(0x85ebca6bull);
    // Only the upper part of the noise becomes dry so patches stay discrete.
    s.dry=dry>520?std::min<uint32_t>(1000,(dry-520)*1000/300)*m.field.dryPatchPermille/1000:0;
    return s;
}

inline uint32_t pickGrassSpecies(const GrassMeadow& m,uint32_t field,uint64_t roll) {
    std::array<uint32_t,kMaxGrassSpecies> w{};
    uint64_t total=0;
    for(size_t i=0;i<m.species.size();++i){
        const auto& s=m.species[i];
        const uint32_t distance=field>s.fieldAffinityPermille?field-s.fieldAffinityPermille:s.fieldAffinityPermille-field;
        const uint32_t affinity=distance>=s.fieldTolerancePermille?40u:
            1000u-uint32_t(uint64_t(distance)*960u/s.fieldTolerancePermille);
        w[i]=uint32_t(uint64_t(s.weightPermille)*affinity/1000u);
        total+=w[i];
    }
    if(!total)return 0;
    uint64_t r=roll%total;
    for(size_t i=0;i<m.species.size();++i){if(r<w[i])return uint32_t(i);r-=w[i];}
    return uint32_t(m.species.size()-1);
}

struct GrassClump {
    bool exists=false;
    int64_t xCm=0,zCm=0;
    uint32_t radiusCm=0;
    uint32_t species=0;
    uint32_t vigorPermille=1000;  // per-clump height scale
    uint64_t hash=0;
};

// Clump of the jittered grid cell (cx,cz). World-anchored, patch independent.
// Centre only (cheap): jitter inside the inner 80% of the cell keeps a
// minimum spacing between neighbouring tufts (blue-noise-like).
inline GrassClump grassClumpCentre(const GrassMeadow& m,int64_t cx,int64_t cz) {
    GrassClump out;
    const uint64_t h=grassdetail::cellHash(m.worldSeed,cx,cz,0x636c756dull);
    out.hash=h;
    const int64_t s=m.clumps.spacingCm;
    out.xCm=cx*s+s/10+int64_t((h>>8)%uint64_t(std::max<int64_t>(1,s*8/10)));
    out.zCm=cz*s+s/10+int64_t((h>>24)%uint64_t(std::max<int64_t>(1,s*8/10)));
    return out;
}

inline GrassClump grassClumpAt(const GrassMeadow& m,int64_t cx,int64_t cz) {
    const auto& c=m.clumps;
    GrassClump out=grassClumpCentre(m,cx,cz);
    const uint64_t h=out.hash;
    const auto field=sampleGrassField(m,out.xCm,out.zCm);
    const uint32_t keep=(1000-c.gapPermille)*field.density/1000;
    out.exists=(h>>40)%1000u<keep;
    const uint32_t rv=c.radiusVariationPermille;
    out.radiusCm=std::max<uint32_t>(2,c.radiusCm*(1000-rv+uint32_t((h>>50)%(2*rv+1)))/1000);
    out.species=pickGrassSpecies(m,field.speciesField,detail::mix64(h^0x73706563ull));
    out.vigorPermille=780+uint32_t((h>>17)%421u);
    return out;
}

struct GrassBladeSite {
    int64_t xCm=0,zCm=0;
    uint32_t species=0;
    bool clumped=false;
    uint32_t radialPermille=0;     // 0 clump centre .. 1000 clump rim (1000 for fill)
    float outwardX=0,outwardZ=1;   // lean direction (unit), presentation only
    uint32_t heightPermille=1000;  // clump vigor x field height x fill factor
    uint32_t dryPermille=0;        // macro dryness 0..1000
    uint64_t clumpHash=0;
};

// Place one blade from a stable 64-bit blade hash. Returns nothing for a blade
// that falls into a gap or outside the patch (neighbour patches own it).
inline std::optional<GrassBladeSite> placeMeadowBlade(const GrassMeadow& m,const RectCm& bounds,uint64_t h) {
    const auto& c=m.clumps;
    const int64_t spanX=bounds.maxX-bounds.minX,spanZ=bounds.maxZ-bounds.minZ;
    if(spanX<=0||spanZ<=0)return std::nullopt;
    const int64_t ux=bounds.minX+int64_t((h>>13)%uint64_t(spanX));
    const int64_t uz=bounds.minZ+int64_t((h>>35)%uint64_t(spanZ));
    GrassBladeSite site;
    const uint64_t h2=detail::mix64(h^0x626c6164ull);
    if(h2%1000u<c.clumpedPermille) {
        // Nearest existing clump centre among the 3x3 neighbouring cells.
        const int64_t s=c.spacingCm;
        const int64_t cx=grassdetail::floorDivCm(ux,s),cz=grassdetail::floorDivCm(uz,s);
        int64_t bestX=cx,bestZ=cz,bestD=INT64_MAX;
        for(int64_t dz=-1;dz<=1;++dz)for(int64_t dx=-1;dx<=1;++dx){
            const auto clump=grassClumpCentre(m,cx+dx,cz+dz);
            const int64_t ex=clump.xCm-ux,ez=clump.zCm-uz,d=ex*ex+ez*ez;
            if(d<bestD){bestD=d;bestX=cx+dx;bestZ=cz+dz;}
        }
        const GrassClump best=grassClumpAt(m,bestX,bestZ);
        if(!best.exists)return std::nullopt; // bare gap between tufts
        // Polar offset: r = R * v^p; p>0.5 concentrates blades in the centre.
        const uint32_t v=uint32_t((h2>>10)%1001u);
        const float p=.5f+1.1f*float(c.centreDensityPermille)/1000.f;
        float r01=std::pow(float(v)/1000.f,p);
        const float angle=float((h2>>24)%62832u)*.0001f;
        const float r=r01*float(best.radiusCm);
        // integer rounding keeps placement reproducible for the same inputs
        site.xCm=best.xCm+int64_t(std::lround(std::cos(angle)*r));
        site.zCm=best.zCm+int64_t(std::lround(std::sin(angle)*r));
        site.clumped=true;
        site.radialPermille=uint32_t(std::lround(r01*1000.f));
        site.outwardX=std::cos(angle);site.outwardZ=std::sin(angle);
        site.clumpHash=best.hash;
        site.species=best.species;
        if((h2>>44)%1000u<c.mixPermille) {
            const auto f=sampleGrassField(m,site.xCm,site.zCm);
            site.species=pickGrassSpecies(m,f.speciesField,detail::mix64(h2^0x6d6978ull));
        }
        // Centre blades are tallest; rim blades shorter (dome-shaped tuft).
        site.heightPermille=best.vigorPermille*(1000-site.radialPermille*28/100)/1000;
    } else {
        // Scattered low fill between tufts, thinned by the density field.
        site.xCm=ux;site.zCm=uz;
        const auto f=sampleGrassField(m,ux,uz);
        if((h2>>20)%1000u>=f.density*(1000-c.gapPermille/2)/1000)return std::nullopt;
        site.species=pickGrassSpecies(m,f.speciesField,detail::mix64(h2^0x66696cull));
        site.radialPermille=1000;
        const float angle=float((h2>>24)%62832u)*.0001f;
        site.outwardX=std::cos(angle);site.outwardZ=std::sin(angle);
        site.heightPermille=c.fillHeightPermille;
        site.clumpHash=h2;
    }
    if(site.xCm<bounds.minX||site.xCm>=bounds.maxX||site.zCm<bounds.minZ||site.zCm>=bounds.maxZ)
        return std::nullopt;
    const auto f=sampleGrassField(m,site.xCm,site.zCm);
    site.heightPermille=site.heightPermille*f.height/1000;
    site.dryPermille=f.dry;
    return site;
}

} // namespace aiverse::terraweave
