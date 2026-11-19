// AIVERSE LENS portable PNG encoder. RGBA8 top-down, PNG deflate stored
// blocks (no external zlib/stb dependency). Compression can be added later.
#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <vector>
namespace aiverse::render {
inline void pngBigEndian(std::vector<uint8_t>& out,uint32_t value) {
    out.push_back(uint8_t(value>>24));out.push_back(uint8_t(value>>16));
    out.push_back(uint8_t(value>>8));out.push_back(uint8_t(value));
}
inline uint32_t pngCrc32(std::span<const uint8_t> bytes) {
    uint32_t crc=~uint32_t(0);
    for(uint8_t c:bytes) {
        crc^=uint32_t(c);
        for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    return ~crc;
}
inline void pngChunk(std::vector<uint8_t>& result,const char* type,
                     std::span<const uint8_t> payload) {
    pngBigEndian(result,uint32_t(payload.size()));
    const size_t offset=result.size();
    for(int i=0;i<4;++i)result.push_back(uint8_t(type[i]));
    result.insert(result.end(),payload.begin(),payload.end());
    pngBigEndian(result,pngCrc32(std::span<const uint8_t>(result.data()+offset,4+payload.size())));
}
inline bool cameraEncodePngRgba8(int width,int height,
                                std::span<const uint8_t> rgba,
                                std::vector<uint8_t>& output) {
    if(width<=0 || height<=0 || width>8192 || height>8192)return false;
    const size_t pixels=size_t(width)*size_t(height);
    if(rgba.size()!=pixels*4)return false;
    std::vector<uint8_t> scan;
    scan.reserve((size_t(width)*4+1)*size_t(height));
    for(int y=0;y<height;++y) {
        scan.push_back(0); // filter: None
        const auto begin=rgba.begin()+std::ptrdiff_t(size_t(y)*size_t(width)*4);
        scan.insert(scan.end(),begin,begin+std::ptrdiff_t(size_t(width)*4));
    }
    // ZLIB stream with uncompressed RFC1951 stored blocks.
    std::vector<uint8_t> zlib{0x78,0x01};
    uint32_t a=1,b=0;
    for(uint8_t byte:scan){a=(a+byte)%65521u;b=(b+a)%65521u;}
    size_t offset=0;
    while(offset<scan.size()) {
        const size_t count=std::min<size_t>(65535,scan.size()-offset);
        const bool last=offset+count==scan.size();
        const auto len=uint16_t(count),inv=uint16_t(~len);
        zlib.push_back(last?1:0);
        zlib.push_back(uint8_t(len));zlib.push_back(uint8_t(len>>8));
        zlib.push_back(uint8_t(inv));zlib.push_back(uint8_t(inv>>8));
        zlib.insert(zlib.end(),scan.begin()+std::ptrdiff_t(offset),
                    scan.begin()+std::ptrdiff_t(offset+count));
        offset+=count;
    }
    pngBigEndian(zlib,(b<<16)|a);
    std::vector<uint8_t> file{137,80,78,71,13,10,26,10};
    std::vector<uint8_t> ihdr;
    pngBigEndian(ihdr,uint32_t(width));pngBigEndian(ihdr,uint32_t(height));
    ihdr.insert(ihdr.end(),{8,6,0,0,0});
    pngChunk(file,"IHDR",ihdr);
    pngChunk(file,"IDAT",zlib);
    pngChunk(file,"IEND",{});
    output=std::move(file);
    return true;
}
inline bool cameraWritePngRgba8(const std::string& path,int width,int height,
                               std::span<const uint8_t> rgba) {
    std::vector<uint8_t> png;
    if(!cameraEncodePngRgba8(width,height,rgba,png))return false;
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    if(!stream)return false;
    stream.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));
    return bool(stream);
}
} // namespace aiverse::render
