#pragma once
#include "SceneAssets.h"
#include "engine/render/GiChunkedGeometry.h"
namespace aiverse::bench {
// One immutable geometry source shared by direct shadows and indirect transport.
struct AnimatedLightingGeometry {
    render::GiChunkedGeometryBuilder builder;
    std::shared_ptr<const render::GiChunkedGeometrySnapshot> scene;
    uint64_t revision=1;
    bool enabled=false;
    float previousTime=std::numeric_limits<float>::quiet_NaN();
    void initialize(const render::StaticShadowGeometry& base,const SceneAssets& assets) {
        for(const auto& asset:assets.assets)enabled |= asset.animated && asset.lightingCaster;
        if(!enabled)return;
        constexpr size_t size=65536;
        for(size_t first=0;first<base.triangles.size();first+=size) {
            const auto end=std::min(first+size,base.triangles.size());
            builder.replace(first/size+1,{base.triangles.begin()+first,base.triangles.begin()+end},base.albedos);
        }
    }
    bool update(const SceneAssets& assets,float time) {
        if(!enabled || time==previousTime)return false;
        // Build a complete candidate before publishing; failures retain the old
        // immutable snapshot, and the host refuses to render that failed view.
        auto next=builder;
        for(size_t i=0;i<assets.assets.size();++i) {
            const auto& asset=assets.assets[i];if(!asset.animated||!asset.lightingCaster)continue;
            auto geometry=render::giModelGeometry(*asset.model,asset.transform,&asset.pose);
            next.replace((uint64_t(1)<<63)+i,std::move(geometry.triangles),std::move(geometry.albedos));
        }
        auto snapshot=std::make_shared<render::GiChunkedGeometrySnapshot>(next.publish());
        builder=std::move(next);scene=std::move(snapshot);previousTime=time;++revision;return true;
    }
};
}
