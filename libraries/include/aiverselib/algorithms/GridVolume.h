#pragma once
#include "../Grid.h"
#include <algorithm>
namespace aiverse::lib {
struct GridVolume {size_t width,height,depth;
    bool valid(size_t n)const{return width&&height&&depth&&width<=4096&&height<=4096&&depth<=4096&&width*height<=1048576&&depth<=1048576/(width*height)&&n==width*height*depth;}
};
enum class VolumeConnectivity {Six,Eighteen,TwentySix};
template<class Fn> void volumeNeighbors(GridVolume g,size_t u,VolumeConnectivity mode,Fn fn){
    const int x=int(u%g.width),y=int(u/g.width%g.height),z=int(u/(g.width*g.height));
    for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        int axes=(dx!=0)+(dy!=0)+(dz!=0);
        if(!axes||(mode==VolumeConnectivity::Six&&axes>1)||(mode==VolumeConnectivity::Eighteen&&axes>2))continue;
        int a=x+dx,b=y+dy,c=z+dz;
        if(a>=0&&b>=0&&c>=0&&a<int(g.width)&&b<int(g.height)&&c<int(g.depth))
            fn((size_t(c)*g.height+size_t(b))*g.width+size_t(a));
    }
}
inline std::optional<std::vector<int32_t>> volumeDistance(GridVolume g,std::span<const uint8_t> mask,std::span<const size_t> seeds,VolumeConnectivity mode=VolumeConnectivity::Six){
    if(!g.valid(mask.size()))return {};
    for(auto s:seeds)if(s>=mask.size())return {};
    std::vector<int32_t> d(mask.size(),-1);std::vector<size_t> q;
    for(auto s:seeds)if(mask[s]&&d[s]<0){d[s]=0;q.push_back(s);}
    for(size_t i=0;i<q.size();++i)volumeNeighbors(g,q[i],mode,[&](size_t v){if(mask[v]&&d[v]<0){d[v]=d[q[i]]+1;q.push_back(v);}});
    return d;
}
inline std::optional<std::vector<size_t>> volumeFloodFill(GridVolume g,std::span<const uint8_t> mask,size_t seed,VolumeConnectivity mode=VolumeConnectivity::Six){
    auto d=volumeDistance(g,mask,std::span<const size_t>(&seed,1),mode);if(!d)return {};
    std::vector<size_t> out;for(size_t i=0;i<d->size();++i)if((*d)[i]>=0)out.push_back(i);return out;
}
inline std::optional<std::vector<int32_t>> volumeComponents(GridVolume g,std::span<const uint8_t> mask,VolumeConnectivity mode=VolumeConnectivity::Six){
    if(!g.valid(mask.size()))return {};
    std::vector<int32_t> labels(mask.size(),-1);std::vector<size_t> q;
    for(size_t root=0;root<mask.size();++root){
        if(!mask[root]||labels[root]>=0)continue;
        q.clear();q.push_back(root);labels[root]=int32_t(root);
        for(size_t i=0;i<q.size();++i)volumeNeighbors(g,q[i],mode,[&](size_t v){if(mask[v]&&labels[v]<0){labels[v]=int32_t(root);q.push_back(v);}});
    }return labels;
}
}
