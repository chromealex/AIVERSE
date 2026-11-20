// Presentation-only Vulkan direct-light temporal history contract.
// A history sample is NEVER admitted based on a matching camera pointer,
// unchanged pointer-valued geometry or source-count alone. Stable view/world
// keys, explicit camera-cut epoch and source/geometry revisions are required.
// This does not affect authoritative ECS state, save or gameplay.
#pragma once
#include "Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::render {

struct LumaVulkanHistoryStamp {
    uint64_t viewKey=0;       // stable unique camera+world identity; 0 disables reuse
    uint64_t viewEpoch=0;     // increment on hard cut, world reload or teleport
    uint64_t sourceRevision=0,geometryRevision=0;
    uint32_t width=0,height=0;
    mat4 viewProjection{};
    float3 eye{};
};

struct LumaVulkanDenoiseSettings {
    bool enabled=true;             // applies spatial even without history
    float historyWeight=.84f;      // only after *real completed* Vulkan fence
    float spatialDepthTolerance=.012f;
    float normalAgreement=.88f;
    float reprojectionDepthTolerance=.012f;
    float historyClamp=1.5f;       // RGB clamp around current-frame spatial range
    float cameraCutDistance=2.f;
    float cameraMatrixCut=.40f;    // maximum absolute element delta
    static bool valid(const LumaVulkanDenoiseSettings& s) {
        const auto finite=[](float v){return std::isfinite(v);};
        return finite(s.historyWeight) && s.historyWeight>=0 &&
               s.historyWeight<=.95f &&
               finite(s.spatialDepthTolerance) && s.spatialDepthTolerance>.00001f &&
               s.spatialDepthTolerance<=.05f &&
               finite(s.normalAgreement) && s.normalAgreement>=.0f &&
               s.normalAgreement<=1.f &&
               finite(s.reprojectionDepthTolerance) &&
               s.reprojectionDepthTolerance>.00001f &&
               s.reprojectionDepthTolerance<=.05f &&
               finite(s.historyClamp) && s.historyClamp>=0 &&
               s.historyClamp<=8.f &&
               finite(s.cameraCutDistance) && s.cameraCutDistance>=0 &&
               s.cameraCutDistance<=1000.f &&
               finite(s.cameraMatrixCut) && s.cameraMatrixCut>=0 &&
               s.cameraMatrixCut<=8.f;
    }
};

enum class LumaVulkanHistoryReason : uint8_t {
    Accepted=0,Disabled=1,InvalidIdentity=2,ViewChanged=3,
    ViewCut=4,SourceChanged=5,GeometryChanged=6,ResolutionChanged=7,
    CameraCut=8
};

class LumaVulkanHistoryPolicy {
public:
    static LumaVulkanHistoryReason compare(
            const LumaVulkanHistoryStamp& old,
            const LumaVulkanHistoryStamp& current,
            const LumaVulkanDenoiseSettings& settings,
            bool completedHistory) {
        if(!settings.enabled || !completedHistory)
            return LumaVulkanHistoryReason::Disabled;
        if(!old.viewKey || !current.viewKey ||
           !old.sourceRevision || !current.sourceRevision ||
           !old.geometryRevision || !current.geometryRevision)
            return LumaVulkanHistoryReason::InvalidIdentity;
        if(old.viewKey!=current.viewKey)return LumaVulkanHistoryReason::ViewChanged;
        if(old.viewEpoch!=current.viewEpoch)return LumaVulkanHistoryReason::ViewCut;
        if(old.sourceRevision!=current.sourceRevision)
            return LumaVulkanHistoryReason::SourceChanged;
        if(old.geometryRevision!=current.geometryRevision)
            return LumaVulkanHistoryReason::GeometryChanged;
        if(old.width!=current.width || old.height!=current.height ||
           !old.width || !old.height)
            return LumaVulkanHistoryReason::ResolutionChanged;
        const auto valid3=[](float3 x){
            return std::isfinite(x.x)&&std::isfinite(x.y)&&std::isfinite(x.z);
        };
        if(!valid3(old.eye)||!valid3(current.eye))
            return LumaVulkanHistoryReason::CameraCut;
        const float3 delta=old.eye-current.eye;
        const float distanceSq=dot(delta,delta);
        if(!std::isfinite(distanceSq) ||
           distanceSq>settings.cameraCutDistance*settings.cameraCutDistance)
            return LumaVulkanHistoryReason::CameraCut;
        for(int i=0;i<16;++i) {
            if(!std::isfinite(old.viewProjection.m[i]) ||
               !std::isfinite(current.viewProjection.m[i]) ||
               std::abs(old.viewProjection.m[i]-current.viewProjection.m[i])>
                   settings.cameraMatrixCut)
                return LumaVulkanHistoryReason::CameraCut;
        }
        return LumaVulkanHistoryReason::Accepted;
    }
};

} // namespace aiverse::render
