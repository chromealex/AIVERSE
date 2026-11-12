#pragma once
#include "MeshOcclusion.h"
#include "../core/Hash.h"
#include <bit>
#include <cmath>
namespace aiverse::render {
// Portable mesh AO cache ("AVA2"). The exact key covers only integer data: topology, vertex colors,
// material flags and bake settings. Loaded floats are not hashed: compilers contract normal
// normalization into FMA differently (clang arm64 vs gcc x86), so bit-exact float keys rejected the
// same asset on another platform. Geometry is checked with a tolerance instead: per 4096-vertex block,
// position/normal sums and one sample vertex. This detects replaced or edited assets, not 1-ulp noise.
inline uint64_t meshOcclusionKey(const Model& model,float radius,int samples) {
    Hasher h;h.u32(2);h.u32(std::bit_cast<uint32_t>(radius));h.i32(samples);
    h.u64(model.vertices.size());h.u64(model.indices.size());h.u64(model.meshes.size());h.u64(model.materials.size());
    for(const auto& v:model.vertices)h.u32(v.color&0xffffffu); // alpha holds the cached AO itself
    for(auto i:model.indices)h.u32(i);
    for(const auto& m:model.meshes){h.u32(m.deformable);h.u64(m.primitives.size());for(const auto& p:m.primitives){h.u32(p.firstIndex);h.u32(p.indexCount);h.i32(p.material);h.i32(p.morphTargets);h.u64(p.jointPalette.size());}}
    for(const auto& m:model.materials){h.i32(int(m.alpha));h.i32(int(m.team));h.i32(int(m.shading));h.u32(m.transparent);h.u32(m.doubleSided);h.u32(m.vertexOcclusion);}
    return h.value();
}
namespace detail {
constexpr size_t kAoBlock=4096;
struct AoBlock {double sum[6]={},magnitude=0;float sample[6]={};};
inline AoBlock aoBlock(const Model& model,size_t first) {
    AoBlock b;const size_t end=std::min(model.vertices.size(),first+kAoBlock);
    for(size_t i=first;i<end;++i){const auto& v=model.vertices[i];const float c[6]={v.pos.x,v.pos.y,v.pos.z,v.nrm.x,v.nrm.y,v.nrm.z};
        for(int k=0;k<6;++k){b.sum[k]+=c[k];b.magnitude+=std::abs(c[k]);}}
    const auto& s=model.vertices[first];const float c[6]={s.pos.x,s.pos.y,s.pos.z,s.nrm.x,s.nrm.y,s.nrm.z};
    for(int k=0;k<6;++k)b.sample[k]=c[k];
    return b;
}
}
inline std::string encodeMeshOcclusion(const Model& model,uint64_t key) {
    std::string bytes="AVA2";auto append=[&](uint64_t v){for(int i=0;i<8;++i)bytes.push_back(char(v>>(8*i)));};
    append(key);append(model.vertices.size());append(model.materials.size());
    for(size_t first=0;first<model.vertices.size();first+=detail::kAoBlock){auto b=detail::aoBlock(model,first);
        for(double s:b.sum)append(std::bit_cast<uint64_t>(s));
        for(float s:b.sample)append(std::bit_cast<uint32_t>(s));}
    for(const auto& v:model.vertices)bytes.push_back(char(v.color>>24));
    for(const auto& m:model.materials)bytes.push_back(m.vertexOcclusion?1:0);
    Hasher h;h.bytes(bytes.data(),bytes.size());append(h.value());return bytes;
}
inline void decodeMeshOcclusion(Model& model,std::string_view bytes,uint64_t key) {
    auto read=[&](size_t offset){uint64_t v=0;for(int i=0;i<8;++i)v|=uint64_t(uint8_t(bytes[offset+size_t(i)]))<<(8*i);return v;};
    if(bytes.substr(0,4)=="AVAO")throw std::runtime_error("mesh occlusion cache uses the non-portable v1 format; rebake it");
    const size_t blocks=(model.vertices.size()+detail::kAoBlock-1)/detail::kAoBlock,fingerprint=28,payload=fingerprint+blocks*96;
    if(bytes.size()!=payload+model.vertices.size()+model.materials.size()+8||bytes.substr(0,4)!="AVA2"||read(4)!=key||read(12)!=model.vertices.size()||read(20)!=model.materials.size())
        throw std::runtime_error("mesh occlusion cache does not match geometry/settings");
    Hasher h;h.bytes(bytes.data(),bytes.size()-8);if(read(bytes.size()-8)!=h.value())throw std::runtime_error("mesh occlusion cache checksum mismatch");
    for(size_t b=0;b<blocks;++b){const auto actual=detail::aoBlock(model,b*detail::kAoBlock);const size_t at=fingerprint+b*96;
        for(int k=0;k<6;++k){
            const double sum=std::bit_cast<double>(read(at+size_t(k)*8));
            const float sample=std::bit_cast<float>(uint32_t(read(at+48+size_t(k)*8)));
            // Rounding differences are ~1e-7 relative per value; the 1e-5 bound tolerates them while
            // rejecting replaced assets and block-scale edits (not sub-millimetre tweaks of single vertices).
            if(!std::isfinite(sum)||std::abs(sum-actual.sum[k])>1e-5*actual.magnitude+1e-6||std::abs(double(sample)-actual.sample[k])>1e-4*(1+std::abs(double(sample))))
                throw std::runtime_error("mesh occlusion cache does not match geometry/settings");
        }
    }
    const size_t materials=payload+model.vertices.size();
    for(size_t i=0;i<model.materials.size();++i)if(uint8_t(bytes[materials+i])>1)throw std::runtime_error("invalid mesh occlusion material flag");
    for(size_t i=0;i<model.vertices.size();++i)model.vertices[i].color=(model.vertices[i].color&0xffffffu)|(uint32_t(uint8_t(bytes[payload+i]))<<24);
    for(size_t i=0;i<model.materials.size();++i)model.materials[i].vertexOcclusion=bytes[materials+i]!=0;
}
}
