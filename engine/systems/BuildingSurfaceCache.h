#pragma once
#include "BuildingSurfacePattern.h"
#include "BuildingCpuCache.h"
namespace aiverse::lib::systems {
class BuildingSurfaceCache {
public:
    using Lease=BuildingCpuCache<BuildingSurfacePixels>::Lease;
    explicit BuildingSurfaceCache(size_t budget=64u*1024u*1024u,size_t entries=256):cache_(budget,entries){}
    Lease get(std::string_view pattern,std::string_view material,uint32_t seed,BuildingSurfaceChannel channel,int resolution){
        if(pattern.size()>64||material.size()>256)return std::make_shared<const BuildingSurfacePixels>();
        BuildingCacheKey key;key.text("building-surface-v1");key.text(std::string(pattern));key.text(std::string(material));
        key.number(seed);key.number(uint8_t(channel));key.number(uint64_t(int64_t(resolution)));
        return cache_.get(key.take(),[&]{return generateBuildingSurface(pattern,material,seed,channel,resolution);},
            [](const auto& p){return !p.rgba.empty();},[](const auto& p){return sizeof(p)+p.rgba.capacity();});
    }
    void clear(){cache_.clear();}
    BuildingCacheStats stats()const{return cache_.stats();}
private:BuildingCpuCache<BuildingSurfacePixels> cache_;
};
}
