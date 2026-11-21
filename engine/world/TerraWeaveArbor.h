// TerraWeave Arbor: seeded, genre-independent procedural tree skeletons.
// The integer skeleton is authoritative for replayable world generation; GPU
// tessellation, leaf materials and impostor atlas baking are separate stages.
#pragma once

#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

enum class TreeLOD : uint8_t { High, Medium, Low, Impostor };
enum class CrownShape : uint8_t { Broadleaf, Conifer, Columnar };
enum class CanopyHabit : uint8_t { Automatic, Evergreen, Deciduous };

struct TreeProfile {
    uint32_t speciesId = 1;
    int32_t minHeightCm = 800;
    int32_t maxHeightCm = 1500;
    int32_t trunkRadiusCm = 24;
    uint32_t crownStartPermille = 280;
    uint32_t primaryBranches = 22;
    uint32_t branchLengthPermille = 380;
    uint32_t branchRisePermille = 220;
    uint32_t leafRadiusCm = 95;
    CrownShape shape = CrownShape::Broadleaf;
    uint32_t whorlSize = 0; // opt-in conifers: 2..8 limbs per irregular whorl
    bool roundedConiferCrown = false; // pine-like upper crown, not a cone
    uint32_t secondaryBranches = 0; // opt-in child twigs per primary,0..4
    uint32_t branchDroopPermille = 0; // opt-in weeping canopy,0..2000
    bool roundedBroadleafCrown = false; // opt-in elliptical, not conifer taper
    uint32_t tertiaryBranches = 0; // optional branching hierarchy,0..3
    CanopyHabit canopyHabit = CanopyHabit::Automatic; // larch is a deciduous conifer

};

inline bool treeIsEvergreen(const TreeProfile& p) {
    return p.canopyHabit==CanopyHabit::Evergreen ||
        (p.canopyHabit==CanopyHabit::Automatic&&p.shape==CrownShape::Conifer);
}

struct TreeBranch {
    int32_t x0 = 0, y0 = 0, z0 = 0;
    int32_t x1 = 0, y1 = 0, z1 = 0;
    int32_t startRadiusCm = 0, endRadiusCm = 0;
    int32_t parent = -1; // index of parent branch; trunk has parent=-1
    constexpr bool operator==(const TreeBranch&) const = default;
};

struct LeafCluster {
    int32_t xCm = 0, yCm = 0, zCm = 0;
    uint32_t radiusCm = 0, variation = 0;
    int32_t parentBranch = -1; // structural owner, independent per-branch LOD
    constexpr bool operator==(const LeafCluster&) const = default;
};

struct TreeSkeleton {
    uint32_t speciesId = 0;
    uint64_t seed = 0;
    int32_t heightCm = 0;
    int32_t crownRadiusCm = 0;
    std::vector<TreeBranch> branches;
    std::vector<LeafCluster> leaves;
};

