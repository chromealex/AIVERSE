#pragma once
// Portable deterministic CPU PBR surface textures for the building style
// grammar. No renderer, platform IO, genre or GPU dependency. A project
// texture GUID always overrides the corresponding procedural channel.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::lib::systems {
enum class BuildingSurfaceChannel : uint8_t {
    BaseColor, MetallicRoughness, Normal, Occlusion
};
inline constexpr std::array<BuildingSurfaceChannel,4> kBuildingSurfaceChannels{
    BuildingSurfaceChannel::BaseColor, BuildingSurfaceChannel::MetallicRoughness,
    BuildingSurfaceChannel::Normal, BuildingSurfaceChannel::Occlusion
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
    const int row=y/16;
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
inline bool validBuildingSurfaceResolution(int size){return size==128||size==256||size==512;}
// Periodic bilinear height sampling with fine-scale seeded grain. Preserve
// the material-scale pattern at higher resolution, not nearest-neighbour
// enlargement of the original 128-pixel image.
inline float buildingSurfaceSample(std::string_view pattern,int x,int y,uint32_t seed,int size){
    const float sx=float(x)*128.f/size,sy=float(y)*128.f/size;
    const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
    const float fx=sx-ix,fy=sy-iy;
    auto at=[&](int a,int b){return float(buildingSurfaceHeight(pattern,a,b,seed));};
    const float a=at(ix,iy)*(1-fx)+at(ix+1,iy)*fx;
    const float b=at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx;
    const int xx=(x%size+size)%size,yy=(y%size+size)%size;
    const float grain=float(int(buildingSurfacePixelHash(seed^0xa53c9e1u,xx,yy)&15u)-7);
    return a*(1-fy)+b*fy+grain*.35f;
}
inline BuildingSurfacePixels generateBuildingSurface(
    std::string_view pattern,std::string_view materialName,
    uint32_t seed,BuildingSurfaceChannel channel,int resolution=128) {
    BuildingSurfacePixels result;
    if(!validBuildingSurfacePattern(pattern)||pattern.empty()||!validBuildingSurfaceResolution(resolution)||
       std::find(kBuildingSurfaceChannels.begin(),kBuildingSurfaceChannels.end(),channel)==kBuildingSurfaceChannels.end())return result;
    result.width=result.height=resolution;
    result.rgba.resize(size_t(result.width)*size_t(result.height)*4u);
    seed=buildingSurfaceNameSeed(materialName,seed);
    const auto at=[&](int x,int y){
        return resolution==128?float(buildingSurfaceHeight(pattern,x,y,seed)):buildingSurfaceSample(pattern,x,y,seed,resolution);
    };
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x){
        const size_t index=(size_t(y)*size_t(result.width)+size_t(x))*4u;
        const int height=int(std::lround(at(x,y)));
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
        }else if(channel==BuildingSurfaceChannel::Occlusion){
            // Local cavity approximation in material-space, independent of
            // mesh lighting. Sample periodic neighbours at fixed texel distances
            // in the original 128-unit tile so quality does not change scale.
            const int step=resolution/128;
            const float center=at(x,y);
            float cavity=0.f;
            for(int radius:{2,4}){
                const int d=radius*step;
                for(const auto& offset:std::array<std::array<int,2>,4>{{{d,0},{-d,0},{0,d},{0,-d}}})
                    cavity+=std::max(0.f,at(x+offset[0],y+offset[1])-center);
            }
            const auto ao=uint8_t(std::clamp(int(std::lround(255.f-cavity*.22f)),64,255));
            p[0]=p[1]=p[2]=ao;p[3]=255;
        }else {
            const int dx=int(std::lround((at(x+1,y)-at(x-1,y))*resolution/128.f));
            const int dy=int(std::lround((at(x,y+1)-at(x,y-1))*resolution/128.f));
            p[0]=uint8_t(std::clamp(128-dx/3,0,255));
            p[1]=uint8_t(std::clamp(128-dy/3,0,255));
            p[2]=255;p[3]=255;
            if(resolution>128){
                const float nx=-float(dx)/384.f,ny=-float(dy)/384.f;
                const float inv=1.f/std::sqrt(nx*nx+ny*ny+1.f);
                p[0]=uint8_t(std::clamp(int(std::lround((nx*inv*.5f+.5f)*255.f)),0,255));
                p[1]=uint8_t(std::clamp(int(std::lround((ny*inv*.5f+.5f)*255.f)),0,255));
                p[2]=uint8_t(std::clamp(int(std::lround((inv*.5f+.5f)*255.f)),0,255));
            }
        }
    }
    return result;
}
} // namespace aiverse::lib::systems
