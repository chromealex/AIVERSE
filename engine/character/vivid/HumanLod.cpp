#include "HumanLod.h"
#include <algorithm>
#include <array>
#include <vector>
#if __has_include("meshoptimizer.h")
#include "meshoptimizer.h"
#define AIVERSE_HUMAN_LOD 1
#endif

namespace aiverse::vivid {
size_t generateHumanCharacterLods(render::Model& model) {
#ifndef AIVERSE_HUMAN_LOD
    (void)model; return 0;
#else
    constexpr std::array<float,3> ratios{.5f,.25f,.12f};
    constexpr std::array<float,3> errors{.01f,.02f,.04f};
    constexpr std::array<float,3> pixels{600.f,280.f,120.f};
    if(model.vertices.empty()||model.indices.empty())return 0;
    std::vector<bool> used(model.meshes.size(),false);
    for(const auto& n:model.nodes)if(n.mesh>=0&&size_t(n.mesh)<used.size())used[size_t(n.mesh)]=true;
    size_t added=0;
    const size_t sourceMeshes=model.meshes.size();
    for(size_t mi=0;mi<sourceMeshes;++mi) {
        if(!used[mi])continue;
        {
            const auto& mesh=model.meshes[mi];
            if(!mesh.lods.empty()||mesh.primitives.empty())continue;
            bool ok=true;size_t tris=0;
            for(const auto& p:mesh.primitives){
                if(p.morphTargets>0||p.indexCount%3||size_t(p.firstIndex)+p.indexCount>model.indices.size()){ok=false;break;}
                tris+=p.indexCount/3;}
            if(!ok||tris<512)continue;
        }
        size_t previous=0;for(const auto& p:model.meshes[mi].primitives)previous+=p.indexCount;
        for(size_t level=0;level<ratios.size();++level) {
            render::MeshDef lod;
            lod.name=model.meshes[mi].name+"_lod"+std::to_string(level+1);
            lod.boundsMin=model.meshes[mi].boundsMin;lod.boundsMax=model.meshes[mi].boundsMax;
            size_t total=0;
            for(const auto& source:model.meshes[mi].primitives) {
                const size_t count=source.indexCount;
                std::vector<unsigned> src(model.indices.begin()+source.firstIndex,model.indices.begin()+source.firstIndex+count);
                std::vector<unsigned> dst(count);
                size_t target=std::max<size_t>(3,size_t(float(count)*ratios[level])/3*3);
                float err=0;
                const size_t n=count<384?count:meshopt_simplify(dst.data(),src.data(),count,&model.vertices[0].pos.x,model.vertices.size(),
                    sizeof(render::ModelVertex),target,errors[level],meshopt_SimplifyLockBorder,&err);
                if(count<384)dst=src;
                render::Primitive p=source;
                p.firstIndex=uint32_t(model.indices.size());p.indexCount=uint32_t(n);
                p.shadowBatched=false;p.morphAliasMesh=p.morphAliasPrimitive=-1;
                model.indices.insert(model.indices.end(),dst.begin(),dst.begin()+std::ptrdiff_t(n));
                total+=n;
                if(n)lod.primitives.push_back(std::move(p));
            }
            // Keep a level only if it saves at least 20% over the previous one.
            if(lod.primitives.empty()||float(total)>float(previous)*.8f)break;
            previous=total;
            const int index=int(model.meshes.size());
            model.meshes.push_back(std::move(lod));
            render::ModelLod ref;ref.mesh=index;ref.maxPixels=pixels[level];
            model.meshes[mi].lods.push_back(ref);
            ++added;
        }
    }
    return added;
#endif
}
}
