// LumaWeave CPU correctness reference for importance-sampled DIRECT lighting.
// This is NOT a spatial/temporal ReSTIR reservoir or GPU lighting resolve.
// One sample = at most one shadow ray through the SHARED Hybrid GI GPU BVH.
// No sun, sky, baked/indirect bounce, lightmap or historical color is added.
// Sampling is unbiased for the exact candidate set if every nonzero candidate
// retains positive sampling probability. Cluster overflow ALWAYS scans all
// registry sources, never silently discards potentially contributing lights.
#pragma once
#include "LumaWeaveClusters.h"
#include "GiGpuBvh.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct LumaDirectQuery {
    float3 position{};
    float3 normal{0,1,0};
    uint64_t randomSeed=1;      // caller should mix pixel/frame IDs to de-correlate noise
};

struct LumaDirectStats {
    size_t sourcesVisited=0, candidates=0;
    size_t samples=0, shadowRays=0;
    bool fullScan=false;
};

struct LumaDirectResult {
    float3 direct{};              // linear RGB incident cosine-weighted, BEFORE material albedo/BRDF
    LumaDirectStats stats{};
};

class LumaWeaveDirectReference {
public:
    static constexpr uint32_t kMaxShadowSamples=64;

    // O(eligible sources) exhaustive reference; expensive by design.
    static LumaDirectResult exact(const LumaWeaveRegistry& registry,
                                  const LumaDirectQuery& query,
                                  const GiGpuBvh* shadowBvh=nullptr,
                                  const LumaWeaveClusters* clusters=nullptr) {
        const auto candidates=prepare(registry,query,clusters);
        LumaDirectResult result;
        result.stats.sourcesVisited=candidates.visited;
        result.stats.candidates=candidates.data.size();
        result.stats.fullScan=candidates.fullScan;
        const float3 n=normalize(query.normal);
        for(const auto& candidate:candidates.data) {
            const auto& light=registry.lights()[candidate.index];
            if(shadowed(query.position,n,light,shadowBvh,result.stats))continue;
            result.direct+=candidate.value;
        }
        return result;
    }

