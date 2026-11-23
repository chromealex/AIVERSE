#pragma once
#include "EvolveArea.h"
#include <map>
namespace aiverse::evolve {
// Immutable derived index; rebuild at the authoritative tick barrier.
// Large regions use a bounded overflow list instead of exploding grid memory.
class SpatialIndex {
    static constexpr I cellCm=3200;
    static constexpr I maxCellsPerRegion=64;
    using Cell=std::pair<I,I>;
    std::vector<Extent> zones_;
    std::map<Cell,std::vector<size_t>> cells_;
    std::vector<size_t> overflow_;
    static I cell(I x){return x/cellCm-(x%cellCm<0?1:0);}
public:
    explicit SpatialIndex(const World& world):zones_(extents(world)){
        for(size_t i=0;i<zones_.size();++i){
            const auto& z=zones_[i];
            const I x0=cell(z.minXcm),x1=cell(z.maxXcm-1);
            const I z0=cell(z.minZcm),z1=cell(z.maxZcm-1);
            if((x1-x0+1)*(z1-z0+1)>maxCellsPerRegion){overflow_.push_back(i);continue;}
            for(I x=x0;x<=x1;++x)for(I y=z0;y<=z1;++y)cells_[{x,y}].push_back(i);
        }
    }
    template<class Fn> void query(I x,I z,Fn&& fn)const{
        const auto it=cells_.find({cell(x),cell(z)});
        const auto visit=[&](size_t i){
            const auto& a=zones_[i];
            if(x>=a.minXcm&&x<a.maxXcm&&z>=a.minZcm&&z<a.maxZcm)fn(a);
        };
        if(it==cells_.end()){for(auto i:overflow_)visit(i);return;}
        const auto& local=it->second;
        size_t a=0,b=0;
        while(a<local.size()||b<overflow_.size()){
            if(b==overflow_.size()||(a<local.size()&&local[a]<overflow_[b]))visit(local[a++]);
            else visit(overflow_[b++]);
        }
    }
};
}
