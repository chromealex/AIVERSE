#pragma once
#include "Random.h"
#include <array>
#include <cmath>
#include <vector>
#include <cstddef>
namespace aiverse::lib::algorithms {
// Q32 radical inverse; integer truncation, no floating point.
inline std::optional<uint32_t> radicalInverseQ32(uint64_t index,uint32_t base){
    if(base<2||base>65536)return {};
    uint64_t factor=(uint64_t(1)<<32)/base,result=0;
    while(index&&factor){result+=(index%base)*factor;index/=base;factor/=base;}
    return uint32_t(result);
}
inline std::array<uint32_t,2> halton2Q32(uint64_t index){return {*radicalInverseQ32(index,2),*radicalInverseQ32(index,3)};}
inline std::optional<std::array<uint32_t,2>> hammersley2Q32(uint32_t index,uint32_t count){
    if(!count||index>=count)return {};
    return std::array<uint32_t,2>{uint32_t((uint64_t(index)<<32)/count),*radicalInverseQ32(index,2)};
}
inline std::array<uint32_t,2> sobol2Q32(uint32_t index){
    uint32_t gray=index^(index>>1),x=0,y=0,v=0x80000000u;
    for(unsigned i=0;i<32;++i){if(gray&(uint32_t(1)<<i)){x^=0x80000000u>>i;y^=v;}v^=v>>1;}
    return {x,y};
}
// One jittered sample per stratum; Q32 coordinates.
inline std::optional<std::vector<std::array<uint32_t,2>>> stratified2Q32(uint32_t w,uint32_t h,Pcg32& rng){
    if(!w||!h||uint64_t(w)*h>1000000)return {};
    std::vector<std::array<uint32_t,2>> out;out.reserve(size_t(w)*h);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
        auto a=((uint64_t(x)<<32)|rng.next())/w,b=((uint64_t(y)<<32)|rng.next())/h;
        out.push_back({uint32_t(a),uint32_t(b)});
    }return out;
}
// Floating distributions are NOT for cross-platform lockstep.
inline double randomOpenUnit(Pcg32& rng){return (double(rng.next())+.5)/4294967296.;}
inline std::optional<double> exponentialRandom(Pcg32& rng,double rate){
    if(!std::isfinite(rate)||rate<=0)return {};
    double x=-std::log(randomOpenUnit(rng))/rate;if(!std::isfinite(x))return {};return x;
}
inline std::optional<uint32_t> poissonRandom(Pcg32& rng,double lambda,size_t budget=4096){
    if(!std::isfinite(lambda)||lambda<0||lambda>64||!budget||budget>1000000)return {};
    if(lambda==0)return 0;
    double sum=0;
    for(size_t k=0;k<budget;++k){sum-=std::log(randomOpenUnit(rng));if(sum>lambda)return uint32_t(k);}
    return {};
}
inline double gaussianRandom(Pcg32& rng){
    return std::sqrt(-2*std::log(randomOpenUnit(rng)))*std::cos(6.2831853071795864769*randomOpenUnit(rng));
}
}
