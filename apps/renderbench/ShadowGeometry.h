#pragma once
#include "engine/render/GiModelGeometry.h"
namespace aiverse::bench {
inline void appendShadowGeometry(render::StaticShadowGeometry& out,const render::Model& model,
                                 const render::mat4& transform={}) {
    auto next=render::giModelGeometry(model,transform);
    const auto base=uint32_t(out.albedos.size());
    out.albedos.insert(out.albedos.end(),next.albedos.begin(),next.albedos.end());
    for(auto triangle:next.triangles) {triangle.surface+=base;out.triangles.push_back(triangle);}
}
}
