// SpectraFX — camera/host significance for visual presentation cues.
// Opt-in and read-only. No ECS, gameplay/genre, renderer, or RHI ownership.
#pragma once
#include "SpectraFxCueBridge.h"
#include "SpectraFxView.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace aiverse::fx {

class SpectraFxSignificance {
public:
    struct Config {
        bool enabled=false;
        float nearDistance=12.0f;
        float farDistance=250.0f;
        float targetRadiusPixels=24.0f;
        float peripheralMultiplier=0.25f;
        float behindMultiplier=0.05f;
    };
    bool configure(const Config& cfg) {
        if(!std::isfinite(cfg.nearDistance) || cfg.nearDistance<=0 ||
           !std::isfinite(cfg.farDistance) ||
           cfg.farDistance<=cfg.nearDistance || cfg.farDistance>1000000 ||
           !std::isfinite(cfg.targetRadiusPixels) ||
           cfg.targetRadiusPixels<=0 || cfg.targetRadiusPixels>10000 ||
           !bounded(cfg.peripheralMultiplier,0,1) ||
           !bounded(cfg.behindMultiplier,0,1)) return false;
        config_=cfg;
        return true;
    }
    bool enabled() const {return config_.enabled;}
    const Config& config() const {return config_;}
    // Event weights are presentation importance only; they never suppress
    // a Play/Move/Stop/Kill, alter gameplay events or change a cue's identity.
    bool setEventWeight(uint32_t eventId,float weight) {
        if(!bounded(weight,0.01f,100.0f))return false;
        if(weight==1.0f)weights_.erase(eventId);
        else weights_[eventId]=weight;
        return true;
    }
    void clearEventWeights() {weights_.clear();}
    float eventWeight(uint32_t id) const {
        const auto it=weights_.find(id);
        return it==weights_.end()?1.0f:it->second;
    }
    float score(const SpectraFxCue& cue,const SpectraFxView& view) const {
        if(!view.valid())return eventWeight(cue.id);
        const float dx=cue.position.x-view.eye.x;
        const float dy=cue.position.y-view.eye.y;
        const float dz=cue.position.z-view.eye.z;
        const float d2=dx*dx+dy*dy+dz*dz;
        if(!std::isfinite(d2))return 0;
        const float distance=std::sqrt(std::max(0.0f,d2));
        const float fLen=length(view.forward);
        const float rLen=length(view.right);
        const float uLen=length(view.up);
        const float depth=(dx*view.forward.x+dy*view.forward.y+
                           dz*view.forward.z)/fLen;
        const float horizontal=(dx*view.right.x+dy*view.right.y+
                                dz*view.right.z)/rLen;
        const float vertical=(dx*view.up.x+dy*view.up.y+
                              dz*view.up.z)/uLen;
        if(!std::isfinite(depth)||!std::isfinite(horizontal)||
           !std::isfinite(vertical))return 0;
        const float z=std::max(0.01f,depth);
        const float projectedRadius=std::max(0.0f,cue.scale)*
            view.height*view.projectionYScale/(2.0f*z);
        const float screenX=horizontal*view.projectionYScale*view.height/
                            (z*float(view.width));
        const float screenY=vertical*view.projectionYScale/z;
        const bool peripheral=std::abs(screenX)>1.25f ||
                               std::abs(screenY)>1.25f;
        const float visibility=depth<=0?config_.behindMultiplier:
                               peripheral?config_.peripheralMultiplier:1.0f;
        const float falloff=1.0f-std::clamp(
            (distance-config_.nearDistance)/
            (config_.farDistance-config_.nearDistance),0.0f,1.0f);
        const float radius=std::clamp(
            projectedRadius/config_.targetRadiusPixels,0.0f,1.0f);
        const float score=eventWeight(cue.id)*(0.1f+0.9f*falloff)*
                          (0.2f+0.8f*radius)*visibility;
        return std::isfinite(score)?score:0.0f;
    }
private:
    static bool bounded(float x,float lo,float hi) {
        return std::isfinite(x)&&x>=lo&&x<=hi;
    }
    static float length(render::float3 v) {
        return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    }
    Config config_{};
    std::unordered_map<uint32_t,float> weights_;
};

} // namespace aiverse::fx
