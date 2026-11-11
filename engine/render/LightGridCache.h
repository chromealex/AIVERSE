#pragma once
#include "StaticLightGrid.h"
#include "../core/Hash.h"
#include <bit>
#include <string>
#include <string_view>
namespace aiverse::render {
// Versioned little-endian snapshot. The owner must rebake when source assets change.
inline std::string encodeLightGrid(const StaticLightGrid& g,uint64_t recipeKey) {
    if(g.texels.empty()||g.texels.size()>4194304)throw std::runtime_error("invalid light cache size");
    std::string out="AVGIC004";
    auto u32=[&](uint32_t n){for(int i=0;i<4;++i)out.push_back(char(n>>(i*8)));};
    auto u64=[&](uint64_t n){for(int i=0;i<8;++i)out.push_back(char(n>>(i*8)));};
    auto vec=[&](float4 v){for(float c:{v.x,v.y,v.z,v.w})u32(std::bit_cast<uint32_t>(c));};
    u64(recipeKey);u32(uint32_t(g.texels.size()));u32(uint32_t(g.maxCellLights));vec(g.origin);vec(g.size);
    for(auto v:g.texels)vec(v);
    Hasher hash;hash.bytes(out.data(),out.size());u64(hash.value());return out;
}
inline StaticLightGrid decodeLightGrid(std::string_view bytes,uint64_t recipeKey) {
    if(bytes.size()<64||bytes.size()>64+4194304ull*16||bytes.substr(0,8)!="AVGIC004")throw std::runtime_error("invalid light cache header");
    size_t cursor=8;
    auto u32=[&](){if(cursor+4>bytes.size())throw std::runtime_error("truncated light cache");uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(uint8_t(bytes[cursor++]))<<(i*8);return n;};
    auto u64=[&](){uint64_t lo=u32(),hi=u32();return lo|(hi<<32);};
    auto vec=[&](){float4 v;v.x=std::bit_cast<float>(u32());v.y=std::bit_cast<float>(u32());v.z=std::bit_cast<float>(u32());v.w=std::bit_cast<float>(u32());for(float c:{v.x,v.y,v.z,v.w})if(!std::isfinite(c))throw std::runtime_error("nonfinite light cache");return v;};
    if(u64()!=recipeKey)throw std::runtime_error("light cache recipe mismatch; rebake");
    uint32_t count=u32(),maxCell=u32();
    if(!count||count>4194304||bytes.size()!=64+size_t(count)*16||maxCell>512)throw std::runtime_error("invalid light cache budget");
    StaticLightGrid g;g.maxCellLights=int(maxCell);g.origin=vec();g.size=vec();
    if(g.origin.w<=0||g.origin.w>4||g.size.w!=2048||count%2048)throw std::runtime_error("invalid light cache layout");
    for(float c:{g.size.x,g.size.y,g.size.z})if(c<1||c>256||c!=std::floor(c))throw std::runtime_error("invalid light cache extent");
    size_t cells=size_t(g.size.x*g.size.y*g.size.z);if(cells>262144||cells>count)throw std::runtime_error("invalid light cache cells");
    g.texels.reserve(count);for(uint32_t i=0;i<count;++i)g.texels.push_back(vec());
    Hasher hash;hash.bytes(bytes.data(),bytes.size()-8);if(u64()!=hash.value())throw std::runtime_error("light cache checksum mismatch");
    auto range=[&](float offset,size_t length){return offset>=float(cells)&&offset==std::floor(offset)&&offset<=float(count)&&length<=count-size_t(offset);};
    for(size_t i=0;i<cells;++i){auto cell=g.texels[i];
        if(cell.w<0||cell.w>7||cell.w!=std::floor(cell.w)||cell.y<0||cell.y>512||cell.y!=std::floor(cell.y)||!range(cell.x,size_t(cell.y)*2)||(cell.z!=0&&!range(cell.z,106)))throw std::runtime_error("invalid light cache cell offsets");
        for(size_t j=0;j<size_t(cell.y);++j){auto pos=g.texels[size_t(cell.x)+2*j],energy=g.texels[size_t(cell.x)+2*j+1];
            if(pos.w<=0||energy.x<0||energy.y<0||energy.z<0)throw std::runtime_error("invalid cached light");
            if(energy.w==0)continue;
            if(!range(energy.w,1))throw std::runtime_error("invalid cached shadow offset");
            auto meta=g.texels[size_t(energy.w)];if(meta.x<16||meta.x>256||meta.x!=std::floor(meta.x)||!range(energy.w,1+size_t(6*meta.x*meta.x))||(meta.z!=0&&!range(meta.w,1)))throw std::runtime_error("invalid cached shadow");
        }
        if(cell.z!=0&&g.texels[size_t(cell.z)+3].x!=4)throw std::runtime_error("invalid cached probe");
    }
    return g;
}
}
