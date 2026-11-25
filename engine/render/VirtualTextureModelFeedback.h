// Ready-to-bind rigid ModelVertex VT feedback material adapter.
// Entirely engine-side: no RTS/FPS/Terra dependencies. A material resolver
// maps models to virtual page ranges, while non-VT materials write zero IDs
// and still populate feedback depth for correct occlusion.
#pragma once
#include "Renderer3D.h"
#include "../gfx/VirtualTextureFeedbackProgram.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace aiverse::render {

struct VirtualTextureMaterialMapping {
    // Zero tile counts explicitly mean this material has no VT pages.
    int baseTilesX = 0, baseTilesY = 0;
    int layer = 0, mipLevels = 1, explicitMip = 0;
    float texelWidth = 1, texelHeight = 1, mipBias = 0;
    float uvScaleX = 1, uvScaleY = 1;
    float uvOffsetX = 0, uvOffsetY = 0;
    gfx::SamplerAddressMode addressU = gfx::SamplerAddressMode::ClampToEdge;
    gfx::SamplerAddressMode addressV = gfx::SamplerAddressMode::ClampToEdge;
};
using VirtualTextureMaterialResolver = std::function<bool(
    const Model&, const Material&, VirtualTextureMaterialMapping&)>;

// Includes opaque, masked, LOD-fading and masked+LOD-fading pipeline
// variants, each in single/double-sided cull states. Handles resources
// explicitly: disconnect() before Renderer3D or this object is destroyed,
// shutdown() before destroying gfx::Device.
class VirtualTextureModelFeedback {
public:
    bool initialize(gfx::Device& dev, bool depthAttachment) {
        if (device_) return false;
        device_ = &dev;
        const uint8_t white[4] = {255,255,255,255};
        gfx::TextureDesc whiteDesc;
        whiteDesc.width = whiteDesc.height = 1;
        whiteDesc.format = gfx::PixelFormat::RGBA8;
        whiteDesc.filter = gfx::Filter::Nearest;
        whiteDesc.data = white;
        whiteDesc.label = "VT feedback alpha fallback";
        ownedWhite_ = dev.createTexture(whiteDesc);
        if (!ownedWhite_.valid()) { shutdown(); return false; }
        gfx::VirtualTextureFeedbackVertexLayout vertex;
        vertex.stride = sizeof(ModelVertex);
        vertex.positionOffset = offsetof(ModelVertex,pos);
        vertex.uvOffset = offsetof(ModelVertex,uv);
        vertex.indexType = gfx::IndexType::UInt32;
        for (int masked=0;masked<2;++masked)
            for (int fading=0;fading<2;++fading)
                for (int twoSided=0;twoSided<2;++twoSided) {
                    vertex.cull = twoSided ?
                        gfx::CullMode::None : gfx::CullMode::Back;
                    if (!programs_[masked][fading][twoSided].initialize(
                            dev,masked!=0,fading!=0,depthAttachment,vertex)) {
                        shutdown();
                        return false;
                    }
                }
        // R8 cutouts read the red channel, not the alpha channel (R8
        // sampled alpha defaults to 1). Keep original RGBA alpha programs
        // for non-virtualized and non-R8 masked models.
        for (int fading=0;fading<2;++fading)
            for (int twoSided=0;twoSided<2;++twoSided) {
                vertex.cull=twoSided ?
                    gfx::CullMode::None : gfx::CullMode::Back;
                if (!redMaskPrograms_[fading][twoSided].initialize(
                        dev,true,fading!=0,depthAttachment,vertex,
                        true,true)) {
                    shutdown();
                    return false;
                }
            }
        return true;
    }

    bool ready() const {
        return device_ && programs_[0][0][0].ready();
    }

    // Configures all rigid model feedback variants on this renderer.
    // To activate automatic scheduling, also call:
    // renderer.setVirtualTextureFeedback(&pass, {}, {});
    // A non-VT model still draws into depth using a zero feedback ID.
    bool connect(Renderer3D& renderer,
                 VirtualTextureMaterialResolver resolve,
                 gfx::TextureHandle fallbackWhite = {}) {
        if (!ready() || attached_) return false;
        renderer_ = &renderer;
        resolve_ = std::move(resolve);
        fallbackWhite_ = fallbackWhite.valid() ? fallbackWhite : ownedWhite_;
        renderer.setVirtualTextureModelFeedback(
            programs_[0][0][0].pipeline(),
            [this](gfx::Device& dev,const Model& model,
                   const Primitive&,const Material& material,const mat4& world) {
                return drawMaterial(dev,model,material,world,false,false,
                                    1.0f,false,{});
            });
        renderer.setVirtualTextureMaskedModelFeedback(
            programs_[1][0][0].pipeline(),
            [this](gfx::Device& dev,const Model& model,
                   const Primitive&,const Material& material,const mat4& world) {
                return drawMaterial(dev,model,material,world,true,false,
                                    1.0f,false,{});
            });
        renderer.setVirtualTextureLodModelFeedback(
            programs_[0][1][0].pipeline(),
            [this](gfx::Device& dev,const Model& model,
                   const Primitive&,const Material& material,const mat4& world,
                   float threshold,bool invert,
                   const Renderer3D::FeedbackPixelGrid& grid) {
                return drawMaterial(dev,model,material,world,false,true,
                                    threshold,invert,grid);
            });
        renderer.setVirtualTextureMaskedLodModelFeedback(
            programs_[1][1][0].pipeline(),
            [this](gfx::Device& dev,const Model& model,
                   const Primitive&,const Material& material,const mat4& world,
                   float threshold,bool invert,
                   const Renderer3D::FeedbackPixelGrid& grid) {
                return drawMaterial(dev,model,material,world,true,true,
                                    threshold,invert,grid);
            });
        attached_ = true;
        return true;
    }

