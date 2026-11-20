// Genre-neutral, device-bound validation of a borrowed GI L1 probe atlas.
// Calls are presentation-only and must occur AFTER Renderer3D::beginScene.
// This intentionally does not assume a source is safe merely because its
// RHI handle is nonzero or the previous camera used the same dimensions.
#pragma once
#include "GiDynamicProbeTexture.h"
#include "GiClipmapValidityTexture.h"
#include "Math3D.h"
#include <cmath>
#include <cstddef>

namespace aiverse::render {

struct GiRendererOverlayPolicy {
    static bool validGeometry(float3 origin,float spacing,
                              int nx,int ny,int nz,int ringX,int ringY,int ringZ) {
        return std::isfinite(origin.x) && std::isfinite(origin.y) &&
               std::isfinite(origin.z) && std::isfinite(spacing) &&
               spacing>=.25f && spacing<=1000.f &&
               nx>=1 && ny>=1 && nz>=1 &&
               nx<=256 && ny<=256 && nz<=256 &&
               ringX>=0 && ringY>=0 && ringZ>=0 &&
               ringX<nx && ringY<ny && ringZ<nz;
    }

    static bool acceptBase(const gfx::Device& device,
                           const GiDynamicProbeTexture& atlas,
                           float3 origin,float spacing,
                           int nx,int ny,int nz,
                           int ringX=0,int ringY=0,int ringZ=0,
                           const GiClipmapValidityTexture* validity=nullptr,
                           bool qualityAllowed=true) {
        return qualityAllowed &&
               validGeometry(origin,spacing,nx,ny,nz,ringX,ringY,ringZ) &&
               atlas.texture().valid() && atlas.device()==&device &&
               device.supports(atlas.computeWritable()
                   ? gfx::PixelFormat::RGBA16F:gfx::PixelFormat::RGBA32F) &&
               size_t(nx)*size_t(ny)*size_t(nz)==atlas.probeCount() &&
               (!validity || (validity->ready() &&
                   validity->texture().valid() &&
                   validity->device()==&device &&
                   validity->size()==atlas.probeCount()));
    }

    static bool acceptMetalCascade(const gfx::Device& device,
                                   const GiDynamicProbeTexture& atlas,
                                   float3 origin,float spacing,
                                   int nx,int ny,int nz,
                                   int ringX=0,int ringY=0,int ringZ=0) {
        return device.info().backend==gfx::Backend::Metal &&
               validGeometry(origin,spacing,nx,ny,nz,ringX,ringY,ringZ) &&
               atlas.texture().valid() && atlas.device()==&device &&
               atlas.computeWritable() &&
               device.supports(gfx::PixelFormat::RGBA16F) &&
               size_t(nx)*size_t(ny)*size_t(nz)==atlas.probeCount();
    }
};

} // namespace aiverse::render
