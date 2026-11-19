// TerraWeave Horizon static-world archive: portable little-endian, versioned,
// strictly bounded CPU bake bytes. The engine does not perform file I/O here.
// Native projects decide how to store and retrieve these blobs (pack/asset/CDN).
#pragma once
#include "engine/world/TerraWeaveHorizon.h"
#include <bit>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {
namespace horizon_archive_detail {

inline constexpr uint32_t kMagic=0x38485754u; // "TWH8" in little endian
inline constexpr uint32_t kVersion=8;
inline constexpr size_t kHeaderBytes=24; // magic,version,fingerprint,checksum
inline constexpr size_t kMaxBytes=20u*1024u*1024u;

inline void put(std::vector<uint8_t>& out,uint64_t v,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)
        out.push_back(static_cast<uint8_t>(v>>(i*8)));
}
inline uint64_t get(std::span<const uint8_t> bytes,size_t& cursor,unsigned count) {
    if(cursor>bytes.size()||count>bytes.size()-cursor)
        throw std::invalid_argument("TerraWeave Horizon archive truncated");
    uint64_t v=0;
    for(unsigned i=0;i<count;++i)v|=uint64_t(bytes[cursor++])<<(i*8);
    return v;
}
inline void put32(std::vector<uint8_t>& out,uint32_t v){put(out,v,4);}
inline void put64(std::vector<uint8_t>& out,uint64_t v){put(out,v,8);}
inline void putI32(std::vector<uint8_t>& out,int32_t v) {
    put32(out,std::bit_cast<uint32_t>(v));
}
inline void putI64(std::vector<uint8_t>& out,int64_t v) {
    put64(out,std::bit_cast<uint64_t>(v));
}
inline uint32_t get32(std::span<const uint8_t> in,size_t& at) {
    return static_cast<uint32_t>(get(in,at,4));
}
inline uint64_t get64(std::span<const uint8_t> in,size_t& at) {
    return get(in,at,8);
}
inline int32_t getI32(std::span<const uint8_t> in,size_t& at) {
    return std::bit_cast<int32_t>(get32(in,at));
}
inline int64_t getI64(std::span<const uint8_t> in,size_t& at) {
    return std::bit_cast<int64_t>(get64(in,at));
}
inline uint64_t checksum(std::span<const uint8_t> bytes) {
    uint64_t hash=14695981039346656037ULL;
    for(const uint8_t value:bytes) {
        hash^=value;
        hash*=1099511628211ULL;
    }
    return hash;
}
inline void validate(const HorizonBake& b) {
    if(b.atlasWidth==0||b.atlasWidth!=b.atlasHeight||
       b.atlasWidth>2048||
       b.atlasRgba.size()!=size_t(b.atlasWidth)*b.atlasHeight*4u||
       b.atlasRgba.size()>16u*1024u*1024u||
       b.terrainResolution>96u||
       b.sideCm<=0||b.sideCm>kWorldLimitCm||
       b.terrainBeginCm<0||b.terrainBeginCm>kWorldLimitCm||
       b.terrainSkirtDepthCm<0||b.terrainSkirtDepthCm>5000||
       b.innerTerrainLipCm<0||b.innerTerrainLipCm>200 ||
       b.maxNearClipTriangles>8192 ||
       b.maxNearWeldSlopePermille>8000 ||
       b.nearSeamNormalBlendPercent>100 ||
       (b.nearWeldSlopeFallback && !b.nearSeamSamples.empty()) ||
       b.nearClipRects.size()>1024 ||
       b.nearSeamSamples.size()>4096 ||
       std::any_of(b.nearSeamSamples.begin(),b.nearSeamSamples.end(),
           [](const HorizonBake::NearSeamSample& s) {
               return s.xQ16cm < -kWorldLimitCm*65536 ||
                      s.xQ16cm > kWorldLimitCm*65536 ||
                      s.zQ16cm < -kWorldLimitCm*65536 ||
                      s.zQ16cm > kWorldLimitCm*65536 ||
                      s.nearHeightCm < -kWorldLimitCm ||
                      s.nearHeightCm > kWorldLimitCm ||
                      (s.nearNormalValid &&
                       (s.nearSlopeXPermille < -1000000 ||
                        s.nearSlopeXPermille > 1000000 ||
                        s.nearSlopeZPermille < -1000000 ||
                        s.nearSlopeZPermille > 1000000));
           }) ||
       std::any_of(b.nearClipRects.begin(),b.nearClipRects.end(),
           [](const RectCm& r) {
               return r.minX>=r.maxX||r.minZ>=r.maxZ||
                   r.minX<-kWorldLimitCm||r.maxX>kWorldLimitCm||
                   r.minZ<-kWorldLimitCm||r.maxZ>kWorldLimitCm;
           }) ||
       (!b.nearOwnedQuads.empty() &&
        (b.terrainResolution==0 ||
         b.nearOwnedQuads.size()!=size_t(b.terrainResolution)*
                                 b.terrainResolution)) ||
       std::any_of(b.nearOwnedQuads.begin(),b.nearOwnedQuads.end(),
                   [](uint8_t v){return v>1;}) ||
       b.originXcm<-kWorldLimitCm||b.originXcm>kWorldLimitCm||
       b.originZcm<-kWorldLimitCm||b.originZcm>kWorldLimitCm||
       b.cameraXcm<-kWorldLimitCm||b.cameraXcm>kWorldLimitCm||
       b.cameraZcm<-kWorldLimitCm||b.cameraZcm>kWorldLimitCm||
       b.terrainVertices.size()>10000||b.clusters.size()>2048)
        throw std::invalid_argument("TerraWeave Horizon archive bounds");
    if(!b.terrainVertices.empty() &&
       (b.terrainResolution==0||
        b.terrainVertices.size()!=size_t(b.terrainResolution+1)*
                                    size_t(b.terrainResolution+1)))
        throw std::invalid_argument("TerraWeave Horizon archive terrain grid");
    for(const auto& v:b.terrainVertices)
        if(v.xCm<-kWorldLimitCm||v.xCm>kWorldLimitCm||
           v.zCm<-kWorldLimitCm||v.zCm>kWorldLimitCm||
           v.heightCm<-kWorldLimitCm||v.heightCm>kWorldLimitCm||
           v.slopeXPermille < -1000000 || v.slopeXPermille > 1000000 ||
           v.slopeZPermille < -1000000 || v.slopeZPermille > 1000000)
            throw std::invalid_argument("TerraWeave Horizon archive terrain vertex");
    for(const auto& c:b.clusters)
        if(c.centerXcm<-kWorldLimitCm||c.centerXcm>kWorldLimitCm||
           c.centerZcm<-kWorldLimitCm||c.centerZcm>kWorldLimitCm||
           c.tileSizeCm<=0||c.tileSizeCm>400000||
           c.bottomCm>=c.topCm||
           c.atlasSize==0||c.atlasSize>64||
           c.atlasSize>b.atlasWidth ||
           c.atlasX>b.atlasWidth-c.atlasSize||
           c.atlasY>b.atlasHeight-c.atlasSize||
           c.atlasSideX>b.atlasWidth-c.atlasSize||
           c.atlasSideY>b.atlasHeight-c.atlasSize)
            throw std::invalid_argument("TerraWeave Horizon archive cluster");
}
} // namespace horizon_archive_detail

