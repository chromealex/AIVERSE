// Narrow, bounds-checked OpenEXR scanline importer for native AIVERSE games.
// Supports single-part, untiled, uncompressed scanlines with HALF/FLOAT
// R/G/B[/A] channels; rejects unsupported EXR compression and multipart data.
// Other channels are retained only for scanline offset calculation and ignored.
// No external EXR dependency and no changes to ECS/save/replication.
#pragma once
#include "HdrTexture.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::render {

namespace exr_detail {
inline bool u32(std::string_view s, size_t at, uint32_t& out) {
    if (at > s.size() || s.size() - at < 4) return false;
    out = uint32_t(uint8_t(s[at])) |
          (uint32_t(uint8_t(s[at+1])) << 8) |
          (uint32_t(uint8_t(s[at+2])) << 16) |
          (uint32_t(uint8_t(s[at+3])) << 24);
    return true;
}
inline bool u64(std::string_view s, size_t at, uint64_t& out) {
    uint32_t lo=0,hi=0;
    if (!u32(s,at,lo) || !u32(s,at+4,hi)) return false;
    out = uint64_t(lo) | (uint64_t(hi)<<32);
    return true;
}
inline bool cstring(std::string_view s, size_t& at, size_t limit, std::string_view& out) {
    if (at >= s.size()) return false;
    const size_t end = s.find('\0',at);
    if (end == std::string_view::npos || end-at > limit) return false;
    out = s.substr(at,end-at);
    at = end+1;
    return true;
}
inline bool floatValue(std::string_view s, size_t offset, float& out) {
    uint32_t bits = 0;
    if (!u32(s,offset,bits)) return false;
    std::memcpy(&out,&bits,sizeof(out));
    return std::isfinite(out);
}
inline bool halfValue(std::string_view s, size_t offset, uint16_t& out) {
    if (offset > s.size() || s.size()-offset < 2) return false;
    out = uint16_t(uint8_t(s[offset])) |
          uint16_t(uint16_t(uint8_t(s[offset+1])) << 8);
    // Disallow INF and NaN radiance (both are invalid for finite GPU color).
    return (out & 0x7c00u) != 0x7c00u;
}
inline uint16_t signedHalf(float value) {
    const uint16_t sign = std::signbit(value) ? 0x8000u : 0u;
    return uint16_t(sign | hdr_detail::toHalf(std::fabs(value)));
}
struct Channel {
    std::string_view name;
    int type=0, bytes=0;
    int slot=-1; // R=0, G=1, B=2, A=3, other=-1
};
} // namespace exr_detail

