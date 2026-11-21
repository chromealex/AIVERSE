// Read-only authoring light projection into AIVERSE's generic Renderer3D LightSystem.
// The same adapter can be reused by Metal/Vulkan/GLES hosts; no ImGui or genre UI.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "apps/studio-native/StudioLightPreviewMath.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace aiverse::studio {
inline float lightScalar(const Json& object, const char* key, float fallback) {
    const Json& field=object[key];
    if (!field.isNumber()) return fallback;
    const double v=field.toDouble();
    return std::isfinite(v) && v>=-1000000 && v<=1000000 ? float(v) : fallback;
}
inline PreviewDayLighting previewDocumentDay(const editor::MapDoc& doc, bool fpsProject) {
    const Json& source=fpsProject ? doc.data.fps : doc.data.extra;
    const Json& block=source["lighting"];
    return studioPreviewDayLighting(lightScalar(block,"time",.5f),
        lightScalar(block,"nightLevel",.18f),lightScalar(block,"clouds",0),lightScalar(block,"storm",0));
}
inline std::vector<render::PointLight> studioAuthoredLights(
    const editor::MapDoc& doc,bool fpsProject,float levelHeight=1.5f,size_t limit=render::LightSystem::kMaxLights) {
    const Json& source=fpsProject ? doc.data.fps : doc.data.extra;
    const Json& list=source["lights"];
    const auto day=previewDocumentDay(doc,fpsProject);
    std::vector<render::PointLight> lights;
    if (!list.isArray()) return lights;
    const size_t count=std::min(list.size(),limit);
    lights.reserve(count);
    for(size_t i=0;i<count;++i) {
        const Json& item=list[i];
        if(!item.isObject())continue;
        if(item.getBool("nightOnly") && day.night<=.5f)continue;
        const float x=lightScalar(item,"x",0);
        const float z=lightScalar(item,"z",0);
        const float height=lightScalar(item,"height",1);
        const float radius=lightScalar(item,"radius",4);
        const float intensity=lightScalar(item,"intensity",1.5f);
        if(radius<=0 || radius>100000 || intensity<0 || intensity>10000)continue;
        render::PointLight light;
        light.pos={x,height+(fpsProject ? 0.f : terrainPreviewHeight(doc.data,x,z,levelHeight)),z};
        light.radius=radius;
        light.intensity=intensity;
        light.color={1.f,.8f,.6f};
        const Json& rgb=item["color"];
        if(rgb.isArray() && rgb.size()>=3) {
            const auto rgbComponent=[&](size_t component,float fallback) {
                if(!rgb[component].isNumber())return fallback;
                const double value=rgb[component].toDouble();
                return std::isfinite(value) ? float(std::clamp(value,0.0,1.0)) : fallback;
            };
            light.color={rgbComponent(0,1),rgbComponent(1,.8f),rgbComponent(2,.6f)};
        }
        light.halo=std::clamp(lightScalar(item,"halo",0),0.f,10000.f);
        light.key=uint32_t(i+1); // stable until authoring order changes
        if(item.getStr("type")=="spot") {
            constexpr float rad=0.0174532925199432957f;
            const float yaw=lightScalar(item,"yaw",0)*rad;
            const float pitch=std::clamp(lightScalar(item,"pitch",35),0.f,89.f)*rad;
            const render::float3 direction{
                std::cos(yaw)*std::cos(pitch),
                -std::sin(pitch),
                std::sin(yaw)*std::cos(pitch)
            };
            light.makeSpot(direction,std::clamp(lightScalar(item,"angle",40),1.f,179.f),
                           std::clamp(lightScalar(item,"softness",.6f),.02f,1.f));
        }
        lights.push_back(light);
    }
    return lights;
}
} // namespace aiverse::studio
