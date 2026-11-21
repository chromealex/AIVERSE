// Shared CPU visual-surface picking. Bound rigid authoring geometry only;
// missing CPU data never falls back to an invisible radius or box. GPU vertex
// displacement/baked deformation require a dedicated rendered-ID path.
#pragma once
#include "Model.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace aiverse::render {
// Model-space bounds are tested in world coordinates; this is never a hit result.
inline bool rayModelBounds(float3 origin,float3 direction,float3 lo,float3 hi,const mat4& transform,float limit=std::numeric_limits<float>::infinity()) {
    float3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
    for(int i=0;i<8;++i){const auto p=transform.transformPoint({(i&1)?hi.x:lo.x,(i&2)?hi.y:lo.y,(i&4)?hi.z:lo.z});
        low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
    const float start[3]={origin.x,origin.y,origin.z},dir[3]={direction.x,direction.y,direction.z},minimum[3]={low.x,low.y,low.z},maximum[3]={high.x,high.y,high.z};
    float near=0,far=limit;
    for(int i=0;i<3;++i){if(std::abs(dir[i])<1e-8f){if(start[i]<minimum[i]||start[i]>maximum[i])return false;}
        else{float a=(minimum[i]-start[i])/dir[i],b=(maximum[i]-start[i])/dir[i];if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);if(near>far)return false;}}
    return true;
}
inline float modelPickAlpha(const Model& model,const Material& material,float2 uv) {
    if(material.baseTex<0)return 1;
    if(size_t(material.baseTex)>=model.images.size())return 0;
    const auto& image=model.images[size_t(material.baseTex)];
    const auto* pixels=!image.rgba.empty()?&image.rgba:image.sharedRgba.get();
    if(!pixels||image.width<1||image.height<1||pixels->size()<size_t(image.width)*image.height*4)return 0;
    const float x=uv.x*material.uvScale.x,y=uv.y*material.uvScale.y,cs=std::cos(material.uvRotation),sn=std::sin(material.uvRotation);
    uv={material.uvOffset.x+cs*x-sn*y,material.uvOffset.y+sn*x+cs*y};
    const auto alpha=[&](int x,int y){if(image.repeat){x=(x%image.width+image.width)%image.width;y=(y%image.height+image.height)%image.height;}
        else{x=std::clamp(x,0,image.width-1);y=std::clamp(y,0,image.height-1);}return float((*pixels)[(size_t(y)*image.width+x)*4+3])/255.f;};
    if(!std::isfinite(uv.x)||!std::isfinite(uv.y))return 0;
    uv.x=image.repeat?uv.x-std::floor(uv.x):std::clamp(uv.x,0.f,1.f);
    uv.y=image.repeat?uv.y-std::floor(uv.y):std::clamp(uv.y,0.f,1.f);
    const float px=uv.x*image.width-.5f,py=uv.y*image.height-.5f;
    if(image.nearest)return alpha(int(std::floor(px+.5f)),int(std::floor(py+.5f)));
    const int ix=int(std::floor(px)),iy=int(std::floor(py));const float fx=px-ix,fy=py-iy;
    return (alpha(ix,iy)*(1-fx)+alpha(ix+1,iy)*fx)*(1-fy)+(alpha(ix,iy+1)*(1-fx)+alpha(ix+1,iy+1)*fx)*fy;
}
struct ModelSurfaceHit {
    float distance = std::numeric_limits<float>::infinity();
    int node = -1;
    int mesh = -1;
    int primitive = -1;
    std::size_t triangle = 0;
    explicit operator bool() const { return std::isfinite(distance); }
};

// Moller-Trumbore, double-sided, ray measured in world-space units when dir
// is normalized. Handles thin and open surfaces without inflated bounds.
inline float rayModelTriangle(float3 origin,float3 direction,
                              float3 a,float3 b,float3 c) {
    constexpr float epsilon=1e-7f;
    const auto edge1=b-a, edge2=c-a;
    const auto p=cross(direction,edge2);
    const float determinant=dot(edge1,p);
    if (!std::isfinite(determinant) || std::abs(determinant)<epsilon)
        return std::numeric_limits<float>::infinity();
    const float inverse=1.0f/determinant;
    const auto t=origin-a;
    const float u=dot(t,p)*inverse;
    if (!std::isfinite(u) || u<0 || u>1) return std::numeric_limits<float>::infinity();
    const auto q=cross(t,edge1);
    const float v=dot(direction,q)*inverse;
    if (!std::isfinite(v) || v<0 || u+v>1) return std::numeric_limits<float>::infinity();
    const float distance=dot(edge2,q)*inverse;
    return std::isfinite(distance) && distance>=epsilon
        ? distance : std::numeric_limits<float>::infinity();
}

