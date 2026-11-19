// TerraWeave lossless packing of inactive CPU wind overlays (no GPU readback).
#pragma once
#include "engine/render/TerraWeaveHorizonWindPatch.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace aiverse::terraweave {
// Bytewise zero-run/literal codec. Each token encodes 1..128 bytes.
// The high bit selects a run of zeros, otherwise the count is literal.
// This preserves exact float bit patterns, vertex colour and padding bytes.
inline std::vector<uint8_t> packHorizonWindVertexBytes(
        std::span<const uint8_t> source) {
    std::vector<uint8_t> packed;
    size_t pos=0;
    while(pos<source.size()) {
        size_t zeros=0;
        while(zeros<128u && zeros<source.size()-pos &&
              source[pos+zeros]==0u)++zeros;
        if(zeros>=3u) {
            packed.push_back(static_cast<uint8_t>(0x80u | (zeros-1u)));
            pos+=zeros;
            continue;
        }
        const size_t start=pos;
        while(pos<source.size() && pos-start<128u) {
            if(source.size()-pos>=3u && source[pos]==0u &&
               source[pos+1u]==0u && source[pos+2u]==0u)break;
            ++pos;
        }
        const size_t count=pos-start;
        packed.push_back(static_cast<uint8_t>(count-1u));
        packed.insert(packed.end(),source.begin()+start,source.begin()+pos);
    }
    return packed;
}
inline bool unpackHorizonWindVertexBytes(
        std::span<const uint8_t> packed,
        std::span<uint8_t> destination) {
    size_t input=0,output=0;
    while(input<packed.size()) {
        const uint8_t token=packed[input++];
        const size_t count=size_t(token&0x7fu)+1u;
        if(count>destination.size()-output)return false;
        if((token&0x80u)!=0u) {
            std::memset(destination.data()+output,0,count);
        } else {
            if(count>packed.size()-input)return false;
            std::memcpy(destination.data()+output,packed.data()+input,count);
            input+=count;
        }
        output+=count;
    }
    return output==destination.size();
}
inline bool compressHorizonForestWindOverlay(
        HorizonForestWindOverlay& overlay) {
    static_assert(std::is_trivially_copyable_v<render::ModelVertex>);
    constexpr size_t kMaxBytes=2u*1024u*1024u;
    const size_t count=overlay.vertices.size();
    if(!count || overlay.packedVertexCount || count%8u ||
       count!=overlay.centersCm.size()*8u ||
       count>kMaxBytes/sizeof(render::ModelVertex))return false;
    const auto* bytes=reinterpret_cast<const uint8_t*>(
        overlay.vertices.data());
    const auto candidate=packHorizonWindVertexBytes(
        {bytes,count*sizeof(render::ModelVertex)});
    // Use tight capacity instead of the codec's exponential push-back growth.
    std::vector<uint8_t> tight(candidate.begin(),candidate.end());
    if(tight.capacity()>=overlay.vertices.capacity()*
                         sizeof(render::ModelVertex))return false;
    overlay.packedVertices.swap(tight);
    overlay.packedVertexCount=count;
    std::vector<render::ModelVertex>{}.swap(overlay.vertices);
    return true;
}
inline bool restoreHorizonForestWindOverlay(
        HorizonForestWindOverlay& overlay) {
    if(!overlay.packedVertexCount)return false;
    static_assert(std::is_trivially_copyable_v<render::ModelVertex>);
    constexpr size_t kMaxBytes=2u*1024u*1024u;
    const size_t count=overlay.packedVertexCount;
    if(count%8u || count>kMaxBytes/sizeof(render::ModelVertex) ||
       count!=overlay.centersCm.size()*8u ||
       !overlay.vertices.empty())
        throw std::logic_error("TerraWeave packed wind shadow metadata");
    std::vector<render::ModelVertex> restored(count);
    auto* bytes=reinterpret_cast<uint8_t*>(restored.data());
    if(!unpackHorizonWindVertexBytes(
            overlay.packedVertices,
            {bytes,count*sizeof(render::ModelVertex)}))
        throw std::logic_error("TerraWeave packed wind shadow data");
    overlay.vertices.swap(restored);
    std::vector<uint8_t>{}.swap(overlay.packedVertices);
    overlay.packedVertexCount=0;
    return true;
}
} // namespace aiverse::terraweave
