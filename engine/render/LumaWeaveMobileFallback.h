// Explicit low-end/mobile fallback through existing LightSystem's raster map.
// Uses its one-fetch indirect ground light map and optional eight detailed
// pixel lights. Does NOT attempt GPU ray tracing or pretend to retain all 4096
// in a 1024-light legacy path. Omitted visible source count and potential
// energy share are explicit diagnostics, never silently hidden.
// Host must choose either this fallback OR LumaWeave Metal direct for one view.
#pragma once
#include "LumaWeaveLights.h"
#include "Lights.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct LumaMobilePlan {
    std::vector<PointLight> selected;
    size_t totalRegistered=0,candidates=0,dropped=0;
    double consideredWeight=0,selectedWeight=0;
    float retainedImportanceFraction=1;
    bool lossy=false;
};

class LumaWeaveMobileFallback {
public:
    static LumaMobilePlan plan(const LumaWeaveRegistry& sources,
                               float3 focus,float visibleRadius,
                               size_t maxLegacyLights=LightSystem::kMaxLights) {
        if(!std::isfinite(focus.x)||!std::isfinite(focus.y)||
           !std::isfinite(focus.z)||!std::isfinite(visibleRadius) ||
           visibleRadius<=0 || visibleRadius>100000 ||
           !maxLegacyLights || maxLegacyLights>LightSystem::kMaxLights)
            throw std::invalid_argument("invalid LumaWeave mobile view/budget");
        struct Choice {uint32_t index;double importance;};
        std::vector<Choice> candidates;
        candidates.reserve(sources.lights().size());
        LumaMobilePlan result;
        result.totalRegistered=sources.lights().size();
        for(uint32_t i=0;i<sources.lights().size();++i) {
            const auto& light=sources.lights()[i];
            if(light.intensity<=0)continue;
            const float3 d=light.position-focus;
            const float distance2=dot(d,d);
            const float reach=light.radius+visibleRadius;
            if(distance2>reach*reach)continue;
            const double lum=.2126*light.color.x+.7152*light.color.y+.0722*light.color.z;
            const double score=double(light.intensity)*lum*
                               light.radius*light.importanceBias/
                               (1.0+std::sqrt(double(distance2))*.1);
            if(!std::isfinite(score)||score<=0)continue;
            candidates.push_back({i,score});
            result.consideredWeight+=score;
        }
        result.candidates=candidates.size();
        std::sort(candidates.begin(),candidates.end(),[&](const Choice& a,const Choice& b){
            return a.importance==b.importance ?
                   sources.lights()[a.index].key<sources.lights()[b.index].key :
                   a.importance>b.importance;
        });
        const size_t kept=std::min(maxLegacyLights,candidates.size());
        result.selected.reserve(kept);
        for(size_t i=0;i<kept;++i) {
            const auto& choice=candidates[i];
            const auto& l=sources.lights()[choice.index];
            PointLight legacy;
            legacy.pos=l.position;
            legacy.radius=l.radius;
            legacy.color=l.color;
            legacy.intensity=l.intensity;
            legacy.priority=std::clamp(l.importanceBias-1.0f,-.99f,63.0f);
            legacy.dir=l.direction;
            legacy.cosOuter=l.kind==LumaLightKind::Spot ? l.cosOuter : -2.f;
            legacy.cosInner=l.kind==LumaLightKind::Spot ? l.cosInner : -2.f;
            // Legacy keys are uint32. Never hash an arbitrary uint64 down
            // and silently alias two independent lights for hysteresis.
            legacy.key=l.key<=UINT32_MAX ? uint32_t(l.key) : 0u;
            legacy.rank=-1; // existing importance calculation remains consistent
            result.selected.push_back(legacy);
            result.selectedWeight+=choice.importance;
        }
        result.dropped=candidates.size()-kept;
        result.lossy=result.dropped!=0;
        result.retainedImportanceFraction=result.consideredWeight>0 ?
            float(result.selectedWeight/result.consideredWeight) : 1.f;
        return result;
    }

    // Only call BETWEEN legacy.begin() and Renderer3D::render().
    // Enqueue checks remaining capacity; no implicit LightSystem::begin(),
    // no rendering pass and no mutation if the active light map is already full.
    static bool enqueue(LightSystem& legacy,const LumaMobilePlan& plan) {
        if(legacy.count()+plan.selected.size()>LightSystem::kMaxLights)
            return false;
        if(legacy.quality()==LightQuality::Off)return false;
        for(const auto& light:plan.selected)legacy.add(light);
        return true;
    }
};

} // namespace aiverse::render