inline ModelSurfaceHit pickModelSurface(
    const Model& model,const mat4& world,
    float3 origin,float3 direction,
    std::size_t maxTriangles=1000000,
    const std::vector<mat4>* posedNodes=nullptr,
    const std::vector<Material>* resolvedMaterials=nullptr,
    const std::vector<int>* nodeMaterials=nullptr) {
    ModelSurfaceHit hit;
    const float length2=dot(direction,direction);
    if (!std::isfinite(length2) || std::abs(length2-1.0f)>0.01f ||
        !std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))
        return hit;
    if (model.vertices.empty() || model.indices.empty() ||
        model.bindWorld.size()!=model.nodes.size() || maxTriangles==0)
        return hit;
    const auto& worlds=posedNodes?*posedNodes:model.bindWorld;
    if(worlds.size()!=model.nodes.size())return hit;
    const auto& materials=resolvedMaterials?*resolvedMaterials:model.materials;
    std::size_t tested=0;
    for (std::size_t n=0;n<model.nodes.size();++n) {
        const auto& node=model.nodes[n];
        if (node.mesh<0 || std::size_t(node.mesh)>=model.meshes.size()) continue;
        // CPU bind-world is valid for rigid geometry only. Deformed meshes
        // need the same current skin/morph pose as the rendered surface.
        if (node.skin>=0 || model.meshes[std::size_t(node.mesh)].deformable) continue;
        const auto& mesh=model.meshes[std::size_t(node.mesh)];
        const mat4 transform=world*worlds[n];
        for (std::size_t p=0;p<mesh.primitives.size();++p) {
            const auto& primitive=mesh.primitives[p];
            if (primitive.morphTargets>0 || !primitive.jointPalette.empty()) continue;
            if (primitive.firstIndex>model.indices.size() ||
                primitive.indexCount>model.indices.size()-primitive.firstIndex)
                continue;
            // Broad phase is only an acceleration; it never selects a box.
            if(primitive.vertexCount>0&&!rayModelBounds(origin,direction,primitive.boundsMin,primitive.boundsMax,transform,hit.distance))continue;
            int materialIndex=primitive.material;
            if(nodeMaterials&&n<nodeMaterials->size()&&(*nodeMaterials)[n]>=0)materialIndex=(*nodeMaterials)[n];
            const Material* material=materialIndex>=0&&size_t(materialIndex)<materials.size()?&materials[size_t(materialIndex)]:nullptr;
            for (std::size_t t=0;t+2<primitive.indexCount;t+=3) {
                if (tested++>=maxTriangles) return {};
                const auto i=primitive.firstIndex+t;
                const uint32_t ia=model.indices[i], ib=model.indices[i+1],ic=model.indices[i+2];
                if (ia>=model.vertices.size()||ib>=model.vertices.size()||
                    ic>=model.vertices.size()) continue;
                const auto a=transform.transformPoint(model.vertices[ia].pos);
                const auto b=transform.transformPoint(model.vertices[ib].pos);
                const auto c=transform.transformPoint(model.vertices[ic].pos);
                const float d=rayModelTriangle(origin,direction,a,b,c);
                if (d<hit.distance) {
                    if(material&&!material->doubleSided&&dot(cross(b-a,c-a),direction)>=0)continue;
                    if(material&&material->alpha!=AlphaMode::Opaque){
                        const auto edge1=b-a,edge2=c-a,p=cross(direction,edge2),q=cross(origin-a,edge1);
                        const float inverse=1.f/dot(edge1,p);
                        const float u=dot(origin-a,p)*inverse,v=dot(direction,q)*inverse;
                        const auto& va=model.vertices[ia];const auto& vb=model.vertices[ib];const auto& vc=model.vertices[ic];
                        const float2 uv{va.uv.x*(1-u-v)+vb.uv.x*u+vc.uv.x*v,va.uv.y*(1-u-v)+vb.uv.y*u+vc.uv.y*v};
                        float alpha=material->baseColor.w;
                        if(material->vertexColors)alpha*=float((va.color>>24)&255)*(1-u-v)/255.f+float((vb.color>>24)&255)*u/255.f+float((vc.color>>24)&255)*v/255.f;
                        if(material->baseTex>=0)alpha*=modelPickAlpha(model,*material,uv);
                        if(alpha<(material->alpha==AlphaMode::Mask?material->alphaCutoff:1.f/255.f))continue;
                    }
                    hit.distance=d;
                    hit.node=int(n);
                    hit.mesh=node.mesh;
                    hit.primitive=int(p);
                    hit.triangle=t/3;
                }
            }
        }
    }
    return hit;
}

}
