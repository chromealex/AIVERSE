// TerraWeave: rebase authored legacy heightfields onto a new procedural base.
// Pure CPU C++20; no editor, renderer, ECS, or graphics ownership assumptions.
// The saved heightfield is the composite; its residual relative to the
// regenerated previous base is the implicit non-destructive author layer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::terraweave {

struct LegacyRebaseResult {
    std::vector<int16_t> corners;
    std::vector<int16_t> detail;
    size_t authoredCorners = 0; // old composite differs from old base
    size_t shiftedCorners = 0;  // procedural base changed at this sample
    size_t shiftedDetail = 0;   // interpolated base displacement != 0
};

// Fixed 64-bit FNV-1a over dimensions and signed R16 samples, all encoded
// little-endian explicitly (independent of host byte order / std::hash).
inline std::string legacyBaseFingerprint(
        const std::vector<int16_t>& base,int widthCells,int heightCells) {
    if(widthCells<=0 || heightCells<=0 ||
       widthCells>2048 || heightCells>2048 ||
       base.size()!=(size_t(widthCells)+1u)*(size_t(heightCells)+1u))
        throw std::invalid_argument("TerraWeave base fingerprint: dimensions");
    uint64_t hash=14695981039346656037ull;
    auto feed=[&](uint8_t b) {hash^=b;hash*=1099511628211ull;};
    for(const uint32_t v : {uint32_t(widthCells),uint32_t(heightCells)})
        for(int n=0;n<4;++n)feed(uint8_t((v>>(8*n))&255u));
    for(const int16_t sample:base) {
        const uint16_t bits=static_cast<uint16_t>(sample);
        feed(uint8_t(bits&255u));
        feed(uint8_t(bits>>8));
    }
    std::string hex(16,'0');
    static constexpr char digits[]="0123456789abcdef";
    for(int i=15;i>=0;--i){hex[size_t(i)]=digits[hash&15ull];hash>>=4;}
    return hex;
}

// Does not mutate inputs. Throws on invalid grids or any R16 overflow,
// rather than silently clamping or leaving a half-regenerated landscape.
// Detail samples retain their original fine-grained authored variations:
// each receives only the bilinearly interpolated coarse-base difference.
inline LegacyRebaseResult rebaseLegacyHeightfield(
        const std::vector<int16_t>& compositeCorners,
        const std::vector<int16_t>& compositeDetail,
        int widthCells,int heightCells,int detailScale,
        const std::vector<int16_t>& oldBase,
        const std::vector<int16_t>& newBase) {
    if(widthCells<=0 || heightCells<=0 ||
       widthCells>2048 || heightCells>2048)
        throw std::invalid_argument("TerraWeave rebase: map dimensions");
    const size_t width=size_t(widthCells)+1u;
    const size_t count=width*(size_t(heightCells)+1u);
    if(compositeCorners.size()!=count || oldBase.size()!=count ||
       newBase.size()!=count)
        throw std::invalid_argument("TerraWeave rebase: corner grid mismatch");
    if(detailScale<0 || detailScale>8)
        throw std::invalid_argument("TerraWeave rebase: detail scale");
    if(!compositeDetail.empty() && detailScale==0)
        throw std::invalid_argument("TerraWeave rebase: detail scale missing");
    const size_t fineWidth=size_t(widthCells)*size_t(detailScale)+1u;
    const size_t fineHeight=size_t(heightCells)*size_t(detailScale)+1u;
    // Bound transient allocations for malformed/unreasonably dense input.
    if(!compositeDetail.empty() &&
       (fineWidth*fineHeight>16000000u ||
        compositeDetail.size()!=fineWidth*fineHeight))
        throw std::invalid_argument("TerraWeave rebase: detail grid mismatch/budget");

    LegacyRebaseResult output;
    output.corners.resize(count);
    std::vector<int32_t> displacement(count);
    for(size_t i=0;i<count;++i) {
        const int32_t diff=int32_t(newBase[i])-int32_t(oldBase[i]);
        displacement[i]=diff;
        output.shiftedCorners+=diff!=0;
        output.authoredCorners+=compositeCorners[i]!=oldBase[i];
        const int32_t value=int32_t(compositeCorners[i])+diff;
        if(value<std::numeric_limits<int16_t>::min() ||
           value>std::numeric_limits<int16_t>::max())
            throw std::out_of_range("TerraWeave rebase: corner height overflow");
        output.corners[i]=static_cast<int16_t>(value);
    }
    if(compositeDetail.empty())return output;
    output.detail.resize(compositeDetail.size());
    const int64_t k=detailScale, denominator=k*k;
    for(size_t z=0;z<fineHeight;++z) {
        const size_t iz=z/size_t(detailScale);
        const size_t iz1=iz<size_t(heightCells)?iz+1u:iz;
        const int64_t dz=int64_t(z%size_t(detailScale));
        for(size_t x=0;x<fineWidth;++x) {
            const size_t ix=x/size_t(detailScale);
            const size_t ix1=ix<size_t(widthCells)?ix+1u:ix;
            const int64_t dx=int64_t(x%size_t(detailScale));
            // Round each base independently before subtracting. This makes
            // authored fine-detail residuals EXACTLY invariant over repeated
            // A->B->C generator edits (round(B)-round(A) telescopes), unlike
            // rounding the interpolated difference on each regeneration.
            const auto interpolate=[&](const std::vector<int16_t>& base) {
                const int64_t a=base[iz*width+ix];
                const int64_t b=base[iz*width+ix1];
                const int64_t c=base[iz1*width+ix];
                const int64_t d=base[iz1*width+ix1];
                const int64_t numerator=
                    a*(k-dx)*(k-dz)+b*dx*(k-dz)+
                    c*(k-dx)*dz+d*dx*dz;
                // C++ division truncates toward zero; round ties away.
                return (numerator>=0 ?
                    numerator+denominator/2 : numerator-denominator/2)/denominator;
            };
            const int64_t delta=interpolate(newBase)-interpolate(oldBase);
            output.shiftedDetail+=delta!=0;
            const size_t pos=z*fineWidth+x;
            const int64_t value=int64_t(compositeDetail[pos])+delta;
            if(value<std::numeric_limits<int16_t>::min() ||
               value>std::numeric_limits<int16_t>::max())
                throw std::out_of_range("TerraWeave rebase: detail height overflow");
            output.detail[pos]=static_cast<int16_t>(value);
        }
    }
    return output;
}

} // namespace aiverse::terraweave