    // Explicitly detach before releasing either Renderer3D or this binder.
    void disconnect() {
        if (attached_ && renderer_) renderer_->clearVirtualTextureModelFeedback();
        renderer_ = nullptr;
        attached_ = false;
        resolve_ = {};
        fallbackWhite_ = {};
    }

    void shutdown() {
        disconnect();
        if (!device_) return;
        for (auto& masks : programs_)
            for (auto& fades : masks)
                for (auto& program : fades)
                    program.shutdown(*device_);
        for (auto& fades : redMaskPrograms_)
            for (auto& program : fades)
                program.shutdown(*device_);
        if (ownedWhite_.valid()) device_->destroyTexture(ownedWhite_);
        ownedWhite_ = {};
        device_ = nullptr;
    }

private:
    bool drawMaterial(gfx::Device& dev,const Model& model,
                      const Material& material,const mat4& world,
                      bool masked,bool fading,float threshold,bool invert,
                      const Renderer3D::FeedbackPixelGrid& grid) {
        if (!renderer_ || !device_ || !attached_) return false;
        gfx::VirtualTextureFeedbackUniforms u;
        const mat4 clip = renderer_->viewProj() * world;
        std::memcpy(u.clipFromLocal,clip.m,sizeof(u.clipFromLocal));

        VirtualTextureMaterialMapping map;
        // Unmapped materials still draw depth to hide background VT pages.
        // page=(0,0,...) causes aiverseMaterialFeedback() to output zero.
        if (resolve_) {
            VirtualTextureMaterialMapping resolved;
            if (resolve_(model,material,resolved)) map = resolved;
        }
        u.page[0] = float(map.baseTilesX);
        u.page[1] = float(map.baseTilesY);
        u.page[2] = float(map.layer);
        u.page[3] = float(map.explicitMip);
        u.texels[0] = map.texelWidth;
        u.texels[1] = map.texelHeight;
        u.texels[2] = float(map.mipLevels);
        u.texels[3] = map.mipBias;
        u.virtualUv[0] = map.uvScaleX;
        u.virtualUv[1] = map.uvScaleY;
        u.virtualUv[2] = map.uvOffsetX;
        u.virtualUv[3] = map.uvOffsetY;
        u.addressModes[0] = float(uint32_t(map.addressU));
        u.addressModes[1] = float(uint32_t(map.addressV));

        // Identical KHR_texture_transform convention to ModelShaders.h:
        // uv = rotate(uv0 * scale, -rotation) + offset.
        const float c=std::cos(material.uvRotation),s=std::sin(material.uvRotation);
        u.alphaUv0[0] = c*material.uvScale.x;
        u.alphaUv0[1] = s*material.uvScale.y;
        u.alphaUv0[2] = material.uvOffset.x;
        u.alphaUv1[0] = -s*material.uvScale.x;
        u.alphaUv1[1] = c*material.uvScale.y;
        u.alphaUv1[2] = material.uvOffset.y;
        u.coverage[0] = material.baseColor.w;
        u.coverage[1] = material.alphaCutoff;
        u.coverage[2] = threshold;
        u.coverage[3] = invert ? 1.0f : 0.0f;
        u.scenePixel[0] = grid.scaleX;
        u.scenePixel[1] = grid.scaleY;
        u.scenePixel[2] = grid.offsetX;
        u.scenePixel[3] = grid.offsetY;

        gfx::TextureHandle alpha = fallbackWhite_;
        bool redChannel = false;
        if (const auto* resolved=resolveMaterialImage(model,material,&Material::baseTex)) {
            const auto& image=*resolved;
            if (masked && image.virtualAlphaMask.valid()) {
                alpha=image.virtualAlphaMask;
                redChannel=true;
            } else if (image.tex.valid()) alpha=image.tex;
        }
        const auto& program = masked && redChannel ?
            redMaskPrograms_[fading?1:0][material.doubleSided?1:0] :
            programs_[masked?1:0][fading?1:0][material.doubleSided?1:0];
        return program.bind(dev,u,masked ? alpha : gfx::TextureHandle{});
    }

    gfx::Device* device_ = nullptr;              // non-owning
    Renderer3D* renderer_ = nullptr;             // non-owning
    bool attached_ = false;
    VirtualTextureMaterialResolver resolve_;
    gfx::TextureHandle fallbackWhite_;
    gfx::TextureHandle ownedWhite_;
    gfx::VirtualTextureFeedbackProgram programs_[2][2][2];
    gfx::VirtualTextureFeedbackProgram redMaskPrograms_[2][2];
};

} // namespace aiverse::render
