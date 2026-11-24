// Read-only sampled terrain collision adapter. Height/altitude is not an
// exact Euclidean SDF; radius is measured along the supplied reference up.
// Fixed work, first sampled crossing, no guarantee for sub-sample features.
#pragma once
#include "SpectraSceneCollision.h"
namespace aiverse::fx {
struct TerrainCollisionSample {
    float clearance=0;
    render::float3 up{0,1,0};
};
using TerrainCollisionSampler=std::function<bool(render::float3,TerrainCollisionSample&)>;
inline SceneCollisionRegistry::Sweep makeTerrainSweep(TerrainCollisionSampler sampler,
                                                       float stepMetres=0.25f) {
    if(!sampler||!std::isfinite(stepMetres)||stepMetres<0.01f||stepMetres>100.f)return {};
    return [sampler=std::move(sampler),stepMetres](const SceneCollisionQuery& q,SceneCollisionHit& out){
        const auto delta=q.to-q.from;
        const float length=render::length(delta);
        if(!std::isfinite(length))return false;
        const auto count=unsigned(std::clamp(std::ceil(double(length)/stepMetres),1.0,64.0));
        auto read=[&](float t,TerrainCollisionSample& s){
            if(!sampler(q.from+delta*t,s)||!std::isfinite(s.clearance)||
               !sceneCollisionFinite(s.up))return false;
            const float n=render::length(s.up);
            if(!std::isfinite(n)||n<=1e-5f)return false;
            s.up=s.up*(1.f/n);
            return true;
        };
        TerrainCollisionSample first;
        if(!read(0,first))return false;
        const float initial=first.clearance-q.radius;
        if(initial<0.f || (initial<=0.001f&&render::dot(delta,first.up)<0)) {
            out={0,std::max(0.f,-initial),first.up,0};return true;
        }
        float previous=0;
        for(unsigned i=1;i<=count;++i) {
            const float at=float(i)/float(count);
            TerrainCollisionSample sample;
            if(!read(at,sample))return false; // never bridge missing coverage
            if(sample.clearance-q.radius<=0.f) {
                float lo=previous,hi=at;
                for(unsigned j=0;j<10;++j) {
                    const float mid=(lo+hi)*0.5f;
                    TerrainCollisionSample m;
                    if(!read(mid,m))return false;
                    if(m.clearance-q.radius>0)lo=mid;
                    else {hi=mid;sample=m;}
                }
                // Resolve from the last known nonpenetrating position.
                out={lo,0,sample.up,0};return true;
            }
            previous=at;
        }
        return false;
    };
}
}
