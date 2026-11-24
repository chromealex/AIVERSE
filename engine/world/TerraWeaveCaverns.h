// TerraWeave Caverns: pure, deterministic 3D cave volumes in integer centimetres.
// No ECS, renderer, voxel storage, global RNG, mutable caches, or genre dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {

struct PointCm { int64_t x=0, y=0, z=0; };
struct ChunkKey3 { int64_t x=0, y=0, z=0; constexpr bool operator==(const ChunkKey3&) const = default; };
struct Settings {
    uint64_t seed=1;
    int32_t cellCm=4800;          // global lattice cell length
    int32_t tunnelRadiusCm=450;   // capsule radius around lattice connections
    int32_t chamberRadiusCm=1200; // sphere radius around selected lattice centers
    uint32_t tunnelChance=550;    // chance per positive-direction edge, 0..1000
    uint32_t chamberChance=180;   // chance per cell, 0..1000
    int64_t minYcm=-200000;
    int64_t maxYcm=-300;          // half-open underground cave band
};
inline constexpr int64_t kLimitCm=1000000000LL;
inline uint64_t mix(uint64_t n) {
    n ^= n >> 30; n *= 0xbf58476d1ce4e5b9ULL;
    n ^= n >> 27; n *= 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
inline uint64_t hash(uint64_t seed,int64_t x,int64_t y,int64_t z,uint64_t salt=0) {
    return mix(seed ^ mix(uint64_t(x)+0x9e3779b97f4a7c15ULL) ^
        mix(uint64_t(y)+0xd1b54a32d192ed03ULL) ^
        mix(uint64_t(z)+0x94d049bb133111ebULL) ^ mix(salt));
}
inline int64_t floorDiv(int64_t a,int64_t b) {
    const int64_t q=a/b, r=a%b;
    return q-(r<0?1:0); // b > 0
}
inline void validate(const Settings& s) {
    if(s.cellCm<100 || s.cellCm>10000 ||
       s.tunnelRadiusCm<1 || s.tunnelRadiusCm>s.cellCm/3 ||
       s.chamberRadiusCm<1 || s.chamberRadiusCm>s.cellCm/2 ||
       s.tunnelChance>1000 || s.chamberChance>1000 ||
       s.minYcm < -kLimitCm || s.maxYcm>kLimitCm ||
       s.minYcm>=s.maxYcm)
        throw std::invalid_argument("TerraWeave Caverns settings");
}
inline void validatePoint(PointCm p) {
    if(p.x < -kLimitCm || p.x > kLimitCm ||
       p.y < -kLimitCm || p.y > kLimitCm ||
       p.z < -kLimitCm || p.z > kLimitCm)
        throw std::out_of_range("TerraWeave Caverns world extent");
}
inline PointCm center(const Settings& s,int64_t cx,int64_t cy,int64_t cz) {
    const uint64_t h=hash(s.seed,cx,cy,cz,11);
    const int64_t spread=s.cellCm/8;
    const auto jitter=[spread](uint64_t v)->int64_t {
        return (int64_t(v&65535ULL)*2-65535)*spread/65535;
    };
    return { cx*s.cellCm+s.cellCm/2+jitter(h),
             cy*s.cellCm+s.cellCm/2+jitter(mix(h^17)),
             cz*s.cellCm+s.cellCm/2+jitter(mix(h^31)) };
}
// Exact integer predicate for a capsule. The projection is intentionally Q16
// so all clients/platforms agree; dimensions are bounded by validate().
inline bool insideCapsule(PointCm p,PointCm a,PointCm b,int64_t radius) {
    const int64_t dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
    const int64_t vx=p.x-a.x,vy=p.y-a.y,vz=p.z-a.z;
    const int64_t denom=dx*dx+dy*dy+dz*dz;
    const int64_t dot=vx*dx+vy*dy+vz*dz;
    const int64_t t=denom? std::clamp<int64_t>(dot*65536/denom,0,65536):0;
    const int64_t qx=a.x+dx*t/65536,qy=a.y+dy*t/65536,qz=a.z+dz*t/65536;
    const int64_t ex=p.x-qx,ey=p.y-qy,ez=p.z-qz;
    return ex*ex+ey*ey+ez*ez <= radius*radius;
}
inline bool insideSphere(PointCm p,PointCm c,int64_t r) {
    const int64_t x=p.x-c.x,y=p.y-c.y,z=p.z-c.z;
    return x*x+y*y+z*z <= r*r;
}
// Sample only adjacent cell centers and positive-direction edges. The same
// physical point gives the same result regardless of chunk boundaries/order.
inline bool isVoidUnchecked(const Settings& s,PointCm p) {
    if(p.y<s.minYcm || p.y>=s.maxYcm) return false;
    const int64_t cx=floorDiv(p.x,s.cellCm);
    const int64_t cy=floorDiv(p.y,s.cellCm);
    const int64_t cz=floorDiv(p.z,s.cellCm);
    // Endpoint jitter is <= cell/8, sphere radius <= cell/2, and tunnel
    // radius <= cell/3: cells more than one away cannot influence p.
    for(int64_t z=cz-1;z<=cz+1;++z)
    for(int64_t y=cy-1;y<=cy+1;++y)
    for(int64_t x=cx-1;x<=cx+1;++x) {
        const PointCm a=center(s,x,y,z);
        if(hash(s.seed,x,y,z,19)%1000 < s.chamberChance &&
           insideSphere(p,a,s.chamberRadiusCm)) return true;
        for(int axis=0;axis<3;++axis) {
            if(hash(s.seed,x,y,z,101+uint64_t(axis))%1000 >= s.tunnelChance)
                continue;
            const PointCm b=center(s,x+(axis==0),y+(axis==1),z+(axis==2));
            if(insideCapsule(p,a,b,s.tunnelRadiusCm)) return true;
        }
    }
    return false;
}
inline bool isVoid(const Settings& s,PointCm p) {
    validate(s); validatePoint(p);
    return isVoidUnchecked(s,p);
}
struct VolumeChunk {
    ChunkKey3 key{};
    uint32_t resolution=0;
    int32_t spacingCm=0;
    // X fastest, then Y, then Z. 1 = void, 0 = solid.
    std::vector<uint8_t> voidMask;
    bool at(uint32_t x,uint32_t y,uint32_t z) const {
        if(x>=resolution || y>=resolution || z>=resolution)
            throw std::out_of_range("TerraWeave Caverns chunk cell");
        return voidMask[(size_t(z)*resolution+y)*resolution+x]!=0;
    }
};
inline VolumeChunk generateChunk(const Settings& s,ChunkKey3 key,
                                 uint32_t resolution=16,int32_t spacingCm=100) {
    validate(s);
    if(resolution==0 || resolution>128 || spacingCm<=0 || spacingCm>10000)
        throw std::invalid_argument("TerraWeave Caverns chunk settings");
    const int64_t span=int64_t(resolution)*spacingCm;
    const int64_t maxKey=(kLimitCm-span)/span;
    if(key.x < -maxKey || key.x > maxKey ||
       key.y < -maxKey || key.y > maxKey ||
       key.z < -maxKey || key.z > maxKey)
        throw std::out_of_range("TerraWeave Caverns chunk extent");
    VolumeChunk out{key,resolution,spacingCm,{}};
    const size_t count=size_t(resolution)*resolution*resolution;
    out.voidMask.resize(count);
    const PointCm origin{key.x*span,key.y*span,key.z*span};
    for(uint32_t z=0;z<resolution;++z)
    for(uint32_t y=0;y<resolution;++y)
    for(uint32_t x=0;x<resolution;++x) {
        const PointCm p{origin.x+int64_t(x)*spacingCm+spacingCm/2,
                        origin.y+int64_t(y)*spacingCm+spacingCm/2,
                        origin.z+int64_t(z)*spacingCm+spacingCm/2};
        out.voidMask[(size_t(z)*resolution+y)*resolution+x]=uint8_t(isVoidUnchecked(s,p));
    }
    return out;
}
} // namespace aiverse::terraweave::caverns
