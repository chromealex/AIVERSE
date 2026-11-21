// Shared visual-model surface picker with CPU alpha coverage. Bind-pose
// fallback is never used for skinned/morphed meshes; those and GPU vertex
// displacement need rendered-ID picking. Rigid LOD picking currently uses
// authored base geometry and is not exact during a LOD crossfade.
#pragma once
#include "engine/render/ModelSurfacePicking.h"
#include "apps/studio-native/StudioVisualPicking.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace aiverse::studio {

using StudioModelSurfaceHit=render::ModelSurfaceHit;
using render::pickModelSurface;
inline float studioRayTriangle(render::float3 o,render::float3 d,render::float3 a,render::float3 b,render::float3 c){return render::rayModelTriangle(o,d,a,b,c);}
inline StudioModelSurfaceHit studioPickModelSurface(const render::Model& m,const render::mat4& w,render::float3 o,render::float3 d,std::size_t budget=1000000){return render::pickModelSurface(m,w,o,d,budget);}

struct StudioModelPickInstance {
    std::string id;                  // Studio document identity, not a mesh/node ID
    const render::Model* model = nullptr;
    render::mat4 world{};
};

struct StudioModelInstanceHit {
    std::string id;
    StudioModelSurfaceHit surface;
};

// Query all currently rendered rigid model instances and choose their
// foremost actual triangle. Callers should include ONLY instances also
// submitted to Renderer3D, using identical world transforms.
inline StudioModelInstanceHit studioPickModelInstances(
    const std::vector<StudioModelPickInstance>& instances,
    render::float3 origin,render::float3 direction,
    std::size_t triangleBudgetPerModel=1000000) {
    StudioModelInstanceHit nearest;
    for (const auto& instance:instances) {
        if (!instance.model || instance.id.empty()) continue;
        const auto surface=studioPickModelSurface(*instance.model,instance.world,
            origin,direction,triangleBudgetPerModel);
        if (surface.distance<nearest.surface.distance) {
            nearest.id=instance.id;
            nearest.surface=surface;
        }
    }
    return nearest;
}

// Resolve the visible nearest surface among both real model instances and
// fallback document cubes. Any proxy replaced by a drawn model is excluded:
// otherwise an invisible proxy could steal the click from the real model.
inline std::string studioPickSceneSurfaces(
    const std::vector<StudioModelPickInstance>& renderedModels,
    const std::vector<StudioDocumentProxy>& documentProxies,
    render::float3 origin,render::float3 direction) {
    const auto modelHit=studioPickModelInstances(renderedModels,origin,direction);
    std::vector<StudioDocumentProxy> visibleProxies;
    visibleProxies.reserve(documentProxies.size());
    for (const auto& proxy:documentProxies) {
        bool replaced=false;
        for (const auto& instance:renderedModels)
            if (instance.id==proxy.id) {replaced=true;break;}
        if (!replaced) visibleProxies.push_back(proxy);
    }
    const auto proxyHit=studioPickVisibleProxy(visibleProxies,origin,direction);
    return modelHit.surface.distance<proxyHit.distance ? modelHit.id : proxyHit.id;
}
} // namespace aiverse::studio
