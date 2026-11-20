// Resource-budget-only terrain shader fallback for GLES3/WebGL2 or limited
// Vulkan/GL devices. This is a genre module implementation: the common GI
// transport and renderer remain independent of RTS. Low-tier terrain keeps
// baked lightmap, vision/fog, traced height shadows and direct sun/points.
//
// Existing rich terrain uses tex0..20; this compact no-GI variant preserves
// tex0/1/2/3/15 but aliases unused low-tier material slots:
//   baked lightmap tex19 -> tex4
//   baked mask     tex20 -> tex5
//   traced height  tex18 -> tex6
// The shader body MUST compile with TERRAIN_TIER 0 and TERRAIN_DETAIL 0.
// Never apply to a medium/high shader; its material tex4..14 are live.
#pragma once
#include "RtsTerrainShaders.h"
#include <cctype>
#include <stdexcept>
#include <string>

namespace aiverse::game {

constexpr bool terrainNeedsCompactSamplers(int maxFragmentSlots) {
    return maxFragmentSlots < 21;
}
constexpr bool terrainCompactSupported(int maxFragmentSlots) {
    return maxFragmentSlots >= 16;
}

inline void terrainReplaceIdentifier(std::string& shader,
                                     const std::string& original,
                                     const std::string& replacement) {
    const auto ident=[](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c=='_';
    };
    size_t index=0;
    while((index=shader.find(original,index))!=std::string::npos) {
        const bool start=index==0 || !ident(shader[index-1]);
        const size_t end=index+original.size();
        const bool finish=end==shader.size() || !ident(shader[end]);
        if(start && finish) {
            shader.replace(index,original.size(),replacement);
            index+=replacement.size();
        }else index=end;
    }
}

inline std::string terrainCompactLowFragment() {
    std::string shader=shaders::kTerrainFS;
    // Three independent old names; none of the target names can match
    // another source name, so no accidental remapping chain occurs.
    terrainReplaceIdentifier(shader,"tex19","tex4");
    terrainReplaceIdentifier(shader,"tex20","tex5");
    terrainReplaceIdentifier(shader,"tex18","tex6");
    if(shader.find("tex19")!=std::string::npos ||
       shader.find("tex20")!=std::string::npos ||
       shader.find("tex18")!=std::string::npos)
        throw std::logic_error("low terrain compact sampler remap incomplete");
    return shader;
}

} // namespace aiverse::game
