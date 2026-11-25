#pragma once
#include "Random.h"
#include <array>
#include <cstddef>
#include <algorithm>
#include <optional>
namespace aiverse::lib::algorithms {
inline constexpr int64_t noiseOne=65536;
inline int64_t noiseFloor(int64_t x){auto q=x/noiseOne;return q-(x%noiseOne<0);}
inline int64_t noiseWrap(int64_t x,int32_t period){if(!period)return x;auto r=x%period;return r<0?r+period:r;}
inline uint64_t noiseHash(int64_t x,int64_t y,int64_t z,uint64_t seed){
    return randomMix(randomMix(seed)^randomMix(uint64_t(x)+0x9e3779b97f4a7c15ull)^randomMix(uint64_t(y)+0xd1b54a32d192ed03ull)^randomMix(uint64_t(z)+0x94d049bb133111ebull));
}
inline int64_t noiseFade(int64_t t){auto t2=t*t/noiseOne;return t2*(3*noiseOne-2*t)/noiseOne;}
inline int64_t noiseLerp(int64_t a,int64_t b,int64_t t){return a+(b-a)*t/noiseOne;}
template<size_t N> inline bool noiseValid(std::array<int64_t,N> p,std::array<int32_t,N> period){
    for(size_t i=0;i<N;++i)if(p[i]<-(int64_t(1)<<46)||p[i]>(int64_t(1)<<46)||period[i]<0||period[i]>100000000)return false;return true;
}
namespace noise_detail {
// Shared scalar lattice preparation for single-point and SIMD interpolation.
// Coordinates and periods must already have passed noiseValid.
template<size_t N> inline void valueNoiseCorners(std::array<int64_t,N> p,uint64_t seed,
    std::array<int32_t,N> period,std::array<int64_t,N>& t,std::array<int64_t,1<<N>& values){
    std::array<int64_t,N> cell{};for(size_t i=0;i<N;++i){cell[i]=noiseFloor(p[i]);t[i]=noiseFade(p[i]-cell[i]*noiseOne);}
    for(size_t i=0;i<values.size();++i){
        std::array<int64_t,3> c{};for(size_t j=0;j<N;++j)c[j]=noiseWrap(cell[j]+((i>>j)&1),period[j]);
        values[i]=int64_t(noiseHash(c[0],c[1],c[2],seed)>>47)-noiseOne;
    }
}
} // namespace noise_detail
template<size_t N> inline std::optional<int32_t> periodicValueNoise(std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={}){
    static_assert(N==2||N==3);if(!noiseValid(p,period))return {};
    std::array<int64_t,N> t{};std::array<int64_t,1<<N> values{};
    noise_detail::valueNoiseCorners(p,seed,period,t,values);
    for(size_t j=0;j<N;++j)for(size_t i=0;i<values.size();i+=size_t(2)<<j)values[i]=noiseLerp(values[i],values[i+(size_t(1)<<j)],t[j]);
    return int32_t(values[0]);
}
struct CellularDistance {int64_t firstSquared,secondSquared;};
// One feature per lattice cell; exact F1/F2 squared distances in Q32.
template<size_t N> inline std::optional<CellularDistance> cellularNoise(std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={}){
    static_assert(N==2||N==3);if(!noiseValid(p,period))return {};
    std::array<int64_t,N> base{},c{};for(size_t i=0;i<N;++i)base[i]=noiseFloor(p[i]);
    CellularDistance out{INT64_MAX,INT64_MAX};
    auto visit=[&](auto&& self,size_t axis)->void{
        if(axis<N){for(int o=-3;o<=3;++o){c[axis]=base[axis]+o;self(self,axis+1);}return;}
        std::array<int64_t,3> h{};for(size_t i=0;i<N;++i)h[i]=noiseWrap(c[i],period[i]);
        auto key=noiseHash(h[0],h[1],h[2],seed);int64_t d=0;
        for(size_t i=0;i<N;++i){auto x=c[i]*noiseOne+int64_t((key>>(16*i))&65535)-p[i];d+=x*x;}
        if(d<out.firstSquared){out.secondSquared=out.firstSquared;out.firstSquared=d;}else if(d<out.secondSquared)out.secondSquared=d;
    };visit(visit,0);return out;
}

// Signed Q16 value and derivative per lattice unit. Derivatives describe
// the underlying smooth interpolant, approximated by fixed-point arithmetic.
template<std::size_t N> struct NoiseSample {
    int64_t value=0;
    std::array<int64_t,N> derivative{};
    bool operator==(const NoiseSample&) const = default;
};

namespace noise_detail {
enum class Basis { Value, Gradient };

template<std::size_t N> inline std::array<int64_t,N> gradient(uint64_t hash) {
    std::array<int64_t,N> g{};
    constexpr int64_t diagonal=46341; // round(65536 / sqrt(2))
    if constexpr(N==2) {
        const unsigned k=unsigned(hash%8);
        if(k<4) g[k/2]=(k&1)?-noiseOne:noiseOne;
        else {
            g[0]=(k&1)?-diagonal:diagonal;
            g[1]=(k&2)?-diagonal:diagonal;
        }
    } else {
        const unsigned k=unsigned(hash%12);
        const unsigned axis=k/4;
        g[axis]=(k&1)?-diagonal:diagonal;
        g[(axis+1)%3]=(k&2)?-diagonal:diagonal;
    }
    return g;
}

template<std::size_t N> inline std::optional<NoiseSample<N>> sample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period,Basis basis) {
    static_assert(N==2||N==3);
    if(!noiseValid(p,period)) return {};
    std::array<int64_t,N> cell{},fraction{},fade{},slope{};
    for(std::size_t j=0;j<N;++j) {
        cell[j]=noiseFloor(p[j]);
        fraction[j]=p[j]-cell[j]*noiseOne;
        fade[j]=noiseFade(fraction[j]);
        slope[j]=6*fraction[j]*(noiseOne-fraction[j])/noiseOne;
    }
    std::array<NoiseSample<N>,std::size_t(1)<<N> corners{};
    for(std::size_t i=0;i<corners.size();++i) {
        std::array<int64_t,3> h{};
        for(std::size_t j=0;j<N;++j)
            h[j]=noiseWrap(cell[j]+int64_t((i>>j)&1),period[j]);
        const auto key=noiseHash(h[0],h[1],h[2],seed);
        if(basis==Basis::Value) {
            corners[i].value=int64_t(key>>47)-noiseOne;
        } else {
            auto& c=corners[i];
            c.derivative=gradient<N>(key);
            int64_t dot=0;
            for(std::size_t j=0;j<N;++j)
                dot+=c.derivative[j]*(fraction[j]-int64_t((i>>j)&1)*noiseOne);
            c.value=dot/noiseOne;
        }
    }
    for(std::size_t j=0;j<N;++j) {
        const auto stride=std::size_t(1)<<j;
        for(std::size_t i=0;i<corners.size();i+=2*stride) {
            const auto a=corners[i],b=corners[i+stride];
            auto& out=corners[i];
            out.value=noiseLerp(a.value,b.value,fade[j]);
            for(std::size_t k=0;k<N;++k)
                out.derivative[k]=noiseLerp(a.derivative[k],b.derivative[k],fade[j]);
            out.derivative[j]+=(b.value-a.value)*slope[j]/noiseOne;
        }
    }
    return corners[0];
}
} // namespace noise_detail

