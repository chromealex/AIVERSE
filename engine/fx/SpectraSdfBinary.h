// Portable immutable signed-distance lattice interchange. No GPU or filesystem ownership.
#pragma once
#include "SpectraStaticSdfVolume.h"
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::fx {
using SdfResourceReader=std::function<bool(const std::string&,std::string&)>;
inline bool validSdfResourcePath(std::string_view path){
    if(path.size()<16||path.size()>512||path.substr(0,8)!="content/"||
       path.substr(path.size()-7)!=".sdfbin")return false;
    size_t start=0;
    for(size_t i=0;i<=path.size();++i){
        if(i==path.size()||path[i]=='/'){
            const auto part=path.substr(start,i-start);
            if(part.empty()||part=="."||part=="..")return false;
            start=i+1;
        }else{
            const auto c=static_cast<unsigned char>(path[i]);
            if(c<32||c==127||c=='\\'||c==':')return false;
        }
    }
    return true;
}
inline constexpr size_t kSdfBinaryHeaderBytes=32;
inline constexpr size_t kSdfBinaryMaxBytes=kSdfBinaryHeaderBytes+4*kStaticSdfMaxSamples+4;
namespace sdf_binary_detail {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"SDF requires IEEE float32");
inline void put(std::string& out,uint32_t v){
    for(unsigned i=0;i<4;++i)out.push_back(char((v>>(8*i))&255u));
}
inline uint32_t get(std::string_view s,size_t p){
    uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(uint8_t(s[p+i]))<<(8*i);return v;
}
inline void putFloat(std::string& out,float f){uint32_t v;std::memcpy(&v,&f,4);put(out,v);}
inline float getFloat(std::string_view s,size_t p){uint32_t v=get(s,p);float f;std::memcpy(&f,&v,4);return f;}
inline uint32_t checksum(std::string_view bytes){
    uint32_t crc=0xffffffffu;
    for(unsigned char c:bytes){
        crc^=c;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
}
// SDFB, version, dimension, origin.xyz, spacing, sample count; floats x-fastest;
// trailing CRC32 of header + payload. Explicit little endian, no native struct padding.
inline bool encodeSdfBinary(const StaticSdfVolume& sdf,std::string& output){
    using namespace sdf_binary_detail;
    if(!sdf.dimension||!sdf.valid())return false;
    std::string bytes;bytes.reserve(kSdfBinaryHeaderBytes+sdf.samples.size()*4+4);
    bytes.append("SDFB",4);put(bytes,1);put(bytes,sdf.dimension);
    putFloat(bytes,sdf.origin.x);putFloat(bytes,sdf.origin.y);putFloat(bytes,sdf.origin.z);
    putFloat(bytes,sdf.spacing);put(bytes,uint32_t(sdf.samples.size()));
    for(float f:sdf.samples)putFloat(bytes,f);
    put(bytes,checksum(bytes));output.swap(bytes);return true;
}
inline bool decodeSdfBinary(std::string_view bytes,StaticSdfVolume& output,std::string* error=nullptr){
    using namespace sdf_binary_detail;
    const auto fail=[&](const char* msg){if(error)*error=msg;return false;};
    if(bytes.size()<kSdfBinaryHeaderBytes+4||bytes.size()>kSdfBinaryMaxBytes)
        return fail("invalid SDF binary length");
    if(bytes.substr(0,4)!="SDFB"||get(bytes,4)!=1)return fail("invalid SDF magic or version");
    const uint32_t n=get(bytes,8),count=get(bytes,28);
    if(n<3||n>kStaticSdfMaxDimension||count!=n*n*n||
       bytes.size()!=kSdfBinaryHeaderBytes+size_t(count)*4+4)
        return fail("invalid SDF dimensions or sample count");
    if(checksum(bytes.substr(0,bytes.size()-4))!=get(bytes,bytes.size()-4))
        return fail("SDF checksum mismatch");
    StaticSdfVolume next;
    next.dimension=n;next.origin={getFloat(bytes,12),getFloat(bytes,16),getFloat(bytes,20)};
    next.spacing=getFloat(bytes,24);next.samples.reserve(count);
    for(size_t i=0;i<count;++i)next.samples.push_back(getFloat(bytes,kSdfBinaryHeaderBytes+4*i));
    if(!next.valid())return fail("invalid SDF lattice values");
    output=std::move(next);if(error)error->clear();return true;
}
// Reader must enforce its own IO limit before allocation; codec enforces decoded bounds.
template<class Reader>
inline bool loadSdfBinary(const std::string& path,const Reader& reader,StaticSdfVolume& out,std::string& error){
    std::string bytes;
    if(!reader(path,bytes)){error="cannot read SDF binary";return false;}
    return decodeSdfBinary(bytes,out,&error);
}
}
