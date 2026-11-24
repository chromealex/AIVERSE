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
template<size_t N> inline std::optional<int32_t> periodicValueNoise(std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period={}){
    static_assert(N==2||N==3);if(!noiseValid(p,period))return {};
    std::array<int64_t,N> cell{},t{};for(size_t i=0;i<N;++i){cell[i]=noiseFloor(p[i]);t[i]=noiseFade(p[i]-cell[i]*noiseOne);}
    std::array<int64_t,1<<N> values{};
    for(size_t i=0;i<values.size();++i){
        std::array<int64_t,3> c{};for(size_t j=0;j<N;++j)c[j]=noiseWrap(cell[j]+((i>>j)&1),period[j]);
        values[i]=int64_t(noiseHash(c[0],c[1],c[2],seed)>>47)-noiseOne;
    }
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

enum class NoiseBasis { Value, Gradient };
enum class NoiseFractalMode { FBm, Turbulence, Ridged };
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
        (options.basis!=NoiseBasis::Value&&options.basis!=NoiseBasis::Gradient)||
        (options.mode!=NoiseFractalMode::FBm&&options.mode!=NoiseFractalMode::Turbulence&&
         options.mode!=NoiseFractalMode::Ridged)) return {};
    constexpr int64_t coordLimit=int64_t(1)<<46;
    constexpr int64_t frequencyLimit=65536;
    constexpr int32_t periodLimit=100000000;
    NoiseSample<N> out{};
    int64_t total=0,weight=noiseOne,frequency=1;
    for(unsigned i=0;i<options.octaves;++i) {
        const auto octaveSeed=i?deriveSeed(seed,0x4e4f49534546424dull,i):seed;
        const auto sample=options.basis==NoiseBasis::Value?
            periodicValueNoiseSample(p,octaveSeed,period):
            periodicGradientNoiseSample(p,octaveSeed,period);
        if(!sample) return {};
        auto s=*sample;
        if(options.mode!=NoiseFractalMode::FBm) {
            const int64_t sign=(s.value>0)-(s.value<0);
            s.value*=sign;
            for(auto& d:s.derivative) d*=sign;
            if(options.mode==NoiseFractalMode::Ridged) {
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
}
