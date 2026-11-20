// Genre-neutral, explicit authoring bridge: direct point / spot sources -> GI.
// Stable keys and user-controlled indirectMultiplier prevent accidentally
// rebouncing baked/stationary light sources. Does not alter direct lighting.
// Only read after the ECS/light owner has published a consistent snapshot.
#pragma once
#include "GiDynamicProbeReference.h"
#include "Lights.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class GiLightOverflowPolicy : uint8_t {
    Reject,      // default: never silently remove authored indirect sources
    TopInfluence // explicit bounded selection; report dropped in Stats
};

class GiLightBridge {
public:
    struct Stats {
        size_t input=0;
        size_t enabled=0;
        size_t dropped=0;
        size_t output=0;
    };

    // namespaceId isolates stable 32-bit keys from unrelated worlds or light
    // publishers. key=0 is not stable across frames and must not drive GI.
    // The bridge accepts both point and cone-correct spot emitters.
    // TopInfluence is optional; maxSources is usually 16 for GPU or 64 CPU.
    static std::vector<GiMovingLight> collect(
        const std::vector<PointLight>& direct, uint32_t namespaceId,
        size_t maxSources, float3 focus={},
        GiLightOverflowPolicy overflow=GiLightOverflowPolicy::Reject,
        Stats* stats=nullptr) {
        if (namespaceId==0 || maxSources==0 || maxSources>1024 ||
            !finite(focus))
            throw std::invalid_argument("invalid GI source namespace/budget/focus");

        struct Candidate {
            GiMovingLight light;
            double score=0;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(std::min(direct.size(),maxSources));
        for (const auto& p:direct) {
            if (!std::isfinite(p.indirectMultiplier) ||
                p.indirectMultiplier<0 || p.indirectMultiplier>10000)
                throw std::invalid_argument("invalid indirect light multiplier");
            // No GI source: preserve existing direct lighting/baked indirect.
            if (p.indirectMultiplier==0 || p.intensity==0) continue;
            if (p.key==0 || !finite(p.pos) || !finite(p.color) ||
                p.color.x<0 || p.color.y<0 || p.color.z<0 ||
                p.color.x>10000 || p.color.y>10000 || p.color.z>10000 ||
                !std::isfinite(p.intensity) || p.intensity<0 ||
                !std::isfinite(p.radius) || p.radius<=0 || p.radius>1000 ||
                !std::isfinite(p.indirectMultiplier*p.intensity) ||
                p.indirectMultiplier*p.intensity>10000)
                throw std::invalid_argument("invalid GI point light");

            GiMovingLight light;
            light.key=(uint64_t(namespaceId)<<32) | uint64_t(p.key);
            light.position=p.pos;
            light.color=p.color;
            light.radius=p.radius;
            light.intensity=p.indirectMultiplier*p.intensity;
            if(p.spot()) {
                if(!finite(p.dir) || !std::isfinite(p.cosInner) ||
                   !std::isfinite(p.cosOuter) ||
                   p.cosOuter<-1.0f || p.cosOuter>=1.0f ||
                   p.cosInner<p.cosOuter || p.cosInner>1.0f ||
                   dot(p.dir,p.dir)<1e-8f)
                    throw std::invalid_argument("invalid GI spotlight cone");
                light.direction=normalize(p.dir);
                light.cosOuter=p.cosOuter;
                light.cosInner=p.cosInner;
            }
            const float3 delta=p.pos-focus;
            const double d2=double(dot(delta,delta));
            const double stableIntensity=p.rank>=0 && std::isfinite(p.rank)
                ? double(p.rank) : double(p.intensity);
            const double score=stableIntensity*double(p.indirectMultiplier)*
                               double(p.radius)*double(p.radius)/(1.0+d2);
            candidates.push_back({light,score});
        }

        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
            return a.light.key < b.light.key;
        });
        for(size_t i=1;i<candidates.size();++i)
            if(candidates[i-1].light.key==candidates[i].light.key)
                throw std::invalid_argument("duplicate stable GI point light key");

        Stats report;
        report.input=direct.size();
        report.enabled=candidates.size();
        if(candidates.size()>maxSources) {
            if(overflow==GiLightOverflowPolicy::Reject)
                throw std::length_error("dynamic GI light count exceeds selected backend");
            std::partial_sort(candidates.begin(),candidates.begin()+maxSources,
                              candidates.end(),[](const auto& a,const auto& b) {
                                  if(a.score!=b.score)return a.score>b.score;
                                  return a.light.key<b.light.key;
                              });
            report.dropped=candidates.size()-maxSources;
            candidates.resize(maxSources);
        }
        std::vector<GiMovingLight> result;
        result.reserve(candidates.size());
        for(auto& c:candidates)result.push_back(c.light);
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) {
            return a.key<b.key;
        });
        report.output=result.size();
        if(stats)*stats=report;
        return result;
    }

private:
    static bool finite(float3 x) {
        return std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z);
    }
};

} // namespace aiverse::render