// Deterministic configuration identity; authored terrain masks require an
// independently stable content hash (0 means purely procedural).
inline uint64_t horizonArchiveFingerprint(
        const TerrainSettings& terrain,const BiomeSettings& biomes,
        const HorizonSettings& settings,uint64_t authoredContentHash=0) {
    uint64_t h=14695981039346656037ULL;
    const auto add=[&](uint64_t value){h^=value;h*=1099511628211ULL;};
    add(horizon_archive_detail::kVersion);
    add(terrain.seed);
    add(static_cast<uint32_t>(terrain.baseHeightCm));
    add(static_cast<uint32_t>(terrain.reliefCm));
    add(static_cast<uint32_t>(terrain.basePeriodCm));
    add(terrain.octaves);
    add(biomes.seed);
    add(static_cast<uint32_t>(biomes.seaLevelCm));
    add(static_cast<uint32_t>(biomes.baseTemperature));
    add(static_cast<uint32_t>(biomes.baseHumidity));
    add(static_cast<uint32_t>(biomes.climatePeriodCm));
    add(static_cast<uint32_t>(biomes.variation));
    add(static_cast<uint32_t>(biomes.lapsePer100m));
    add(static_cast<uint32_t>(settings.middleTileCm));
    add(settings.middleTilesPerSide);
    add(static_cast<uint32_t>(settings.distantTileCm));
    add(settings.distantTilesPerSide);
    add(static_cast<uint32_t>(settings.closeForestEndCm));
    add(static_cast<uint32_t>(settings.middleForestEndCm));
    add(static_cast<uint32_t>(settings.terrainBeginCm));
    add(static_cast<uint32_t>(settings.terrainSkirtDepthCm));
    add(settings.terrainGridSnapSteps);
    add(static_cast<uint32_t>(settings.innerTerrainLipCm));
    add(settings.maxNearClipTriangles);
    add(settings.maxNearWeldSlopePermille);
    add(settings.nearSeamNormalBlendPercent);
    add(settings.terrainResolution);
    add(settings.atlasTilePixels);
    add(static_cast<uint32_t>(settings.canopyHeightCm));
    add(settings.crownDetail);
    add(settings.orientationViews);
    add(static_cast<uint32_t>(settings.nearBlendCm));
    add(static_cast<uint32_t>(settings.tierBlendCm));
    add(settings.distantTerrain?1u:0u);
    add(authoredContentHash);
    return h?h:1u;
}

