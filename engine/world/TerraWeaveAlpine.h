// Opt-in native alpine height source. Integer world-space samples, no project solver.
#pragma once
#include "engine/world/TerraWeave.h"
#include "engine/core/Fixed.h"
namespace aiverse::terraweave {
enum class AlpineMorphology : uint8_t { LegacyCone=0, RidgedMassif=1, CraggedMassif=2 };
struct AlpinePeak {
    int64_t xCm=0,zCm=0;
    int32_t radiusXcm=10000,radiusZcm=16000,heightCm=12000;
    int32_t shearPermille=0;
    AlpineMorphology morphology=AlpineMorphology::LegacyCone; // old seeds stay exact
};
inline int32_t alpineHeightCm(const TerrainSettings& terrain,
        const std::vector<AlpinePeak>& peaks,int64_t x,int64_t z,
        int32_t valleyHalfWidthCm=0,int32_t valleyBlendCm=10000) {
    if(x< -kWorldLimitCm||x>kWorldLimitCm||z< -kWorldLimitCm||z>kWorldLimitCm)throw std::out_of_range("Terra alpine sample");
    int64_t mountain=0;
    if(peaks.size()>32)throw std::length_error("Terra alpine peak budget");
    for(const auto& p:peaks){
        if(p.xCm< -kWorldLimitCm||p.xCm>kWorldLimitCm||p.zCm< -kWorldLimitCm||p.zCm>kWorldLimitCm||p.radiusXcm<1000||p.radiusXcm>100000||p.radiusZcm<1000||p.radiusZcm>100000||p.heightCm<0||p.heightCm>100000||std::abs(int64_t(p.shearPermille))>1000||
                (p.morphology!=AlpineMorphology::LegacyCone&&p.morphology!=AlpineMorphology::RidgedMassif&&p.morphology!=AlpineMorphology::CraggedMassif))
            throw std::invalid_argument("Terra alpine profile");
        const int64_t dx=x-p.xCm+(z-p.zCm)*p.shearPermille/1000,dz=z-p.zCm;
        if(std::abs(dx)>p.radiusXcm||std::abs(dz)>p.radiusZcm)continue;
        const int64_t nx=dx*65536/p.radiusXcm,nz=dz*65536/p.radiusZcm;
        const auto r=fxSqrt(Fixed::fromRaw(nx*nx/65536+nz*nz/65536)).raw;
        if(r>=65536)continue;
        if(p.morphology==AlpineMorphology::RidgedMassif||p.morphology==AlpineMorphology::CraggedMassif) {
            const bool cragged=p.morphology==AlpineMorphology::CraggedMassif;
            // Broad connected rock masses: the support goes to zero with
            // zero slope, while ridged multifractal relief owns the crest.
            // A linear radial carrier would remain a cone at every seed.
            const int64_t envelope=65536-r*r/65536;
            const int64_t support=envelope*envelope/65536;
            // CraggedMassif moves modest relief to a third-radius scale. A high
            // smooth carrier bounds cliffs instead of amplifying narrow spires.
            const int64_t period=std::max<int64_t>(1000,std::min(p.radiusXcm,p.radiusZcm)/(cragged?3:1));
            const uint64_t seed=detail::mix64(terrain.seed^uint64_t(p.xCm)^detail::mix64(uint64_t(p.zCm)));
            const int64_t warpX=detail::noise(x,z,period,seed)*period/65536/(cragged?8:4);
            const int64_t warpZ=detail::noise(x,z,period,seed+1)*period/65536/(cragged?8:4);
            // Each doubled frequency gets < half the height: gradients
            // decay instead of growing into needle-like towers.
            int64_t ridge=0,amplitude=32768,feedback=65536,total=0;
            for(int octave=0;octave<5;++octave) {
                const auto n=std::abs(detail::noise(x+warpX,z+warpZ,std::max<int64_t>(150,period>>octave),seed+uint64_t(octave)*7919));
                int64_t signal=65536-n;signal=signal*signal/65536;
                signal=signal*feedback/65536;
                ridge+=signal*amplitude/65536;total+=amplitude;
                feedback=std::min<int64_t>(65536,signal*2);amplitude=cragged?amplitude*21/50:amplitude*7/20;
            }
            const int64_t crest=total?ridge*65536/total:0;
            const int64_t carrier=cragged?52000:16000;
            const int64_t relief=int64_t(p.heightCm)*support/65536*(carrier+crest*(65536-carrier)/65536)/65536;
            mountain=std::max(mountain,relief);continue;
        }
        // Narrow ridgelines, unequal flanks and nested gullies, not circular domes.
        int64_t ridge=0,weight=32768;
        for(int i=0;i<5;++i){
            const auto n=std::abs(detail::noise(x+dz/3,z-dx/5,std::max(150,6000>>i),terrain.seed+uint64_t(i)*7919));
            ridge+=(65536-n)*weight/65536;weight/=2;
        }
        const auto envelope=65536-r;
        const auto relief=int64_t(p.heightCm)*envelope/65536*(35000+ridge/2)/65536;
        mountain=std::max(mountain,relief);
    }
    if(valleyHalfWidthCm<0||valleyHalfWidthCm>100000||valleyBlendCm<100||valleyBlendCm>100000)throw std::invalid_argument("Terra alpine valley");
    if(valleyHalfWidthCm){const int64_t t=std::clamp<int64_t>((std::abs(x)-valleyHalfWidthCm)*65536/valleyBlendCm,0,65536);const auto blend=t*t/65536*(196608-2*t)/65536;mountain=mountain*blend/65536;}
    return int32_t(std::clamp<int64_t>(int64_t(heightAtCm(terrain,x,z))+mountain,-1000000,1000000));
}
} // namespace aiverse::terraweave
