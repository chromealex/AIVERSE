// Renderer3D: baked lighting (agent-lightbake) — which model items take a lightmap or the light
// probes, and the GI bindings of their draws. Shader side: ModelShaders.h LIGHTMAP / PROBES, BakedShaders.h.
#include "Renderer3D.h"
#include "BakedLighting.h"
#include "BspVisibility.h"
#include <cstdlib>
#include <string>

namespace aiverse::render {

BakedQuality Renderer3D::defaultBakedQualityFor(const gfx::Device&, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_BAKED")) {   // testing override: off|low|high
        const std::string v = q;
        return v == "off" ? BakedQuality::Off : v == "low" ? BakedQuality::Low : BakedQuality::High;
    }
    return mobile ? BakedQuality::Low : BakedQuality::High;
}

void Renderer3D::setBakedQuality(BakedQuality q) {
    if (q == bakedQuality_) return;
    bakedQuality_ = q;
    if (!dev_) return;
    // AIVERSE_GIQ is baked into the shader variants
    for (auto& [k, p] : modelPipes_) dev_->destroyPipeline(p);
    for (auto& [k, s] : modelShaders_) if (s.shader.valid()) dev_->destroyShader(s.shader);
    pendingModelPipes_.clear(); modelPipes_.clear(); modelShaders_.clear();
}

void Renderer3D::bakedItem(ModelItem& it, const ModelDraw& d, int node) {
    it.gi = 0; it.lmPage = -1;
    if (bakedQuality_ == BakedQuality::Off || it.mat->shading == ShadingModel::Unlit) return;
    const auto& bsp=it.model->bspVisibility;
    if (bsp && it.prim->lightmapUv && it.meshIndex==0 && it.primitiveIndex<bsp->lightmapImages.size()) {
        const int image=bsp->lightmapImages[it.primitiveIndex];
        if (image>=0 && size_t(image)<it.model->images.size() && it.model->images[size_t(image)].tex.valid()) {
            it.gi=1; it.lmPage=int16_t(-2-image); it.anim={1,1,0,0}; ++bakedCountLm_;
            return;
        }
    }
    if (!bakedActive()) return;
    if (it.joints < 0 && !it.baked && it.morph < 0 && it.prim->lightmapUv && !it.model->path.empty()) {
        if (const auto* r = baked_->find(it.model->path, node, it.world)) {
            it.gi = 1; it.lmPage = int16_t(r->page);
            ++bakedCountLm_;
            it.anim = {r->st[0], r->st[1], r->st[2], r->st[3]};
            return;
        }
    }
    (void)d;
    if (it.morph < 0 && baked_->probeTexture().valid()) { it.gi = 2; ++bakedCountPr_; }
}

void Renderer3D::bindBakedGI(const ModelItem& it) {
    if (it.gi==1 && it.lmPage<=-2) {
        float gi[16]{};
        gi[7]=1.f; gi[11]=3.f; // Normalized RGB irradiance; direct sunlight is already baked.
        dev_->setUniforms(4,gi,sizeof gi);
        dev_->setTexture(7,it.model->images[size_t(-2-it.lmPage)].tex);
        dev_->setTexture(8,white_);
        return;
    }
    float gi[16];
    baked_->giBlock(gi);
    dev_->setUniforms(4, gi, sizeof gi);
    if (it.gi == 1) {
        const auto& pg = baked_->pages()[size_t(it.lmPage)];
        dev_->setTexture(7, pg.lightmap);
        dev_->setTexture(8, pg.mask.valid() ? pg.mask : white_);
    } else {
        dev_->setTexture(8, baked_->probeTexture());
    }
}

void Renderer3D::bindBakedTerrain(int uniformSlot, int lightmapSlot, int maskSlot) {
    float b[12] = {};
    if (bakedActive() && baked_->terrainBlock(b)) {
        const auto& pg = baked_->pages()[size_t(baked_->data().terrain.page)];
        dev_->setTexture(lightmapSlot, pg.lightmap);
        dev_->setTexture(maskSlot, pg.mask.valid() ? pg.mask : white_);
    } else {
        dev_->setTexture(lightmapSlot, black_);
        dev_->setTexture(maskSlot, white_);
    }
    dev_->setUniforms(uniformSlot, b, sizeof b);
}

} // namespace aiverse::render