inline std::vector<uint8_t> encodeHorizonArchive(
        const HorizonBake& bake,uint64_t fingerprint) {
    using namespace horizon_archive_detail;
    validate(bake);
    if(!fingerprint)
        throw std::invalid_argument("TerraWeave Horizon archive fingerprint");
    std::vector<uint8_t> out;
    out.reserve(kHeaderBytes+bake.atlasRgba.size()+
                bake.clusters.size()*60+bake.terrainVertices.size()*32+
                bake.nearOwnedQuads.size()+
                bake.nearClipRects.size()*32+
                bake.nearSeamSamples.size()*32+140);
    put32(out,kMagic);
    put32(out,kVersion);
    put64(out,fingerprint);
    put64(out,0); // content checksum, backfilled after payload
    putI64(out,bake.originXcm);putI64(out,bake.originZcm);
    putI32(out,bake.sideCm);put32(out,bake.terrainResolution);
    putI32(out,bake.terrainBeginCm);
    putI32(out,bake.terrainSkirtDepthCm);
    putI32(out,bake.innerTerrainLipCm);
    put32(out,bake.maxNearClipTriangles);
    put32(out,bake.maxNearWeldSlopePermille);
    put32(out,bake.nearSeamNormalBlendPercent);
    put32(out,bake.nearWeldSlopeFallback?1u:0u);
    putI64(out,bake.cameraXcm);putI64(out,bake.cameraZcm);
    put32(out,bake.atlasWidth);put32(out,bake.atlasHeight);
    put32(out,static_cast<uint32_t>(bake.terrainVertices.size()));
    put32(out,static_cast<uint32_t>(bake.clusters.size()));
    put32(out,static_cast<uint32_t>(bake.nearOwnedQuads.size()));
    put32(out,static_cast<uint32_t>(bake.nearClipRects.size()));
    put32(out,static_cast<uint32_t>(bake.nearSeamSamples.size()));
    out.insert(out.end(),bake.atlasRgba.begin(),bake.atlasRgba.end());
    for(const auto& v:bake.terrainVertices) {
        putI64(out,v.xCm);putI64(out,v.zCm);
        putI32(out,v.heightCm);put32(out,v.colorRgba);
        putI32(out,v.slopeXPermille);putI32(out,v.slopeZPermille);
    }
    for(const auto& c:bake.clusters) {
        putI64(out,c.centerXcm);putI64(out,c.centerZcm);
        putI32(out,c.tileSizeCm);putI32(out,c.bottomCm);
        putI32(out,c.topCm);put32(out,c.atlasX);
        put32(out,c.atlasY);put32(out,c.atlasSideX);
        put32(out,c.atlasSideY);put32(out,c.atlasSize);
        put32(out,c.coverage);
    }
    out.insert(out.end(),bake.nearOwnedQuads.begin(),
               bake.nearOwnedQuads.end());
    for(const RectCm& r:bake.nearClipRects) {
        putI64(out,r.minX);putI64(out,r.minZ);
        putI64(out,r.maxX);putI64(out,r.maxZ);
    }
    for(const auto& s:bake.nearSeamSamples){
        putI64(out,s.xQ16cm);putI64(out,s.zQ16cm);
        putI32(out,s.nearHeightCm);
        putI32(out,s.nearSlopeXPermille);
        putI32(out,s.nearSlopeZPermille);
        put32(out,s.nearNormalValid?1u:0u);
    }
    if(out.size()>kMaxBytes)
        throw std::invalid_argument("TerraWeave Horizon archive exceeds budget");
    const uint64_t hash=checksum(
        std::span<const uint8_t>(out).subspan(kHeaderBytes));
    for(unsigned i=0;i<8;++i)
        out[16+i]=static_cast<uint8_t>(hash>>(i*8));
    return out;
}