inline TreeSkeleton generateTree(const TreeProfile& p, uint64_t instanceSeed,
                                 TreeLOD lod = TreeLOD::High) {
    if (p.minHeightCm < 100 || p.maxHeightCm < p.minHeightCm ||
        p.maxHeightCm > 100000 || p.trunkRadiusCm <= 0 ||
        p.trunkRadiusCm > p.minHeightCm / 2 ||
        p.primaryBranches > 256 || p.crownStartPermille >= 1000 ||
        p.branchLengthPermille > 2000 || p.branchRisePermille > 2000 ||
        p.leafRadiusCm > 100000 || p.secondaryBranches>4 || p.tertiaryBranches>3 || p.branchDroopPermille>2000 ||
        uint8_t(p.canopyHabit)>uint8_t(CanopyHabit::Deciduous) ||
        (p.whorlSize && (p.whorlSize<2 || p.whorlSize>8 || p.shape!=CrownShape::Conifer)) ||
        (p.roundedConiferCrown && p.shape!=CrownShape::Conifer) ||
        (p.shape != CrownShape::Broadleaf &&
         p.shape != CrownShape::Conifer && p.shape != CrownShape::Columnar))
        throw std::invalid_argument("TerraWeave Arbor profile");

    // Variation is derived only from species id and stable world instance key.
    const uint64_t seed = detail::mix64(instanceSeed ^ (uint64_t(p.speciesId) << 32));
    const uint64_t range = uint64_t(p.maxHeightCm) - p.minHeightCm + 1;
    const int32_t height = static_cast<int32_t>(p.minHeightCm + seed % range);
    TreeSkeleton out;
    out.speciesId = p.speciesId;
    out.seed = instanceSeed;
    out.heightCm = height;
    // Conservative visual bounds also available for the impostor tier.
    out.crownRadiusCm = static_cast<int32_t>(
        int64_t(height) * p.branchLengthPermille * (p.tertiaryBranches?3:p.secondaryBranches?2:1) / 1000 + p.leafRadiusCm);
    if (lod == TreeLOD::Impostor) return out;

    // Root trunk first, allowing parent indices to be stable per generated LOD.
    out.branches.push_back({0, 0, 0, 0, height, 0,
                            p.trunkRadiusCm, std::max<int32_t>(1, p.trunkRadiusCm / 12), -1});

    const uint32_t count = (lod == TreeLOD::High) ? p.primaryBranches :
        (lod == TreeLOD::Medium) ? (p.primaryBranches + 1u) / 2u :
        (p.primaryBranches + 3u) / 4u;
    const int64_t crownBase = int64_t(height) * p.crownStartPermille / 1000;
    const int64_t available = height - crownBase;
    for (uint32_t i = 0; i < count; ++i) {
        // A separate hash per branch prevents a change to LOD from shifting
        // any tree generation elsewhere in the world.
        const uint64_t h = detail::mix64(seed + uint64_t(i) * 0x9e3779b97f4a7c15ULL);
        int64_t dx = static_cast<int64_t>(h & 0xffffULL) - 32768;
        int64_t dz = static_cast<int64_t>((h >> 16) & 0xffffULL) - 32768;
        int64_t baseY = crownBase + available * (2 * i + 1) / (2 * count);
        // Stratified growth heights fill the crown instead of concentrating
        // scaffold limbs in one pollarded, umbrella-like layer.
        if(p.roundedBroadleafCrown)baseY=crownBase+available*
            (40+(int64_t(i)*880+int64_t((h>>24)%601))/count)/1000;
        if(p.whorlSize) {
            // Fixed integer directions/jitter keep generation replayable.
            static constexpr int32_t circle[16][2]={{32767,0},{30273,12539},{23170,23170},{12539,30273},{0,32767},{-12539,30273},{-23170,23170},{-30273,12539},{-32767,0},{-30273,-12539},{-23170,-23170},{-12539,-30273},{0,-32767},{12539,-30273},{23170,-23170},{30273,-12539}};
            const uint32_t whorl=i/p.whorlSize,limb=i%p.whorlSize;
            const uint32_t whorls=(count+p.whorlSize-1)/p.whorlSize;
            const uint64_t wh=detail::mix64(seed^uint64_t(whorl)*0xd1b54a32d192ed03ULL);
            const uint32_t direction=(uint32_t(wh%16)+limb*16/p.whorlSize)%16;
            dx=circle[direction][0]+int64_t(h%3601)-1800;
            dz=circle[direction][1]+int64_t((h>>16)%3601)-1800;
            const int64_t step=2*int64_t(whorl)+1,total=2*int64_t(whorls);
            // Shorter growth increments near the young upper crown avoid
            // uniformly spaced bare trunk shelves without adding more cards.
            baseY=crownBase+available*step*(3*total-step)/(2*total*total)+
                available/int64_t(whorls)*(int64_t((h>>40)%201)-100)/1200;
            baseY=std::clamp(baseY,crownBase,int64_t(height-1));
        }
        const int64_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
        const int64_t denom = p.whorlSize?32768:std::max<int64_t>(1, std::max(ax, az));
        // Ordered trunk heights; azimuth jittered independently. All integer.
        const int64_t remain = height - baseY;
        const int64_t crownT=baseY-crownBase;
        int64_t envelope=p.roundedConiferCrown && available>0 ? 4*crownT*(available-crownT)/available : remain;
        if(p.roundedBroadleafCrown && available>0) {
            const int64_t t=baseY-crownBase;
            envelope=4*t*(available-t)/available;
        }
        int64_t length = envelope * p.branchLengthPermille / 1000;
        if (p.shape == CrownShape::Conifer) length = length * 80 / 100;
        if (p.shape == CrownShape::Columnar) length = length * 35 / 100;
        length = length * (850 + static_cast<int64_t>((h >> 32) % 301)) / 1000;
        const int32_t endX = static_cast<int32_t>(dx * length / denom);
        const int32_t endZ = static_cast<int32_t>(dz * length / denom);
        int32_t endY = static_cast<int32_t>(
            std::min<int64_t>(height + p.leafRadiusCm,
                baseY + length * (int64_t(p.branchRisePermille)-p.branchDroopPermille) / 1000));
        if(p.roundedBroadleafCrown) {
            const int64_t rise=std::clamp(int64_t(p.branchRisePermille)+int64_t((h>>40)%701)-450,int64_t(-150),int64_t(550));
            endY=std::max(int32_t(p.leafRadiusCm*2),int32_t(baseY+remain*rise/1000));
        }
        if(p.branchDroopPermille&&!p.roundedBroadleafCrown)endY=std::max(endY,int32_t(p.leafRadiusCm*2));
        const int32_t startRadius = std::max<int32_t>(
            1, static_cast<int32_t>(int64_t(p.trunkRadiusCm) * remain / height));
        const int32_t endRadius = std::max<int32_t>(1, startRadius / 4);
        out.branches.push_back({0, static_cast<int32_t>(baseY), 0,
                                endX, endY, endZ, startRadius, endRadius, 0});
        // 1 leaf cluster on Medium/Low, 2 on High; foliage meshes may replace
        // them with camera-facing leaf cards or more detailed leaf geometry.
        const uint32_t leafCount = lod == TreeLOD::High ? 2u : 1u;
        for (uint32_t j = 0; j < leafCount; ++j) {
            const int32_t fraction = leafCount == 1 ? 1000 : (j == 0 ? 720 : 1000);
            const int32_t y = static_cast<int32_t>(
                baseY + (int64_t(endY) - baseY) * fraction / 1000);
            const int32_t x = static_cast<int32_t>(int64_t(endX) * fraction / 1000);
            const int32_t z = static_cast<int32_t>(int64_t(endZ) * fraction / 1000);
            const uint64_t leafSeed = detail::mix64(h + j * 0xd1b54a32d192ed03ULL);
            const uint32_t radius = p.leafRadiusCm *
                (80u + static_cast<uint32_t>(leafSeed % 41u)) / 100u;
            out.leaves.push_back({x, y, z, radius,
                static_cast<uint32_t>(leafSeed >> 32), static_cast<int32_t>(i + 1)});
        }
    }
    // Add children only after primary generation, preserving existing primary
    // indices and every legacy seed when the opt-in count is zero.
    const size_t primaryCount=out.branches.size();
    const auto appendChildren=[&](size_t begin,size_t end,uint32_t children) {
    for(size_t parent=begin;parent<end;++parent) {
        const auto b=out.branches[parent];
        for(uint32_t j=0;j<children;++j) {
            const uint64_t h=detail::mix64(seed^uint64_t(parent)*104729^uint64_t(j+1)*7919);
            const int64_t t=400+(j*450)/std::max(1u,children-1)+int64_t(h%61)-30;
            const int64_t dx=int64_t(b.x1)-b.x0,dz=int64_t(b.z1)-b.z0,dy=int64_t(b.y1)-b.y0;
            const int64_t sign=(j&1)?-1:1;
            const int64_t reach=240+int64_t((h>>16)%181);
            TreeBranch child;
            child.x0=int32_t(b.x0+dx*t/1000);child.y0=int32_t(b.y0+dy*t/1000);child.z0=int32_t(b.z0+dz*t/1000);
            child.x1=child.x0+int32_t((dx*650-sign*dz*750)*reach/1000000);
            child.z1=child.z0+int32_t((dz*650+sign*dx*750)*reach/1000000);
            const int64_t horizontal=std::max(std::abs(dx),std::abs(dz))*reach/1000;
            const int64_t jitter=int64_t((h>>32)%1201)-600;
            const int64_t rise=int64_t(p.branchRisePermille/(p.branchDroopPermille?3u:1u))-p.branchDroopPermille+
                (p.branchDroopPermille?jitter/2:jitter);
            // Carry the parent's vertical growth into its children as well as
            // its azimuth. Ignoring dy flattened every fork into a leaf shelf.
            child.y1=child.y0+int32_t(dy*650*reach/1000000+horizontal*rise/1000);
            child.y1=std::min(child.y1,height);
            child.y1=std::max(child.y1,int32_t(p.leafRadiusCm*2));
            child.startRadiusCm=std::max(1,int32_t((int64_t(b.startRadiusCm)*(1000-t)+int64_t(b.endRadiusCm)*t)*55/100000));
            child.endRadiusCm=1;child.parent=int32_t(parent);
            const int32_t childIndex=int32_t(out.branches.size());out.branches.push_back(child);
            for(uint32_t k=0;k<(lod==TreeLOD::High?2u:1u);++k) {
                const int64_t at=k==0?650:1000;
                out.leaves.push_back({int32_t(child.x0+(int64_t(child.x1)-child.x0)*at/1000),
                    int32_t(child.y0+(int64_t(child.y1)-child.y0)*at/1000),
                    int32_t(child.z0+(int64_t(child.z1)-child.z0)*at/1000),
                    p.leafRadiusCm,uint32_t(h>>32)+k*97,childIndex});
            }
        }
    }
    };
    appendChildren(1,primaryCount,p.secondaryBranches);
    if(p.tertiaryBranches)appendChildren(primaryCount,out.branches.size(),p.tertiaryBranches);
    return out;
}

// Converts one stable foliage prototype placement into a deterministic tree.
// Tree geometry is local-space; use FoliageInstance world position/yaw/scale
// to render it. Never use GPU vertex data for authoritative collision.
inline TreeSkeleton generateTree(const TreeProfile& profile,
                                 const FoliageInstance& instance, TreeLOD lod) {
    if (instance.speciesId != profile.speciesId)
        throw std::invalid_argument("TerraWeave Arbor species mismatch");
    return generateTree(profile, instance.key, lod);
}

} // namespace aiverse::terraweave
