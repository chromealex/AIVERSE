#pragma once
#include <chrono>
#include "StudioTerrainGeometry.h"
#include "StudioWindPreview.h"
#include "apps/editor/core/PhysisOverlayTiles.h"
#include "apps/editor/PhysisNaturePreview.h"
#include "apps/editor/core/PhysisDiagnosticFrame.h"

namespace aiverse::studio {

struct StudioNaturePreview {
    std::string epoch;
    std::uint64_t revision;
    std::uint64_t ignitionRevision=0;
    bool forestEnabled=false;
    bool forestFramed=false;
    render::wind::WindDebugView forestMask=render::wind::WindDebugView::Off;
    bool commandPlaying=false;
    std::chrono::steady_clock::time_point commandTime{};
    float yaw=.6f,pitch=.55f,distance=0;
    editor::PhysisSceneOverlayState overlay;
    Json presentation=Json::object();
    fx::ParticleSystem particles;
    editor::PhysisNaturePreview preview;

    StudioNaturePreview(std::string scope, const editor::MapDoc& map, float levelHeight,
                       editor::PhysisNaturePreviewSettings settings,bool privateWorld=true)
        : epoch(std::move(scope)), revision(map.revision) {
        distance=std::clamp(float(std::max(map.data.w,map.data.h))*.9f,8.f,50.f);
        preview.settings=settings;
        preview.settings.enabled=true;
        if (!privateWorld) return;
        preview.setWater(editor::AuthoredWaterQuery::fromDoc(map));
        preview.prepareSource(particles,map,nullptr,[&](float x,float z) {
            return terrainPreviewHeight(map.data,x,z,levelHeight);
        });
    }

    Json displayStatus() const {
        auto status=presentation;
        status.set("forestMask",Json::string(windDebugName(forestMask)));
        status.set("forest",Json::fromBool(forestEnabled));
        status.set("overlayRadius",Json::number(int64_t(overlay.radiusCells)));
        status.set("overlayStride",Json::number(int64_t(overlay.stride)));
        status.set("overlayAlpha",Json::number(double(overlay.alpha)));
        status.set("overlayVolumes",Json::fromBool(overlay.drawVolumes));
        status.set("fieldOverlay",Json::fromBool(overlay.enabled));
        status.set("igniteOnClick",Json::fromBool(preview.settings.igniteOnClick));
        status.set("quality",Json::number(int64_t(preview.settings.grassQuality)));
        status.set("volume",Json::number(double(preview.settings.volume)));
        status.set("grass",Json::fromBool(preview.settings.grass));
        status.set("flames",Json::fromBool(preview.settings.flames));
        status.set("sound",Json::fromBool(preview.settings.sound));
        return status;
    }

