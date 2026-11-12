#pragma once
#include "engine/render/StaticPointShadows.h"
#include "engine/render/Math3D.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace aiverse::bench {
// Offline diagnostic: rank sun directions against artist-marked screen-space receivers.
// Uses the existing opaque BVH; candidates still require a real shader/shadow-map capture.
inline Json studySun(const render::StaticShadowGeometry& geometry,const render::RTSCamera& camera,
                     float aspect,const Json& targets) {
    using namespace render;
    if(targets.size()<1||targets.size()>32)throw std::runtime_error("sunStudy requires 1..32 targets");
    struct Target {float3 position,normal;bool lit;float weight;};std::vector<Target> samples;
    Json receivers=Json::array();const auto eye=camera.eye(),forward=normalize(camera.target-eye);
    const auto right=normalize(cross(forward,{0,1,0})),up=cross(right,forward);const float scale=std::tan(camera.fovY*.5f);
    for(const auto& t:targets.items()){
        const auto& uv=t["screen"];float x=float(uv[0].toDouble(-1)),y=float(uv[1].toDouble(-1)),weight=float(t["weight"].toDouble(1));
        if(uv.size()!=2||!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1||!std::isfinite(weight)||weight<=0||weight>10)throw std::runtime_error("invalid sunStudy target");
        auto ray=normalize(forward+right*((2*x-1)*scale*aspect)+up*((1-2*y)*scale));float3 normal;
        float distance=geometry.distance(eye,ray,500,&normal);
        if(distance>=500||dot(normal,normal)<.5f)throw std::runtime_error("sunStudy target misses opaque geometry");
        if(dot(normal,ray)>0)normal=normal*-1.0f;
        auto position=eye+ray*distance; samples.push_back({position+normal*.02f,normal,t.getBool("lit",false),weight});
        Json receiver=Json::object();receiver.set("name",Json::string(t.getStr("name")));Json pos=Json::array();for(float c:{position.x,position.y,position.z})pos.push(Json::number(double(c)));receiver.set("position",pos);receivers.push(receiver);
    }
    struct Candidate {float3 direction;float score;std::vector<float> light;};std::vector<Candidate> candidates;
    for(int elevation=10;elevation<=85;elevation+=5)for(int azimuth=0;azimuth<360;azimuth+=5){
        float e=elevation*.01745329252f,a=azimuth*.01745329252f;Candidate c{{std::cos(e)*std::cos(a),std::sin(e),std::cos(e)*std::sin(a)},0,{}};
        for(const auto& s:samples){float ndl=std::max(0.0f,dot(s.normal,c.direction));float light=ndl>0&&geometry.distance(s.position,c.direction,500)>=500?ndl:0;
            c.light.push_back(light);c.score+=s.weight*(s.lit?std::min(light/.35f,1.0f):(light<.01f?1.0f:-2.0f));}
        candidates.push_back(std::move(c));
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    Json result=Json::object(),ranked=Json::array();result.set("receivers",receivers);
    for(size_t i=0;i<std::min(size_t(12),candidates.size());++i){const auto& c=candidates[i];Json item=Json::object(),dir=Json::array(),light=Json::array();for(float v:{c.direction.x,c.direction.y,c.direction.z})dir.push(Json::number(double(v),6));for(float v:c.light)light.push(Json::number(double(v),6));item.set("direction",dir);item.set("score",Json::number(double(c.score)));item.set("visibleNdotL",light);ranked.push(item);}
    result.set("candidates",ranked);return result;
}
}
