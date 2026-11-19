// TerraWeave: optional, host-fed adaptive presentation quality controller.
// Never probes GPU timing or changes geometry: the native host supplies
// measured frame timings and a monotonically increasing frame sequence.
#pragma once
#include "engine/world/TerraWeaveHorizonPresentationQuality.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace aiverse::terraweave {

struct HorizonAdaptiveQualityConfig {
    bool enabled=false; // explicit opt-in; unconfigured worlds never adapt
    HorizonPresentationQualityPreset minimum=HorizonPresentationQualityPreset::Mobile;
    HorizonPresentationQualityPreset maximum=HorizonPresentationQualityPreset::Desktop;
    HorizonPresentationQualityPreset initial=HorizonPresentationQualityPreset::Balanced;
    uint32_t targetFrameUs=16667; // host-measured frame budget (about 60 Hz)
    uint32_t downgradePercent=115; // 15% over target for consecutive samples
    uint32_t upgradePercent=80;    // 20% under target for consecutive samples
    uint32_t downgradeFrames=30;   // slower sustained overload required
    uint32_t upgradeFrames=240;    // substantially slower quality recovery
    uint32_t cooldownFrames=240;   // valid frame IDs since last transition
};

inline void validateHorizonAdaptiveQualityConfig(
        const HorizonAdaptiveQualityConfig& c) {
    const auto rank=[](HorizonPresentationQualityPreset p) {
        return static_cast<uint32_t>(p);
    };
    if(rank(c.minimum)>3u || rank(c.maximum)>3u ||
       rank(c.initial)>3u || rank(c.minimum)>rank(c.maximum) ||
       rank(c.initial)<rank(c.minimum) ||
       rank(c.initial)>rank(c.maximum) ||
       c.targetFrameUs<4000u || c.targetFrameUs>100000u ||
       c.downgradePercent<101u || c.downgradePercent>200u ||
       c.upgradePercent<25u || c.upgradePercent>=100u ||
       c.downgradeFrames<2u || c.downgradeFrames>600u ||
       c.upgradeFrames<2u || c.upgradeFrames>3600u ||
       c.cooldownFrames>36000u)
        throw std::invalid_argument("TerraWeave adaptive Horizon quality config");
}

// Zero time means the host did NOT provide that measurement. At least one
// real CPU/GPU measurement is required; no synthesized or guessed GPU time.
struct HorizonFrameTimingSample {
    uint64_t frameId=0; // monotonically increasing once per rendered frame
    uint32_t cpuFrameUs=0;
    uint32_t gpuFrameUs=0;
};

struct HorizonAdaptiveQualityState {
    HorizonPresentationQualityPreset current=HorizonPresentationQualityPreset::Balanced;
    uint64_t lastFrameId=0;
    uint64_t lastSwitchFrameId=0;
    uint64_t samples=0, switches=0, downgrades=0, upgrades=0;
    uint32_t overloadStreak=0, headroomStreak=0, lastMeasuredFrameUs=0;
    bool hasFrame=false, hasSwitched=false;
};

inline HorizonAdaptiveQualityState initialHorizonAdaptiveQualityState(
        const HorizonAdaptiveQualityConfig& c) {
    validateHorizonAdaptiveQualityConfig(c);
    HorizonAdaptiveQualityState s;
    s.current=c.initial;
    return s;
}

// Deterministic, bounded, one-preset-at-a-time state transition. Only
// consecutive frame IDs count as a sustained streak; duplicates, missing,
// out-of-order and implausible (>10s) samples cannot trigger switches.
// Host may call this with CPU only, GPU only, or both; worst available wins.
inline std::optional<HorizonPresentationQualityPreset>
advanceHorizonAdaptiveQuality(
        const HorizonAdaptiveQualityConfig& c,
        HorizonAdaptiveQualityState& state,
        HorizonFrameTimingSample sample) {
    if(!c.enabled || (!sample.cpuFrameUs && !sample.gpuFrameUs) ||
       sample.cpuFrameUs>10000000u || sample.gpuFrameUs>10000000u ||
       (state.hasFrame && sample.frameId<=state.lastFrameId))
        return std::nullopt;
    const bool consecutive=state.hasFrame &&
        sample.frameId-state.lastFrameId==1u;
    state.lastFrameId=sample.frameId;
    state.hasFrame=true;
    ++state.samples;
    const uint32_t worst=std::max(sample.cpuFrameUs,sample.gpuFrameUs);
    state.lastMeasuredFrameUs=worst;
    if(!consecutive) {
        state.overloadStreak=0;
        state.headroomStreak=0;
    }
    const uint64_t measured=uint64_t(worst)*100u;
    const uint64_t target=uint64_t(c.targetFrameUs);
    if(measured>target*c.downgradePercent) {
        state.overloadStreak=std::min(
            state.overloadStreak+1u,c.downgradeFrames);
        state.headroomStreak=0;
    } else if(measured<target*c.upgradePercent) {
        state.headroomStreak=std::min(
            state.headroomStreak+1u,c.upgradeFrames);
        state.overloadStreak=0;
    } else {
        state.overloadStreak=0;
        state.headroomStreak=0;
    }
    const bool cooldown=state.hasSwitched &&
        sample.frameId-state.lastSwitchFrameId<c.cooldownFrames;
    if(cooldown)return std::nullopt;
    const auto rank=static_cast<uint32_t>(state.current);
    const auto low=static_cast<uint32_t>(c.minimum);
    const auto high=static_cast<uint32_t>(c.maximum);
    uint32_t next=rank;
    if(state.overloadStreak>=c.downgradeFrames && rank>low)
        next=rank-1u;
    else if(state.headroomStreak>=c.upgradeFrames && rank<high)
        next=rank+1u;
    if(next==rank)return std::nullopt;
    state.current=static_cast<HorizonPresentationQualityPreset>(next);
    state.lastSwitchFrameId=sample.frameId;
    state.hasSwitched=true;
    state.overloadStreak=0;
    state.headroomStreak=0;
    ++state.switches;
    if(next<rank)++state.downgrades;
    else ++state.upgrades;
    return state.current;
}
} // namespace aiverse::terraweave
