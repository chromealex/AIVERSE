#pragma once
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace aiverse::view {
// Presentation-only settings, shared by HUDs and native preview consumers.
// The host supplies the subject's colour; this code has no player/type lookup.
struct PortraitOptions {
    std::string model;
    float yaw=-1, zoom=1, focusHeight=.45f;
    render::float4 background{.16f,.18f,.22f,1.f};

    static PortraitOptions resolve(const Json& authored,const Json& defaults,
                                   render::float4 subjectColor) {
        PortraitOptions out;
        auto apply=[&](const Json& data) {
            auto number=[&](const char* key,float& value) {
                if(!data[key].isNumber())return;
                const float n=float(data[key].toDouble());
                if(std::isfinite(n))value=n;
            };
            number("yaw",out.yaw); number("zoom",out.zoom); number("focusHeight",out.focusHeight);
            if(data["model"].isString())out.model=data["model"].str();
            const auto& bg=data["background"];
            if(bg.isString() && bg.str()=="player")out.background=subjectColor;
            else if(bg.isArray() && (bg.size()==3 || bg.size()==4)) {
                float rgba[4]={0,0,0,1};
                for(size_t i=0;i<bg.size();++i) {
                    if(!bg[i].isNumber())return;
                    rgba[i]=float(bg[i].toDouble());
                    if(!std::isfinite(rgba[i]) || rgba[i]<0 || rgba[i]>1)return;
                }
                out.background={rgba[0],rgba[1],rgba[2],rgba[3]};
            }
        };
        apply(defaults); apply(authored);
        out.zoom=std::clamp(out.zoom,.25f,8.f);
        out.focusHeight=std::clamp(out.focusHeight,0.f,1.f);
        return out;
    }
};
} // namespace aiverse::view