    void setDisplay(const Json& params) {
        for (const char* key:{"forest","grass","flames","sound","igniteOnClick","fieldOverlay","overlayVolumes"})
            if (params.has(key) && params[key].type()!=Json::Type::Bool)
                throw std::runtime_error("Invalid nature presentation setting");
        const double quality=params["quality"].toDouble(preview.settings.grassQuality);
        const double volume=params["volume"].toDouble(preview.settings.volume);
        if ((params.has("quality") && !params["quality"].isNumber()) || !std::isfinite(quality) ||
            quality!=std::floor(quality) || quality<0 || quality>2 ||
            (params.has("volume") && !params["volume"].isNumber()) || !std::isfinite(volume) || volume<0 || volume>1)
            throw std::runtime_error("Invalid grass quality or preview volume.");
        const auto layer=params.getInt("layer",int(overlay.layer));
        const auto palette=params.getInt("palette",int(overlay.palette));
        if (layer<0 || layer>int(physis::DiagnosticLayer::LocalFloodMeters) ||
            palette<0 || palette>int(physis::DebugPalette::Ocean))
            throw std::runtime_error("Unknown diagnostic field or palette.");
        const auto setting=[&](const char* name,double current,double min,double max,bool integral){
            if (!params.has(name)) return current;
            const double value=params[name].toDouble();
            if (!params[name].isNumber() || !std::isfinite(value) || value<min || value>max ||
                (integral && value!=std::floor(value)))
                throw std::runtime_error(std::string("Invalid diagnostic setting: ")+name);
            return value;
        };
        const auto radius=setting("overlayRadius",overlay.radiusCells,6,24,true);
        const auto stride=setting("overlayStride",overlay.stride,1,4,true);
        const auto alpha=setting("overlayAlpha",overlay.alpha,.1,.8,false);
        if (params.has("forestMask")) forestMask=parseWindDebugView(params["forestMask"]);
        if (params.has("forest")) {
            forestEnabled=params.getBool("forest");
            if (forestEnabled && !forestFramed) {
                // Start above the canopy, within the near forest preview's culling range.
                distance=std::max(distance,48.f);
                pitch=std::max(pitch,1.25f);
                forestFramed=true;
            }
        }
        overlay.radiusCells=int(radius);
        overlay.stride=int(stride);
        overlay.alpha=float(alpha);
        if (params.has("overlayVolumes")) overlay.drawVolumes=params.getBool("overlayVolumes");
        if (params.has("fieldOverlay")) overlay.enabled=params.getBool("fieldOverlay");
        if (layer!=int(overlay.layer) || palette!=int(overlay.palette)) overlay.dirty=true;
        overlay.layer=static_cast<physis::DiagnosticLayer>(layer);
        overlay.palette=static_cast<physis::DebugPalette>(palette);
        preview.settings.grassQuality=int(quality);
        preview.settings.volume=float(volume);
        if (params.has("igniteOnClick")) preview.settings.igniteOnClick=params.getBool("igniteOnClick");
        if (params.has("grass")) preview.settings.grass=params.getBool("grass");
        if (params.has("flames")) preview.settings.flames=params.getBool("flames");
        if (params.has("sound")) preview.settings.sound=params.getBool("sound");
    }

    Json read(const Json& params) const {
        auto frame=editor::readPhysisGameFrame(preview.previewWorld(),params);
        if (!frame.getBool("active")) return frame;
        frame.set("presentation",displayStatus());
        Json wind=Json::object();
        wind.set("speed",Json::number(double(preview.settings.windSpeed)));
        wind.set("direction",Json::number(double(preview.settings.windDirectionDeg)));
        frame.set("previewWind",std::move(wind));
        const auto& config=preview.previewWorld()->resourceConst<physis::State>()->settings;
        Json grid=Json::object();
        grid.set("requestedMeters",Json::number(int64_t(preview.settings.previewCellMeters)));
        grid.set("shoreSand",Json::number(double(preview.settings.shoreSandMeters)));
        grid.set("humidity",Json::number(double(preview.settings.humidity)));
        grid.set("mapAvailable",Json::fromBool(preview.hasAuthoredGrid()));
        grid.set("meters",Json::number(int64_t(config.cellSizeMeters)));
        grid.set("width",Json::number(int64_t(config.width)));
        grid.set("height",Json::number(int64_t(config.height)));
        grid.set("baseHumidity",Json::number(config.baseHumidity));
        frame.set("previewGrid",std::move(grid));
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
            overlay.dirty=true;
        } else if (action=="presentation") {
            setDisplay(params);
        } else if (action=="ignite") {
            if (!frame["cell"].getBool("available")) throw std::runtime_error("Select a PHYSIS cell first.");
            const auto* state=preview.previewWorld()->resourceConst<physis::State>();
            const float meters=float(state->settings.cellSizeMeters);
            std::string reason;
            if (!preview.ignite((params.getInt("cellX")+.5f)*meters,(params.getInt("cellZ")+.5f)*meters,&reason))
                throw std::runtime_error(reason);
            ++ignitionRevision;
        } else {
            throw std::runtime_error("Unknown nature preview action");
        }
        return read(params);
    }
};

} // namespace aiverse::studio
