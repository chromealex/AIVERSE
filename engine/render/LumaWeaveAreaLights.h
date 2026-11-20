// LumaWeave finite rectangular/disk/sphere emitter approximation.
// Each area emitter expands into 4/8/16 stable point/hemisphere Spot samples.
// Existing clustered, CPU stochastic and Metal BVH shadow paths need NO new
// ray tracer. Per-sample energy is divided by sample count, so moving shadows
// become soft by averaging visibility. NOT an exact analytic area-light BRDF.
// Area children occupy reserved stable-key namespace (high bit=1); regular
// light keys must stay below that namespace when combined.
#pragma once
#include "LumaWeaveLights.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

enum class LumaAreaKind : uint8_t { Rectangle=0, Disk=1, Sphere=2 };
struct LumaAreaEmitter {
    uint64_t key=0; // 1..2^59-1 so key<<4 and child sample index are stable
    LumaAreaKind kind=LumaAreaKind::Rectangle;
    float3 center{};
    float3 tangentU{1,0,0},tangentV{0,0,1}; // orthonormal emitter plane
    float halfWidth=1,halfHeight=1;          // metres; Sphere uses halfWidth as radius
    float influenceRadius=8;                 // total source attenuation extent
    float3 color{1,1,1};
    float intensity=4;                      // distributed across all samples
    uint8_t samples=8;                      // 4, 8 or 16
    bool twoSided=false;
    bool castsShadows=true;
    bool contributesToGi=false;            // point GI approximation only if requested
};

class LumaWeaveAreaLights {
public:
    static constexpr uint64_t kAreaDomain=1ull<<63;
    static constexpr uint64_t kMaxEmitterKey=(1ull<<59)-1;

    static uint64_t childKey(uint64_t source,uint32_t index) {
        if(!source || source>kMaxEmitterKey || index>=16)
            throw std::invalid_argument("LumaWeave area source key/index out of range");
        return kAreaDomain|(source<<4)|index;
    }
    static void validate(const LumaAreaEmitter& source) {
        const auto finite=[](float3 v) {
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        };
        if(!source.key || source.key>kMaxEmitterKey ||
           source.samples!=4 && source.samples!=8 && source.samples!=16 ||
           !finite(source.center)||!finite(source.tangentU)||!finite(source.tangentV) ||
           !finite(source.color)||source.color.x<0||source.color.y<0||source.color.z<0 ||
           !std::isfinite(source.intensity)||source.intensity<0 ||
           source.intensity>10000 ||
           !std::isfinite(source.influenceRadius)||source.influenceRadius<.05f ||
           source.influenceRadius>1000 ||
           !std::isfinite(source.halfWidth)||source.halfWidth<=0 ||
           source.halfWidth>100 ||
           !std::isfinite(source.halfHeight)||source.halfHeight<=0 ||
           source.halfHeight>100 ||
           unsigned(source.kind)>unsigned(LumaAreaKind::Sphere))
            throw std::invalid_argument("invalid LumaWeave area emitter");
        if(source.kind==LumaAreaKind::Sphere)return;
        const float lu=length(source.tangentU),lv=length(source.tangentV);
        if(std::abs(lu-1)>0.001f ||std::abs(lv-1)>0.001f ||
           std::abs(dot(source.tangentU,source.tangentV))>.001f)
            throw std::invalid_argument("LumaWeave emitter tangent basis must be orthonormal");
    }

    static std::vector<LumaLight> sample(const LumaAreaEmitter& src) {
        validate(src);
        std::vector<LumaLight> result;
        result.reserve(src.samples);
        const float3 normal=src.kind==LumaAreaKind::Sphere ?
            float3{0,1,0} : normalize(cross(src.tangentU,src.tangentV));
        constexpr float goldenAngle=2.39996322972865332f;
        const int rows=src.samples==4?2:src.samples==8?2:4;
        const int columns=int(src.samples)/rows;
        for(uint32_t i=0;i<src.samples;++i) {
            LumaLight light;
            light.key=childKey(src.key,i);
            light.color=src.color;
            light.radius=src.influenceRadius;
            light.intensity=src.intensity/float(src.samples);
            light.importanceBias=1;
            light.castsShadows=src.castsShadows;
            light.contributesToGi=src.contributesToGi;
            const float u=(float(i%uint32_t(columns))+.5f)/float(columns)*2-1;
            const float v=(float(i/uint32_t(columns))+.5f)/float(rows)*2-1;
            float3 offset{};
            if(src.kind==LumaAreaKind::Rectangle) {
                offset=src.tangentU*(u*src.halfWidth)+
                       src.tangentV*(v*src.halfHeight);
            } else if(src.kind==LumaAreaKind::Disk) {
                const float r=std::sqrt((float(i)+.5f)/float(src.samples));
                const float theta=goldenAngle*float(i);
                offset=src.tangentU*(std::cos(theta)*r*src.halfWidth)+
                       src.tangentV*(std::sin(theta)*r*src.halfWidth);
            } else {
                const float y=1-2*(float(i)+.5f)/float(src.samples);
                const float phi=goldenAngle*float(i);
                const float ring=std::sqrt(std::max(0.f,1-y*y));
                offset=float3{ring*std::cos(phi),y,ring*std::sin(phi)}*src.halfWidth;
            }
            light.position=src.center+offset;
            if(src.kind!=LumaAreaKind::Sphere) {
                light.kind=LumaLightKind::Spot;
                light.direction=normal;
                light.cosOuter=src.twoSided?-1.f:0.f;
                light.cosInner=src.twoSided?-1.f:.2f;
            }
            if(!lumaValidLight(light))
                throw std::invalid_argument("generated invalid LumaWeave area child");
            result.push_back(light);
        }
        return result;
    }

    static std::vector<LumaLight> combine(std::vector<LumaLight> regular,
                                           std::span<const LumaAreaEmitter> areas) {
        if(areas.size()>1024)throw std::length_error("LumaWeave too many area emitters");
        for(const auto& l:regular)
            if(l.key&kAreaDomain)
                throw std::invalid_argument("ordinary light occupies reserved area ID namespace");
        size_t total=regular.size();
        for(const auto& emitter:areas) {
            validate(emitter);
            total+=emitter.samples;
            if(total>LumaWeaveRegistry::kMaxSources)
                throw std::length_error("LumaWeave area expansion exceeds registry budget");
        }
        regular.reserve(total);
        for(const auto& emitter:areas) {
            auto sampled=sample(emitter);
            regular.insert(regular.end(),sampled.begin(),sampled.end());
        }
        // LumaWeaveRegistry::publish validates duplicates transactionally.
        return regular;
    }
};

} // namespace aiverse::render
