#pragma once
#include "engine/render/Renderer3D.h"
#include "engine/data/Json.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <vector>

namespace aiverse::bench {
// Presentation-only shot track. Sample absolute time so frame rate cannot change the path.
struct CameraTour {
    struct Shot { render::RTSCamera from, to; double duration; };
    std::vector<Shot> shots;
    double duration=0;
    static render::float3 vector(const Json& j) {
        if(!j.isArray()||j.size()!=3)throw std::runtime_error("tour: expected xyz");
        render::float3 v{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("tour: nonfinite position");
        return v;
    }
    static render::RTSCamera pose(const Json& j) {
        render::RTSCamera c;c.target=vector(j["target"]);auto d=vector(j["eye"])-c.target;
        c.distance=render::length(d);
        const float fov=float(j["fovYDegrees"].toDouble(58.5));
        if(c.distance<0.01f||!std::isfinite(c.distance)||!std::isfinite(fov)||fov<5||fov>150)throw std::runtime_error("tour: invalid camera");
        c.pitch=std::asin(d.y/c.distance);c.yaw=std::atan2(d.x,d.z);c.fovY=fov*3.14159265f/180;
        if(std::abs(c.pitch)>1.55f)throw std::runtime_error("tour: vertical camera is unsupported");
        return c;
    }
    void load(const Json& j) {
        shots.clear();duration=0;
        if(!j["shots"].isArray()||j["shots"].size()==0)throw std::runtime_error("tour: shots required");
        for(const auto& s:j["shots"].items()) {
            double t=s["duration"].toDouble();
            if(!std::isfinite(t)||t<0.1||t>3600)throw std::runtime_error("tour: invalid duration");
            shots.push_back({pose(s["from"]),pose(s["to"]),t});duration+=t;
        }
        if(duration>3600)throw std::runtime_error("tour: duration exceeds one hour");
    }
    render::RTSCamera sample(double time,int& shot) const {
        if(shots.empty())throw std::runtime_error("tour: empty track");
        time=std::clamp(time,0.0,duration);shot=0;
        while(size_t(shot+1)<shots.size()&&time>=shots[size_t(shot)].duration)time-=shots[size_t(shot++)].duration;
        const auto& s=shots[size_t(shot)];float t=float(time/s.duration);
        // Quintic ease has zero velocity and acceleration at shot boundaries.
        t=t*t*t*(t*(t*6-15)+10);
        auto c=s.from;const auto eye=s.from.eye()*(1-t)+s.to.eye()*t;
        c.yaw+=std::remainder(s.to.yaw-c.yaw,6.2831853f)*t;
        c.pitch+=(s.to.pitch-c.pitch)*t;c.fovY+=(s.to.fovY-c.fovY)*t;
        c.distance+=(s.to.distance-c.distance)*t;
        c.target={0,0,0};c.target=eye-c.eye();return c;
    }
};
}
