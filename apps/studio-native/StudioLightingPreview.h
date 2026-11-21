#pragma once
#include "StudioTerrainGeometry.h"
#include "StudioDocumentVisuals.h"
#include "engine/render/Model.h"
#include <set>
namespace aiverse::studio {
// Uses exactly the existing document preview geometry, not game assets. This
// PBR representation enables the shared receiver/GI passes on that preview.
inline std::unique_ptr<render::Model> studioLightingPreview(const editor::MapDoc& doc,
                                                         bool fps,float levelHeight,const std::set<std::string>& excluded={}) {
    using namespace render;
    MeshBuilder ground,box;box.color(rgba(255,255,255));box.box({0,0,0},{1,1,1});
    const auto proxies=studioDocumentProxies(doc,fps,levelHeight);
    if(fps) {
        float3 lo{-3,0,-3},hi{3,0,3};
        if(!proxies.empty()) {
            lo=proxies.front().boundsMin;hi=proxies.front().boundsMax;
            for(const auto& p:proxies) {
                lo={std::min(lo.x,p.boundsMin.x),std::min(lo.y,p.boundsMin.y),std::min(lo.z,p.boundsMin.z)};
                hi={std::max(hi.x,p.boundsMax.x),std::max(hi.y,p.boundsMax.y),std::max(hi.z,p.boundsMax.z)};
            }
        }
        ground.color(rgba(46,66,84));
        ground.box({(lo.x+hi.x)*.5f,lo.y-.08f,(lo.z+hi.z)*.5f},
                   {hi.x-lo.x+2.f,.12f,hi.z-lo.z+2.f});
    } else if(doc.data.extra.getStr("terrainPresentation","legacy")!="entities" && !buildTerrainPreview(doc.data,ground,levelHeight).quads) {
        ground.color(rgba(46,66,84));
        const float w=float(std::max(1,doc.data.w)),h=float(std::max(1,doc.data.h));
        ground.box({w*.5f,-.08f,h*.5f},{w,.12f,h});
    }
    Material base;base.metallic=0;base.roughness=.8f;
    std::vector<ModelPart> parts{{"Terrain preview",&ground,base,mat4{}}};
    for(const auto& proxy:proxies) {
        const int index=studioMapEntityIndex(proxy.id,doc);
        if(excluded.count(proxy.id)||(index>=0&&doc.data.entities[size_t(index)].extra["components"].has("NereidWater")))continue;
        if(proxy.light)continue; // editor handle is not a receiver or shadow caster
        auto material=base;material.vertexColors=false;material.baseColor=proxy.color;
        parts.push_back({proxy.id,&box,material,mat4::translate(proxy.center)*
            mat4::rotateY(proxy.yaw)*mat4::scale(proxy.half*2.f)});
    }
    if(parts.size()==1 && ground.indices().empty())return {};
    auto model=modelFromParts("studio/lighting-preview",parts);
    if(model)model->batching.autoLod=false;
    return model;
}
}
