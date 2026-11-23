#pragma once
// Small dependency-free PNG RGBA8 encoder for procedural GLB interchange.
// zlib DEFLATE stored blocks (no compression); this intentionally favors
// correctness/portability over compactness. Limit dimensions in caller.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::building {
inline void buildingPngU32(std::vector<uint8_t>& out,uint32_t value){
    for(int shift=24;shift>=0;shift-=8)out.push_back(uint8_t(value>>shift));
}
inline uint32_t buildingPngCrc(const uint8_t* bytes,size_t length){
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<length;++i){
        crc^=bytes[i];
        for(int bit=0;bit<8;++bit)
            crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
inline void buildingPngChunk(std::vector<uint8_t>& output,
                             const char* type,const std::vector<uint8_t>& bytes){
    if(bytes.size()>UINT32_MAX)
        throw std::length_error("PNG chunk exceeds 32-bit size");
    buildingPngU32(output,uint32_t(bytes.size()));
    const size_t start=output.size();
    for(int i=0;i<4;++i)output.push_back(uint8_t(type[i]));
    output.insert(output.end(),bytes.begin(),bytes.end());
    buildingPngU32(output,buildingPngCrc(output.data()+start,
                                        output.size()-start));
}
inline std::vector<uint8_t> encodeBuildingPngRgba(
    const std::vector<uint8_t>& rgba,int width,int height){
    if(width<=0||height<=0||width>2048||height>2048||
       uint64_t(width)*uint64_t(height)*4!=rgba.size())
        throw std::invalid_argument("Unsupported PNG image dimensions or RGBA buffer");
    const size_t rowBytes=size_t(width)*4;
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height)*(rowBytes+1));
    for(int y=0;y<height;++y){
        raw.push_back(0); // PNG filter None
        const size_t offset=size_t(y)*rowBytes;
        raw.insert(raw.end(),rgba.begin()+offset,
                   rgba.begin()+offset+rowBytes);
    }
    std::vector<uint8_t> zlib{0x78,0x01}; // no-compression DEFLATE
    uint32_t a=1,b=0; // Adler32 of uncompressed filtered scanlines
    for(uint8_t byte:raw){
        a=(a+byte)%65521u;
        b=(b+a)%65521u;
    }
    for(size_t offset=0;offset<raw.size();){
        const uint16_t size=uint16_t(
            (raw.size()-offset)>65535u?65535u:(raw.size()-offset));
        const bool last=offset+size==raw.size();
        zlib.push_back(last?0x01:0x00); // final + stored block
        zlib.push_back(uint8_t(size));
        zlib.push_back(uint8_t(size>>8));
        const uint16_t inverse=uint16_t(~size);
        zlib.push_back(uint8_t(inverse));
        zlib.push_back(uint8_t(inverse>>8));
        zlib.insert(zlib.end(),raw.begin()+offset,raw.begin()+offset+size);
        offset+=size;
    }
    buildingPngU32(zlib,(b<<16)|a);
    std::vector<uint8_t> png{137,80,78,71,13,10,26,10};
    std::vector<uint8_t> ihdr;
    buildingPngU32(ihdr,uint32_t(width));
    buildingPngU32(ihdr,uint32_t(height));
    ihdr.insert(ihdr.end(),{8,6,0,0,0}); // RGBA8
    buildingPngChunk(png,"IHDR",ihdr);
    buildingPngChunk(png,"IDAT",zlib);
    buildingPngChunk(png,"IEND",{});
    return png;
}
} // namespace aiverse::building
