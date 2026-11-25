#pragma once
#include "Random.h"
#include <vector>
#include <bit>
#include <cstddef>
namespace aiverse::lib::algorithms {
struct Xoroshiro128Plus {
    uint64_t s0,s1;
    explicit Xoroshiro128Plus(uint64_t seed):s0(randomMix(seed)),s1(randomMix(seed+0x9e3779b97f4a7c15ull)){if(!(s0|s1))s1=1;}
    uint64_t next(){auto a=s0,b=s1,result=a+b;b^=a;s0=std::rotl(a,24)^b^(b<<16);s1=std::rotl(b,37);return result;}
};
class IntegerAliasTable {
    uint64_t total_=0;
    std::vector<uint64_t> threshold_;
    std::vector<size_t> alias_;
public:
    bool build(std::span<const uint64_t> weights){
        size_t n=weights.size();if(!n||n>1000000)return false;
        uint64_t total=0;
        for(auto w:weights){if(w>std::numeric_limits<uint64_t>::max()-total)return false;total+=w;}
        if(!total||total>std::numeric_limits<uint64_t>::max()/n)return false;
        std::vector<uint64_t> scaled(n),threshold(n,total);
        std::vector<size_t> alias(n),small,large;
        for(size_t i=0;i<n;++i){alias[i]=i;scaled[i]=weights[i]*n;(scaled[i]<total?small:large).push_back(i);}
        while(!small.empty()&&!large.empty()){
            auto s=small.back(),l=large.back();small.pop_back();large.pop_back();
            threshold[s]=scaled[s];alias[s]=l;
            scaled[l]-=total-scaled[s];(scaled[l]<total?small:large).push_back(l);
        }
        threshold_.swap(threshold);alias_.swap(alias);total_=total;return true;
    }
    std::optional<size_t> sample(Pcg32& rng)const{
        if(!total_)return {};
        auto i=size_t(*rng.below(alias_.size()));return *rng.below(total_)<threshold_[i]?i:alias_[i];
    }
    size_t size()const{return alias_.size();}
};
inline std::optional<std::vector<size_t>> reservoirIndices(size_t count,size_t k,Pcg32& rng){
    if(k>count||k>1000000||count>100000000)return {};
    std::vector<size_t> out(k);for(size_t i=0;i<k;++i)out[i]=i;
    if(!k)return out;
    for(size_t i=k;i<count;++i){auto j=*rng.below(i+1);if(j<k)out[size_t(j)]=i;}
    return out;
}
// Approximate standard normal in Q16 using sum of 12 discrete uniforms.
inline int32_t approximateNormalQ16(Pcg32& rng){
    int32_t sum=0;for(int i=0;i<12;++i)sum+=int32_t(rng.next()>>16);
    return sum-393210;
}
}
