// Logical GPU payload admission for the graph owner's optional texture set.
// The RHI's existing texture footprint estimate supplies bytes per image.
#pragma once
#include <algorithm>
#include <cstddef>

namespace aiverse::render::texture_graph::gpu {
inline constexpr size_t maxTextureRequestBytes=64u*1024u*1024u;
inline constexpr size_t maxTextureSetImages=6;
struct TextureRequestBudget {
    size_t textureBytes=0,validationBytes=0,dagBudget=0;
    constexpr explicit operator bool()const noexcept{return dagBudget!=0;}
};
// Reserve all candidate textures and one diagnostic readback staging buffer
// before admitting the DAG. Excludes CPU images and driver allocation padding.
inline constexpr TextureRequestBudget textureRequestBudget(size_t count,
        size_t imageBytes,size_t validationBytes,size_t materializedLimit,
        size_t totalLimit=maxTextureRequestBytes,bool fenceOnly=false)noexcept{
    if(!count||count>maxTextureSetImages||!imageBytes||(!fenceOnly&&!validationBytes)||(fenceOnly&&validationBytes)||
       !materializedLimit||!totalLimit||totalLimit>maxTextureRequestBytes||
       imageBytes>totalLimit/count)return {};
    const size_t textures=count*imageBytes;
    if(validationBytes>=totalLimit-textures)return {};
    return {textures,validationBytes,std::min(materializedLimit,totalLimit-textures-validationBytes)};
}
} // namespace aiverse::render::texture_graph::gpu
