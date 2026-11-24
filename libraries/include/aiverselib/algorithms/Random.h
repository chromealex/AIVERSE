#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <limits>
namespace aiverse::lib::algorithms {
inline uint64_t randomMix(uint64_t x){x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
inline uint64_t deriveSeed(uint64_t root,uint64_t domain,uint64_t key){return randomMix(randomMix(root+0x9e3779b97f4a7c15ull)^randomMix(domain)^randomMix(key+0xd1b54a32d192ed03ull));}
struct Pcg32 {
    uint64_t state=0,increment=1;
    explicit Pcg32(uint64_t seed=0,uint64_t stream=0){increment=(stream<<1)|1;next();state+=seed;next();}
    uint32_t next(){auto old=state;state=old*6364136223846793005ull+increment;auto x=uint32_t(((old>>18)^old)>>27);auto r=uint32_t(old>>59);return (x>>r)|(x<<((-r)&31));}
    uint64_t next64(){auto hi=next();return (uint64_t(hi)<<32)|next();}
    std::optional<uint64_t> below(uint64_t n){if(!n)return {};auto threshold=(uint64_t(0)-n)%n;for(;;){auto r=next64();if(r>=threshold)return r%n;}}
    void advance(uint64_t delta){
        uint64_t mul=6364136223846793005ull,add=increment,am=1,aa=0;
        while(delta){if(delta&1){am*=mul;aa=aa*mul+add;}add=(mul+1)*add;mul*=mul;delta>>=1;}
        state=am*state+aa;
    }
};
template<class T> inline void randomShuffle(std::span<T> values,Pcg32& rng){
    for(size_t i=values.size();i>1;--i)std::swap(values[i-1],values[size_t(*rng.below(i))]);
}
inline std::optional<size_t> weightedRandom(std::span<const uint64_t> weights,Pcg32& rng){
    uint64_t total=0;for(auto w:weights){if(w>std::numeric_limits<uint64_t>::max()-total)return {};total+=w;}
    auto r=rng.below(total);if(!r)return {};
    for(size_t i=0;i<weights.size();++i){if(*r<weights[i])return i;*r-=weights[i];}return {};
}
}
