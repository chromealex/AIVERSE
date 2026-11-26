#pragma once
#include "Prisma.h"
#include "engine/data/Json.h"

namespace aiverse::render {
// Serialize only completed resolves. View and sequence IDs retain their full uint64 precision.
inline Json prismaViewReports(const Prisma& pipeline) {
    Json views = Json::array();
    for (const auto id : pipeline.effectReportViewIds()) {
        const auto* frame = pipeline.effectReport(id);
        if (!frame) continue;
        Json view = Json::object(), effects = Json::array();
        view.set("id", Json::string(std::to_string(id)));
        view.set("sequence", Json::string(std::to_string(frame->sequence)));
        view.set("quality", Json::string(prismaQualityName(frame->quality)));
        view.set("width", Json::number(int64_t(frame->width)));
        view.set("height", Json::number(int64_t(frame->height)));
        view.set("compositorPasses", Json::number(int64_t(frame->compositorPasses)));
        view.set("shaderGraphTextures", Json::number(int64_t(frame->shaderGraphTextures)));
        view.set("autoExposureTextureSlot", Json::number(int64_t(frame->autoExposureTextureSlot)));
        view.set("hdr", Json::fromBool(frame->hdr));
        view.set("sceneReady", Json::fromBool(frame->sceneReady));
        view.set("shaderGraphFallback", Json::fromBool(frame->shaderGraphFallback));
        view.set("manualExposureEncoded", Json::fromBool(frame->manualExposureEncoded));
        view.set("autoExposureFrozen", Json::fromBool(frame->autoExposureFrozen));
        view.set("autoExposureRegionMetered", Json::fromBool(frame->autoExposureRegionMetered));
        view.set("autoExposureBoxMetered", Json::fromBool(frame->autoExposureBoxMetered));
        view.set("reactiveInputSupplied", Json::fromBool(frame->reactiveInputSupplied));
        view.set("reactiveInputAccepted", Json::fromBool(frame->reactiveInputAccepted));
        view.set("objectVelocityInputAccepted", Json::fromBool(frame->objectVelocityInputAccepted));
        view.set("reactiveWithoutObjectVelocity", Json::fromBool(frame->reactiveWithoutObjectVelocity));
        view.set("motionBlurReactiveEdgesEnabled", Json::fromBool(frame->motionBlurReactiveEdgesEnabled));
        view.set("manualExposureEV", Json::number(double(frame->manualExposureEV), 4));
        view.set("autoExposureHighlightRejection", Json::number(double(frame->autoExposureHighlightRejection), 4));
        for (const auto& state : frame->effects) {
            Json effect = Json::object();
            effect.set("type", Json::string(prismaType(state.type).id));
            effect.set("requested", Json::fromBool(state.requested));
            effect.set("pipelinePrepared", Json::fromBool(state.pipelinePrepared));
            effect.set("encoded", Json::fromBool(state.encoded));
            effect.set("degraded", Json::fromBool(state.degraded));
            effect.set("reason", Json::string(state.reason));
            effects.push(std::move(effect));
        }
        view.set("effects", std::move(effects));
        views.push(std::move(view));
    }
    return views;
}
} // namespace aiverse::render
