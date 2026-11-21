// Universal authoring-document visualization proxies, not runtime ECS objects.
// Rendered using AIVERSE Renderer3D without ImGui. No game genre enters the UI.
#pragma once
#include "apps/editor/EditorSceneNodes.h"
#include "apps/studio-native/StudioDocumentIds.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aiverse::studio {
struct StudioDocumentProxy {
    std::string id;
    render::float3 center{};
    render::float3 half{.5f,.5f,.5f};
    render::float4 color{.67f,.75f,.84f,1};
    float yaw = 0;
    bool light = false;
    render::float3 boundsMin{}, boundsMax{};

    void calculateBounds() {
        const float c = std::abs(std::cos(yaw)), s = std::abs(std::sin(yaw));
        const float hx = c * half.x + s * half.z;
        const float hz = s * half.x + c * half.z;
        boundsMin = {center.x-hx,center.y-half.y,center.z-hz};
        boundsMax = {center.x+hx,center.y+half.y,center.z+hz};
    }
};

inline std::vector<StudioDocumentProxy> studioDocumentProxies(
    const editor::MapDoc& map, bool fpsProject, float levelHeight=1.5f) {
    std::vector<StudioDocumentProxy> proxies;
    const auto nodes=editor::enumerateDocumentSceneNodes(map,fpsProject);
    proxies.reserve(std::min(size_t(2000),nodes.size()));
    for(const auto& node:nodes) {
        if(proxies.size()>=2000)break;
        if(!node.hasPosition)continue;
        StudioDocumentProxy p;
        p.id=studioDocumentId(map,node.ref);
        p.center={float(node.position.x),float(node.position.y),float(node.position.z)};
        switch(node.ref.source) {
          case editor::DocumentEntityRef::Source::MapLight:
          case editor::DocumentEntityRef::Source::FpsLight:
            p.light=true;p.half={.25f,.25f,.25f};p.color={1,.8f,.4f,1};
            if(!fpsProject)p.center.y+=terrainPreviewHeight(map.data,p.center.x,p.center.z,levelHeight);
            break;
          case editor::DocumentEntityRef::Source::MapEntity: {
            const auto* source=editor::documentMapEntity(map,node.ref);
            if(!source || !source->at.isNull())continue; // planet coordinate needs a planet adapter
            p.center.y=terrainPreviewHeight(map.data,p.center.x,p.center.z,levelHeight)+.6f;
            p.half={.5f,.6f,.5f};
           // Runtime "facing": degrees clockwise from north, viewed from above.
           if(source->extra["facing"].isNumber() && std::isfinite(source->extra["facing"].toDouble()))
               p.yaw=float(-source->extra["facing"].toDouble()*3.141592653589793/180.0);
           if(editor::authoredEntity(*source)) {
               const auto t=editor::authoredLocalTransform(*source);
               p.center.y=float(t.position.y.toDouble())+.6f;
               p.yaw=float(editor::authoredRotationDegrees(*source)[1]*3.141592653589793/180.0);
           }
            p.color=source->owner==0 ? render::float4{.65f,.75f,.84f,1} :
                    source->owner==1 ? render::float4{.44f,.70f,.98f,1} :
                    source->owner==2 ? render::float4{.94f,.52f,.42f,1} :
                                       render::float4{.83f,.76f,.48f,1};
            break;
          }
          case editor::DocumentEntityRef::Source::FpsCollider: {
            const Json* object=editor::fpsDocumentEntity(map.data.fps,node.ref);
            if(!object)continue;
            if((*object)["yaw"].isNumber()) {
                const Json& half=(*object)["half"];
                if(!half.isArray() || half.size()<3)continue;
                p.half={float(half[0].toDouble()),float(half[1].toDouble()),float(half[2].toDouble())};
                p.yaw=float((*object)["yaw"].toDouble()*6.283185307179586);
            } else {
                const Json& lo=(*object)["min"], &hi=(*object)["max"];
                if(!lo.isArray() || !hi.isArray() || lo.size()<3 || hi.size()<3)continue;
                p.half={float((hi[0].toDouble()-lo[0].toDouble())*.5),
                        float((hi[1].toDouble()-lo[1].toDouble())*.5),
                        float((hi[2].toDouble()-lo[2].toDouble())*.5)};
            }
            p.color={.65f,.63f,.97f,1};
            break;
          }
          case editor::DocumentEntityRef::Source::FpsSpawn:
            p.half={.42f,.9f,.42f};
            p.color={.48f,.89f,.63f,1};
            break;
        }
        const auto finite=[](float v){return std::isfinite(v);};
        if(!finite(p.center.x)||!finite(p.center.y)||!finite(p.center.z)||
           !finite(p.half.x)||!finite(p.half.y)||!finite(p.half.z)||
           p.half.x<=0||p.half.y<=0||p.half.z<=0||
           p.half.x>100000||p.half.y>100000||p.half.z>100000||!finite(p.yaw))
           continue;
        p.calculateBounds();
        proxies.push_back(std::move(p));
    }
    return proxies;
}
} // namespace aiverse::studio
