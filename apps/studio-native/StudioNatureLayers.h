#pragma once
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::studio {
// Toolbar preferences use natureWind/natureFire and metre-valued natureRadius/
// natureSpacing. Keep the original MCP argument names on the wire.
struct StudioNatureLayers {
    bool physisWind=false,physisFire=false,nereidFlow=true,nereidAllWater=false;
    bool terraSlope=true,terraSlopeAtTarget=false,terraTreeWind=true,erosionZones=true;
    bool strataBounds=false,strataWire=false,strataFill=false,strataRoutes=false;
    unsigned char modelDebugMode=0;
    int strataColor=0;
    float radius=48,spacing=6;
    bool operator==(const StudioNatureLayers&) const = default;

    struct Flag {const char* agent;const char* toolbar;bool StudioNatureLayers::*member;};
    static constexpr Flag flags[]={
        {"strataBounds","strataBounds",&StudioNatureLayers::strataBounds},
        {"strataWire","strataWire",&StudioNatureLayers::strataWire},
        {"strataFill","strataFill",&StudioNatureLayers::strataFill},
        {"strataRoutes","strataRoutes",&StudioNatureLayers::strataRoutes},
        {"physisWind","natureWind",&StudioNatureLayers::physisWind},
        {"physisFire","natureFire",&StudioNatureLayers::physisFire},
        {"nereidFlow","nereidFlow",&StudioNatureLayers::nereidFlow},
        {"nereidAllWater","nereidAllWater",&StudioNatureLayers::nereidAllWater},
        {"terraSlope","terraSlope",&StudioNatureLayers::terraSlope},
        {"terraSlopeAtTarget","terraSlopeAtTarget",&StudioNatureLayers::terraSlopeAtTarget},
        {"terraTreeWind","terraTreeWind",&StudioNatureLayers::terraTreeWind},
        {"erosionZones","erosionZones",&StudioNatureLayers::erosionZones}
    };

    StudioNatureLayers patched(const Json& input,bool agent) const {
        auto next=*this;
        for(const auto& flag:flags){
            const char* key=agent?flag.agent:flag.toolbar;
            if(!input.has(key))continue;
            if(input[key].type()!=Json::Type::Bool)throw std::runtime_error(std::string(key)+" must be boolean");
            next.*flag.member=input[key].boolean();
        }
        if(input.has("modelDebugMode")){
            const auto& mode=input["modelDebugMode"];
            const double value=mode.toDouble(-1);
            if(!mode.isNumber() || !(value>=0 && value<=9 && value==std::floor(value)))
                throw std::runtime_error("Invalid Scene model visualization");
            next.modelDebugMode=static_cast<unsigned char>(value);
        }
        if(input.has("strataColor")){
            const auto color=input["strataColor"].toDouble(-1);
            if(!(color>=0 && color<=3 && std::floor(color)==color))throw std::runtime_error("Invalid STRATA color mode");
            next.strataColor=int(color);
        }
        const auto distance=[&](const char* key,float value,double low,double high){
            if(!input.has(key))return value;
            const double n=input[key].toDouble();
            if(!input[key].isNumber() || !std::isfinite(n) || (!agent && (n<low || n>high)))
                throw std::runtime_error(std::string("Invalid Scene field distance: ")+key);
            return float(std::clamp(n,low,high));
        };
        next.radius=distance(agent?"radius":"natureRadius",radius,8,200);
        next.spacing=distance(agent?"spacing":"natureSpacing",spacing,2,32);
        return next;
    }
    Json json(bool agent) const {
        Json out=Json::object();
        for(const auto& flag:flags)out.set(agent?flag.agent:flag.toolbar,Json::fromBool(this->*flag.member));
        out.set("modelDebugMode",Json::number(int64_t(modelDebugMode)));
        out.set("strataColor",Json::number(int64_t(strataColor)));
        out.set(agent?"radius":"natureRadius",Json::number(double(radius),6));
        out.set(agent?"spacing":"natureSpacing",Json::number(double(spacing),6));
        return out;
    }
};
}
