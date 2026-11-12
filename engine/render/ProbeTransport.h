#pragma once
#include "StaticDiffuseField.h"
namespace aiverse::render {
// Offline diffuse transport links; no geometry traversal or extra coefficients at runtime.
struct ProbeTransportLink {
    uint32_t receiver=0;
    float3 direction{}, normal{}, albedo{};
    std::array<uint32_t,8> sources{};
    std::array<float,8> weights{};
    int count=0;
};
inline void appendProbeTransport(std::vector<ProbeTransportLink>& links,const StaticShadowGeometry& geometry,
                                const StaticDiffuseField& field,uint32_t receiver,float3 position,
                                float3 normal,float3 direction,float3 albedo) {
    auto coordinate=(position-field.origin)*(1/field.spacing)-float3{.5f,.5f,.5f};
    const int bx=int(std::floor(coordinate.x)),by=int(std::floor(coordinate.y)),bz=int(std::floor(coordinate.z));
    const auto fraction=coordinate-float3{float(bx),float(by),float(bz)};
    ProbeTransportLink link;link.receiver=receiver;link.direction=direction;link.normal=normal;link.albedo=albedo;
    float total=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int px=bx+x,py=by+y,pz=bz+z;
        if(px<0||py<0||pz<0||px>=field.nx||py>=field.ny||pz>=field.nz)continue;
        float weight=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y)*(z?fraction.z:1-fraction.z);
        if(weight<1e-6f)continue;
        auto source=field.origin+float3{px+.5f,py+.5f,pz+.5f}*field.spacing;
        auto delta=source-position;float distance=length(delta);
        if(distance>.001f&&geometry.distance(position,delta*(1/distance),distance)<distance-.03f)continue;
        link.sources[size_t(link.count)]=uint32_t(px+field.nx*(py+field.ny*pz));
        link.weights[size_t(link.count++)]=weight;total+=weight;
    }
    if(total<=1e-6f)return;
    for(int i=0;i<link.count;++i)link.weights[size_t(i)]/=total;
    links.push_back(link);
}
inline void propagateProbeTransport(StaticDiffuseField& field,const std::vector<ProbeTransportLink>& links,int samples,int bounces) {
    struct Basis {std::array<float4,3> sun{},sky{};};
    std::vector<Basis> previous(field.probes.size()),next(field.probes.size());
    for(size_t i=0;i<previous.size();++i){previous[i].sun=field.probes[i].sun;previous[i].sky=field.probes[i].sky;}
    auto eval=[](float4 c,float3 n){return std::max(0.0f,c.x+c.y*n.x+c.z*n.y+c.w*n.z);};
    auto add=[](float4& c,float energy,float3 direction){c.x+=energy;c.y+=2*energy*direction.x;c.z+=2*energy*direction.y;c.w+=2*energy*direction.z;};
    for(int bounce=1;bounce<bounces;++bounce){
        std::fill(next.begin(),next.end(),Basis{});
        for(const auto& link:links){
            const float albedo[3]={link.albedo.x,link.albedo.y,link.albedo.z};
            for(size_t c=0;c<3;++c){float sun=0,sky=0;
                for(int i=0;i<link.count;++i){const auto& source=previous[link.sources[size_t(i)]];const float w=link.weights[size_t(i)];
                    sun+=eval(source.sun[c],link.normal)*w;sky+=eval(source.sky[c],link.normal)*w;}
                add(next[link.receiver].sun[c],sun*albedo[c]/samples,link.direction);
                add(next[link.receiver].sky[c],sky*albedo[c]/samples,link.direction);
            }
        }
        for(size_t i=0;i<next.size();++i)for(size_t c=0;c<3;++c){
            auto accumulate=[](float4& a,float4 b){a.x+=b.x;a.y+=b.y;a.z+=b.z;a.w+=b.w;};
            accumulate(field.probes[i].sun[c],next[i].sun[c]);accumulate(field.probes[i].sky[c],next[i].sky[c]);
        }
        previous.swap(next);
    }
}
}
