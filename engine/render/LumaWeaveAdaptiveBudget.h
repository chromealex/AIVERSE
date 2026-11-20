// Conservative advisory controller for LumaWeave screen-space quality.
// Device::gpuFrameMs() is the WHOLE previous GPU frame, not LumaWeave time.
// Therefore adaptive changes are intentionally slow, hysteretic and optional;
// no source is ever discarded to meet a timing target.
#pragma once
#include "LumaWeaveQuality.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

struct LumaAdaptiveSettings {
    float targetGpuFrameMs=16.6f;
    float degradeRatio=1.15f;
    float upgradeRatio=.72f;
    uint32_t consecutiveSlowFrames=8;
    uint32_t consecutiveFastFrames=120;
    uint32_t cooldownFrames=120;
    LumaWeaveQuality minimum=LumaWeaveQuality::Mobile;
    LumaWeaveQuality maximum=LumaWeaveQuality::Ultra;
};

class LumaWeaveAdaptiveBudget {
public:
    explicit LumaWeaveAdaptiveBudget(
            LumaAdaptiveSettings config={},
            LumaWeaveQuality initial=LumaWeaveQuality::Balanced)
        :config_(config),quality_(initial) {
        if(!std::isfinite(config.targetGpuFrameMs) ||
           config.targetGpuFrameMs<3 || config.targetGpuFrameMs>100 ||
           !std::isfinite(config.degradeRatio)||config.degradeRatio<=1 ||
           !std::isfinite(config.upgradeRatio)||config.upgradeRatio<=0 ||
           config.upgradeRatio>=1 ||
           !config.consecutiveSlowFrames||!config.consecutiveFastFrames ||
           config.cooldownFrames>100000 ||
           config.minimum==LumaWeaveQuality::Off ||
           uint8_t(config.minimum)>uint8_t(config.maximum) ||
           initial==LumaWeaveQuality::Off)
            throw std::invalid_argument("invalid LumaWeave adaptive GPU budget");
        quality_=clampQuality(initial);
    }

    LumaWeaveQuality quality() const{return quality_;}
    float estimatedGpuFrameMs() const{return emaMs_;}
    uint64_t observations() const{return observations_;}
    uint64_t qualityChanges() const{return switches_;}
    void setQuality(LumaWeaveQuality quality) {
        if(quality==LumaWeaveQuality::Off)
            throw std::invalid_argument("adaptive quality cannot select Off");
        quality_=clampQuality(quality);
        slow_=fast_=0;
        cooldown_=config_.cooldownFrames;
    }
    void reset() {
        emaMs_=0;observations_=switches_=0;
        slow_=fast_=cooldown_=0;
    }

    // Returns true only if an advisory tier change occurred. If the backend
    // has no reliable GPU frame timing, pass 0 or valid=false to do NOTHING.
    bool observe(float completedGpuFrameMs,bool valid) {
        if(!valid||!std::isfinite(completedGpuFrameMs) ||
           completedGpuFrameMs<=0||completedGpuFrameMs>10000)
            return false;
        emaMs_=observations_ ?
            emaMs_*.85f+completedGpuFrameMs*.15f : completedGpuFrameMs;
        ++observations_;
        if(cooldown_) {--cooldown_;slow_=fast_=0;return false;}
        if(emaMs_>config_.targetGpuFrameMs*config_.degradeRatio) {
            ++slow_;fast_=0;
            if(slow_>=config_.consecutiveSlowFrames &&
               uint8_t(quality_)>uint8_t(config_.minimum)) {
                quality_=LumaWeaveQuality(uint8_t(quality_)-1);
                changed();
                return true;
            }
        } else if(emaMs_<config_.targetGpuFrameMs*config_.upgradeRatio) {
            ++fast_;slow_=0;
            if(fast_>=config_.consecutiveFastFrames &&
               uint8_t(quality_)<uint8_t(config_.maximum)) {
                quality_=LumaWeaveQuality(uint8_t(quality_)+1);
                changed();
                return true;
            }
        } else {
            slow_=fast_=0;
        }
        return false;
    }

private:
    LumaWeaveQuality clampQuality(LumaWeaveQuality q) const {
        return LumaWeaveQuality(std::clamp(uint8_t(q),
             uint8_t(config_.minimum),uint8_t(config_.maximum)));
    }
    void changed() {
        ++switches_;
        cooldown_=config_.cooldownFrames;
        slow_=fast_=0;
    }

    LumaAdaptiveSettings config_;
    LumaWeaveQuality quality_;
    float emaMs_=0;
    uint64_t observations_=0,switches_=0;
    uint32_t slow_=0,fast_=0,cooldown_=0;
};

} // namespace aiverse::render
