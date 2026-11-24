#pragma once
#include "SpatialSampling.h"
namespace aiverse::lib::algorithms {
// Bridson-style active list with integer shell rejection, not maximal sampling.
template<size_t N> inline std::optional<DiskSamples<N>> activeDiskInteger(std::array<int32_t,N> extent,int32_t radius,size_t target,size_t attempts,Pcg32& rng,size_t candidates=30){
    static_assert(N==2||N==3);
    if(radius<=0||radius>100000000||target>1000000||attempts>10000000||!candidates||candidates>1024)return {};
    for(auto e:extent)if(e<=0||e>100000000)return {};
    using P=std::array<int32_t,N>;DiskSamples<N> out;
    if(!target){out.reachedTarget=true;return out;}if(!attempts)return out;
    std::map<P,std::vector<size_t>> buckets;std::vector<size_t> active;
    auto insert=[&](P p){P cell{};for(size_t i=0;i<N;++i)cell[i]=p[i]/radius;buckets[cell].push_back(out.points.size());active.push_back(out.points.size());out.points.push_back(p);};
    P first{};for(size_t i=0;i<N;++i)first[i]=int32_t(*rng.below(extent[i]));insert(first);--attempts;
    int64_t rr=int64_t(radius)*radius;
    while(!active.empty()&&attempts&&out.points.size()<target){
        auto index=size_t(*rng.below(active.size()));auto center=out.points[active[index]];bool found=false;
        for(size_t k=0;k<candidates&&attempts;++k){
            --attempts;P p{},cell{},neighbor{};int64_t distance=0;bool valid=true;
            for(size_t i=0;i<N;++i){auto delta=int64_t(*rng.below(uint64_t(radius)*4+1))-int64_t(radius)*2;distance+=delta*delta;auto x=int64_t(center[i])+delta;if(x<0||x>=extent[i])valid=false;else{p[i]=int32_t(x);cell[i]=p[i]/radius;}}
            if(!valid||distance<rr||distance>4*rr)continue;
            auto visit=[&](auto&& self,size_t axis)->void{
                if(!valid)return;
                if(axis==N){auto it=buckets.find(neighbor);if(it==buckets.end())return;for(auto j:it->second){int64_t d=0;for(size_t i=0;i<N;++i){auto x=int64_t(p[i])-out.points[j][i];d+=x*x;}if(d<rr){valid=false;break;}}return;}
                for(int o=-1;o<=1;++o){neighbor[axis]=cell[axis]+o;self(self,axis+1);}
            };visit(visit,0);
            if(valid){insert(p);found=true;break;}
        }
        if(!found){active[index]=active.back();active.pop_back();}
    }
    out.reachedTarget=out.points.size()==target;return out;
}
struct ImportanceSample {size_t index;uint64_t weight,total;};
class DiscreteImportance {
    IntegerAliasTable alias_;std::vector<uint64_t> weights_;uint64_t total_=0;
public:
    bool build(std::span<const uint64_t> weights){
        IntegerAliasTable next;if(!next.build(weights))return false;
        std::vector<uint64_t> copy(weights.begin(),weights.end());uint64_t sum=0;for(auto w:copy)sum+=w;
        alias_=std::move(next);weights_.swap(copy);total_=sum;return true;
    }
    std::optional<ImportanceSample> sample(Pcg32& rng)const{
        auto i=alias_.sample(rng);if(!i)return {};return ImportanceSample{*i,weights_[*i],total_};
    }
};
}
