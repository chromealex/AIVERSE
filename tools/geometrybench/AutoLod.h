#pragma once
#include "engine/render/Model.h"
#include "meshoptimizer.h"
#include <algorithm>
namespace aiverse::render {
// Host-only benchmark adapter for the same attribute-aware meshoptimizer policy as autolod.py.
inline void prepareBenchmarkLods(Model& m) {
    if(m.vb.valid()||!m.skins.empty()||!m.clips.empty())return;
    const size_t baseCount=m.meshes.size();
    std::vector<float> attributes;
    for(const auto& v:m.vertices){for(float f:{v.nrm.x,v.nrm.y,v.nrm.z,v.uv.x,v.uv.y,v.tangent.x,v.tangent.y,v.tangent.z,v.tangent.w})attributes.push_back(f);for(int i=0;i<4;++i)attributes.push_back(float((v.color>>(i*8))&255)/255);}
    const float weights[13]={.5f,.5f,.5f,1,1,.25f,.25f,.25f,.25f,1,1,1,1};
    for(size_t mi=0;mi<baseCount;++mi){
        const auto base=m.meshes[mi];if(base.deformable||!base.lods.empty())continue;
        if(std::any_of(base.primitives.begin(),base.primitives.end(),[&](const Primitive& p){const auto& mat=m.materials[size_t(p.material)];return mat.shaderGraph||mat.alpha!=AlphaMode::Opaque||mat.transparent||!p.variantMaterial.empty();}))continue;
        size_t previous=0;for(const auto& p:base.primitives)previous+=p.indexCount;
        float previousPixels=1e30f;
        for(int level=0;level<3;++level){
            MeshDef coarse;coarse.name=base.name+"-lod"+std::to_string(level+1);std::vector<uint32_t> indices;float error=0;
            for(auto p:base.primitives){
                std::vector<uint32_t> result(p.indexCount);float e=0;size_t count=p.indexCount;
                if(count>=384)count=meshopt_simplifyWithAttributes(result.data(),m.indices.data()+p.firstIndex,p.indexCount,&m.vertices[0].pos.x,m.vertices.size(),sizeof(ModelVertex),attributes.data(),13*sizeof(float),weights,13,nullptr,size_t(p.indexCount*(.5f/float(1<<level)))/3*3,.005f*float(1<<level),meshopt_SimplifyLockBorder,&e);
                else std::copy_n(m.indices.data()+p.firstIndex,count,result.data());
                error=std::max(error,e);p.firstIndex=uint32_t(m.indices.size()+indices.size());p.indexCount=uint32_t(count);coarse.primitives.push_back(p);indices.insert(indices.end(),result.begin(),result.begin()+count);
            }
            if(indices.size()>=previous*.8||indices.empty())continue;
            const float pixels=std::min({256.f/float(1<<level),.5f/std::max(error,1e-9f),previousPixels*.5f});
            m.meshes[mi].lods.push_back({int(m.meshes.size()),pixels});m.meshes.push_back(std::move(coarse));m.indices.insert(m.indices.end(),indices.begin(),indices.end());previous=indices.size();previousPixels=pixels;
        }
    }
    m.finalize();
}
}
