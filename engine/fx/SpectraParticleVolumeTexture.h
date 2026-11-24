// Bounded portable density animation: cubic R8 frames, x-fastest, frame-major.
#pragma once
#include "SpectraSdfBinary.h"
#include <memory>
#include <array>
namespace aiverse::spectra {
struct ParticleVolumeTexture {
    uint32_t dimension=0,frames=0;
    std::vector<uint8_t> density;
    bool valid()const{
        return dimension>=4&&dimension<=64&&frames>=1&&frames<=32&&
            dimension*frames<=256&&density.size()==size_t(dimension)*dimension*dimension*frames;
    }
};
// Sparse GPU representation of existing SPVT data. Four logical texels per
// brick axis, with a one-texel halo on both sides for seamless filtering.
struct SparseParticleVolume {
    uint32_t grid=0,atlasX=0,atlasY=0,atlasZ=0,occupied=0;
    std::vector<uint8_t> map,atlas;
    size_t bytes()const{return map.size()+atlas.size();}
};
inline bool buildSparseParticleVolume(const ParticleVolumeTexture& input,SparseParticleVolume& out){
    if(!input.valid())return false;
    constexpr uint32_t b=4,p=6,maxAxis=42;
    SparseParticleVolume next;
    const uint32_t n=input.dimension,f=input.frames,m=(n+b-1)/b;
    next.grid=m;next.map.resize(size_t(m)*m*m*f*4);
    std::vector<std::array<uint8_t,p*p*p>> bricks;
    const auto sample=[&](uint32_t frame,int x,int y,int z){
        x=std::clamp(x,0,int(n)-1);y=std::clamp(y,0,int(n)-1);z=std::clamp(z,0,int(n)-1);
        return input.density[((size_t(frame)*n+z)*n+y)*n+x];
    };
    for(uint32_t frame=0;frame<f;++frame)for(uint32_t z=0;z<m;++z)
        for(uint32_t y=0;y<m;++y)for(uint32_t x=0;x<m;++x){
            std::array<uint8_t,p*p*p> tile{};bool active=false;
            for(uint32_t k=0;k<p;++k)for(uint32_t j=0;j<p;++j)for(uint32_t i=0;i<p;++i){
                auto value=sample(frame,int(x*b+i)-1,int(y*b+j)-1,int(z*b+k)-1);
                tile[(k*p+j)*p+i]=value;active|=value!=0;
            }
            if(!active)continue;
            bricks.push_back(tile);uint32_t id=uint32_t(bricks.size());
            const size_t at=((size_t(frame)*m+z)*m*m+y*m+x)*4;
            next.map[at]=uint8_t(id);next.map[at+1]=uint8_t(id>>8);next.map[at+2]=uint8_t(id>>16);
        }
    next.occupied=uint32_t(bricks.size());
    const uint32_t count=std::max(1u,next.occupied);
    next.atlasX=std::min(maxAxis,count);
    next.atlasY=std::min(maxAxis,(count+next.atlasX-1)/next.atlasX);
    next.atlasZ=(count+next.atlasX*next.atlasY-1)/(next.atlasX*next.atlasY);
    if(next.atlasZ>maxAxis)return false;
    const uint32_t w=next.atlasX*p,h=next.atlasY*p,d=next.atlasZ*p;
    if(size_t(w)*h*d+next.map.size()>=input.density.size())return false;
    next.atlas.resize(size_t(w)*h*d);
    for(uint32_t id=0;id<next.occupied;++id){
        const uint32_t x=(id%next.atlasX)*p,y=(id/next.atlasX%next.atlasY)*p,z=(id/(next.atlasX*next.atlasY))*p;
        for(uint32_t k=0;k<p;++k)for(uint32_t j=0;j<p;++j)for(uint32_t i=0;i<p;++i)
            next.atlas[(size_t(z+k)*h+y+j)*w+x+i]=bricks[id][(k*p+j)*p+i];
    }
    out=std::move(next);return true;
}
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