template<std::size_t N> inline std::optional<NoiseSample<N>> periodicValueNoiseSample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={}) {
    return noise_detail::sample(p,seed,period,noise_detail::Basis::Value);
}

template<std::size_t N> inline std::optional<NoiseSample<N>> periodicGradientNoiseSample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={}) {
    return noise_detail::sample(p,seed,period,noise_detail::Basis::Gradient);
}

template<std::size_t N> inline std::optional<int32_t> periodicGradientNoise(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={}) {
    auto s=periodicGradientNoiseSample(p,seed,period);
    if(!s) return {};
    return int32_t(s->value);
}

namespace noise_detail {
// Rational skew transform S=I+F*11^T and exact inverse I-G*11^T,
// G=F/(1+N*F). The 2D F approximates (sqrt(3)-1)/2; 3D F=1/3.
// Unskew only local fractions: no coordinate-dependent rounding drift.
template<std::size_t N> struct SimplexTransform {
    static constexpr int64_t numerator=N==2?23988:1;
    static constexpr int64_t denominator=N==2?65536:3;
    static constexpr int64_t inverseDenominator=denominator+int64_t(N)*numerator;
};

template<std::size_t N> inline NoiseSample<N> simplexKernel(
    std::array<int64_t,N> skew,uint64_t seed,std::array<int32_t,N> period) {
    using Transform=SimplexTransform<N>;
    constexpr int64_t one30=int64_t(1)<<30;
    constexpr int64_t one32=int64_t(1)<<32;
    // Slightly below squared radius 1/2, so omitted vertices have zero
    // support even at quantized simplex boundaries (including 3D faces).
    constexpr int64_t radiusSquared=32760*noiseOne;
    constexpr int64_t scale=48;
    std::array<int64_t,N> cell{},fraction{},offset{};
    std::array<std::size_t,N> rank{};
    int64_t sum=0;
    for(std::size_t j=0;j<N;++j) {
        cell[j]=noiseFloor(skew[j]);
        fraction[j]=skew[j]-cell[j]*noiseOne;
        sum+=fraction[j]; rank[j]=j;
    }
    // Fixed insertion sort with explicit axis order for equal fractions.
    for(std::size_t j=1;j<N;++j) {
        const auto k=rank[j]; auto i=j;
        while(i&&fraction[k]>fraction[rank[i-1]]) {rank[i]=rank[i-1];--i;}
        rank[i]=k;
    }
    NoiseSample<N> out{}; // accumulate in Q30, convert only once
    for(std::size_t corner=0;corner<=N;++corner) {
        const auto unskew=(sum-int64_t(corner)*noiseOne)*Transform::numerator/Transform::inverseDenominator;
        std::array<int64_t,N> d{};
        std::array<int64_t,3> h{};
        int64_t r2=0;
        for(std::size_t j=0;j<N;++j) {
            d[j]=fraction[j]-offset[j]*noiseOne-unskew;
            r2+=d[j]*d[j];
            h[j]=noiseWrap(cell[j]+offset[j],period[j]);
        }
        if(r2<radiusSquared) {
            const auto g=gradient<N>(noiseHash(h[0],h[1],h[2],seed));
            int64_t dot=0; // Q32
            for(std::size_t j=0;j<N;++j) dot+=g[j]*d[j];
            const int64_t t=(radiusSquared-r2)/4;
            const int64_t t2=t*t/one30,t3=t2*t/one30,t4=t3*t/one30;
            out.value+=t4*dot/one32;
            // d(t^4*dot(g,d))/dx = t^4*g - 8*t^3*dot(g,d)*d.
            for(std::size_t j=0;j<N;++j) {
                const int64_t dotD=dot*d[j]/noiseOne;
                out.derivative[j]+=t4*g[j]/noiseOne-8*(t3*dotD/one32);
            }
        }
        if(corner<N) offset[rank[corner]]=1;
    }
    out.value=out.value*scale/(one30/noiseOne);
    for(auto& v:out.derivative) v=v*scale/(one30/noiseOne);
    return out;
}
} // namespace noise_detail

