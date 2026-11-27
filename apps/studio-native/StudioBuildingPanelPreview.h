#pragma once
#include "StudioPanelModelTarget.h"

namespace aiverse::studio {
class StudioBuildingPanelPreview final : public StudioPanelModelTarget {
public:
    using StudioPanelModelTarget::StudioPanelModelTarget;
    std::vector<uint8_t> frame(float yaw,float pitch,float zoom,float panX=0.f,float panY=0.f){
        if(!ready_||!model_)throw std::runtime_error("No active Building preview");
        if(!std::isfinite(yaw)||!std::isfinite(pitch)||!std::isfinite(zoom)||!std::isfinite(panX)||!std::isfinite(panY)||zoom<.55f||zoom>2.5f||std::abs(panX)>2.f||std::abs(panY)>2.f)
            throw std::runtime_error("Invalid Building preview camera");
        const auto lo=model_->boundsMin,hi=model_->boundsMax,size=hi-lo;
        const float radius=std::max(.05f,.5f*std::sqrt(render::dot(size,size)));
        render::Camera camera;
        camera.target=(lo+hi)*.5f;camera.yaw=std::remainder(yaw,6.2831853f);
        camera.pitch=std::clamp(pitch,-1.35f,1.35f);camera.ortho=false;
        const render::float3 right{std::cos(camera.yaw),0,-std::sin(camera.yaw)};
        const render::float3 up{-std::sin(camera.yaw)*std::sin(camera.pitch),std::cos(camera.pitch),-std::cos(camera.yaw)*std::sin(camera.pitch)};
        camera.target=camera.target+right*(panX*radius)+up*(panY*radius);
        camera.distance=radius/std::sin(camera.fovY*.5f)*1.04f*zoom;
        camera.minDistance=.01f;camera.maxDistance=1e6f;
        camera.zNear=std::max(.01f,camera.distance-radius*2.f);
        camera.zFar=camera.distance+radius*4.f;
        return captureFrame(camera,[this](render::Renderer3D& renderer) {
            render::ModelDraw draw;
            draw.model=model_.get();
            renderer.drawModel(draw);
        });
    }
};
}
