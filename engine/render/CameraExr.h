// AIVERSE LENS portable OpenEXR writer for linear HDR captures.
// Single-part scanline image, HALF channels A,B,G,R, no compression, so it
// needs no external library (TinyEXR/OpenEXR optional, not required). Input
// rows are top-down RGBA in half floats (the readback layout of RGBA16F) or
// 32-bit floats; output is readable by any OpenEXR 1.x+ reader.
#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace aiverse::render {

// IEEE 754 binary32 -> binary16, round to nearest even, Inf/NaN preserved.
inline uint16_t cameraFloatToHalf(float value) {
    const uint32_t f=std::bit_cast<uint32_t>(value);
    const uint32_t sign=(f>>16)&0x8000u;
    const uint32_t exponent=(f>>23)&0xffu;
    uint32_t mantissa=f&0x7fffffu;
    if(exponent==0xffu) // Inf / NaN (keep a quiet NaN payload bit)
        return uint16_t(sign|0x7c00u|(mantissa?0x200u|(mantissa>>13):0u));
    const int e=int(exponent)-127+15;
    if(e>=0x1f)return uint16_t(sign|0x7c00u); // overflow -> Inf
    if(e<=0) {                                 // subnormal half or zero
        if(e<-10)return uint16_t(sign);
        mantissa|=0x800000u;
        const int shift=14-e;
        uint32_t half=mantissa>>shift;
        const uint32_t rest=mantissa&((1u<<shift)-1u),mid=1u<<(shift-1);
        if(rest>mid||(rest==mid&&(half&1u)))++half;
        return uint16_t(sign|half);
    }
    uint32_t half=(uint32_t(e)<<10)|(mantissa>>13);
    const uint32_t rest=mantissa&0x1fffu;
    if(rest>0x1000u||(rest==0x1000u&&(half&1u)))++half; // may carry into Inf
    return uint16_t(sign|half);
}
inline float cameraHalfToFloat(uint16_t h) {
    const uint32_t sign=uint32_t(h&0x8000u)<<16;
    const uint32_t exponent=(h>>10)&0x1fu, mantissa=h&0x3ffu;
    uint32_t f;
    if(exponent==0) {
        if(!mantissa)f=sign;
        else { // normalize subnormal
            int e=-1;uint32_t m=mantissa;
            do{m<<=1;++e;}while(!(m&0x400u));
            f=sign|(uint32_t(127-15-e)<<23)|((m&0x3ffu)<<13);
        }
    } else if(exponent==0x1f) f=sign|0x7f800000u|(mantissa<<13);
    else f=sign|((exponent+127-15)<<23)|(mantissa<<13);
    return std::bit_cast<float>(f);
}

namespace exr_detail {
inline void u32(std::vector<uint8_t>& o,uint32_t v){for(int i=0;i<4;++i)o.push_back(uint8_t(v>>(8*i)));}
inline void u64(std::vector<uint8_t>& o,uint64_t v){for(int i=0;i<8;++i)o.push_back(uint8_t(v>>(8*i)));}
inline void f32(std::vector<uint8_t>& o,float v){u32(o,std::bit_cast<uint32_t>(v));}
inline void str(std::vector<uint8_t>& o,const char* s){while(*s)o.push_back(uint8_t(*s++));o.push_back(0);}
inline void attr(std::vector<uint8_t>& o,const char* name,const char* type,
                 const std::vector<uint8_t>& value) {
    str(o,name);str(o,type);u32(o,uint32_t(value.size()));
    o.insert(o.end(),value.begin(),value.end());
}
} // namespace exr_detail

// rgbaHalf: width*height*4 halves, top-down rows, channel order R,G,B,A.
inline bool cameraEncodeExrHalf(int width,int height,std::span<const uint16_t> rgbaHalf,
                                std::vector<uint8_t>& output) {
    using namespace exr_detail;
    if(width<=0||height<=0||width>16384||height>16384)return false;
    const size_t pixels=size_t(width)*size_t(height);
    if(rgbaHalf.size()!=pixels*4)return false;
    std::vector<uint8_t> out;
    u32(out,20000630u);           // magic
    u32(out,2u);                  // version 2, single-part scanline
    std::vector<uint8_t> v;
    for(const char* name:{"A","B","G","R"}) { // channel list, sorted by name
        str(v,name);u32(v,1u);    // pixel type HALF
        v.push_back(0);v.push_back(0);v.push_back(0);v.push_back(0); // pLinear + reserved
        u32(v,1u);u32(v,1u);      // x/y sampling
    }
    v.push_back(0);attr(out,"channels","chlist",v);
    v.assign(1,0);attr(out,"compression","compression",v);          // NO_COMPRESSION
    v.clear();u32(v,0);u32(v,0);u32(v,uint32_t(width-1));u32(v,uint32_t(height-1));
    attr(out,"dataWindow","box2i",v);attr(out,"displayWindow","box2i",v);
    v.assign(1,0);attr(out,"lineOrder","lineOrder",v);              // INCREASING_Y
    v.clear();f32(v,1.f);attr(out,"pixelAspectRatio","float",v);
    v.clear();f32(v,0.f);f32(v,0.f);attr(out,"screenWindowCenter","v2f",v);
    v.clear();f32(v,1.f);attr(out,"screenWindowWidth","float",v);
    out.push_back(0);             // end of header
    const size_t lineBytes=size_t(width)*4*2;
    const uint64_t first=out.size()+size_t(height)*8;
    for(int y=0;y<height;++y)u64(out,first+uint64_t(y)*(8+lineBytes));
    static constexpr int kPlane[4]={3,2,1,0}; // A,B,G,R from R,G,B,A input
    out.reserve(out.size()+size_t(height)*(8+lineBytes));
    for(int y=0;y<height;++y) {
        u32(out,uint32_t(y));u32(out,uint32_t(lineBytes));
        const uint16_t* row=rgbaHalf.data()+size_t(y)*size_t(width)*4;
        for(int c:kPlane)
            for(int x=0;x<width;++x) {
                const uint16_t h=row[size_t(x)*4+size_t(c)];
                out.push_back(uint8_t(h));out.push_back(uint8_t(h>>8));
            }
    }
    output=std::move(out);
    return true;
}
inline bool cameraEncodeExrFloat(int width,int height,std::span<const float> rgba,
                                 std::vector<uint8_t>& output) {
    if(width<=0||height<=0||rgba.size()!=size_t(width)*size_t(height)*4)return false;
    std::vector<uint16_t> half(rgba.size());
    for(size_t i=0;i<rgba.size();++i)half[i]=cameraFloatToHalf(rgba[i]);
    return cameraEncodeExrHalf(width,height,half,output);
}
// RGBA16F readback bytes (8 per pixel, little-endian halves) -> EXR file.
inline bool cameraWriteExrRgba16F(const std::string& path,int width,int height,
                                  std::span<const uint8_t> bytes) {
    if(width<=0||height<=0||bytes.size()!=size_t(width)*size_t(height)*8)return false;
    std::vector<uint16_t> half(bytes.size()/2);
    for(size_t i=0;i<half.size();++i)half[i]=uint16_t(bytes[2*i]|(bytes[2*i+1]<<8));
    std::vector<uint8_t> file;
    if(!cameraEncodeExrHalf(width,height,half,file))return false;
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()),std::streamsize(file.size()));
    return bool(out);
}
} // namespace aiverse::render
