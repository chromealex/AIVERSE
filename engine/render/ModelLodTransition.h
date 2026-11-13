#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace aiverse::render {
// Stateless screen-size crossfade: reversible, stable for moving instances and
// independent editor views. No persistent pointer-keyed per-instance history.
struct ModelLodBlend {
    int mesh = -1, incoming = -1;
    size_t level = 0;
    float progress = 0;
};
template<class Levels>
ModelLodBlend selectModelLod(int base, const Levels& levels, float pixels, float width) {
    ModelLodBlend result{base};
    if (!std::isfinite(pixels)) return result;
    for (size_t i=0;i<levels.size();++i)
        if(pixels<levels[i].maxPixels){result.mesh=levels[i].mesh;result.level=i+1;}
    if(!std::isfinite(width) || width<=0)return result;
    width=std::min(width,.49f);
    for(size_t i=0;i<levels.size();++i){
        const float threshold=levels[i].maxPixels;
        float upper=threshold*(1+width), lower=threshold*(1-width);
        // Close adjacent thresholds must not request three simultaneous meshes.
        if(i)upper=std::min(upper,(levels[i-1].maxPixels+threshold)*.5f);
        if(i+1<levels.size())lower=std::max(lower,(levels[i+1].maxPixels+threshold)*.5f);
        if(pixels>lower && pixels<upper){
            result.mesh=i?levels[i-1].mesh:base;result.incoming=levels[i].mesh;result.level=i;
            result.progress=(upper-pixels)/(upper-lower);return result;
        }
    }
    return result;
}
} // namespace aiverse::render
