// Bounded portable HDR lat-long convolution. Renderer owns no asset I/O.
// Bands 0..3 are cosine-power filtered radiance, band 4 is cosine-weighted
// irradiance / pi (engine no-1/pi diffuse convention). This is not GGX PMREM.
#pragma once
#include "HdrTexture.h"
#include <stdexcept>
namespace aiverse::render::environment_ibl {
inline constexpr int width=128,height=64,bandWidth=width+2,bandHeight=height+2,bands=5;
inline constexpr float pi=3.14159265358979323846f;
struct Vec {float x=0,y=0,z=0;};
inline Vec operator+(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator*(Vec a,float b){return {a.x*b,a.y*b,a.z*b};}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec unit(Vec a){const float q=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);return q>0?a*(1.f/q):Vec{0,1,0};}
inline float halfValue(uint16_t h){
    const unsigned e=(h>>10)&31u,m=h&1023u;
    if((h&0x8000u)||e==31)return 0;
    return e?std::ldexp(1.f+float(m)/1024.f,int(e)-15):std::ldexp(float(m),-24);
}
inline Vec direction(float u,float v){
    const float phi=(u-.5f)*2*pi,theta=v*pi,s=std::sin(theta);
    return {std::cos(phi)*s,std::cos(theta),std::sin(phi)*s};
}
inline Vec sample(const RadianceImage& src,Vec d){
    const float u=std::atan2(d.z,d.x)/(2*pi)+.5f,v=std::acos(std::clamp(d.y,-1.f,1.f))/pi;
    const float fx=u*src.width-.5f,fy=v*src.height-.5f;
    const int x=int(std::floor(fx)),y=int(std::floor(fy));
    auto texel=[&](int xx,int yy){
        xx=(xx%src.width+src.width)%src.width;yy=std::clamp(yy,0,src.height-1);
        const auto i=(size_t(yy)*src.width+xx)*4;
        return Vec{halfValue(src.rgba16f[i]),halfValue(src.rgba16f[i+1]),halfValue(src.rgba16f[i+2])};
    };
    const float a=fx-x,b=fy-y;
    return (texel(x,y)*(1-a)+texel(x+1,y)*a)*(1-b)+(texel(x,y+1)*(1-a)+texel(x+1,y+1)*a)*b;
}
inline RadianceImage convolve(const RadianceImage& source){
    if(source.width<2||source.height<1||source.width!=2*source.height||
       source.width>8192||source.rgba16f.size()!=size_t(source.width)*source.height*4)
        throw std::invalid_argument("HDR environment must be a bounded 2:1 lat-long image");
    RadianceImage result;result.width=bandWidth;result.height=bandHeight*bands;
    result.rgba16f.resize(size_t(result.width)*result.height*4);
    constexpr int samples=48;
    constexpr float golden=.6180339887498948f;
    for(int band=0;band<bands;++band)for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const Vec n=direction((x+.5f)/width,(y+.5f)/height);
        Vec sum{};
        if(band==0)sum=sample(source,n);
        else{
            const float r=float(band)/3.f;
            const float exponent=band==4?1.f:std::max(0.f,2.f/std::pow(r,4.f)-2.f);
            const Vec t=unit(cross(std::abs(n.y)<.99f?Vec{0,1,0}:Vec{1,0,0},n)),b=cross(n,t);
            for(int i=0;i<samples;++i){
                const float u=(i+.5f)/samples,phase=(i*golden-std::floor(i*golden))*2*pi;
                const float z=std::pow(1.f-u,1.f/(exponent+1.f)),s=std::sqrt(std::max(0.f,1.f-z*z));
                sum=sum+sample(source,unit(n*z+t*(s*std::cos(phase))+b*(s*std::sin(phase))));
            }
            sum=sum*(1.f/samples);
        }
        const size_t i=(size_t(band*bandHeight+y+1)*bandWidth+x+1)*4;
        result.rgba16f[i]=hdr_detail::toHalf(sum.x);result.rgba16f[i+1]=hdr_detail::toHalf(sum.y);result.rgba16f[i+2]=hdr_detail::toHalf(sum.z);result.rgba16f[i+3]=0x3c00u;
    }
    // Duplicate wrap columns and clamp pole rows to isolate each filtered band.
    auto copy=[&](int dx,int dy,int sx,int sy){
        const size_t d=(size_t(dy)*bandWidth+dx)*4,s=(size_t(sy)*bandWidth+sx)*4;
        for(int k=0;k<4;++k)result.rgba16f[d+k]=result.rgba16f[s+k];
    };
    for(int band=0;band<bands;++band){
        const int off=band*bandHeight;
        for(int y=1;y<=height;++y){copy(0,off+y,width,off+y);copy(width+1,off+y,1,off+y);}
        for(int x=0;x<bandWidth;++x){copy(x,off,x,off+1);copy(x,off+height+1,x,off+height);}
    }
    return result;
}
} // namespace aiverse::render::environment_ibl
