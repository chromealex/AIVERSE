// Opt-in CPU asset policy for small VT fallback textures.
// Use after publishing a matching immutable virtual page archive and BEFORE
// the first Model::uploadModel call. No GPU work occurs here.
// Conservatively rejects shared non-base/alpha-sensitive image consumers.
#pragma once

#include "Model.h"
#include "../gfx/VirtualTexturePageArchiveWriter.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>

namespace aiverse::render {

struct VirtualFallbackSavings {
    size_t originalBaseBytes = 0;
    size_t fallbackBaseBytes = 0;
    size_t fullResolutionAlphaBytes = 0; // R8 base level when preserveAlpha=true
};

// Changes only the image's upload policy. uploadModel() then creates the
// reduced version as Image::tex instead of the original source. Existing
// renderer/shadow/unvirtualized paths can still bind a valid tex1 fallback.
// No full-resolution texture is created for this image, so source VRAM is
// avoided rather than duplicated with a second fallback.
// Caller must ensure dynamic ModelDraw::materials overrides also use VT
// when referring to this source image.
inline bool configureModelVirtualFallback(
    Model& model,
    size_t imageIndex,
    const gfx::VirtualTextureArchiveVersion& publishedArchive,
    int maxDimension = 64,
    VirtualFallbackSavings* savings = nullptr,
    std::string* reason = nullptr,
    bool preserveAlpha = false) {
    if (savings) *savings = {};
    auto fail = [&](const char* error) {
        if (reason) *reason = error;
        return false;
    };
    if (reason) reason->clear();
    if (imageIndex >= model.images.size() ||
        !publishedArchive.valid() ||
        maxDimension < 4 || maxDimension > 1024)
        return fail("invalid image, archive or reduced fallback dimension");
    Image& image = model.images[imageIndex];
    if (image.tex.valid() || image.virtualAlphaMask.valid() ||
        image.borrowedTexture || image.textureLease ||
        image.virtualFallbackMaxDimension > 0)
        return fail("configure fallback before any cached or native GPU upload");
    if (image.width < 1 || image.height < 1 ||
        image.width != publishedArchive.width ||
        image.height != publishedArchive.height)
        return fail("virtual archive dimensions do not match source image");
    if (std::max(image.width,image.height) <= maxDimension)
        return fail("image is not larger than requested fallback dimension");

    const auto& source = image.sharedRgba ? *image.sharedRgba : image.rgba;
    const size_t w = size_t(image.width);
    const size_t h = size_t(image.height);
    if (w > std::numeric_limits<size_t>::max()/h ||
        w*h > std::numeric_limits<size_t>::max()/4u ||
        source.size() != w*h*4u)
        return fail("full RGBA8 CPU pixels must be retained until upload");

    bool usedAsBase = false;
    bool needsAlphaMask = false;
    for (const Material& material : model.materials) {
        const auto idx = int(imageIndex);
        if (material.mrTex == idx || material.normalTex == idx ||
            material.occlusionTex == idx || material.emissiveTex == idx ||
            material.teamTex == idx)
            return fail("image also supplies a non-base material texture");
        for (int graphIndex : material.graphTextures)
            if (graphIndex == idx)
                return fail("image is referenced by Shader Graph texture");
        if (material.baseTex == idx) {
            usedAsBase = true;
            if (material.alpha == AlphaMode::Mask)
                needsAlphaMask = true;
            if (alphaBlended(material.alpha) || material.transparent ||
                material.shaderGraph || material.team == TeamMode::BaseAlpha ||
                material.team == TeamMode::VertexAlpha ||
                material.baseColor.w < 0.999f)
                return fail("blended, graph or team-alpha material needs original base");
            if (material.alpha == AlphaMode::Mask && !preserveAlpha)
                return fail("alpha-masked base requires a full-resolution R8 alpha channel");
        }
    }
    if (!usedAsBase)
        return fail("image is not the base texture of a model material");
    if (preserveAlpha && !needsAlphaMask)
        return fail("full-resolution alpha allocation requested for non-masked image");

    int reducedW = image.width, reducedH = image.height;
    while (std::max(reducedW,reducedH) > maxDimension) {
        reducedW = std::max(1,(reducedW+1)/2);
        reducedH = std::max(1,(reducedH+1)/2);
    }
    if (savings) {
        // Base-level bytes only. Driver mip allocation, block formats,
        // row alignment and shared texture cache are not measured here.
        savings->originalBaseBytes = w*h*4u;
        savings->fallbackBaseBytes = size_t(reducedW)*size_t(reducedH)*4u;
        savings->fullResolutionAlphaBytes = preserveAlpha ? w*h : 0u;
    }
    image.virtualFallbackMaxDimension = maxDimension;
    image.virtualFallbackPreserveAlpha = preserveAlpha;
    return true;
}

inline bool configureModelVirtualMaskedFallback(
    Model& model, size_t imageIndex,
    const gfx::VirtualTextureArchiveVersion& publishedArchive,
    int maxDimension = 64,
    VirtualFallbackSavings* savings = nullptr,
    std::string* reason = nullptr) {
    return configureModelVirtualFallback(model,imageIndex,publishedArchive,
                                         maxDimension,savings,reason,true);
}

} // namespace aiverse::render
