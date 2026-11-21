// Presentation-only RGBA8 cutout filtering. Base pixels remain untouched.
// Explicit smaller mip levels preserve texel alpha-test coverage; RGB filters
// in linear light with alpha weighting, avoiding dark transparent fringes.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace aiverse::render {
struct AlphaCoverageMip {
    int width=0,height=0;
    std::vector<uint8_t> rgba;
};

inline double alphaTestCoverage(std::span<const uint8_t> rgba,uint8_t threshold) {
    if(rgba.empty()||rgba.size()%4)return 0;
    size_t covered=0;
    for(size_t i=3;i<rgba.size();i+=4)covered+=rgba[i]>=threshold;
    return double(covered)/double(rgba.size()/4);
}

inline void preserveAlphaCoverage(std::vector<uint8_t>& rgba,uint8_t threshold,double target) {
    std::array<size_t,256> histogram{};
    for(size_t i=3;i<rgba.size();i+=4)++histogram[rgba[i]];
    const double desired=target*double(rgba.size()/4);
    size_t original=0;
    for(unsigned a=threshold;a<256;++a)original+=histogram[a];
    double bestError=std::abs(double(original)-desired);
    float bestScale=1;
    const auto consider=[&](size_t count,float scale) {
        const double error=std::abs(double(count)-desired);
        if(error<bestError || (error==bestError&&std::abs(scale-1)<std::abs(bestScale-1))) {
            bestError=error;bestScale=scale;
        }
    };
    consider(0,0);
    size_t covered=0;
    for(int a=255;a>0;--a) {
        covered+=histogram[size_t(a)];
        if(histogram[size_t(a)])consider(covered,(float(threshold)-.499f)/float(a));
    }
    if(bestScale==1)return;
    for(size_t i=3;i<rgba.size();i+=4)
        rgba[i]=uint8_t(std::clamp(int(std::floor(float(rgba[i])*bestScale+.5f)),0,255));
}

// levels excludes the source/base level. Bounds keep authoring/game load-time
// scratch finite; unsupported sources retain ordinary backend mip generation.
inline bool buildAlphaCoverageMips(int width,int height,std::span<const uint8_t> source,
                                  uint8_t threshold,std::vector<AlphaCoverageMip>& levels) {
    levels.clear();
    if(width<1||height<1||width>4096||height>4096||!threshold||
       uint64_t(width)*height>4u*1024u*1024u||source.size()!=uint64_t(width)*height*4u)return false;
    const double target=alphaTestCoverage(source,threshold);
    static const auto linear=[] {
        std::array<float,256> values{};
        for(size_t i=0;i<values.size();++i) {
            const float x=float(i)/255;
            values[i]=x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);
        }
        return values;
    }();
    const auto encode=[](float value) {
        value=std::clamp(value,0.f,1.f);
        const float encoded=value<=.0031308f?value*12.92f:1.055f*std::pow(value,1.f/2.4f)-.055f;
        return uint8_t(std::clamp(int(std::floor(encoded*255+.5f)),0,255));
    };
    std::vector<uint8_t> previous;
    auto pixels=source;
    while(width>1||height>1) {
        const int nextWidth=std::max(1,width/2),nextHeight=std::max(1,height/2);
        std::vector<uint8_t> raw(size_t(nextWidth)*nextHeight*4);
        for(int y=0;y<nextHeight;++y)for(int x=0;x<nextWidth;++x) {
            // Include final odd rows/columns rather than dropping edge leaves.
            const int x0=x*width/nextWidth,x1=(x+1)*width/nextWidth;
            const int y0=y*height/nextHeight,y1=(y+1)*height/nextHeight;
            float alpha=0;std::array<float,3> rgb{};
            for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx) {
                const size_t at=(size_t(sy)*width+sx)*4;
                const float a=float(pixels[at+3]);alpha+=a;
                for(size_t c=0;c<3;++c)rgb[c]+=linear[pixels[at+c]]*a;
            }
            const size_t at=(size_t(y)*nextWidth+x)*4;
            for(size_t c=0;c<3;++c)raw[at+c]=alpha>0?encode(rgb[c]/alpha):0;
            raw[at+3]=uint8_t(std::clamp(int(std::floor(alpha/float((x1-x0)*(y1-y0))+.5f)),0,255));
        }
        // One texel of color bleed keeps hardware bilinear filtering from
        // blending a surviving leaf edge with black transparent background.
        // Alpha stays zero and contributes nothing to the next reduction.
        for(int y=0;y<nextHeight;++y)for(int x=0;x<nextWidth;++x) {
            const size_t at=(size_t(y)*nextWidth+x)*4;
            if(raw[at+3])continue;
            float sum=0;std::array<float,3> rgb{};
            for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox) {
                const int sx=x+ox,sy=y+oy;
                if(sx<0||sy<0||sx>=nextWidth||sy>=nextHeight)continue;
                const size_t neighbor=(size_t(sy)*nextWidth+sx)*4;
                const float alpha=float(raw[neighbor+3]);sum+=alpha;
                for(size_t c=0;c<3;++c)rgb[c]+=linear[raw[neighbor+c]]*alpha;
            }
            if(sum>0)for(size_t c=0;c<3;++c)raw[at+c]=encode(rgb[c]/sum);
        }
        levels.push_back({nextWidth,nextHeight,raw});
        preserveAlphaCoverage(levels.back().rgba,threshold,target);
        // Filter the unscaled alpha next time; coverage adjustment must not
        // accumulate and progressively inflate every leaf into a rectangle.
        previous=std::move(raw);pixels=previous;
        width=nextWidth;height=nextHeight;
    }
    return true;
}
} // namespace aiverse::render
