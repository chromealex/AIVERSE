#pragma once
#include "apps/editor/Devices.h"
#include "engine/data/Json.h"
#include "engine/platform/Platform.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::studio {
struct StudioGameDevice {
    std::string id="none";
    bool landscape=true, safeArea=true;
    const editor::DeviceProfile& profile() const {
        const auto* p=editor::findDevice(id);
        if (!p) throw std::runtime_error("Unknown Game device profile");
        return *p;
    }
    std::pair<float,float> size(float width,float height) const {
        const auto& p=profile();
        if (!p.w) return {width,height};
        float w=p.w,h=p.h;
        if ((w<h)==landscape) std::swap(w,h);
        return {w,h};
    }
    platform::Insets insets() const {
        if (!safeArea) return {};
        const auto v=editor::deviceInsets(profile(),landscape);
        return {v.left,v.top,v.right,v.bottom};
    }
    Json json() const {
        Json out=Json::object();const auto wh=size(0,0);
        out.set("id",Json::string(id));out.set("landscape",Json::fromBool(landscape));
        out.set("safeArea",Json::fromBool(safeArea));out.set("width",Json::number(double(wh.first)));
        out.set("height",Json::number(double(wh.second)));out.set("touch",Json::fromBool(profile().touch));
        out.set("chrome",chrome());
        return out;
    }
    // Device frame drawn around / over the Game screen (as the legacy Game window did): screen corner
    // radius, the camera cut-out and the OS safe area, all in screen points of the current orientation.
    Json chrome() const {
        const auto& d=profile();
        Json out=Json::object();
        out.set("corner",Json::number(double(d.corner)));
        const bool rotated=(d.w<d.h)==landscape;
        const float W=float(d.w),cx=W*.5f;
        float x0=0,y0=0,x1=0,y1=0,round=0;bool cut=true;
        switch (d.cutout) {
          case editor::Cutout::None: cut=false;break;
          case editor::Cutout::Notch: x0=cx-d.cutW*.5f;y0=0;x1=cx+d.cutW*.5f;y1=d.cutH;round=d.cutH*.45f;break;
          case editor::Cutout::Island: x0=cx-d.cutW*.5f;y0=d.cutY;x1=cx+d.cutW*.5f;y1=d.cutY+d.cutH;round=d.cutH*.5f;break;
          case editor::Cutout::PunchCenter: x0=cx-d.cutW*.5f;y0=d.cutY;x1=cx+d.cutW*.5f;y1=d.cutY+d.cutH;round=d.cutW*.5f;break;
          case editor::Cutout::PunchLeft: x0=d.cutY+6;y0=d.cutY;x1=d.cutY+6+d.cutW;y1=d.cutY+d.cutH;round=d.cutW*.5f;break;
          case editor::Cutout::Waterdrop: x0=cx-d.cutW*.5f;y0=0;x1=cx+d.cutW*.5f;y1=d.cutH;round=d.cutW*.5f;break;
        }
        if (cut && d.w>0) {
            // natural (x, y) -> screen (y, W - x) when the device is turned
            Json c=Json::object();
            const float sx0=rotated?y0:x0,sy0=rotated?W-x1:y0,sx1=rotated?y1:x1,sy1=rotated?W-x0:y1;
            c.set("x",Json::number(double(sx0)));c.set("y",Json::number(double(sy0)));
            c.set("width",Json::number(double(sx1-sx0)));c.set("height",Json::number(double(sy1-sy0)));
            c.set("radius",Json::number(double(round)));
            out.set("cutout",std::move(c));
        }
        const auto v=editor::deviceInsets(d,landscape);
        Json in=Json::object();
        in.set("left",Json::number(double(v.left)));in.set("top",Json::number(double(v.top)));
        in.set("right",Json::number(double(v.right)));in.set("bottom",Json::number(double(v.bottom)));
        out.set("insets",std::move(in));
        return out;
    }
    static StudioGameDevice parse(const Json& args) {
        if (!args["id"].isString() || args["landscape"].type()!=Json::Type::Bool || args["safeArea"].type()!=Json::Type::Bool)
            throw std::runtime_error("Device requires id, landscape and safeArea");
        StudioGameDevice d;d.id=args.getStr("id");d.landscape=args["landscape"].boolean();
        d.safeArea=args["safeArea"].boolean();(void)d.profile();return d;
    }
};
inline Json studioGameDevices() {
    Json out=Json::array();
    for (const auto& p:editor::kDevices) {
        Json v=Json::object();v.set("id",Json::string(p.id));
        v.set("name",Json::string(p.name));v.set("group",Json::string(p.group));out.push(std::move(v));
    }
    return out;
}
}
