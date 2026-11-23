#pragma once
#include "../Grid.h"
#include <algorithm>
#include <limits>
namespace aiverse::lib {
inline constexpr int64_t kGridInfiniteDistance=std::numeric_limits<int64_t>::max();
// Exact integer squared Euclidean distance, separable lower envelope.
inline std::optional<std::vector<int64_t>> gridSquaredDistance(size_t w,size_t h,std::span<const uint8_t> seeds){
    if(!validGrid(w,h,seeds.size()))return {};
    std::vector<int64_t> field(seeds.size(),kGridInfiniteDistance);
    for(size_t i=0;i<seeds.size();++i)if(seeds[i])field[i]=0;
    std::vector<int64_t> f(std::max(w,h)),d(f.size()),start(f.size());
    std::vector<size_t> sites(f.size());
    auto transform=[&](size_t n){
        size_t count=0;
        for(size_t q=0;q<n;++q){
            if(f[q]==kGridInfiniteDistance)continue;
            int64_t boundary=0;
            while(count){
                auto p=sites[count-1];auto x=int64_t(q),y=int64_t(p);
                auto num=f[q]+x*x-f[p]-y*y,den=2*(x-y);
                boundary=num/den+(num%den>0);
                if(boundary>start[count-1])break;
                --count;
            }
            if(!count)boundary=0;
            if(boundary>=int64_t(n))continue;
            sites[count]=q;start[count]=boundary;++count;
        }
        size_t k=0;
        for(size_t x=0;x<n;++x){
            if(!count){d[x]=kGridInfiniteDistance;continue;}
            while(k+1<count&&start[k+1]<=int64_t(x))++k;
            auto delta=int64_t(x)-int64_t(sites[k]);d[x]=f[sites[k]]+delta*delta;
        }
    };
    for(size_t y=0;y<h;++y){for(size_t x=0;x<w;++x)f[x]=field[y*w+x];transform(w);for(size_t x=0;x<w;++x)field[y*w+x]=d[x];}
    for(size_t x=0;x<w;++x){for(size_t y=0;y<h;++y)f[y]=field[y*w+x];transform(h);for(size_t y=0;y<h;++y)field[y*w+x]=d[y];}
    return field;
}
// Negative inside, positive outside; magnitude is squared distance to opposite class.
inline std::optional<std::vector<int64_t>> gridSignedSquaredDistance(size_t w,size_t h,std::span<const uint8_t> mask){
    if(!validGrid(w,h,mask.size()))return {};
    std::vector<uint8_t> inverse(mask.size());for(size_t i=0;i<mask.size();++i)inverse[i]=!mask[i];
    auto a=gridSquaredDistance(w,h,mask),b=gridSquaredDistance(w,h,inverse);
    for(size_t i=0;i<mask.size();++i)if(mask[i])(*a)[i]=-(*b)[i];
    return a;
}
// Max field propagation, nonnegative integer attenuation per edge, blocked cells zero.
inline std::optional<std::vector<uint32_t>> gridPropagate(size_t w,size_t h,std::span<const uint8_t> passable,std::span<const uint32_t> source,uint32_t loss,size_t iterations,GridConnectivity mode=GridConnectivity::Four){
    if(!validGrid(w,h,passable.size())||source.size()!=passable.size()||iterations>256)return {};
    std::vector<uint32_t> a(source.begin(),source.end()),b(a.size());
    for(size_t i=0;i<a.size();++i)if(!passable[i])a[i]=0;
    for(size_t pass=0;pass<iterations;++pass){
        for(size_t i=0;i<a.size();++i){b[i]=a[i];if(!passable[i])continue;
            gridNeighbors(w,h,i,mode,[&](size_t v){b[i]=std::max(b[i],a[v]>loss?a[v]-loss:0u);});}
        if(a==b)break;a.swap(b);
    }return a;
}
}
