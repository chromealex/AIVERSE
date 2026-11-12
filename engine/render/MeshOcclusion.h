#pragma once
#include "Model.h"
#include "StaticPointShadows.h"
namespace aiverse::render {
// Deterministic, camera-independent local visibility. Radius is in mesh-local units.
inline float localDiffuseVisibility(const StaticShadowGeometry& geometry,float3 position,float3 normal,float radius,int samples) {
    if(!std::isfinite(radius)||radius<=.01f||radius>100||samples<16||samples>256)
        throw std::runtime_error("invalid local occlusion settings");
    if(dot(normal,normal)<1e-8f)return 1;
    normal=normalize(normal);float visible=0,total=0;
    const auto origin=position+normal*std::min(.003f,radius*.01f);
    // Fixed world directions avoid a tangent-frame seam as the normal varies.
    for(int i=0;i<samples;++i){
        const float y=1-2*(i+.5f)/samples,angle=i*2.39996323f,r=std::sqrt(std::max(0.0f,1-y*y));
        const float3 direction{r*std::cos(angle),y,r*std::sin(angle)};
        const float weight=std::max(0.0f,dot(normal,direction));if(weight<=0)continue;
        const float hit=geometry.distance(origin,direction,radius),fraction=hit/radius;
        visible+=weight*fraction*fraction;total+=weight;
    }
    return total>0?std::clamp(visible/total,0.0f,1.0f):1;
}
// Bake self-occlusion per static mesh, including its opaque primitive boundaries.
// Does not include other scene instances. Existing color alpha is never overwritten.
inline size_t bakeMeshOcclusion(Model& model,float radius=.75f,int samples=64) {
    if(!std::isfinite(radius)||radius<=.01f||radius>100||samples<16||samples>256)
        throw std::runtime_error("invalid mesh occlusion settings");
    for(const auto& v:model.vertices)if((v.color>>24)!=255)throw std::runtime_error("mesh occlusion requires unused vertex alpha");
    for(const auto& m:model.materials)if(m.team==TeamMode::VertexAlpha||m.vertexOcclusion)
        throw std::runtime_error("mesh occlusion conflicts with existing vertex alpha use");
    auto eligible=[&](const MeshDef& mesh,const Primitive& p){const auto& m=model.materials.at(size_t(p.material));
        return !mesh.deformable&&p.jointPalette.empty()&&!p.morphTargets&&m.alpha==AlphaMode::Opaque&&!m.transparent&&!m.doubleSided&&m.shading==ShadingModel::Pbr;
    };
    std::vector<bool> reserved(model.vertices.size());
    for(const auto& mesh:model.meshes)for(const auto& p:mesh.primitives)if(!eligible(mesh,p))
        for(uint32_t j=0;j<p.indexCount;++j)reserved.at(model.indices.at(p.firstIndex+j))=true;
    std::vector<int> owner(model.vertices.size(),-1);std::vector<float> values(model.vertices.size(),1);
    size_t count=0;
    for(size_t mi=0;mi<model.meshes.size();++mi){const auto& mesh=model.meshes[mi];if(mesh.deformable)continue;
        StaticShadowGeometry geometry;std::vector<uint32_t> vertices;
        for(const auto& p:mesh.primitives){
            if(!eligible(mesh,p))continue;
            for(uint32_t j=0;j+2<p.indexCount;j+=3){const auto first=p.firstIndex+j;
                uint32_t a=model.indices.at(first),b=model.indices.at(first+1),c=model.indices.at(first+2);
                geometry.triangles.push_back({model.vertices.at(a).pos,model.vertices.at(b).pos,model.vertices.at(c).pos});
                for(auto index:{a,b,c}){
                    if(reserved.at(index))throw std::runtime_error("mesh occlusion vertex is also used by an unsupported primitive");
                    if(owner.at(index)>=0&&owner[index]!=int(mi))throw std::runtime_error("mesh occlusion requires unshared mesh vertex ranges");
                    if(owner[index]<0){owner[index]=int(mi);vertices.push_back(index);}
                }
            }
        }
        if(vertices.empty())continue;geometry.build();
        for(auto index:vertices){const auto& v=model.vertices[index];values[index]=localDiffuseVisibility(geometry,v.pos,v.nrm,radius,samples);++count;}
    }
    // Commit only after validation and tracing finish; failed bakes leave the model intact.
    for(size_t i=0;i<values.size();++i)if(owner[i]>=0)model.vertices[i].color=(model.vertices[i].color&0xffffffu)|(uint32_t(std::lround(values[i]*255))<<24);
    for(auto& m:model.materials)if(m.alpha==AlphaMode::Opaque&&!m.transparent&&!m.doubleSided&&m.shading==ShadingModel::Pbr)m.vertexOcclusion=true;
    return count;
}
}
