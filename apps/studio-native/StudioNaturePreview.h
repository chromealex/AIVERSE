#pragma once
#include "StudioTerrainGeometry.h"
#include "apps/editor/PhysisNaturePreview.h"
#include "apps/editor/core/PhysisDiagnosticFrame.h"

namespace aiverse::studio {

struct StudioNaturePreview {
    std::string epoch;
    std::uint64_t revision;
    fx::ParticleSystem particles;
    editor::PhysisNaturePreview preview;

    StudioNaturePreview(std::string scope, const editor::MapDoc& map, float levelHeight)
        : epoch(std::move(scope)), revision(map.revision) {
        preview.settings.enabled=true;
        preview.setWater(editor::AuthoredWaterQuery::fromDoc(map));
        preview.prepareSource(particles,map,nullptr,[&](float x,float z) {
            return terrainPreviewHeight(map.data,x,z,levelHeight);
        });
    }

    Json read(const Json& params) const {
        return editor::readPhysisGameFrame(preview.previewWorld(),params);
    }

    Json apply(const std::string& action,const Json& params) {
        auto frame=read(params);
        if (action=="view") return frame;
        if (action=="step" || action=="step60") {
            preview.advance(action=="step"?1:60);
        } else if (action=="ignite") {
            if (!frame["cell"].getBool("available")) throw std::runtime_error("Select a PHYSIS cell first.");
            const auto* state=preview.previewWorld()->resourceConst<physis::State>();
            const float meters=float(state->settings.cellSizeMeters);
            std::string reason;
            if (!preview.ignite((params.getInt("cellX")+.5f)*meters,(params.getInt("cellZ")+.5f)*meters,&reason))
                throw std::runtime_error(reason);
        } else {
            throw std::runtime_error("Unknown nature preview action");
        }
        return read(params);
    }
};

} // namespace aiverse::studio
