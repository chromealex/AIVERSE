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
// Double-sided cloth (folds seen from both sides) also stores the back-face visibility in COLOR_0 red;
// that needs white vertex colours, which are then switched off for the material (multiplying by white).
inline size_t bakeMeshOcclusion(Model& model,float radius=.75f,int samples=64) {
    if(!std::isfinite(radius)||radius<=.01f||radius>100||samples<16||samples>256)
        throw std::runtime_error("invalid mesh occlusion settings");
    for(const auto& v:model.vertices)if((v.color>>24)!=255)throw std::runtime_error("mesh occlusion requires unused vertex alpha");
    for(const auto& m:model.materials)if(m.team==TeamMode::VertexAlpha||m.vertexOcclusion)
        throw std::runtime_error("mesh occlusion conflicts with existing vertex alpha use");
    // Double-sided materials qualify only when their vertex colours are white and not shared with
    // single-sided primitives; otherwise they stay without AO, as before.
    auto opaquePbr=[](const Material& m){return m.alpha==AlphaMode::Opaque&&!m.transparent&&m.shading==ShadingModel::Pbr;};
    auto rigid=[](const MeshDef& mesh,const Primitive& p){return !mesh.deformable&&p.jointPalette.empty()&&!p.morphTargets;};
    std::vector<bool> backOk(model.materials.size()),used(model.materials.size());
    for(size_t i=0;i<model.materials.size();++i){const auto& m=model.materials[i];backOk[i]=m.doubleSided&&m.team==TeamMode::None&&opaquePbr(m);}
    std::vector<bool> otherUse(model.vertices.size());
    for(const auto& mesh:model.meshes)for(const auto& p:mesh.primitives)if(!backOk.at(size_t(p.material))||!rigid(mesh,p))
        for(uint32_t j=0;j<p.indexCount;++j)otherUse.at(model.indices.at(p.firstIndex+j))=true;
    for(const auto& mesh:model.meshes)for(const auto& p:mesh.primitives){const auto mat=size_t(p.material);if(!backOk.at(mat))continue;
        if(!rigid(mesh,p)){backOk[mat]=false;continue;}
        for(uint32_t j=0;j<p.indexCount&&backOk[mat];++j){const auto index=model.indices.at(p.firstIndex+j);
            if((model.vertices.at(index).color&0xffffffu)!=0xffffffu||otherUse[index])backOk[mat]=false;}
    }
    auto eligibleMaterial=[&](size_t i){const auto& m=model.materials.at(i);return opaquePbr(m)&&(!m.doubleSided||backOk[i]);};
    auto eligible=[&](const MeshDef& mesh,const Primitive& p){return rigid(mesh,p)&&eligibleMaterial(size_t(p.material));};
    std::vector<bool> reserved(model.vertices.size()),twoSided(model.vertices.size());
    for(const auto& mesh:model.meshes)for(const auto& p:mesh.primitives){const bool ok=eligible(mesh,p),two=model.materials.at(size_t(p.material)).doubleSided;
        if(ok)used.at(size_t(p.material))=true;
        for(uint32_t j=0;j<p.indexCount;++j){const auto index=model.indices.at(p.firstIndex+j);if(!ok)reserved.at(index)=true;else if(two)twoSided.at(index)=true;}}
    std::vector<int> owner(model.vertices.size(),-1);std::vector<float> values(model.vertices.size(),1),backs(model.vertices.size(),1);
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
        if(vertices.empty())continue;
        geometry.build();
        for(auto index:vertices){const auto& v=model.vertices[index];values[index]=localDiffuseVisibility(geometry,v.pos,v.nrm,radius,samples);
            if(twoSided[index])backs[index]=localDiffuseVisibility(geometry,v.pos,-v.nrm,radius,samples);
            ++count;}
    }
    // Commit only after validation and tracing finish; failed bakes leave the model intact.
    for(size_t i=0;i<values.size();++i)if(owner[i]>=0){auto& c=model.vertices[i].color;c=(c&0xffffffu)|(uint32_t(std::lround(values[i]*255))<<24);
        if(twoSided[i])c=(c&0xffffff00u)|uint32_t(std::lround(backs[i]*255));}
    for(size_t i=0;i<model.materials.size();++i){auto& m=model.materials[i];if(!eligibleMaterial(i)||(m.doubleSided&&!used[i]))continue;
        m.vertexOcclusion=true;if(m.doubleSided){m.backOcclusion=true;m.vertexColors=false;}}
    return count;
}
}
