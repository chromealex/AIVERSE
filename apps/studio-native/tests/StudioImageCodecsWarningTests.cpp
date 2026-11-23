// Real memory PNG/JPEG roundtrip using Studio's same private STB
// implementation wrapper. Compiled with selected warnings as errors to
// prevent regressions in unused API markings, C aggregate zero init and
// portable bounded formatting. No GPU or project assets required.
#include "apps/studio-native/StudioStbCodecs.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok,const char* msg) {
    if(!ok)throw std::runtime_error(msg);
}
void appendBytes(void* context, void* data, int size) {
    if(size<=0)return;
    auto* out=static_cast<std::string*>(context);
    out->append(static_cast<const char*>(data),size_t(size));
}
void decodeCheck(const std::string& bytes,bool requireExact) {
    require(!bytes.empty(),"encoder wrote no bytes");
    int w=0,h=0,n=0;
    auto* pixels=stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes.data()),int(bytes.size()),
        &w,&h,&n,4);
    require(pixels!=nullptr,"cannot decode encoded image");
    const bool dimensions=w==2 && h==2 && n>=3;
    const bool alpha=!requireExact || (pixels[3]==255 && pixels[7]==128 &&
        pixels[11]==255 && pixels[15]==255);
    const bool rgb=!requireExact || (pixels[0]==255 && pixels[1]==10 && pixels[2]==0);
    stbi_image_free(pixels);
    require(dimensions,"image dimensions or channels changed");
    require(alpha,"PNG alpha channel changed in Studio codec");
    require(rgb,"PNG RGB channels changed in Studio codec");
}
}
int main() {
    try {
        const uint8_t rgba[]={
            255,10,0,255,  3,120,200,128,
            80,150,255,255, 40,25,15,255
        };
        std::string png;
        require(stbi_write_png_to_func(appendBytes,&png,2,2,4,rgba,2*4)!=0,
            "PNG encode failed");
        decodeCheck(png,true);

        // JPEG is lossy: test transport, pixel dimensions and channel count,
        // never require exact RGB values.
        const uint8_t rgb[]={
            255,10,0, 3,120,200, 80,150,255, 40,25,15
        };
        std::string jpeg;
        require(stbi_write_jpg_to_func(appendBytes,&jpeg,2,2,3,rgb,85)!=0,
            "JPEG encode failed");
        decodeCheck(jpeg,false);
        std::cout<<"Studio image codecs PASS\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Studio image codecs FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
