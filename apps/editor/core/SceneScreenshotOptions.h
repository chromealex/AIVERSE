#pragma once
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace aiverse::editor {
inline float cameraNumber(const Json& args,const char* key,float fallback) {
    if(!args.has(key))return fallback;
    const double value=args[key].toDouble();
    if(!args[key].isNumber() || !std::isfinite(value) || std::abs(value)>1000000.0)
        throw std::runtime_error(std::string("camera ")+key+" must be a finite number within +/-1000000");
    return float(value);
}

struct SceneScreenshotOptions {
    bool clean=false;
    int width=0,height=0;
    std::optional<float> x,y,z,distance,pitch,yaw;

    static SceneScreenshotOptions parse(const Json& args,bool scene) {
        SceneScreenshotOptions options;
        if(args.has("clean") && args["clean"].type()!=Json::Type::Bool)
            throw std::runtime_error("clean must be boolean");
        options.clean=args.getBool("clean",scene);
        if(options.clean && !scene)throw std::runtime_error("clean capture requires window scene");
        if(args.has("width")!=args.has("height"))
            throw std::runtime_error("capture width and height must be supplied together");
        if(args.has("width")) {
            if(!options.clean)throw std::runtime_error("capture dimensions require a clean scene capture");
            const double w=args["width"].toDouble(),h=args["height"].toDouble();
            if(!args["width"].isNumber() || !args["height"].isNumber() ||
               !std::isfinite(w) || !std::isfinite(h) || w<256 || h<256 || w>4096 || h>4096 ||
               w!=std::floor(w) || h!=std::floor(h) || w*h>8388608)
                throw std::runtime_error("capture dimensions must be integers from 256 to 4096 and at most 8388608 pixels");
            options.width=int(w);
            options.height=int(h);
        }
        if(args.has("x")!=args.has("z"))throw std::runtime_error("camera x and z must be supplied together");
        auto number=[&](const char* key)->std::optional<float>{
            if(args.has(key))return cameraNumber(args,key,0);
            return std::nullopt;
        };
        options.x=number("x");options.y=number("y");options.z=number("z");
        options.distance=number("distance");options.pitch=number("pitch");options.yaw=number("yaw");
        return options;
    }

    render::Camera camera(render::Camera base) const {
        if(x)base.target={*x,y.value_or(0),*z};
        else if(y)base.target.y=*y;
        if(distance)base.distance=std::clamp(*distance,base.minDistance,base.maxDistance);
        constexpr float radians=0.01745329252f;
        if(pitch)base.pitch=std::clamp(*pitch,-89.f,89.f)*radians;
        if(yaw)base.yaw=*yaw*radians;
        return base;
    }
};
}
