// Bounded portable density animation: cubic R8 frames, x-fastest, frame-major.
#pragma once
#include "SpectraSdfBinary.h"
#include <memory>
namespace aiverse::spectra {
struct ParticleVolumeTexture {
    uint32_t dimension=0,frames=0;
    std::vector<uint8_t> density;
    bool valid()const{
        return dimension>=4&&dimension<=64&&frames>=1&&frames<=32&&
            dimension*frames<=256&&density.size()==size_t(dimension)*dimension*dimension*frames;
    }
};
inline constexpr size_t kParticleVolumeTextureMaxBytes=64u*64u*256u+24u;
inline bool encodeParticleVolumeTexture(const ParticleVolumeTexture& a,std::string& out){
    using namespace fx::sdf_binary_detail;
    if(!a.valid())return false;
    std::string bytes="SPVT";put(bytes,1);put(bytes,a.dimension);put(bytes,a.frames);
    put(bytes,uint32_t(a.density.size()));
    bytes.append(reinterpret_cast<const char*>(a.density.data()),a.density.size());
    put(bytes,checksum(bytes));out.swap(bytes);return true;
}
inline bool decodeParticleVolumeTexture(std::string_view bytes,ParticleVolumeTexture& out,std::string& error){
    using namespace fx::sdf_binary_detail;
    if(bytes.size()<24||bytes.size()>kParticleVolumeTextureMaxBytes||
       bytes.substr(0,4)!="SPVT"||get(bytes,4)!=1){error="Invalid SPVT header";return false;}
    ParticleVolumeTexture next;next.dimension=get(bytes,8);next.frames=get(bytes,12);
    const auto n=next.dimension,f=next.frames,count=get(bytes,16);
    if(n<4||n>64||f<1||f>32||n*f>256||count!=n*n*n*f||bytes.size()!=24u+count||
       checksum(bytes.substr(0,bytes.size()-4))!=get(bytes,bytes.size()-4)){
        error="Invalid SPVT extent, size or checksum";return false;
    }
    next.density.assign(bytes.begin()+20,bytes.end()-4);
    out=std::move(next);error.clear();return true;
}
}
