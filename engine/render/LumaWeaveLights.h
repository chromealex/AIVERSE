// LumaWeave: stable, genre-independent source registry for mass dynamic direct lighting.
// This is a PRESENTATION snapshot, not an authoritative ECS/save/replication store.
// Direct lighting and GI share source identity, but never share accumulated radiance.
// The old LightSystem is deliberately unchanged; adoption is explicit and opt-in.
#pragma once
#include "Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

enum class LumaLightKind : uint8_t { Point=0, Spot=1 };

struct LumaLight {
    uint64_t key=0;                        // stable ECS/scene source, MUST NOT be frame index
    LumaLightKind kind=LumaLightKind::Point;
    float3 position{};
    float3 direction{0,-1,0};              // normalized for Spot; ignored for Point
    float3 color{1,1,1};                   // linear RGB, NO baked/mapped contribution
    float radius=4;                        // world metres, finite extent
    float intensity=1;                     // linear nonnegative energy scale
    float cosInner=1, cosOuter=0;           // Spot inner/outer cone cosines
    float importanceBias=1;                // sampling weight ONLY, never brightness
    bool castsShadows=true;
    bool contributesToGi=false;           // ONLY if this light was NOT baked as static
};

struct LumaLightBounds {
    float3 min{},max{};
};

inline LumaLightBounds lumaLightBounds(const LumaLight& light) {
    const float3 extent{light.radius,light.radius,light.radius};
    return {light.position-extent,light.position+extent};
}

inline bool lumaSameLight(const LumaLight& a,const LumaLight& b) {
    return a.key==b.key && a.kind==b.kind &&
           a.position.x==b.position.x && a.position.y==b.position.y && a.position.z==b.position.z &&
           a.direction.x==b.direction.x && a.direction.y==b.direction.y && a.direction.z==b.direction.z &&
           a.color.x==b.color.x && a.color.y==b.color.y && a.color.z==b.color.z &&
           a.radius==b.radius && a.intensity==b.intensity &&
           a.cosInner==b.cosInner && a.cosOuter==b.cosOuter &&
           a.importanceBias==b.importanceBias &&
           a.castsShadows==b.castsShadows && a.contributesToGi==b.contributesToGi;
}

inline bool lumaValidLight(const LumaLight& light) {
    const auto finite3=[](float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    };
    if(!light.key || !finite3(light.position)||!finite3(light.color) ||
       !finite3(light.direction) || light.color.x<0 ||light.color.y<0 ||
       light.color.z<0 ||light.color.x>10000 ||light.color.y>10000 ||
       light.color.z>10000 ||
       !std::isfinite(light.radius)||light.radius<.05f||light.radius>1000 ||
       !std::isfinite(light.intensity)||light.intensity<0||light.intensity>10000 ||
       !std::isfinite(light.importanceBias)||light.importanceBias<=0 ||
       light.importanceBias>64)
        return false;
    if(light.kind==LumaLightKind::Point)return true;
    if(light.kind!=LumaLightKind::Spot ||
       !std::isfinite(light.cosOuter)||!std::isfinite(light.cosInner) ||
       light.cosOuter< -1 ||light.cosOuter>1 ||
       light.cosInner<light.cosOuter||light.cosInner>1)
        return false;
    const float d=dot(light.direction,light.direction);
    return std::isfinite(d)&&std::abs(d-1.0f)<.002f;
}

struct LumaLightEdit {
    uint64_t key=0;
    bool hadPrevious=false,hasCurrent=false;
    LumaLightBounds previous{},current{}; // both ends of a moved light must invalidate GI
    bool affectGi=false;
};

struct LumaPublishResult {
    bool changed=false;
    uint64_t revision=0;
    size_t inserted=0,removed=0,updated=0;
    std::vector<LumaLightEdit> edits;  // stable-key ordered, source-change notifications
};

class LumaWeaveRegistry {
public:
    static constexpr size_t kMaxSources=4096;

    LumaPublishResult publish(std::vector<LumaLight> next) {
        if(next.size()>kMaxSources)
            throw std::length_error("LumaWeave light count exceeds 4096");
        for(const auto& light:next)
            if(!lumaValidLight(light))
                throw std::invalid_argument("invalid LumaWeave source");
        std::sort(next.begin(),next.end(),[](const auto& a,const auto& b) {
            return a.key<b.key;
        });
        for(size_t i=1;i<next.size();++i)
            if(next[i-1].key==next[i].key)
                throw std::invalid_argument("duplicate LumaWeave stable source ID");

        LumaPublishResult out;
        out.revision=revision_;
        size_t a=0,b=0;
        while(a<lights_.size() || b<next.size()) {
            if(b==next.size() || (a<lights_.size() && lights_[a].key<next[b].key)) {
                const auto& old=lights_[a++];
                out.edits.push_back({old.key,true,false,lumaLightBounds(old),{},old.contributesToGi});
                ++out.removed;
            } else if(a==lights_.size() || next[b].key<lights_[a].key) {
                const auto& current=next[b++];
                out.edits.push_back({current.key,false,true,{},lumaLightBounds(current),
                                     current.contributesToGi});
                ++out.inserted;
            } else {
                const auto& old=lights_[a++];
                const auto& current=next[b++];
                if(!lumaSameLight(old,current)) {
                    out.edits.push_back({current.key,true,true,
                        lumaLightBounds(old),lumaLightBounds(current),
                        old.contributesToGi||current.contributesToGi});
                    ++out.updated;
                }
            }
        }
        if(!out.edits.empty()) {
            if(revision_==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("LumaWeave revision exhausted");
            lights_.swap(next); // atomic source publication after all validation
            out.changed=true;
            out.revision=++revision_;
        }
        return out;
    }

    const std::vector<LumaLight>& lights() const {return lights_;}
    uint64_t revision() const {return revision_;}
    const LumaLight* find(uint64_t key) const {
        auto it=std::lower_bound(lights_.begin(),lights_.end(),key,
                                 [](const LumaLight& l,uint64_t k){return l.key<k;});
        return it!=lights_.end() && it->key==key ? &*it : nullptr;
    }
private:
    std::vector<LumaLight> lights_;
    uint64_t revision_=0;
};

} // namespace aiverse::render
