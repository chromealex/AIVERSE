#pragma once
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include "engine/game/Components.h"
#include "engine/game/ProgressionData.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace aiverse::game::view_adapter {
struct ProjectileAppearance {
    std::string model, clip;
    float scale=-1.f; // inherit existing projectileScale
    float clipSpeed=1.f;
    render::float4 tint{1,1,1,1};
    render::mat4 axisFix{};
};
inline ProjectileAppearance parseProjectileAppearance(const Json& j) {
    using namespace render;
    ProjectileAppearance out;
    if (j.isString()) out.model=j.str();
    else if (j.isObject()) {
        for (const auto& [key,value]:j.members())
            if(key!="model"&&key!="clip"&&key!="scale"&&key!="clipSpeed"&&key!="tint"&&key!="forward")
                throw std::runtime_error("unknown projectile appearance field: "+key);
        if(!j["model"].isString())throw std::runtime_error("projectile model must be a path");
        out.model=j["model"].str();
        if(j.has("clip")&&!j["clip"].isString())throw std::runtime_error("projectile clip must be a name");
        out.clip=j.getStr("clip");
        auto number=[](const Json& v,float lo,float hi) {
            if(!v.isNumber())throw std::runtime_error("expected projectile appearance number");
            const float n=float(v.toDouble(0));
            if(!std::isfinite(n)||n<lo||n>hi)throw std::runtime_error("projectile appearance number out of range");
            return n;
        };
        if(j.has("scale"))out.scale=number(j["scale"],0,1024);
        if(j.has("clipSpeed"))out.clipSpeed=number(j["clipSpeed"],0,100);
        if(j.has("tint")) {
            const auto& t=j["tint"];
            if(!t.isArray()||(t.size()!=3&&t.size()!=4))throw std::runtime_error("projectile tint requires RGB or RGBA");
            out.tint={number(t[0],0,1),number(t[1],0,1),number(t[2],0,1),t.size()==4?number(t[3],0,1):1.f};
        }
        if(j.has("forward")&&!j["forward"].isString())throw std::runtime_error("projectile forward must be an axis");
        const auto axis=j.getStr("forward","+x");
        constexpr float halfPi=1.5707963267948966f;
        if(axis=="-x")out.axisFix=mat4::rotateY(2*halfPi);
        else if(axis=="+z")out.axisFix=mat4::rotateY(halfPi);
        else if(axis=="-z")out.axisFix=mat4::rotateY(-halfPi);
        else if(axis=="+y")out.axisFix=mat4::rotateZ(-halfPi);
        else if(axis=="-y")out.axisFix=mat4::rotateZ(halfPi);
        else if(axis!="+x")throw std::runtime_error("projectile forward must be +x/-x/+y/-y/+z/-z");
    } else throw std::runtime_error("projectile appearance must be a path or object");
    if(out.model.empty())throw std::runtime_error("empty projectile model path");
    return out;
}
// Use the interpolated simulation position to measure progress across the map.
// This keeps the presentation independent of render frame rate and pause.
inline render::float3 straightShotPoint(render::float3 first, render::float3 muzzle,
                                         render::float3 current, render::float3 impact) {
    const render::float3 direction{impact.x-first.x, 0, impact.z-first.z};
    const float squared = render::dot(direction, direction);
    const float t = squared > 1e-8f
        ? std::clamp(render::dot(current-first, direction)/squared, 0.f, 1.f) : 1.f;
    return muzzle + (impact-muzzle)*t;
}

// Model +X points along actual flight; choose a stable perpendicular when
// flight is vertical. Works in either flat or radial-up view coordinates.
inline render::mat4 projectileFlightFrame(render::float3 pos,render::float3 direction,render::float3 up) {
    using namespace render;
    if(length(direction)<1e-6f)direction={1,0,0};
    const float3 x=normalize(direction);
    if(length(up)<1e-6f)up={0,1,0};
    up=normalize(up);
    if(std::fabs(dot(x,up))>.999f)up=std::fabs(x.z)<.9f?float3{0,0,1}:float3{0,1,0};
    const float3 z=normalize(cross(x,up)),y=cross(z,x);
    mat4 m;
    for(int n=0;n<3;++n) {
        const float3 axis=n==0?x:n==1?y:z;
        m.at(0,n)=axis.x;m.at(1,n)=axis.y;m.at(2,n)=axis.z;
    }
    m.at(0,3)=pos.x;m.at(1,3)=pos.y;m.at(2,3)=pos.z;
    return m;
}
inline int projectileAppearanceSlot(const Rules& rules,const Projectile& p) {
    if(p.effList>=0||p.sourceType>=rules.types.size())return -1;
    int slot=p.weapon;
    if(slot<=-2) {
        const auto variant=size_t(-2-slot);
        if(!rules.progression||variant>=rules.progression->variants.size()||
            rules.progression->variants[variant].type!=p.sourceType)return -1;
        slot=rules.progression->variants[variant].weapon;
    }
    return slot>=0&&size_t(slot)<rules.types[p.sourceType].weapons.size()?slot:-1;
}
} // namespace aiverse::game::view_adapter
