#include "engine/render/DdsTexture.h"
#include "engine/render/TextureBcEncode.h"
#include "engine/render/TextureAsset.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::render;
namespace {
void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
void u32(std::string& s, size_t p, uint32_t v) {
    for (int i = 0; i < 4; ++i) s[p+size_t(i)] = char((v >> (8*i)) & 255);
}
uint32_t cc(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 |
           uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24;
}
std::string makeDx10(uint32_t dxgi, int w, int h, int depth,
                     int mips, int layers, bool cube, uint32_t dimension) {
    std::string s(148, '\0');
    s.replace(0, 4, "DDS ");
    u32(s,4,124); u32(s,8,0x20000u | (depth > 1 ? 0x800000u : 0u));
    u32(s,12,uint32_t(h)); u32(s,16,uint32_t(w));
    u32(s,24,uint32_t(depth)); u32(s,28,uint32_t(mips));
    u32(s,76,32); u32(s,80,4); u32(s,84,cc("DX10"));
    u32(s,112,cube ? 0x200u : 0u);
    u32(s,128,dxgi); u32(s,132,dimension);
    u32(s,136,cube ? 4u : 0u);
    u32(s,140,uint32_t(layers));
    return s;
}
void cubeArray() {
    std::string s = makeDx10(71,4,4,0,2,2,true,3); // 2 cubemaps / 12 BC1 faces
    for (int face = 0; face < 12; ++face) {
        s.append(8,char(face + 1));    // mip0 of each DDS face
        s.append(8,char(face + 101));  // mip1 of each DDS face
    }
    CompressedImage img; std::string error;
    check(parseDdsTexture(s,img,error), "DX10 BC1 cube array parse");
    check(img.width == 4 && img.arrayLayers == 2 && img.faces == 6 &&
          img.format == BlockFormat::BC1 && img.levels.size() == 2,
          "DX10 cube metadata");
    check(img.levels[0].size() == 12*8 && img.levels[1].size() == 12*8,
          "mip-major full face bytes");
    for (int face = 0; face < 12; ++face) {
        check(img.levels[0][size_t(face)*8] == uint8_t(face+1),
              "mip0 reorders DDS surface-major");
        check(img.levels[1][size_t(face)*8] == uint8_t(face+101),
              "mip1 reorders DDS surface-major");
    }
    check(!parseDdsTexture(s.substr(0,s.size()-1),img,error),
          "truncated cube-array tail fails");
}
void rgbaArray() {
    std::string s=makeDx10(28,2,2,0,1,3,false,3); // RGBA8 3-layer array
    for (int layer=0;layer<3;++layer)
        for (int pixel=0;pixel<4;++pixel) {
            s.push_back(char(layer+10));s.push_back(char(20));
            s.push_back(char(30));s.push_back(char(255));
        }
    CompressedImage img;std::string error;std::vector<uint8_t> rgba;
    check(parseDdsTexture(s,img,error), "DX10 RGBA array parse");
    check(img.format==BlockFormat::None && img.arrayLayers==3 &&
          img.levels[0].size()==48, "RGBA array size and format");
    check(decodeBlocksSlice(img,0,2,rgba) && rgba.size()==16 &&
          rgba[0]==12 && rgba[1]==20, "third array slice");
}
void volume() {
    std::string s=makeDx10(28,2,2,2,2,1,false,4); // 2*2*2 then 1*1*1
    s.append(16,char(4));
    s.append(16,char(9));
    s.append(4,char(33));
    CompressedImage img;std::string error;std::vector<uint8_t> rgba;
    check(parseDdsTexture(s,img,error), "3D volume parse");
    check(img.depth==2 && img.arrayLayers==1 && img.levels[0].size()==32 &&
          img.levels[1].size()==4 && levelImageCount(img,1)==1,
          "volume per-level z shrink");
    check(decodeBlocksSlice(img,0,1,rgba) && rgba[0]==9,
          "volume second z slice");
    check(decodeBlocksSlice(img,1,0,rgba) && rgba[0]==33,
          "volume mip one");
    std::string single = makeDx10(28,2,2,1,1,1,false,4);
    single.append(16,char(77));
    check(parseDdsTexture(single,img,error) &&
          img.threeDimensional && img.depth == 1 && img.arrayLayers == 1,
          "DX10 one-slice volume remains a declared 3D texture");
}
void bc5() {
    std::string s = makeDx10(83, 4, 4, 0, 1, 1, false, 3);
    s.append(16, char(0));
    s[148] = char(255); // R endpoint
    s[156] = char(128); // G endpoint
    CompressedImage image;
    std::string error;
    std::vector<uint8_t> rgba;
    check(parseDdsTexture(s, image, error), "DX10 BC5 DDS parse");
    check(image.format == BlockFormat::BC5 && image.vkFormat == 141 &&
          image.levels[0].size() == 16, "BC5 DDS block preservation");
    check(decodeBlocksSlice(image, 0, 0, rgba) && rgba.size() == 64 &&
          rgba[0] == 255 && rgba[1] == 128 && rgba[3] == 255,
          "BC5 DDS decoded normal channels");
}
void partialLegacyCube() {
    std::string s(128,'\0');
    s.replace(0,4,"DDS "); u32(s,4,124);u32(s,12,4);u32(s,16,4);
    u32(s,76,32);u32(s,80,4);u32(s,84,cc("DXT1"));
    u32(s,112,0x200u | 0x400u); // missing five faces
    s.append(8,'\0');
    CompressedImage img;std::string error;
    check(!parseDdsTexture(s,img,error), "reject incomplete legacy cubemap");
}
} // namespace
int main() {
    {
        using namespace aiverse::render;
        using namespace aiverse::render::bc_encode;
        std::vector<uint8_t> pixels(8*8*4);
        for(size_t i=0;i<pixels.size();i+=4){pixels[i]=128;pixels[i+1]=64;pixels[i+2]=255;pixels[i+3]=255;}
        for(auto format:{Format::BC1,Format::BC3,Format::BC4,Format::BC5}){
            const auto encoded=encodeDds(pixels,8,8,format,false,format==Format::BC5,true);
            CompressedImage decoded;std::string error;
            if(!parseDdsTexture(encoded.dds,decoded,error)||decoded.levels.size()!=4||encoded.rgbaBytes!=340||encoded.payload!=size_t(7*bytes(format)))
                throw std::runtime_error("BC export DDS mip round trip failed");
            std::vector<uint8_t> rgba;
            if(!decodeBlocks(decoded,0,rgba)||rgba.size()!=pixels.size()||std::abs(int(rgba[0])-128)>8)
                throw std::runtime_error("BC export decode mismatch");
        }
        pixels[3]=64;
        bool rejected=false;try{(void)encodeDds(pixels,8,8,Format::BC1,false,false,true);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("BC1 silently lost alpha");
    }
    try {
        cubeArray();
        rgbaArray();
        volume();
        bc5();
        partialLegacyCube();
    } catch(const std::exception& e) {
        std::fprintf(stderr,"DDS layered texture test FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("DDS cube arrays, RGBA arrays, volumes and bounds: OK");
    return 0;
}
