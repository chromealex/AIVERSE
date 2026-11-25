// AIVERSE Studio: strict safe subset of the shared materials.json authoring API.
// No filesystem paths, arbitrary JSON, shader source or dangerous material keys.
#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
namespace aiverse::studio {
inline std::optional<std::array<double,3>> parseStudioSssColor(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::array<double,3> color{};
    char comma=0;
    if(!(input>>color[0]) || !(input>>comma) || comma!=',' ||
       !(input>>color[1]) || !(input>>comma) || comma!=',' ||
       !(input>>color[2])) return std::nullopt;
    input>>std::ws;
    if(!input.eof()) return std::nullopt;
    for(double channel:color)
        if(!std::isfinite(channel) || channel<0 || channel>1) return std::nullopt;
    return color;
}
inline std::optional<std::array<double,2>> parseStudioUvPair(std::string_view text){
    std::istringstream input{std::string(text)};
    std::array<double,2> value{};
    char comma=0;
    if(!(input>>value[0])||!(input>>comma)||comma!=','||
       !(input>>value[1]))return std::nullopt;
    input>>std::ws;
    if(!input.eof())return std::nullopt;
    for(double n:value)if(!std::isfinite(n)||std::abs(n)>1024.)return std::nullopt;
    return value;
}
inline bool studioMaterialTextureKey(std::string_view key) {
    return key=="texture" || key=="normalMap" || key=="emissiveMap" ||
           key=="teamMask" || key=="metallicRoughnessMap" || key=="occlusionMap";
}
inline bool validStudioMaterialPreset(std::string_view preset) {
    return preset=="matte" || preset=="metal" || preset=="glass";
}
inline bool validStudioMaterialName(std::string_view name) {
    if(name.empty()||name.size()>64)return false;
    for(char c:name)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
inline bool validStudioMaterialNumber(std::string_view key,double value) {
    if(!std::isfinite(value))return false;
    if(key=="metallic"||key=="roughness"||key=="shadowDiffusion"||
       key=="thinTransmission"||key=="sssStrength"||key=="sssScale"||key=="sssThickness"||key=="cutoff"||
       key=="teamStrength"||key=="occlusionStrength")
        return value>=0&&value<=1;
    if(key=="emissiveStrength")return value>=0&&value<=100;
    if(key=="normalScale")return value>=0&&value<=16;
    return false;
}
inline bool validStudioMaterialBoolean(std::string_view key) {
    return key=="doubleSided"||key=="unlit"||key=="castShadow"||
           key=="receiveShadow"||key=="vertexColors";
}
inline bool validStudioMaterialText(std::string_view key,std::string_view value) {
    if(key=="sssColor")return parseStudioSssColor(value).has_value();
    if(key=="alpha")return value=="opaque"||value=="mask"||value=="blend";
    if(key=="team")return value=="none"||value=="tint"||
           value=="mask"||value=="alpha"||value=="vertex";
    if(key=="description")return value.size()<=256;
    return false;
}
} // namespace aiverse::studio
