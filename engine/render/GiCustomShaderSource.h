// Shader-source helpers for custom PBR terrain, foliage and procedural meshes.
// The caller builds the custom fragment shader and selects its GI permutation.
// Source remapping is deliberately limited to the GI snippets: never rewrite
// an existing terrain/foliage material's tex13..20 declarations or samples.
// GLSL snippets are shader bodies (RHI injects the GLSL #version); MSL helpers
// return extra fragment arguments and call expressions for the same bindings.
#pragma once
#include "GiCustomShaderPolicy.h"
#include "GiDynamicShaders.h"
#include "GiCascadeShaders.h"
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::render {

inline std::string giCustomReplaceIdentifier(std::string source,
                                             const std::string& from,
                                             const std::string& to) {
    auto ident=[](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c=='_';
    };
    size_t pos=0;
    while ((pos=source.find(from,pos))!=std::string::npos) {
        const bool left=pos==0 || !ident(source[pos-1]);
        const size_t end=pos+from.size();
        const bool right=end==source.size() || !ident(source[end]);
        if(left && right) {
            source.replace(pos,from.size(),to);
            pos+=to.size();
        } else pos=end;
    }
    return source;
}

inline std::string giCustomShaderDefines(GiCustomShaderMode mode) {
    if(giCustomModeTextureCount(mode)==0) return {};
    std::string result="#define DYNAMIC_GI 1\n";
    if(giCustomModeUsesValidity(mode))
        result+="#define DYNAMIC_GI_VALIDITY 1\n";
    if(giCustomModeUsesFar(mode))
        result+="#define DYNAMIC_GI_CASCADES 1\n";
    if(giCustomModeUsesThird(mode))
        result+="#define DYNAMIC_GI_TRIPLE 1\n";
    return result;
}

inline std::string giCustomGlslFunctions(GiCustomShaderMode mode,int firstSlot) {
    if(mode==GiCustomShaderMode::Off) return {};
    if(mode==GiCustomShaderMode::TripleCascade || firstSlot<0)
        throw std::invalid_argument("custom GLSL GI mode or first texture slot invalid");
    std::string source=giCustomShaderDefines(mode);
    source+=AIVERSE_GLSL_DYNAMIC_GI;
    if(giCustomModeUsesFar(mode))source+=AIVERSE_GLSL_CASCADE_GI;
    // Rewrite ONLY generated GI declarations/functions, never the host shader.
    // Two passes prevent alias chains: remapping 13->14 must not cause the
    // newly produced tex14 to be renamed again to 15.
    source=giCustomReplaceIdentifier(std::move(source),"tex13","AIVERSE_GI_FINE_ATLAS");
    source=giCustomReplaceIdentifier(std::move(source),"tex14","AIVERSE_GI_FINE_MASK");
    source=giCustomReplaceIdentifier(std::move(source),"tex15","AIVERSE_GI_FAR_ATLAS");
    source=giCustomReplaceIdentifier(std::move(source),"AIVERSE_GI_FINE_ATLAS",
                                     "tex"+std::to_string(firstSlot));
    source=giCustomReplaceIdentifier(std::move(source),"AIVERSE_GI_FINE_MASK",
                                     "tex"+std::to_string(firstSlot+1));
    source=giCustomReplaceIdentifier(std::move(source),"AIVERSE_GI_FAR_ATLAS",
                                     "tex"+std::to_string(firstSlot+2));
    return source;
}

inline std::string giCustomGlslExpression(GiCustomShaderMode mode,
                                         const std::string& position,
                                         const std::string& normal) {
    if(mode==GiCustomShaderMode::Single || mode==GiCustomShaderMode::Validity)
        return "dynamicGiAt("+position+","+normal+")";
    if(mode==GiCustomShaderMode::DualCascade)
        return "dynamicGiCascadedAt("+position+","+normal+")";
    throw std::invalid_argument("unsupported custom GLSL GI expression mode");
}

inline std::string giCustomMslFunctions(GiCustomShaderMode mode) {
    if(mode==GiCustomShaderMode::Off)return {};
    std::string source=giCustomShaderDefines(mode);
    source+=AIVERSE_MSL_DYNAMIC_GI;
    if(giCustomModeUsesFar(mode))source+=AIVERSE_MSL_CASCADE_GI;
    return source;
}

// Append these extra arguments to a first-party MSL fragment entrypoint.
// Metal GI texture reads use .read() without hardware sampler allocations.
inline std::string giCustomMslFragmentArguments(GiCustomShaderMode mode,
                                                int firstSlot) {
    const int count=giCustomModeTextureCount(mode);
    if(!count) return {};
    if(firstSlot<0)throw std::invalid_argument("negative custom GI first texture slot");
    auto texture=[&](const char* name,int slot) {
        return ", texture2d<float> "+std::string(name)+" [[texture("+
               std::to_string(slot)+")]]";
    };
    std::string args=", constant GiDynamicU& giFine [[buffer(7)]]";
    args+=texture("giFineAtlas",firstSlot);
    if(giCustomModeUsesValidity(mode))
        args+=texture("giFineMask",firstSlot+1);
    if(giCustomModeUsesFar(mode)) {
        args+=", constant GiDynamicU& giFar [[buffer(8)]]";
        args+=texture("giFarAtlas",firstSlot+2);
    }
    if(giCustomModeUsesThird(mode)) {
        args+=", constant GiDynamicU& giThird [[buffer(9)]]";
        args+=texture("giThirdAtlas",firstSlot+3);
    }
    return args;
}

inline std::string giCustomMslExpression(GiCustomShaderMode mode,
                                        const std::string& position,
                                        const std::string& normal) {
    if(mode==GiCustomShaderMode::Single)
        return "dynamicGiAt(giFineAtlas,giFine,"+position+","+normal+")";
    if(mode==GiCustomShaderMode::Validity)
        return "dynamicGiAt(giFineAtlas,giFine,giFineMask,"+position+","+normal+")";
    if(mode==GiCustomShaderMode::DualCascade)
        return "dynamicGiCascadedAt(giFineAtlas,giFineMask,giFarAtlas,giFine,giFar,"+
               position+","+normal+")";
    if(mode==GiCustomShaderMode::TripleCascade)
        return "dynamicGiTripleAt(giFineAtlas,giFineMask,giFarAtlas,giThirdAtlas,giFine,giFar,giThird,"+
               position+","+normal+")";
    throw std::invalid_argument("unsupported custom MSL GI expression mode");
}

} // namespace aiverse::render
