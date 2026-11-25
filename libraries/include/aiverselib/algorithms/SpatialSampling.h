#pragma once
#include "RandomSampling.h"
#include <array>
#include <map>
#include <algorithm>
namespace aiverse::lib::algorithms {
template<size_t N> struct DiskSamples {std::vector<std::array<int32_t,N>> points;bool reachedTarget=false;};
// Uniform dart throwing on integer lattice with spatial buckets.
// Minimum Euclidean separation is exact; not guaranteed maximal or Bridson.
template<size_t N> inline std::optional<DiskSamples<N>> poissonDiskInteger(
    std::array<int32_t,N> extent,int32_t radius,size_t target,size_t attempts,Pcg32& rng){
    static_assert(N==2||N==3);
    if(radius<=0||radius>100000000||target>1000000||attempts>10000000)return {};
    for(auto e:extent)if(e<=0||e>100000000)return {};
    using P=std::array<int32_t,N>;
    std::map<P,std::vector<size_t>> buckets;DiskSamples<N> out;
    int64_t rr=int64_t(radius)*radius;
    for(size_t step=0;step<attempts&&out.points.size()<target;++step){
        P p{},cell{};
        for(size_t i=0;i<N;++i){p[i]=int32_t(*rng.below(extent[i]));cell[i]=p[i]/radius;}
        bool accepted=true;P neighbor{};
        auto visit=[&](auto&& self,size_t axis)->void{
            if(!accepted)return;
            if(axis==N){
                auto it=buckets.find(neighbor);if(it==buckets.end())return;
                for(auto index:it->second){int64_t d=0;for(size_t i=0;i<N;++i){int64_t delta=int64_t(p[i])-out.points[index][i];d+=delta*delta;}if(d<rr){accepted=false;break;}}
                return;
            }
            for(int offset=-1;offset<=1;++offset){neighbor[axis]=cell[axis]+offset;self(self,axis+1);}
        };
        visit(visit,0);
        if(accepted){buckets[cell].push_back(out.points.size());out.points.push_back(p);}
    }
    out.reachedTarget=out.points.size()==target;return out;
}
template<size_t N> inline std::optional<std::vector<std::array<uint32_t,N>>> latinHypercubeQ32(size_t count,Pcg32& rng){
    static_assert(N>=1&&N<=16);
    if(!count||count>1000000)return {};
    std::vector<std::array<uint32_t,N>> out(count);std::vector<size_t> order(count);
    for(size_t axis=0;axis<N;++axis){
        for(size_t i=0;i<count;++i)order[i]=i;
        randomShuffle<size_t>(std::span<size_t>(order),rng);
        for(size_t i=0;i<count;++i)out[i][axis]=uint32_t(((uint64_t(order[i])<<32)|rng.next())/count);
    }return out;
}
}