inline HorizonBake decodeHorizonArchive(
        std::span<const uint8_t> bytes,uint64_t expectedFingerprint) {
    using namespace horizon_archive_detail;
    if(bytes.size()<kHeaderBytes||bytes.size()>kMaxBytes)
        throw std::invalid_argument("TerraWeave Horizon archive byte count");
    size_t at=0;
    if(get32(bytes,at)!=kMagic||get32(bytes,at)!=kVersion)
        throw std::invalid_argument("TerraWeave Horizon archive format/version");
    if(!expectedFingerprint||get64(bytes,at)!=expectedFingerprint)
        throw std::invalid_argument("TerraWeave Horizon archive stale fingerprint");
    if(get64(bytes,at)!=checksum(bytes.subspan(kHeaderBytes)))
        throw std::invalid_argument("TerraWeave Horizon archive checksum");
    HorizonBake b;
    b.originXcm=getI64(bytes,at);b.originZcm=getI64(bytes,at);
    b.sideCm=getI32(bytes,at);b.terrainResolution=get32(bytes,at);
    b.terrainBeginCm=getI32(bytes,at);
    b.terrainSkirtDepthCm=getI32(bytes,at);
    b.innerTerrainLipCm=getI32(bytes,at);
    b.maxNearClipTriangles=get32(bytes,at);
    b.maxNearWeldSlopePermille=get32(bytes,at);
    b.nearSeamNormalBlendPercent=get32(bytes,at);
    const uint32_t slopeFallback=get32(bytes,at);
    if(slopeFallback>1u)
        throw std::invalid_argument("TerraWeave Horizon slope fallback flag");
    b.nearWeldSlopeFallback=slopeFallback!=0u;
    b.cameraXcm=getI64(bytes,at);b.cameraZcm=getI64(bytes,at);
    b.atlasWidth=get32(bytes,at);b.atlasHeight=get32(bytes,at);
    const uint32_t terrainCount=get32(bytes,at);
    const uint32_t clusterCount=get32(bytes,at);
    const uint32_t nearQuadCount=get32(bytes,at);
    const uint32_t nearRectCount=get32(bytes,at);
    const uint32_t nearSeamCount=get32(bytes,at);
    if(!b.atlasWidth||b.atlasWidth!=b.atlasHeight||
       b.atlasWidth>2048||terrainCount>10000||clusterCount>2048 ||
       nearRectCount>1024 || nearSeamCount>4096 ||
       (nearQuadCount!=0 &&
        nearQuadCount!=size_t(b.terrainResolution)*b.terrainResolution))
        throw std::invalid_argument("TerraWeave Horizon archive declared sizes");
    const size_t pixelBytes=size_t(b.atlasWidth)*b.atlasHeight*4;
    if(pixelBytes>16u*1024u*1024u||
       at>bytes.size()||pixelBytes>bytes.size()-at)
        throw std::invalid_argument("TerraWeave Horizon archive atlas truncated");
    b.atlasRgba.assign(bytes.begin()+static_cast<std::ptrdiff_t>(at),
                       bytes.begin()+static_cast<std::ptrdiff_t>(at+pixelBytes));
    at+=pixelBytes;
    b.terrainVertices.reserve(terrainCount);
    for(uint32_t i=0;i<terrainCount;++i) {
        HorizonTerrainVertex v;
        v.xCm=getI64(bytes,at);v.zCm=getI64(bytes,at);
        v.heightCm=getI32(bytes,at);v.colorRgba=get32(bytes,at);
        v.slopeXPermille=getI32(bytes,at);
        v.slopeZPermille=getI32(bytes,at);
        b.terrainVertices.push_back(v);
    }
    b.clusters.reserve(clusterCount);
    for(uint32_t i=0;i<clusterCount;++i) {
        HorizonCluster c;
        c.centerXcm=getI64(bytes,at);
        c.centerZcm=getI64(bytes,at);
        c.tileSizeCm=getI32(bytes,at);
        c.bottomCm=getI32(bytes,at);
        c.topCm=getI32(bytes,at);
        c.atlasX=get32(bytes,at);
        c.atlasY=get32(bytes,at);
        c.atlasSideX=get32(bytes,at);
        c.atlasSideY=get32(bytes,at);
        c.atlasSize=get32(bytes,at);
        const uint32_t cov=get32(bytes,at);
        if(cov>255)throw std::invalid_argument(
            "TerraWeave Horizon archive coverage");
        c.coverage=static_cast<uint8_t>(cov);
        b.clusters.push_back(c);
    }
    if(at>bytes.size() || nearQuadCount>bytes.size()-at)
        throw std::invalid_argument("TerraWeave Horizon near quad mask truncated");
    b.nearOwnedQuads.assign(bytes.begin()+static_cast<std::ptrdiff_t>(at),
                            bytes.begin()+static_cast<std::ptrdiff_t>(
                                at+nearQuadCount));
    at+=nearQuadCount;
    if(nearRectCount>(bytes.size()-at)/32)
        throw std::invalid_argument("TerraWeave Horizon near clip rects truncated");
    b.nearClipRects.reserve(nearRectCount);
    for(uint32_t i=0;i<nearRectCount;++i){
        RectCm r;
        r.minX=getI64(bytes,at);r.minZ=getI64(bytes,at);
        r.maxX=getI64(bytes,at);r.maxZ=getI64(bytes,at);
        b.nearClipRects.push_back(r);
    }
    if(nearSeamCount>(bytes.size()-at)/32)
        throw std::invalid_argument("TerraWeave Horizon near seam samples truncated");
    b.nearSeamSamples.reserve(nearSeamCount);
    for(uint32_t i=0;i<nearSeamCount;++i) {
        HorizonBake::NearSeamSample s;
        s.xQ16cm=getI64(bytes,at);
        s.zQ16cm=getI64(bytes,at);
        s.nearHeightCm=getI32(bytes,at);
        s.nearSlopeXPermille=getI32(bytes,at);
        s.nearSlopeZPermille=getI32(bytes,at);
        const uint32_t nearNormal=get32(bytes,at);
        if(nearNormal>1u)
            throw std::invalid_argument("TerraWeave Horizon near normal flag");
        s.nearNormalValid=nearNormal!=0u;
        b.nearSeamSamples.push_back(s);
    }
    if(at!=bytes.size())
        throw std::invalid_argument("TerraWeave Horizon archive trailing bytes");
    validate(b);
    return b;
}

} // namespace aiverse::terraweave