// Cartesian Q16 input, aperiodic. Derivatives per Cartesian input unit.
template<std::size_t N> inline std::optional<NoiseSample<N>> simplexNoiseSample(
    std::array<int64_t,N> p,uint64_t seed) {
    static_assert(N==2||N==3);
    if(!noiseValid(p,std::array<int32_t,N>{})) return {};
    using Transform=noise_detail::SimplexTransform<N>;
    int64_t sum=0; for(auto v:p) sum+=v;
    // For N=2, |sum*numerator| <= 2^47*23988 < 2^62.
    // N=3 uses numerator 1. The skew coordinates fit comfortably in int64.
    const auto product=sum*Transform::numerator;
    const auto shift=product/Transform::denominator-(product%Transform::denominator<0);
    for(auto& v:p) v+=shift;
    return noise_detail::simplexKernel(p,seed,std::array<int32_t,N>{});
}

template<std::size_t N> inline std::optional<int64_t> simplexNoise(
    std::array<int64_t,N> p,uint64_t seed) {
    auto s=simplexNoiseSample(p,seed);
    if(!s) return {};
    return s->value;
}

// Skew-lattice coordinates, NOT Cartesian coordinates. Exact periodicity
// along each skew axis; derivative rows are transformed by inverse S.
template<std::size_t N> inline std::optional<NoiseSample<N>> simplexSkewNoiseSample(
    std::array<int64_t,N> skew,uint64_t seed,std::array<int32_t,N> period={}) {
    static_assert(N==2||N==3);
    if(!noiseValid(skew,period)) return {};
    using Transform=noise_detail::SimplexTransform<N>;
    auto out=noise_detail::simplexKernel(skew,seed,period);
    int64_t sum=0; for(auto v:out.derivative) sum+=v;
    const auto correction=sum*Transform::numerator/Transform::inverseDenominator;
    for(auto& v:out.derivative) v-=correction;
    return out;
}

