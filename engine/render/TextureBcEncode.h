// Deterministic bounded BC encoder for authored texture graph exports.
// No external compressor process, no GPU readback here. DDS DX10 stores
// the full mip chain with explicit color space and format metadata.
#pragma once
#include "TextureCodec.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::render::bc_encode {
enum class Format { BC1, BC3, BC4, BC5 };
inline Format parseFormat(const std::string& name){
    if(name=="bc1")return Format::BC1;
    if(name=="bc3")return Format::BC3;
    if(name=="bc4")return Format::BC4;
    if(name=="bc5")return Format::BC5;
    throw std::invalid_argument("Unsupported BC texture export format");
}
inline int bytes(Format f){return f==Format::BC1||f==Format::BC4?8:16;}
inline uint16_t rgb565(const std::array<int,3>& p){
    return uint16_t(((p[0]*31+127)/255)<<11|((p[1]*63+127)/255)<<5|((p[2]*31+127)/255));
}
inline std::array<int,3> expand(uint16_t v){
    const int r=v>>11,g=(v>>5)&63,b=v&31;
    return {(r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)};
}
using Block=std::array<std::array<uint8_t,4>,16>;
inline void encodeColor(const Block& p,uint8_t* out){
    std::array<int,3> lo{255,255,255},hi{};
    for(const auto& t:p)for(int c=0;c<3;++c){lo[c]=std::min(lo[c],int(t[c]));hi[c]=std::max(hi[c],int(t[c]));}
    uint16_t a=rgb565(hi),b=rgb565(lo);
    if(a<b)std::swap(a,b);
    // Always four-color opaque mode (BC1); constant endpoint stays exact.
    if(a==b){if(a<65535)++a;else --b;}
    std::array<std::array<int,3>,4> palette{};palette[0]=expand(a);palette[1]=expand(b);
    for(int c=0;c<3;++c){palette[2][c]=(2*palette[0][c]+palette[1][c])/3;palette[3][c]=(palette[0][c]+2*palette[1][c])/3;}
    uint32_t indices=0;
    for(int i=0;i<16;++i){
        int best=0,error=1000000;
        for(int j=0;j<4;++j){
            int e=0;for(int c=0;c<3;++c){int delta=int(p[i][c])-palette[j][c];e+=delta*delta;}
            if(e<error){error=e;best=j;}
        }
        indices|=uint32_t(best)<<(2*i);
    }
    out[0]=uint8_t(a);out[1]=uint8_t(a>>8);out[2]=uint8_t(b);out[3]=uint8_t(b>>8);
    for(int i=0;i<4;++i)out[4+i]=uint8_t(indices>>(8*i));
}
inline void encodeChannel(const Block& p,int c,uint8_t* out){
    int lo=255,hi=0;for(const auto& t:p){lo=std::min(lo,int(t[c]));hi=std::max(hi,int(t[c]));}
    out[0]=uint8_t(hi);out[1]=uint8_t(lo);
    int palette[8]={hi,lo};
    if(hi>lo)for(int i=2;i<8;++i)palette[i]=((8-i)*hi+(i-1)*lo)/7;
    else {for(int i=2;i<6;++i)palette[i]=lo;palette[6]=0;palette[7]=255;}
    uint64_t bits=0;
    for(int i=0;i<16;++i){
        int best=0,error=256;
        for(int j=0;j<8;++j){const int e=std::abs(int(p[i][c])-palette[j]);if(e<error){error=e;best=j;}}
        bits|=uint64_t(best)<<(3*i);
    }
    for(int i=0;i<6;++i)out[2+i]=uint8_t(bits>>(8*i));
}
inline std::vector<uint8_t> encodeLevel(std::span<const uint8_t> rgba,int w,int h,Format f){
    if(w<1||h<1||w>1024||h>1024||rgba.size()!=size_t(w)*h*4u)throw std::invalid_argument("Invalid BC source extent");
    const int bw=(w+3)/4,bh=(h+3)/4;
    std::vector<uint8_t> out(size_t(bw)*bh*bytes(f));
    for(int y=0;y<bh;++y)for(int x=0;x<bw;++x){
        Block block{};
        for(int j=0;j<4;++j)for(int i=0;i<4;++i){
            const size_t at=(size_t(std::min(h-1,y*4+j))*w+std::min(w-1,x*4+i))*4u;
            for(int c=0;c<4;++c)block[j*4+i][c]=rgba[at+c];
        }
        auto* dst=out.data()+(size_t(y)*bw+x)*bytes(f);
        if(f==Format::BC1)encodeColor(block,dst);
        else if(f==Format::BC3){encodeChannel(block,3,dst);encodeColor(block,dst+8);}
        else if(f==Format::BC4)encodeChannel(block,0,dst);
        else {encodeChannel(block,0,dst);encodeChannel(block,1,dst+8);}
    }
    return out;
}
inline float linear(float x){return x<=0.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
inline float srgb(float x){return x<=.0031308f?x*12.92f:1.055f*std::pow(x,1.f/2.4f)-.055f;}
inline uint8_t quantize(float v){return uint8_t(std::lround(std::clamp(v,0.f,1.f)*255.f));}
inline std::vector<uint8_t> nextMip(std::span<const uint8_t> rgba,int w,int h,bool color,bool normal){
    const int nw=std::max(1,w/2),nh=std::max(1,h/2);
    std::vector<uint8_t> dst(size_t(nw)*nh*4u);
    for(int y=0;y<nh;++y)for(int x=0;x<nw;++x){
        float sum[4]{},weight=0;
        const int x0=x*w/nw,x1=(x+1)*w/nw,y0=y*h/nh,y1=(y+1)*h/nh;
        const float count=float((x1-x0)*(y1-y0));
        for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx){
            const auto i=(size_t(yy)*w+xx)*4u;const float a=rgba[i+3]/255.f;
            sum[3]+=a;weight+=color?a:1.f;
            for(int c=0;c<3;++c){float v=rgba[i+c]/255.f;sum[c]+=(color?linear(v)*a:normal?2.f*v-1.f:v);}
        }
        const size_t o=(size_t(y)*nw+x)*4u;
        if(normal){
            const float l=std::sqrt(sum[0]*sum[0]+sum[1]*sum[1]+sum[2]*sum[2]);
            for(int c=0;c<3;++c)dst[o+c]=quantize(l>1e-6f?.5f+.5f*sum[c]/l:(c==2?1.f:.5f));
        }else for(int c=0;c<3;++c)dst[o+c]=quantize(color?srgb(sum[c]/std::max(weight,1e-6f)):sum[c]/count);
        dst[o+3]=quantize(sum[3]/count);
    }
    return dst;
}
struct Encoded {std::string dds;size_t payload=0,rgbaBytes=0;int levels=0;};
inline Encoded encodeDds(std::span<const uint8_t> rgba,int w,int h,Format f,bool color,bool normal,bool mips){
    if(w<1||h<1||w>1024||h>1024||rgba.size()!=size_t(w)*h*4u)throw std::invalid_argument("Invalid BC source image");
    if((color&&(f==Format::BC4||f==Format::BC5))||(normal&&f==Format::BC4))throw std::invalid_argument("Incompatible BC channel format");
    if(f==Format::BC1)for(size_t i=3;i<rgba.size();i+=4)if(rgba[i]!=255)throw std::invalid_argument("BC1 export requires opaque alpha; use BC3");
    Encoded result;std::vector<std::vector<uint8_t>> levels;
    std::vector<uint8_t> pixels(rgba.begin(),rgba.end());int lw=w,lh=h;
    for(;;){
        levels.push_back(encodeLevel(pixels,lw,lh,f));result.payload+=levels.back().size();result.rgbaBytes+=pixels.size();
        if(!mips||(lw==1&&lh==1))break;
        pixels=nextMip(pixels,lw,lh,color,normal);lw=std::max(1,lw/2);lh=std::max(1,lh/2);
    }
    result.levels=int(levels.size());result.dds.assign(148,'\0');
    auto put=[&](size_t at,uint32_t v){for(int i=0;i<4;++i)result.dds[at+i]=char(v>>(i*8));};
    result.dds.replace(0,4,"DDS ");put(4,124);
    put(8,0x81007u|(mips?0x20000u:0u));put(12,uint32_t(h));put(16,uint32_t(w));put(20,uint32_t(levels[0].size()));put(28,uint32_t(result.levels));
    put(76,32);put(80,4);result.dds.replace(84,4,"DX10");put(108,0x1000u|(result.levels>1?0x400008u:0u));
    put(128,f==Format::BC1?(color?72u:71u):f==Format::BC3?(color?78u:77u):f==Format::BC4?80u:83u);
    put(132,3);put(140,1);put(144,f==Format::BC3?1u:3u);
    for(const auto& level:levels)result.dds.append(reinterpret_cast<const char*>(level.data()),level.size());
    return result;
}
} // namespace aiverse::render::bc_encode
