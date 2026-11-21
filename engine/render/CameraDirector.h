// AIVERSE LENS CameraDirector: choose/blend virtual ECS camera views.
// Presentation-only, not an authority over gameplay Transform or camera Entities.
#pragma once
#include "CameraRig.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace aiverse::render {
struct CameraDirectorFrame {
    Camera camera{};
    Entity source{};
    bool valid=false;
    bool cut=false;
    float blendWeight=1.f;
};
class CameraDirector {
    Camera previous_{};
    Camera current_{};
    Entity active_{};
    float blendDuration_=0.35f,elapsed_=0.f;
    bool initialized_=false,blending_=false;
public:
    void setBlendDuration(float seconds) {
        blendDuration_=std::max(0.f,seconds);
    }
    void reset(){initialized_=false;blending_=false;active_={};elapsed_=0;}
    void cut(){blending_=false;elapsed_=blendDuration_;}
    Entity activeEntity() const { return active_; }
    static const EcsCameraView* choose(const std::vector<EcsCameraView>& views) {
        const EcsCameraView* best=nullptr;
        for(const auto& v:views) {
            if(v.output.kind!=CameraOutputKind::Screen) continue;
            if(!best||v.priority>best->priority ||
               (v.priority==best->priority && v.entity.packed()<best->entity.packed()))
                best=&v;
        }
        return best;
    }
    CameraDirectorFrame evaluate(const std::vector<EcsCameraView>& candidates,float dt) {
        CameraDirectorFrame output{};
        const auto* selected=choose(candidates);
        if(!selected||!std::isfinite(dt)||dt<0) return output;
        Camera desired=selected->camera;
        if(!initialized_) {
            initialized_=true;active_=selected->entity;current_=desired;
            output.camera=desired;output.source=active_;output.valid=output.cut=true;
            return output;
        }
        if(active_!=selected->entity) {
            previous_=current_;active_=selected->entity;
            elapsed_=0;blending_=blendDuration_>0 &&
                previous_.ortho==desired.ortho;
            output.cut=!blending_;
        }
        if(!blending_)current_=desired;
        else {
            elapsed_+=dt;
            const float t=std::clamp(elapsed_/blendDuration_,0.f,1.f);
            const float smooth=t*t*(3.f-2.f*t);
            current_=desired;
            auto src=previous_;
            if(!src.usePose)src.setPose(src.eye(),cameraLookRotation(src.eye(),src.target));
            if(!desired.usePose)desired.setPose(desired.eye(),cameraLookRotation(desired.eye(),desired.target));
            current_.setPose(lerp(src.eye(),desired.eye(),smooth),
                slerp(src.rotation,desired.rotation,smooth));
            current_.fovY=src.fovY+(desired.fovY-src.fovY)*smooth;
            current_.orthoHalfHeightOverride=src.orthoHalfHeight()+
                (desired.orthoHalfHeight()-src.orthoHalfHeight())*smooth;
            current_.zNear=src.zNear+(desired.zNear-src.zNear)*smooth;
            current_.zFar=src.zFar+(desired.zFar-src.zFar)*smooth;
            output.blendWeight=smooth;
            if(t>=1)blending_=false;
        }
        output.camera=current_;output.source=active_;output.valid=true;
        return output;
    }
};
} // namespace aiverse::render
