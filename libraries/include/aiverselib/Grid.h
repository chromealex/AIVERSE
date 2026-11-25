#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib {
// Row-major, zero-based IDs. Four neighbors; -1 means blocked/unreachable.
inline std::optional<std::vector<std::int32_t>> gridDistance(
    std::size_t width,std::size_t height,std::span<const std::uint8_t> passable,
    std::span<const std::size_t> seeds) {
    if (!width || !height || width>4096 || height>4096 || width*height>262144 || passable.size()!=width*height) return std::nullopt;
    const auto count=width*height;
    for (auto seed:seeds) if (seed>=count) return std::nullopt;
    std::vector<std::int32_t> distance(count,-1); std::vector<std::size_t> queue;
    for (auto seed:seeds) if (passable[seed] && distance[seed]<0) { distance[seed]=0; queue.push_back(seed); }
    for (std::size_t head=0;head<queue.size();++head) {
        auto u=queue[head];
        auto visit=[&](std::size_t v){ if (passable[v] && distance[v]<0) { distance[v]=distance[u]+1; queue.push_back(v); } };
        if (u%width>0) visit(u-1);
        if (u%width+1<width) visit(u+1);
        if (u>=width) visit(u-width);
        if (u<count-width) visit(u+width);
    }
    return distance;
}

enum class GridConnectivity {Four,Eight};
inline bool validGrid(std::size_t w,std::size_t h,std::size_t n){
    return w&&h&&w<=4096&&h<=4096&&w*h<=262144&&n==w*h;
}
template<class Fn> inline void gridNeighbors(std::size_t w,std::size_t h,std::size_t u,GridConnectivity mode,Fn fn){
    auto x=int(u%w),y=int(u/w);
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        if((!dx&&!dy)||(mode==GridConnectivity::Four&&dx&&dy))continue;
        int nx=x+dx,ny=y+dy;
        if(nx>=0&&ny>=0&&nx<int(w)&&ny<int(h))fn(std::size_t(ny)*w+std::size_t(nx));
    }
}
inline std::optional<std::vector<int32_t>> gridComponents(std::size_t w,std::size_t h,std::span<const uint8_t> mask,GridConnectivity mode=GridConnectivity::Four){
    if(!validGrid(w,h,mask.size()))return {};
    std::vector<int32_t> labels(mask.size(),-1);std::vector<std::size_t> q;
    for(std::size_t root=0;root<mask.size();++root){
        if(!mask[root]||labels[root]>=0)continue;
        q.clear();q.push_back(root);labels[root]=int32_t(root);
        for(std::size_t i=0;i<q.size();++i)gridNeighbors(w,h,q[i],mode,[&](auto v){if(mask[v]&&labels[v]<0){labels[v]=int32_t(root);q.push_back(v);}});
    }return labels;
}
inline std::optional<std::vector<std::size_t>> gridFloodFill(std::size_t w,std::size_t h,std::span<const uint8_t> mask,std::size_t seed,GridConnectivity mode=GridConnectivity::Four){
    if(!validGrid(w,h,mask.size())||seed>=mask.size())return {};
    std::vector<std::size_t> q;if(!mask[seed])return q;
    std::vector<uint8_t> seen(mask.size());seen[seed]=1;q.push_back(seed);
    for(std::size_t i=0;i<q.size();++i)gridNeighbors(w,h,q[i],mode,[&](auto v){if(mask[v]&&!seen[v]){seen[v]=1;q.push_back(v);}});
    return q;
}
enum class GridMorphology {Dilate,Erode};
inline std::optional<std::vector<uint8_t>> gridMorphology(std::size_t w,std::size_t h,std::span<const uint8_t> mask,GridMorphology op,std::size_t iterations=1,GridConnectivity mode=GridConnectivity::Four){
    if(!validGrid(w,h,mask.size())||iterations>256)return {};
    std::vector<uint8_t> a(mask.size()),b(mask.size());
    for(std::size_t i=0;i<a.size();++i)a[i]=mask[i]!=0;
    for(std::size_t pass=0;pass<iterations;++pass){
        for(std::size_t u=0;u<a.size();++u){
            bool value=a[u]!=0;
            if(op==GridMorphology::Erode&&(u%w==0||u%w==w-1||u/w==0||u/w==h-1))value=false;
            gridNeighbors(w,h,u,mode,[&](auto v){value=op==GridMorphology::Dilate?(value||a[v]):(value&&a[v]);});
            b[u]=value;
        }a.swap(b);
    }return a;
}
inline std::optional<std::vector<uint8_t>> gridBoundary(std::size_t w,std::size_t h,std::span<const uint8_t> mask,GridConnectivity mode=GridConnectivity::Four){
    auto eroded=gridMorphology(w,h,mask,GridMorphology::Erode,1,mode);if(!eroded)return {};
    for(std::size_t i=0;i<mask.size();++i)(*eroded)[i]=mask[i]&&!(*eroded)[i];return eroded;
}
}
