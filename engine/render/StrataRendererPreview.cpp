#include "StrataRendererPreview.h"

#include <cmath>

namespace aiverse::render::strata {

PreviewCounts previewModelDraw(const ModelDraw& draw,
                               const ModelAsset& staged,
                               const GpuMetadataScene& pooled,
                               const std::string& sourceKey,
                               const PreviewContext& context) {
    PreviewCounts stats;
    const Model* model = draw.model;
    if (!model || sourceKey.empty() || model->order.empty() ||
        model->bindWorld.size() != model->nodes.size() ||
        draw.nodeWorld || draw.nodeMaterial || draw.materials ||
        draw.pose || draw.baked || draw.variant >= 0 ||
        draw.tint.w < 0.99f || draw.dissolve != 0.f ||
        draw.outline.w != 0.f ||
        (draw.flags & (kDrawTransparent | kDrawFadeDepth |
                       kDrawActorReveal | kDrawWaterSurface |
                       kDrawNereidCascade))) {
        ++stats.skippedUnsafeDraws;
        return stats;
    }

    for (int id : model->order) {
        if (id < 0 || size_t(id) >= model->nodes.size()) {
            ++stats.skippedUnsafeDraws;
            return stats;
        }
        const Node& node = model->nodes[size_t(id)];
        if (node.mesh < 0) continue;
        if (size_t(node.mesh) >= model->meshes.size()) {
            ++stats.skippedUnsafeDraws;
            return stats;
        }
        const mat4 world = draw.world * model->bindWorld[size_t(id)];
        for (const auto& primitive : staged.primitives) {
            if (primitive.mesh != uint32_t(node.mesh)) continue;
            const bool canShadow = context.sunEnabled &&
                context.sunWidth > 0 && context.sunHeight > 0 &&
                !(draw.flags & kDrawNoCastShadow) &&
                model->materials.size() > primitive.material &&
                model->materials[primitive.material].castShadow;
            // Main and sun shadow are independent: no main-view LOD reuse.
            for (int phase = 0; phase < (canShadow ? 2 : 1); ++phase) {
                const bool shadow = phase == 1;
                const RenderPass pass = shadow
                    ? RenderPass::SunShadow : RenderPass::MainOpaque;
                ViewConfig view{};
                view.objectToClip =
                    (shadow ? context.sunViewProjection :
                              context.mainViewProjection) * world;
                view.width = shadow ? context.sunWidth : context.mainWidth;
                view.height = shadow ? context.sunHeight : context.mainHeight;
                view.clipDepthZeroToOne = context.clipDepthZeroToOne;
                view.errorThresholdPixels = shadow
                    ? draw.geometry.shadowErrorPixels
                    : draw.geometry.errorPixels;
                if (!std::isfinite(view.errorThresholdPixels) ||
                    view.errorThresholdPixels <= 0)
                    view.errorThresholdPixels = 1.f;
                ++stats.attempted;
                RenderCandidate candidate;
                if (!prepareRenderCandidate(*model, staged, pooled,
                                            sourceKey, primitive.mesh,
                                            primitive.primitive, pass,
                                            view, candidate)) {
                    ++stats.rejected;
                    continue;
                }
                ++stats.ready;
                stats.selectedClusters += candidate.selectedClusters;
                if (candidate.ranges.empty()) ++stats.fullyCulled;
            }
        }
    }
    return stats;
}
} // namespace aiverse::render::strata

namespace aiverse::render {
void Renderer3D::previewStrataModel(
    const ModelDraw& draw, const strata::ModelAsset& staged,
    const strata::GpuMetadataScene& pooled, const std::string& sourceKey) {
    ++strataPreviewStats_.modelDraws;
    if (!dev_) {
        ++strataPreviewStats_.skippedUnsafeDraws;
        return;
    }
    strata::PreviewContext context{};
    context.mainViewProjection = viewProj_;
    context.sunViewProjection = lightViewProj_;
    context.mainWidth = sceneW_;
    context.mainHeight = sceneH_;
    context.sunWidth = shadowSize_;
    context.sunHeight = shadowSize_;
    context.sunEnabled = quality_ != ShadowQuality::Off;
    context.clipDepthZeroToOne = dev_->info().clipDepthZeroToOne;
    const auto p = strata::previewModelDraw(draw, staged, pooled,
                                           sourceKey, context);
    strataPreviewStats_.attempted += p.attempted;
    strataPreviewStats_.ready += p.ready;
    strataPreviewStats_.rejected += p.rejected;
    strataPreviewStats_.selectedClusters += p.selectedClusters;
    strataPreviewStats_.fullyCulled += p.fullyCulled;
    strataPreviewStats_.skippedUnsafeDraws += p.skippedUnsafeDraws;
}
} // namespace aiverse::render
