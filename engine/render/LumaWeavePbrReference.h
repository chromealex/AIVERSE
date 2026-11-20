// Portable exhaustive CPU PBR oracle for LumaWeave stochastic GGX direct.
// This deliberately avoids shader/temporal code and compares one explicit
// light at a time with the existing LumaWeaveDirectReference (and shared BVH).
// Expensive O(number of lights): testing/parity and artist validation ONLY.
// Output is linear MATERIAL-REFLECTED direct RGB. No sun/sky/baked/indirect.
#pragma once
#include "LumaWeaveDirectReference.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

struct LumaPbrReceiver {
    float3 position{};
    float3 normal{0,1,0};
    float3 cameraPosition{0,0,4};
    float3 baseColor{1,1,1};
    float roughness=.5f;
    float metallic=0;
};

struct LumaPbrExactResult {
    float3 diffuse{},specular{};
    size_t lights=0,shadowRays=0;
    float3 total() const {return diffuse+specular;}
};

class LumaWeavePbrReference {
public:
    static bool valid(const LumaPbrReceiver& s) {
        const auto finite=[](float3 v){
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        };
        return finite(s.position)&&finite(s.normal)&&finite(s.cameraPosition)&&
               finite(s.baseColor)&&dot(s.normal,s.normal)>1e-10f &&
               dot(s.cameraPosition-s.position,s.cameraPosition-s.position)>1e-10f &&
               s.baseColor.x>=0&&s.baseColor.y>=0&&s.baseColor.z>=0&&
               s.baseColor.x<=1&&s.baseColor.y<=1&&s.baseColor.z<=1&&
               std::isfinite(s.roughness)&&s.roughness>=0&&s.roughness<=1&&
               std::isfinite(s.metallic)&&s.metallic>=0&&s.metallic<=1;
    }

    static LumaPbrExactResult exact(const LumaWeaveRegistry& sources,
                                    const LumaPbrReceiver& receiver,
                                    const GiGpuBvh* shadows=nullptr) {
        if(!valid(receiver))
            throw std::invalid_argument("invalid LumaWeave PBR receiver");
        LumaPbrExactResult result;
        const float3 N=normalize(receiver.normal);
        const float3 V=normalize(receiver.cameraPosition-receiver.position);
        const float rough=std::clamp(receiver.roughness,.045f,1.f);
        constexpr float invPi=1.0f/3.14159265358979323846f;
        for(const auto& source:sources.lights()) {
            // The scalar cosine-radiance is EXACTLY the existing direct light
            // and shadow contract; this oracle only applies the receiver BRDF.
            LumaWeaveRegistry one;
            (void)one.publish({source});
            LumaDirectQuery query;
            query.position=receiver.position;
            query.normal=N;
            const auto incident=LumaWeaveDirectReference::exact(one,query,shadows);
            result.shadowRays+=incident.stats.shadowRays;
            const float3 C=incident.direct;
            if(C.x==0 && C.y==0 && C.z==0)continue;
            const float3 relative=source.position-receiver.position;
            if(dot(relative,relative)<1e-8f)continue;
            const float3 L=normalize(relative);
            const float ndl=std::max(0.f,dot(N,L));
            const float ndv=std::max(0.f,dot(N,V));
            if(ndl<1e-5f||ndv<1e-5f)continue;
            const float3 H=normalize(V+L);
            if(dot(V+L,V+L)<1e-8f)continue;
            const float ndh=std::max(0.f,dot(N,H));
            const float vdh=std::max(0.f,dot(V,H));
            const float a=rough*rough;
            const float a2=a*a;
            const float d=ndh*ndh*(a2-1.f)+1.f;
            const float D=a2*invPi/(d*d);
            const float k=(rough+1.f)*(rough+1.f)*.125f;
            const float G=(ndl/(ndl*(1.f-k)+k))*
                          (ndv/(ndv*(1.f-k)+k));
            const float3 F0={
                .04f+(receiver.baseColor.x-.04f)*receiver.metallic,
                .04f+(receiver.baseColor.y-.04f)*receiver.metallic,
                .04f+(receiver.baseColor.z-.04f)*receiver.metallic
            };
            const float factor=std::pow(1.f-vdh,5.f);
            const float3 F={
                F0.x+(1.f-F0.x)*factor,
                F0.y+(1.f-F0.y)*factor,
                F0.z+(1.f-F0.z)*factor
            };
            const float3 diff={
                receiver.baseColor.x*(1.f-receiver.metallic)*(1.f-F.x)*invPi,
                receiver.baseColor.y*(1.f-receiver.metallic)*(1.f-F.y)*invPi,
                receiver.baseColor.z*(1.f-receiver.metallic)*(1.f-F.z)*invPi
            };
            const float scale=D*G/std::max(4.f*ndl*ndv,1e-5f);
            const float3 spec=F*scale;
            result.diffuse+=float3{C.x*diff.x,C.y*diff.y,C.z*diff.z};
            result.specular+=float3{C.x*spec.x,C.y*spec.y,C.z*spec.z};
            ++result.lights;
        }
        return result;
    }
};

} // namespace aiverse::render
