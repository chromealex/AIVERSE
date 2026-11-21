// LumaWeave explicit quality and correctness-first GPU work admission.
// The *entire* source registry remains eligible even on cluster overflow;
// target resolution/sample count shrink to a strict upper bound instead of
// truncating light counts or casting unbudgeted rays.
// This is an opt-in capability policy, NOT a claimed FPS performance tier.
#pragma once
#include "LumaWeaveScreenMetal.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

enum class LumaWeaveQuality : uint8_t { Off=0, Mobile=1, Balanced=2, High=3, Ultra=4 };

struct LumaWeaveDecision {
    bool admitted=false;
    uint32_t width=0,height=0,samples=0,downsample=0;
    uint64_t worstCandidateEvaluations=0;
    uint64_t maximumShadowRays=0;
    size_t outputBytes=0; // one raw RGBA16F + two filtered + two normal/depth
};

class LumaWeaveQualityPolicy {
public:
    static LumaScreenSettings settingsFor(LumaWeaveQuality quality) {
        LumaScreenSettings cfg;
        switch(quality) {
            case LumaWeaveQuality::Off:
                cfg.maxPixels=64;cfg.maxSamples=1;cfg.maxCandidateEvaluations=64;break;
            case LumaWeaveQuality::Mobile:
                cfg.maxPixels=4096;cfg.maxSamples=1;cfg.maxCandidateEvaluations=4000000;
                cfg.maxHistoryWeight=.75f;break;
            case LumaWeaveQuality::Balanced:
                cfg.maxPixels=16384;cfg.maxSamples=2;cfg.maxCandidateEvaluations=24000000;
                cfg.maxHistoryWeight=.86f;break;
            case LumaWeaveQuality::High:
                cfg.maxPixels=32768;cfg.maxSamples=4;cfg.maxCandidateEvaluations=64000000;
                cfg.maxHistoryWeight=.9f;break;
            case LumaWeaveQuality::Ultra:
                cfg.maxPixels=65536;cfg.maxSamples=8;cfg.maxCandidateEvaluations=128000000;
                cfg.maxHistoryWeight=.92f;break;
        }
        return cfg;
    }

    static LumaWeaveDecision choose(LumaWeaveQuality quality,
                                   uint32_t viewWidth,uint32_t viewHeight,
                                   size_t sources,
                                   const LumaScreenSettings& limits) {
        LumaWeaveDecision result;
        if(quality==LumaWeaveQuality::Off || !viewWidth || !viewHeight ||
           viewWidth>16384||viewHeight>16384 ||
           sources>LumaWeaveRegistry::kMaxSources)
            return result;
        // Keep a full registry full-scan worst-case budget, even when most
        // clusters are tiny. This prevents overflowing/outside clusters from
        // silently doing O(screenPixels * everyLight) unbounded work.
        uint32_t samples=std::min(settingsFor(quality).maxSamples,limits.maxSamples);
        const uint32_t cap=std::min(settingsFor(quality).maxPixels,limits.maxPixels);
        const uint64_t checksCap=std::min(settingsFor(quality).maxCandidateEvaluations,
                                           limits.maxCandidateEvaluations);
        // Adapt samples before resolution on very crowded scenes, otherwise
        // a 4096-light level can become a 1x1 invisible shadow buffer.
        if(sources>=1024 && samples>2)samples=2;
        if(sources>=2048)samples=1;
        for(uint32_t factor=2;factor<=256;factor*=2) {
            const uint32_t w=(viewWidth+factor-1)/factor;
            const uint32_t h=(viewHeight+factor-1)/factor;
            const uint64_t pixels=uint64_t(w)*h;
            const uint64_t checks=pixels*(uint64_t(samples)+1)*sources;
            if(pixels>cap || checks>checksCap)continue;
            if(pixels<64)break; // no useful screen coverage -> reject
            result.admitted=true;
            result.width=w;result.height=h;
            result.samples=samples;result.downsample=factor;
            result.worstCandidateEvaluations=checks;
            result.maximumShadowRays=pixels*samples;
            result.outputBytes=size_t(pixels)*5u*8u;
            break;
        }
        return result;
    }
};

} // namespace aiverse::render
