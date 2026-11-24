#pragma once
#include "Random.h"
#include <array>
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
}
