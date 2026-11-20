// LumaWeave CPU temporal accumulation contract for stochastic DIRECT light.
// This is a bounded correctness reference, not a GPU denoiser or ReSTIR.
// The host supplies reprojection-stable receiver IDs; there is deliberately
// no implicit screen motion/depth history available in gfx::Device.
// Historical light is never added to baked, indirect GI or legacy LightSystem.
#pragma once
#include "LumaWeaveLights.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>

namespace aiverse::render {

struct LumaTemporalConfig {
    float maxWorldMotion=.3f;      // metres
    float minNormalAgreement=.92f; // dot of normalized normals
    float maxDepthDelta=.15f;      // world/view depth, same units in both frames
    float maxHistoryBlend=.90f;    // NEVER lock to the old frame
    float colorClamp=3.0f;         // max change around current for each RGB channel
    uint32_t maxHistoryFrames=16;
    size_t maxEntries=8192;
};

struct LumaTemporalReceiver {
    uint64_t key=0;              // stable ID after CPU/GPU reprojection
    float3 worldPosition{};
    float3 normal{0,1,0};
    float depth=0;
    float3 direct{};
    bool valid=true;            // rejected/sky/disoccluded pixel must discard history
};

struct LumaTemporalResult {
    float3 direct{};
    uint32_t accumulatedFrames=0;
    bool reusedHistory=false;
};

class LumaWeaveTemporalHistory {
public:
    explicit LumaWeaveTemporalHistory(LumaTemporalConfig c={}) : cfg_(c) {
        if(!std::isfinite(c.maxWorldMotion)||c.maxWorldMotion<0 ||
           !std::isfinite(c.minNormalAgreement)||c.minNormalAgreement< -1 ||
           c.minNormalAgreement>1 ||
           !std::isfinite(c.maxDepthDelta)||c.maxDepthDelta<0 ||
           !std::isfinite(c.maxHistoryBlend)||c.maxHistoryBlend<0 ||
           c.maxHistoryBlend>.99f ||
           !std::isfinite(c.colorClamp)||c.colorClamp<0 ||
           !c.maxHistoryFrames||c.maxHistoryFrames>1024 ||
           !c.maxEntries||c.maxEntries>1000000)
            throw std::invalid_argument("invalid LumaWeave temporal settings");
    }

    void reset() {previous_.clear();frame_=0;sourceRevision_=geometryRevision_=0;active_=false;}
    size_t historySize() const {return previous_.size();}
    uint64_t frame() const {return frame_;}

    // Call ONCE per displayed frame. A geometry revision invalidates the
    // entire history (until a true motion/disocclusion buffer is provided).
    // On a consecutive light revision, the COMPLETE LumaPublishResult.edits
    // may preserve unaffected receivers; any unknown/nonconsecutive revision
    // change safely clears all history instead.
    void beginFrame(uint64_t lightRevision,uint64_t geometryRevision,
                    std::span<const LumaLightEdit> changed={}) {
        if(active_ && geometryRevision!=geometryRevision_) {
            previous_.clear();
        } else if(active_ && lightRevision!=sourceRevision_) {
            if(changed.empty() || lightRevision!=sourceRevision_+1) {
                previous_.clear();
            } else {
                for(auto it=previous_.begin();it!=previous_.end();) {
                    bool affected=false;
                    for(const auto& edit:changed) {
                        if((edit.hadPrevious && within(it->second.position,edit.previous)) ||
                           (edit.hasCurrent && within(it->second.position,edit.current))) {
                            affected=true;
                            break;
                        }
                    }
                    if(affected)it=previous_.erase(it);
                    else ++it;
                }
            }
        } else if(!changed.empty()) {
            throw std::invalid_argument("LumaWeave edits without source revision change");
        }
        if(frame_==std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("LumaWeave temporal frame overflow");
        ++frame_;
        sourceRevision_=lightRevision;
        geometryRevision_=geometryRevision;
        active_=true;
    }

    LumaTemporalResult resolve(const LumaTemporalReceiver& sample) {
        if(!active_)throw std::logic_error("LumaWeave beginFrame missing");
        if(!sample.key || !finite(sample.worldPosition) || !finite(sample.normal) ||
           !finite(sample.direct) || !std::isfinite(sample.depth) ||
           sample.direct.x<0 || sample.direct.y<0 || sample.direct.z<0 ||
           sample.depth<0 || dot(sample.normal,sample.normal)<1e-10f)
            throw std::invalid_argument("invalid LumaWeave temporal receiver");
        auto it=previous_.find(sample.key);
        if(!sample.valid) {
            if(it!=previous_.end())previous_.erase(it);
            return {};
        }

        LumaTemporalResult result{sample.direct,1,false};
        const float3 n=normalize(sample.normal);
        if(it!=previous_.end()) {
            const auto& old=it->second;
            const float3 delta=sample.worldPosition-old.position;
            const bool accepted=old.frame+1==frame_ &&
                dot(delta,delta)<=cfg_.maxWorldMotion*cfg_.maxWorldMotion &&
                dot(n,old.normal)>=cfg_.minNormalAgreement &&
                std::abs(sample.depth-old.depth)<=cfg_.maxDepthDelta;
            if(accepted) {
                const float alpha=std::min(cfg_.maxHistoryBlend,
                                           float(old.age)/float(old.age+1u));
                const float3 historic=clampHistory(old.color,sample.direct,cfg_.colorClamp);
                result.direct=sample.direct*(1-alpha)+historic*alpha;
                result.accumulatedFrames=std::min(old.age+1u,cfg_.maxHistoryFrames);
                result.reusedHistory=true;
            }
        }
        if(it==previous_.end() && previous_.size()==cfg_.maxEntries) {
            // Deterministic bounded eviction; expensive CPU reference only.
            auto oldest=previous_.begin();
            for(auto e=previous_.begin();e!=previous_.end();++e)
                if(e->second.frame<oldest->second.frame)
                    oldest=e;
            previous_.erase(oldest);
        }
        previous_[sample.key]={sample.worldPosition,n,sample.depth,result.direct,
                               result.accumulatedFrames,frame_};
        return result;
    }

private:
    struct Entry {
        float3 position{},normal{};
        float depth=0;
        float3 color{};
        uint32_t age=0;
        uint64_t frame=0;
    };
    static bool finite(float3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static bool within(float3 p,LumaLightBounds b) {
        return p.x>=b.min.x && p.x<=b.max.x &&
               p.y>=b.min.y && p.y<=b.max.y &&
               p.z>=b.min.z && p.z<=b.max.z;
    }
    static float3 clampHistory(float3 old,float3 now,float strength) {
        const auto channel=[strength](float history,float current) {
            const float delta=strength*(1+current);
            return std::clamp(history,std::max(0.f,current-delta),current+delta);
        };
        return {channel(old.x,now.x),channel(old.y,now.y),channel(old.z,now.z)};
    }

    LumaTemporalConfig cfg_;
    std::map<uint64_t,Entry> previous_;
    uint64_t frame_=0,sourceRevision_=0,geometryRevision_=0;
    bool active_=false;
};

} // namespace aiverse::render
