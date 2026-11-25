#pragma once
#include "engine/data/Json.h"
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace aiverse::studio {
// Match the document Transform convention: absolute dimensions and yaw in degrees.
// Preserve unedited map numbers, including precision beyond the Inspector display.
inline void editOrientedCollider(Json& box,const Json& values) {
    if(!box["yaw"].isNumber() || !box["half"].isArray() || box["half"].size()!=3)
        throw std::runtime_error("Select an oriented box collider.");
    for(const auto& [key,value]:values.members()){
        if(!value.isNumber())throw std::runtime_error("Expected a numeric collider transform.");
        if(key=="rotationY"){
            double turns=std::fmod(value.toDouble()/360.0,1.0);
            if(turns<0)turns+=1.0;
            box.set("yaw",Json::number(turns,6));
        }else{
            const auto axis=key.size()==6 && key.starts_with("scale")?
                std::string_view("XYZ").find(key.back()):std::string_view::npos;
            if(axis==std::string_view::npos || value.toDouble()<=0)
                throw std::runtime_error("Expected a positive box dimension.");
            box.member("half").at(axis)=Json::number(value.toDouble()*0.5,6);
        }
    }
}
}