    // Exactly sampleBudget trials with replacement, each drawing one candidate
    // according to its positive importance weight. No shadow ray is cast for
    // candidates whose castsShadows=false; upper bound is sampleBudget rays.
    static LumaDirectResult estimate(const LumaWeaveRegistry& registry,
                                     const LumaDirectQuery& query,
                                     uint32_t sampleBudget,
                                     const GiGpuBvh* shadowBvh=nullptr,
                                     const LumaWeaveClusters* clusters=nullptr) {
        if(sampleBudget<1 || sampleBudget>kMaxShadowSamples)
            throw std::invalid_argument("LumaWeave samples must be 1..64");
        const auto candidates=prepare(registry,query,clusters);
        LumaDirectResult result;
        result.stats.sourcesVisited=candidates.visited;
        result.stats.candidates=candidates.data.size();
        result.stats.fullScan=candidates.fullScan;
        result.stats.samples=sampleBudget;
        if(candidates.data.empty())return result;
        const float3 n=normalize(query.normal);
        uint64_t rng=query.randomSeed;
        for(uint32_t sample=0;sample<sampleBudget;++sample) {
            const double u=double(random32(rng))*(1.0/4294967296.0);
            const double threshold=u*candidates.totalWeight;
            double cumulative=0.0;
            const Candidate* chosen=&candidates.data.back();
            for(const auto& candidate:candidates.data) {
                cumulative+=candidate.weight;
                if(threshold<cumulative) {chosen=&candidate;break;}
            }
            const auto& light=registry.lights()[chosen->index];
            if(shadowed(query.position,n,light,shadowBvh,result.stats))continue;
            const double inverseProbability=candidates.totalWeight/chosen->weight;
            const float factor=float(inverseProbability/double(sampleBudget));
            result.direct+=chosen->value*factor;
        }
        return result;
    }

private:
    struct Candidate {uint32_t index=0;float3 value{};double weight=0;};
    struct Prepared {
        std::vector<Candidate> data;
        double totalWeight=0;
        size_t visited=0;
        bool fullScan=false;
    };
    static bool finite3(float3 a) {
        return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);
    }
    static uint32_t random32(uint64_t& state) {
        // Deterministic SplitMix64; no std::uniform distribution platform drift.
        state+=0x9e3779b97f4a7c15ULL;
        uint64_t z=state;
        z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
        z=(z^(z>>27))*0x94d049bb133111ebULL;
        return uint32_t((z^(z>>31))>>32);
    }
    static float3 contribution(const LumaLight& light,float3 position,float3 normal) {
        const float3 relative=light.position-position;
        const float d2=dot(relative,relative);
        if(d2<=.0001f || d2>=light.radius*light.radius ||light.intensity<=0)
            return {};
        const float d=std::sqrt(d2);
        const float3 towardLight=relative*(1.0f/d);
        const float cosine=std::max(0.0f,dot(normal,towardLight));
        if(cosine<=0)return {};
        float angular=1;
        if(light.kind==LumaLightKind::Spot) {
            const float cone=dot(light.direction,-towardLight);
            if(cone<=light.cosOuter)return {};
            if(light.cosInner>light.cosOuter)
                angular=std::clamp((cone-light.cosOuter)/(light.cosInner-light.cosOuter),
                                   0.0f,1.0f);
        }
        float attenuation=1.0f-d2/(light.radius*light.radius);
        attenuation=attenuation*attenuation/(1.0f+d2);
        const float factor=light.intensity*cosine*angular*attenuation;
        return light.color*factor;
    }

    static Prepared prepare(const LumaWeaveRegistry& registry,
                             const LumaDirectQuery& query,
                             const LumaWeaveClusters* clusters) {
        if(!finite3(query.position)||!finite3(query.normal)||
           dot(query.normal,query.normal)<1e-10f)
            throw std::invalid_argument("invalid LumaWeave direct-light receiver");
        Prepared out;
        const float3 normal=normalize(query.normal);
        LumaClusterCandidates cell;
        if(clusters)cell=clusters->query(query.position,registry);
        out.fullScan=!clusters || cell.requiresFullScan;
        const auto consider=[&](uint32_t id) {
            ++out.visited;
            const auto& light=registry.lights()[id];
            const auto value=contribution(light,query.position,normal);
            const double lum=.2126*double(value.x)+.7152*double(value.y)+.0722*double(value.z);
            if(lum<=0)return;
            const double weight=lum*light.importanceBias;
            if(!std::isfinite(weight)||weight<=0)
                throw std::overflow_error("LumaWeave importance weight overflow");
            out.data.push_back({id,value,weight});
            out.totalWeight+=weight;
        };
        if(out.fullScan) {
            for(uint32_t i=0;i<registry.lights().size();++i)consider(i);
        } else {
            for(auto id:cell.fastIndices)consider(id);
        }
        if(!std::isfinite(out.totalWeight))
            throw std::overflow_error("LumaWeave total sampling weight overflow");
        return out;
    }

    static bool shadowed(float3 position,float3 normal,
                         const LumaLight& light,const GiGpuBvh* bvh,
                         LumaDirectStats& stats) {
        if(!bvh ||!light.castsShadows ||bvh->nodes.empty())return false;
        const float3 surface=position+normal*.02f;
        const float3 relative=light.position-surface;
        const float lengthSq=dot(relative,relative);
        if(lengthSq<=.0025f)return false;
        const float distance=std::sqrt(lengthSq);
        ++stats.shadowRays;
        return bvh->trace(surface,relative*(1.0f/distance),
                          std::max(0.0f,distance-.03f)).distance<distance-.03f;
    }
};

} // namespace aiverse::render
