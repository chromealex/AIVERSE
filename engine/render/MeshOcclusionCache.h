#pragma once
#include "MeshOcclusion.h"
#include "../core/Hash.h"
#include <bit>
namespace aiverse::render {
inline uint64_t meshOcclusionKey(const Model& model,float radius,int samples) {
    Hasher h;h.u32(1);h.u32(std::bit_cast<uint32_t>(radius));h.i32(samples);
    h.u64(model.vertices.size());h.u64(model.indices.size());h.u64(model.meshes.size());h.u64(model.materials.size());
    for(const auto& v:model.vertices){for(float f:{v.pos.x,v.pos.y,v.pos.z,v.nrm.x,v.nrm.y,v.nrm.z})h.u32(std::bit_cast<uint32_t>(f));h.u32(v.color);}
    for(auto i:model.indices)h.u32(i);
    for(const auto& m:model.meshes){h.u32(m.deformable);h.u64(m.primitives.size());for(const auto& p:m.primitives){h.u32(p.firstIndex);h.u32(p.indexCount);h.i32(p.material);h.i32(p.morphTargets);h.u64(p.jointPalette.size());}}
    for(const auto& m:model.materials){h.i32(int(m.alpha));h.i32(int(m.team));h.i32(int(m.shading));h.u32(m.transparent);h.u32(m.doubleSided);h.u32(m.vertexOcclusion);}
    return h.value();
}
inline std::string encodeMeshOcclusion(const Model& model,uint64_t key) {
    std::string bytes="AVAO";auto append=[&](uint64_t v){for(int i=0;i<8;++i)bytes.push_back(char(v>>(8*i)));};
    append(key);append(model.vertices.size());append(model.materials.size());
    for(const auto& v:model.vertices)bytes.push_back(char(v.color>>24));
    for(const auto& m:model.materials)bytes.push_back(m.vertexOcclusion?1:0);
    Hasher h;h.bytes(bytes.data(),bytes.size());append(h.value());return bytes;
}
inline void decodeMeshOcclusion(Model& model,std::string_view bytes,uint64_t key) {
    auto read=[&](size_t offset){uint64_t v=0;for(int i=0;i<8;++i)v|=uint64_t(uint8_t(bytes[offset+size_t(i)]))<<(8*i);return v;};
    if(bytes.size()!=36+model.vertices.size()+model.materials.size()||bytes.substr(0,4)!="AVAO"||read(4)!=key||read(12)!=model.vertices.size()||read(20)!=model.materials.size())
        throw std::runtime_error("mesh occlusion cache does not match geometry/settings");
    Hasher h;h.bytes(bytes.data(),bytes.size()-8);if(read(bytes.size()-8)!=h.value())throw std::runtime_error("mesh occlusion cache checksum mismatch");
    const size_t materials=28+model.vertices.size();
    for(size_t i=0;i<model.materials.size();++i)if(uint8_t(bytes[materials+i])>1)throw std::runtime_error("invalid mesh occlusion material flag");
    for(size_t i=0;i<model.vertices.size();++i)model.vertices[i].color=(model.vertices[i].color&0xffffffu)|(uint32_t(uint8_t(bytes[28+i]))<<24);
    for(size_t i=0;i<model.materials.size();++i)model.materials[i].vertexOcclusion=bytes[materials+i]!=0;
}
}