enum class NoiseBasis { Value, Gradient, Simplex };
enum class NoiseFractalMode { FBm, Turbulence, Ridged, Billow };
struct NoiseFractalOptions {
    unsigned octaves=4;
    unsigned lacunarity=2; // integer multiplier keeps the base tile periodic
    int32_t gainQ16=32768;
    NoiseBasis basis=NoiseBasis::Gradient;
    NoiseFractalMode mode=NoiseFractalMode::FBm;
};

// Normalized weighted sum, with the chain rule applied to derivatives.
// Reject the entire request if any octave exceeds the coordinate/period/frequency bounds.
template<std::size_t N> inline std::optional<NoiseSample<N>> fractalNoiseSample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseFractalOptions options={}) {
    static_assert(N==2||N==3);
    if(!noiseValid(p,period)||options.octaves<1||options.octaves>16||
        options.lacunarity<1||options.lacunarity>8||options.gainQ16<0||
        options.gainQ16>noiseOne||
        (options.basis!=NoiseBasis::Value&&options.basis!=NoiseBasis::Gradient&&
         options.basis!=NoiseBasis::Simplex)||
        (options.mode!=NoiseFractalMode::FBm&&options.mode!=NoiseFractalMode::Turbulence&&
         options.mode!=NoiseFractalMode::Ridged&&options.mode!=NoiseFractalMode::Billow)) return {};
    if(options.basis==NoiseBasis::Simplex)
        for(auto v:period) if(v) return {}; // Cartesian periods are not skew periods.
    constexpr int64_t coordLimit=int64_t(1)<<46;
    constexpr int64_t frequencyLimit=65536;
    constexpr int32_t periodLimit=100000000;
    NoiseSample<N> out{};
    int64_t total=0,weight=noiseOne,frequency=1;
    for(unsigned i=0;i<options.octaves;++i) {
        const auto octaveSeed=i?deriveSeed(seed,0x4e4f49534546424dull,i):seed;
        const auto sample=options.basis==NoiseBasis::Value?
            periodicValueNoiseSample(p,octaveSeed,period):
            (options.basis==NoiseBasis::Gradient?periodicGradientNoiseSample(p,octaveSeed,period):
             simplexNoiseSample(p,octaveSeed));
        if(!sample) return {};
        auto s=*sample;
        if(options.mode!=NoiseFractalMode::FBm) {
            const int64_t sign=(s.value>0)-(s.value<0);
            s.value*=sign;
            for(auto& d:s.derivative) d*=sign;
            if(options.mode==NoiseFractalMode::Billow) {
                s.value=2*s.value-noiseOne;
                for(auto& d:s.derivative) d*=2;
            } else if(options.mode==NoiseFractalMode::Ridged) {
                if(s.value>=noiseOne) {
                    s.value=0;
                    s.derivative.fill(0);
                } else {
                    s.value=noiseOne-s.value;
                    for(auto& d:s.derivative) d=-d;
                }
            }
        }
        out.value+=s.value*weight;
        for(std::size_t j=0;j<N;++j)
            out.derivative[j]+=s.derivative[j]*frequency*weight;
        total+=weight;
        if(i+1==options.octaves) break;
        const int64_t lac=options.lacunarity;
        if(frequency>frequencyLimit/lac) return {};
        frequency*=lac;
        for(std::size_t j=0;j<N;++j) {
            if(p[j]>coordLimit/lac||p[j]<-coordLimit/lac||
                period[j]>periodLimit/lac) return {};
            p[j]*=lac;
            period[j]=int32_t(period[j]*lac);
        }
        weight=weight*options.gainQ16/noiseOne;
    }
    out.value/=total;
    for(auto& d:out.derivative) d/=total;
    return out;
}

template<std::size_t N> inline std::optional<int64_t> fractalNoise(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseFractalOptions options={}) {
    auto s=fractalNoiseSample(p,seed,period,options);
    if(!s) return {};
    return s->value;
}

// Rows are independent scalar fields; derivative[j] is the Jacobian entry
// for output component i with respect to input coordinate j (signed Q16).
template<std::size_t N> using NoiseVectorSample=std::array<NoiseSample<N>,N>;

template<std::size_t N> inline std::optional<NoiseVectorSample<N>> vectorNoiseSample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseFractalOptions options={}) {
    static_assert(N==2||N==3);
    NoiseVectorSample<N> out{};
    for(std::size_t i=0;i<N;++i) {
        auto s=fractalNoiseSample(p,deriveSeed(seed,0x4e4f495345564543ull,i),period,options);
        if(!s) return {};
        out[i]=*s;
    }
    return out;
}

