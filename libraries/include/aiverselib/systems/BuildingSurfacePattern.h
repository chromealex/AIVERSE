#pragma once
// Portable deterministic CPU PBR surface textures for the building style
// grammar. No renderer, platform IO, genre or GPU dependency. A project
// texture GUID always overrides the corresponding procedural channel.
#include <array>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::lib::systems {
enum class BuildingSurfaceChannel : uint8_t {
    BaseColor, MetallicRoughness, Normal
};
struct BuildingSurfacePixels {
    int width=128,height=128;
    std::vector<uint8_t> rgba;
};
inline constexpr std::array<std::string_view,16> kBuildingSurfacePatterns{
    "stone","brick","wood","metal","concrete","ceramic","fabric","organic",
    "rust","coral","glass","soil","paint","circuit","ice","basalt"
};
inline bool validBuildingSurfacePattern(std::string_view value) {
    return value.empty() || std::find(
        kBuildingSurfacePatterns.begin(),kBuildingSurfacePatterns.end(),
        value)!=kBuildingSurfacePatterns.end();
}
inline uint32_t buildingSurfaceHash(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;
    return x^(x>>16);
}
inline uint32_t buildingSurfacePixelHash(uint32_t seed,int x,int y) {
    return buildingSurfaceHash(seed ^
        uint32_t(x)*0x9e3779b9u ^ uint32_t(y)*0x85ebca6bu);
}
inline uint32_t buildingSurfaceNameSeed(std::string_view name,uint32_t seed) {
    uint32_t hash=2166136261u ^ seed;
    for(unsigned char c:name){hash^=c;hash*=16777619u;}
    return buildingSurfaceHash(hash);
}
inline int buildingSurfaceHeight(
    std::string_view pattern,int x,int y,uint32_t seed) {
    // Fully tileable procedural patterns: all coordinates wrap to [0,127].
    x=(x%128+128)%128;y=(y%128+128)%128;
    const int coarse=int(buildingSurfacePixelHash(seed,x/8,y/8)&31u);
    const int grain=int(buildingSurfacePixelHash(seed,x,y)&31u)-15;
    const int row=y/16,col=(x+((row&1)?8:0))/16;
    if(pattern=="brick")return (y%16<2||(x+((row&1)?8:0))%16<2)
        ?24:151+coarse+grain/2;
    if(pattern=="stone")return (y%32<2||x%32<2)
        ?39:145+coarse+grain;
    if(pattern=="wood")return 138+((x+((y/16)*3))%11)*5+
        grain/2+((y%32==0)?-36:0);
    if(pattern=="metal")return 188+grain/4-
        ((x%32==2||y%32==2)?21:0)+
        ((x%32-16)*(x%32-16)+(y%32-16)*(y%32-16)<5?23:0);
    if(pattern=="concrete")return 151+coarse+grain;
    if(pattern=="ceramic")return (x%24==0||y%24==0)
        ?35:210+grain/5;
    if(pattern=="fabric")return 139+
        ((x%4<2)?17:-4)+((y%4<2)?14:-2)+grain/4;
    if(pattern=="organic")return 153+
        int(buildingSurfacePixelHash(seed,x/12,y/12)%58u)+grain/3;
    if(pattern=="rust")return 120+
        int(buildingSurfacePixelHash(seed,x/6,y/6)%78u)+grain;
    if(pattern=="coral")return (buildingSurfacePixelHash(
            seed,x/6,y/6)%7u==0u)?55:170+coarse+grain/2;
    if(pattern=="glass")return 239+grain/8-
        ((x%32==0||y%32==0)?12:0);
    if(pattern=="soil")return 115+coarse+grain+
        ((x%29==y%29)?-35:0);
    if(pattern=="paint")return 209+grain/3-
        ((x+y*3)%67<2?38:0);
    if(pattern=="circuit")return ((x%16==3||y%24==7)&&
        (buildingSurfacePixelHash(seed,x/16,y/24)%3u!=0u))
        ?221:120+coarse+grain/4;
    if(pattern=="ice")return 186+coarse+grain/2+
        ((x+y)%41==0?28:0);
    if(pattern=="basalt")return (x%24<2||y%24<2)
        ?65:138+coarse+grain/2;
    return 255;
}
inline BuildingSurfacePixels generateBuildingSurface(
    std::string_view pattern,std::string_view materialName,
    uint32_t seed,BuildingSurfaceChannel channel) {
    BuildingSurfacePixels result;
    if(!validBuildingSurfacePattern(pattern)||pattern.empty())return result;
    result.rgba.resize(size_t(result.width)*size_t(result.height)*4u);
    seed=buildingSurfaceNameSeed(materialName,seed);
    const auto at=[&](int x,int y){
        return buildingSurfaceHeight(pattern,x,y,seed);
    };
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x){
        const size_t index=(size_t(y)*size_t(result.width)+size_t(x))*4u;
        const int height=at(x,y);
        uint8_t* p=result.rgba.data()+index;
        if(channel==BuildingSurfaceChannel::BaseColor){
            // Neutral sRGB modulation multiplies authored linear PBR factor
            // in Renderer3D/glTF; not a replacement for the style colour.
            const uint8_t n=uint8_t(std::clamp(172+height/3,0,255));
            p[0]=p[1]=p[2]=n;p[3]=255;
        }else if(channel==BuildingSurfaceChannel::MetallicRoughness){
            p[0]=255; // occlusion is a separate channel
            p[1]=uint8_t(std::clamp(192+(height-128)/4,0,255));
            p[2]=255; // authored metallicFactor controls metalness
            p[3]=255;
        }else {
            const int dx=at(x+1,y)-at(x-1,y);
            const int dy=at(x,y+1)-at(x,y-1);
            p[0]=uint8_t(std::clamp(128-dx/3,0,255));
            p[1]=uint8_t(std::clamp(128-dy/3,0,255));
            p[2]=255;p[3]=255;
        }
    }
    return result;
}
} // namespace aiverse::lib::systems