inline bool parseOpenExr(std::string_view bytes, RadianceImage& out,
                         std::string& error, size_t maxDecodedBytes=size_t(512)<<20) {
    out = {};
    error.clear();
    auto fail = [&](const char* why) {
        out = {};
        error = why;
        return false;
    };
    uint32_t magic=0, version=0;
    if (!exr_detail::u32(bytes,0,magic) || magic != 20000630u ||
        !exr_detail::u32(bytes,4,version) || (version&255u) != 2u ||
        (version & ~255u) != 0u)
        return fail("EXR: unsupported signature, version, tiled or multipart image");

    size_t at=8;
    bool haveChannels=false,haveCompression=false,haveWindow=false;
    uint8_t compression=255;
    int64_t xMin=0,yMin=0,xMax=-1,yMax=-1;
    std::vector<exr_detail::Channel> channels;
    std::array<bool,4> haveComponent{};
    for (int attributes=0;attributes<256;++attributes) {
        std::string_view name;
        if (!exr_detail::cstring(bytes,at,255,name))
            return fail("EXR: malformed header attribute name");
        if (name.empty()) break;
        std::string_view type;
        if (!exr_detail::cstring(bytes,at,255,type))
            return fail("EXR: malformed header attribute type");
        uint32_t attrSize=0;
        if (!exr_detail::u32(bytes,at,attrSize))
            return fail("EXR: missing attribute length");
        at+=4;
        if (attrSize > (1u<<20) || at > bytes.size() ||
            size_t(attrSize) > bytes.size()-at)
            return fail("EXR: attribute exceeds file or header budget");
        const std::string_view value=bytes.substr(at,attrSize);
        at+=size_t(attrSize);
        if (name=="compression") {
            if (haveCompression || type!="compression" || value.size()!=1)
                return fail("EXR: invalid compression attribute");
            haveCompression=true;
            compression=uint8_t(value[0]);
        } else if (name=="dataWindow") {
            if (haveWindow || type!="box2i" || value.size()!=16)
                return fail("EXR: invalid dataWindow");
            uint32_t v[4]{};
            for (int i=0;i<4;++i)
                if (!exr_detail::u32(value,size_t(i)*4,v[i]))
                    return fail("EXR: malformed dataWindow");
            xMin=int32_t(v[0]); yMin=int32_t(v[1]);
            xMax=int32_t(v[2]); yMax=int32_t(v[3]);
            haveWindow=true;
        } else if (name=="channels") {
            if (haveChannels || type!="chlist")
                return fail("EXR: malformed channel list");
            haveChannels=true;
            size_t pos=0;
            bool terminated=false;
            while (pos<value.size()) {
                std::string_view cname;
                if (!exr_detail::cstring(value,pos,255,cname))
                    return fail("EXR: bad channel name");
                if (cname.empty()) {terminated=true;break;}
                uint32_t pixelType=0,xs=0,ys=0;
                if (pos>value.size() || value.size()-pos<16 ||
                    !exr_detail::u32(value,pos,pixelType) ||
                    !exr_detail::u32(value,pos+8,xs) ||
                    !exr_detail::u32(value,pos+12,ys))
                    return fail("EXR: truncated channel descriptor");
                pos+=16; // pixel type, pLinear + 3 reserved bytes, x/y sampling
                if (pixelType>2 || xs!=1 || ys!=1 || channels.size()>=64)
                    return fail("EXR: unsupported pixel type or subsampled channel");
                int slot=-1;
                if (cname=="R") slot=0; else if (cname=="G") slot=1;
                else if (cname=="B") slot=2; else if (cname=="A") slot=3;
                if (slot>=0) {
                    if (haveComponent[size_t(slot)] || pixelType==0)
                        return fail("EXR: duplicate or integer color channel");
                    haveComponent[size_t(slot)]=true;
                }
                channels.push_back({cname,int(pixelType),pixelType==1?2:4,slot});
            }
            if (!terminated || pos!=value.size())
                return fail("EXR: unterminated channel list");
        }
    }
    if (!haveChannels || !haveCompression || !haveWindow ||
        !haveComponent[0] || !haveComponent[1] || !haveComponent[2])
        return fail("EXR: required RGB/format attributes are missing");
    if (compression!=0)
        return fail("EXR: only uncompressed scanline storage is supported");
    const int64_t width=xMax-xMin+1, height=yMax-yMin+1;
    if (width<1 || height<1 || width>16384 || height>16384 ||
        uint64_t(width)*uint64_t(height)*8u > std::min<uint64_t>(uint64_t(512)<<20,maxDecodedBytes))
        return fail("EXR: image extent or decoded size exceeds limits");

    const size_t count=size_t(height);
    if (at>bytes.size() || count>(bytes.size()-at)/8u)
        return fail("EXR: truncated scanline offset table");
    std::vector<uint64_t> offsets(count);
    for (size_t i=0;i<count;++i)
        if (!exr_detail::u64(bytes,at+i*8,offsets[i]))
            return fail("EXR: truncated scanline pointer");
    uint64_t rowBytes=0;
    for (const auto& c:channels) rowBytes+=uint64_t(width)*uint64_t(c.bytes);
    if (rowBytes>uint64_t(std::numeric_limits<int32_t>::max()))
        return fail("EXR: scanline byte count too large");

    out.width=int(width);out.height=int(height);
    out.rgba16f.resize(size_t(width)*size_t(height)*4u);
    for (size_t i=3;i<out.rgba16f.size();i+=4)
        out.rgba16f[i]=0x3c00u;
    std::vector<bool> seen(count,false);
    for (size_t chunk=0;chunk<count;++chunk) {
        const uint64_t offset=offsets[chunk];
        if (offset>uint64_t(bytes.size()) ||
            uint64_t(bytes.size())-offset<8)
            return fail("EXR: scanline chunk offset outside file");
        uint32_t rawY=0,packed=0;
        if (!exr_detail::u32(bytes,size_t(offset),rawY) ||
            !exr_detail::u32(bytes,size_t(offset)+4,packed) ||
            uint64_t(packed)!=rowBytes ||
            rowBytes>uint64_t(bytes.size())-offset-8)
            return fail("EXR: invalid scanline data size");
        const int64_t row=int64_t(int32_t(rawY))-yMin;
        if (row<0 || row>=height || seen[size_t(row)])
            return fail("EXR: out-of-range or duplicate scanline");
        seen[size_t(row)]=true;
        size_t source=size_t(offset)+8;
        for (const auto& c:channels) {
            if (c.slot<0) {
                source+=size_t(width)*size_t(c.bytes);
                continue;
            }
            for (int64_t x=0;x<width;++x) {
                uint16_t h=0;
                if (c.type==1) {
                    if (!exr_detail::halfValue(bytes,source,h))
                        return fail("EXR: nonfinite or truncated half channel");
                    source+=2;
                } else {
                    float f=0;
                    if (!exr_detail::floatValue(bytes,source,f))
                        return fail("EXR: nonfinite or truncated float channel");
                    h=exr_detail::signedHalf(f);
                    source+=4;
                }
                out.rgba16f[(size_t(row)*size_t(width)+size_t(x))*4u+
                            size_t(c.slot)] = h;
            }
        }
    }
    return true;
}

// All decoded texels are linear. No implicit exposure, chromaticity
// conversion or tone mapping; caller owns the GPU texture.
inline gfx::TextureHandle loadOpenExr(gfx::Device& device,
                                      std::string_view encoded,
                                      std::string& error) {
    RadianceImage image;
    if (!parseOpenExr(encoded,image,error)) return {};
    return uploadRadianceHdr(device,image,error);
}

} // namespace aiverse::render
