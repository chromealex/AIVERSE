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

    StudioNaturePreview(std::string scope, const editor::MapDoc& map, float levelHeight,
                       editor::PhysisNaturePreviewSettings settings)
        : epoch(std::move(scope)), revision(map.revision) {
        preview.settings=settings;
        preview.settings.enabled=true;
        preview.setWater(editor::AuthoredWaterQuery::fromDoc(map));
        preview.prepareSource(particles,map,nullptr,[&](float x,float z) {
            return terrainPreviewHeight(map.data,x,z,levelHeight);
        });
    }

    Json read(const Json& params) const {
        auto frame=editor::readPhysisGameFrame(preview.previewWorld(),params);
        Json wind=Json::object();
        wind.set("speed",Json::number(double(preview.settings.windSpeed)));
        wind.set("direction",Json::number(double(preview.settings.windDirectionDeg)));
        frame.set("previewWind",std::move(wind));
        return frame;
    }

    Json apply(const std::string& action,const Json& params) {
        auto frame=read(params);
        if (action=="view") return frame;
        if (action=="step" || action=="step60") {
            preview.advance(action=="step"?1:60);
        } else if (action=="playback") {
            const auto& elapsed=params["elapsed"];
            const auto& speed=params["speed"];
            const double dt=elapsed.toDouble(), rate=speed.toDouble();
            if (!elapsed.isNumber() || !speed.isNumber() || !std::isfinite(dt) || !std::isfinite(rate) ||
                dt<0 || dt>0.25 || rate<0.1 || rate>6)
                throw std::runtime_error("Invalid nature preview playback interval or speed.");
            preview.settings.speed=float(rate);
            preview.advanceElapsed(float(dt));
        } else if (action=="wind") {
            const auto& speed=params["windSpeed"];
            const auto& direction=params["windDirection"];
            const double s=speed.toDouble(), d=direction.toDouble();
            if (!speed.isNumber() || !direction.isNumber() || !std::isfinite(s) || !std::isfinite(d) ||
                s<0 || s>30 || d<-180 || d>180)
                throw std::runtime_error("Wind requires a speed of 0–30 m/s and a direction of −180° to 180°.");
            preview.settings.windSpeed=float(s);
            preview.settings.windDirectionDeg=float(d);
            preview.applyWindSettings();
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