template<std::size_t N> inline std::optional<std::array<int64_t,N>> vectorNoise(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseFractalOptions options={}) {
    auto s=vectorNoiseSample(p,seed,period,options);
    if(!s) return {};
    std::array<int64_t,N> out{};
    for(std::size_t i=0;i<N;++i) out[i]=(*s)[i].value;
    return out;
}

struct NoiseWarpOptions {
    NoiseFractalOptions carrier{};
    NoiseFractalOptions displacement{};
    int32_t strengthQ16=32768; // signed displacement scale; at most 8 lattice units
};

namespace noise_detail {
// Checked Q16 multiplication without __int128, including MSVC/WASM.
// |a|,|b| <= 2^38 => |a*b/65536| <= 2^60. Splitting before
// multiplying avoids overflow and preserves truncation toward zero.
inline std::optional<int64_t> chainProduct(int64_t a,int64_t b) {
    constexpr int64_t bound=int64_t(1)<<38;
    if(a<-bound||a>bound||b<-bound||b>bound) return {};
    return (a/noiseOne)*b+((a%noiseOne)*b)/noiseOne;
}
} // namespace noise_detail

// q_i = p_i + strength * displacement_i(p).
// Gradient = J(q)^T * carrierGradient(q). Zero strength ignores
// displacement options and is exactly the unwarped carrier.
template<std::size_t N> inline std::optional<NoiseSample<N>> domainWarpNoiseSample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseWarpOptions options={}) {
    static_assert(N==2||N==3);
    if(!noiseValid(p,period)||options.strengthQ16<-8*noiseOne||
        options.strengthQ16>8*noiseOne) return {};
    if(options.strengthQ16==0)
        return fractalNoiseSample(p,seed,period,options.carrier);
    auto displacement=vectorNoiseSample(p,deriveSeed(seed,0x4e4f495345574152ull,0),
        period,options.displacement);
    if(!displacement) return {};
    auto q=p;
    std::array<std::array<int64_t,N>,N> jacobian{};
    for(std::size_t i=0;i<N;++i) {
        const auto& d=(*displacement)[i];
        // The scalar result is a weighted average of bounded lattice values.
        // Keep the offset addition checked independently of that invariant.
        if(d.value<-8*noiseOne||d.value>8*noiseOne) return {};
        q[i]+=d.value*options.strengthQ16/noiseOne;
        for(std::size_t j=0;j<N;++j) {
            auto t=noise_detail::chainProduct(d.derivative[j],options.strengthQ16);
            if(!t) return {};
            jacobian[i][j]=*t+(i==j?noiseOne:0);
        }
    }
    auto carrier=fractalNoiseSample(q,seed,period,options.carrier);
    if(!carrier) return {};
    NoiseSample<N> out{};
    out.value=carrier->value;
    for(std::size_t j=0;j<N;++j) {
        for(std::size_t i=0;i<N;++i) {
            auto t=noise_detail::chainProduct(carrier->derivative[i],jacobian[i][j]);
            if(!t) return {};
            out.derivative[j]+=*t; // at most three terms of magnitude <= 2^60
        }
    }
    return out;
}

template<std::size_t N> inline std::optional<int64_t> domainWarpNoise(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseWarpOptions options={}) {
    auto s=domainWarpNoiseSample(p,seed,period,options);
    if(!s) return {};
    return s->value;
}

// Curl of a scalar potential in 2D, or three independent vector-potential
// components in 3D. Fixed-point approximation; not an exactly
// divergence-free discrete grid operator. Output is signed Q16 and
// intentionally unnormalized (preserves the potential derivative scale).
template<std::size_t N> inline std::optional<std::array<int64_t,N>> curlNoise(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={},
    NoiseFractalOptions potential={}) {
    static_assert(N==2||N==3);
    if constexpr(N==2) {
        auto s=fractalNoiseSample(p,seed,period,potential);
        if(!s) return {};
        return std::array<int64_t,2>{s->derivative[1],-s->derivative[0]};
    } else {
        auto v=vectorNoiseSample(p,seed,period,potential);
        if(!v) return {};
        const auto& a=*v;
        return std::array<int64_t,3>{
            a[2].derivative[1]-a[1].derivative[2],
            a[0].derivative[2]-a[2].derivative[0],
            a[1].derivative[0]-a[0].derivative[1]};
    }
}
}
