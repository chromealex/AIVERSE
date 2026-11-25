// Read-only presentation collision queries in particle simulation space.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace aiverse::fx {
struct SceneCollisionQuery {
    render::float3 from{},to{};
    float radius=0;
};
struct SceneCollisionHit {
    float fraction=1,penetration=0; // center path fraction; initial overlap depth
    render::float3 normal{};
    uint64_t provider=0;
};
struct SceneCollisionStats {
    uint32_t queries=0,hits=0,invalid=0,budgetDenied=0;
};
inline bool sceneCollisionFinite(render::float3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
           std::abs(p.x)<=1e7f&&std::abs(p.y)<=1e7f&&std::abs(p.z)<=1e7f;
}
class SceneCollisionRegistry {
public:
    using Sweep=std::function<bool(const SceneCollisionQuery&,SceneCollisionHit&)>;
    static constexpr size_t kMaxProviders=8;
    bool bind(uint64_t id,Sweep sweep) {
        if(!id||!sweep||(!providers_.count(id)&&providers_.size()>=kMaxProviders))return false;
        providers_[id]=std::move(sweep);return true;
    }
    bool unbind(uint64_t id){return providers_.erase(id)!=0;}
    bool sweep(const SceneCollisionQuery& q,SceneCollisionHit& result,SceneCollisionStats& stats)const {
        if(!sceneCollisionFinite(q.from)||!sceneCollisionFinite(q.to)||
           !std::isfinite(q.radius)||q.radius<0||q.radius>1e4f){++stats.invalid;return false;}
        bool found=false;
        for(const auto& [id,fn]:providers_) {
            SceneCollisionHit h;
            if(!fn(q,h))continue;
            const float len=render::length(h.normal);
            if(!sceneCollisionFinite(h.normal)||!std::isfinite(len)||len<=1e-5f||
               !std::isfinite(h.fraction)||h.fraction<0||h.fraction>1||
               !std::isfinite(h.penetration)||h.penetration<0||h.penetration>1e4f||
               (h.penetration>0&&h.fraction!=0)){++stats.invalid;continue;}
            h.normal=h.normal*(1.f/len);h.provider=id;
            if(!found||h.fraction<result.fraction){result=h;found=true;}
        }
        return found;
    }
private:
    std::map<uint64_t,Sweep> providers_;
};

// Host-supplied immutable signed-distance field, measured in metres.
// Distances must be conservative (1-Lipschitz); false means no coverage.
// No extrapolation across missing samples. Fixed work per swept segment.
using SceneSignedDistance=std::function<bool(render::float3,float&)>;
inline SceneCollisionRegistry::Sweep makeSceneSdfSweep(SceneSignedDistance field,
                                                       float epsilon=0.001f) {
    if(!field||!std::isfinite(epsilon)||epsilon<1e-5f||epsilon>0.1f)return {};
    return [field=std::move(field),epsilon](const SceneCollisionQuery& q,SceneCollisionHit& out) {
        const auto delta=q.to-q.from;
        const float length=render::length(delta);
        if(!std::isfinite(length))return false;
        auto sample=[&](render::float3 p,float& d){
            return sceneCollisionFinite(p)&&field(p,d)&&std::isfinite(d)&&std::abs(d)<=1e7f;
        };
        float travelled=0;
        for(unsigned step=0;step<48;++step) {
            const float fraction=length>1e-8f?travelled/length:0.f;
            const auto p=q.from+delta*fraction;
            float d=0;
            if(!sample(p,d))return false;
            const float clearance=d-q.radius;
            if(clearance<=epsilon) {
                float xp,xm,yp,ym,zp,zm;
                if(!sample(p+render::float3{epsilon,0,0},xp)||!sample(p-render::float3{epsilon,0,0},xm)||
                   !sample(p+render::float3{0,epsilon,0},yp)||!sample(p-render::float3{0,epsilon,0},ym)||
                   !sample(p+render::float3{0,0,epsilon},zp)||!sample(p-render::float3{0,0,epsilon},zm))return false;
                const render::float3 gradient{xp-xm,yp-ym,zp-zm};
                const float norm=render::length(gradient);
                if(!std::isfinite(norm)||norm<=1e-8f)return false;
                const auto n=gradient*(1.f/norm);
                // A touching particle moving away must not bounce again.
                if(fraction==0&&clearance>=-epsilon&&render::dot(delta,n)>=0)return false;
                out.fraction=fraction;
                out.penetration=fraction==0?std::max(0.f,-clearance):0.f;
                out.normal=n;
                return true;
            }
            if(travelled>=length)return false;
            travelled=std::min(length,travelled+clearance);
        }
        return false; // bounded trace exhaustion, not an invented contact
    };
}
} // namespace aiverse::fx
