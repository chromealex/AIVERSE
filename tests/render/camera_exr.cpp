// LENS EXR writer: half conversion and the scanline file layout. CPU only.
// Usage: aiverse_camera_exr_tests [out.exr] writes a sample file for an
// independent reader check.
#include "engine/render/CameraExr.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
using namespace aiverse::render;
static void check(bool value,const char* why){
    if(!value)throw std::runtime_error(why);
}
struct Reader {
    const std::vector<uint8_t>& b;size_t p=0;
    uint32_t u32(){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(b.at(p++))<<(8*i);return v;}
    uint64_t u64(){uint64_t v=0;for(int i=0;i<8;++i)v|=uint64_t(b.at(p++))<<(8*i);return v;}
    std::string str(){std::string s;while(b.at(p))s+=char(b[p++]);++p;return s;}
};
int main(int argc,char** argv) {
    // Exact reference values.
    check(cameraFloatToHalf(1.f)==0x3c00 && cameraFloatToHalf(-2.f)==0xc000,"1, -2");
    check(cameraFloatToHalf(.5f)==0x3800 && cameraFloatToHalf(0.f)==0,"0.5, 0");
    check(cameraFloatToHalf(-0.f)==0x8000,"negative zero");
    check(cameraFloatToHalf(65504.f)==0x7bff,"max half");
    check(cameraFloatToHalf(65520.f)==0x7c00 && cameraFloatToHalf(1e9f)==0x7c00,"overflow to Inf");
    check(cameraFloatToHalf(INFINITY)==0x7c00 && cameraFloatToHalf(-INFINITY)==0xfc00,"Inf");
    check(std::isnan(cameraHalfToFloat(cameraFloatToHalf(NAN))),"NaN kept");
    check(cameraFloatToHalf(std::ldexp(1.f,-24))==0x0001,"smallest subnormal");
    check(cameraFloatToHalf(std::ldexp(1.f,-26))==0,"underflow to zero");
    check(cameraFloatToHalf(1.f/3.f)==0x3555,"1/3 rounds to nearest");
    check(cameraFloatToHalf(1.f+std::ldexp(1.f,-11))==0x3c00,"tie to even (down)");
    check(cameraFloatToHalf(1.f+3*std::ldexp(1.f,-11))==0x3c02,"tie to even (up)");
    // Every finite half round-trips through float exactly.
    for(uint32_t h=0;h<65536;++h) {
        const float f=cameraHalfToFloat(uint16_t(h));
        if(std::isnan(f))continue;
        check(cameraFloatToHalf(f)==h,"half -> float -> half is exact");
    }
    // A 3x2 image: header attributes and planar A,B,G,R scanlines.
    const int w=3,h=2;
    std::vector<float> rgba(size_t(w*h*4));
    for(size_t i=0;i<rgba.size();++i)rgba[i]=float(i)*0.25f-1.f;
    rgba[0]=12.5f; // HDR value above 1
    std::vector<uint8_t> file;
    check(cameraEncodeExrFloat(w,h,rgba,file),"encode");
    Reader r{file};
    check(r.u32()==20000630u && r.u32()==2u,"magic + version");
    std::map<std::string,std::pair<std::string,std::vector<uint8_t>>> attrs;
    for(;;) {
        std::string name=r.str();
        if(name.empty())break;
        std::string type=r.str();const uint32_t size=r.u32();
        std::vector<uint8_t> v(file.begin()+long(r.p),file.begin()+long(r.p+size));r.p+=size;
        attrs[name]={type,v};
    }
    for(const char* required:{"channels","compression","dataWindow","displayWindow",
                              "lineOrder","pixelAspectRatio","screenWindowCenter",
                              "screenWindowWidth"})
        check(attrs.count(required)==1,"required header attribute");
    check(attrs["channels"].first=="chlist" && attrs["compression"].second==std::vector<uint8_t>{0},
          "channel list, no compression");
    {
        Reader box{attrs["dataWindow"].second};
        check(box.u32()==0 && box.u32()==0 && box.u32()==uint32_t(w-1) && box.u32()==uint32_t(h-1),
              "data window");
        Reader ch{attrs["channels"].second};
        for(const char* name:{"A","B","G","R"}) {
            check(ch.str()==name && ch.u32()==1u,"channel sorted, HALF");
            ch.p+=4;check(ch.u32()==1u && ch.u32()==1u,"sampling 1");
        }
        check(attrs["channels"].second.at(ch.p)==0,"channel list terminator");
    }
    std::vector<uint64_t> offsets;
    for(int y=0;y<h;++y)offsets.push_back(r.u64());
    for(int y=0;y<h;++y) {
        Reader line{file};line.p=size_t(offsets[size_t(y)]);
        check(line.u32()==uint32_t(y) && line.u32()==uint32_t(w*4*2),"scanline header");
        const int plane[4]={3,2,1,0};
        for(int c:plane)
            for(int x=0;x<w;++x) {
                const uint16_t got=uint16_t(file.at(line.p)|(file.at(line.p+1)<<8));line.p+=2;
                const float want=rgba[size_t((y*w+x)*4+c)];
                check(cameraHalfToFloat(got)==cameraHalfToFloat(cameraFloatToHalf(want)),"pixel value");
            }
        if(y+1==h)check(line.p==file.size(),"file ends after the last scanline");
    }
    // RGBA16F readback bytes path and argument validation.
    std::vector<uint8_t> bytes(size_t(w*h*8));
    for(size_t i=0;i<size_t(w*h*4);++i) {
        const uint16_t half=cameraFloatToHalf(rgba[i]);
        bytes[2*i]=uint8_t(half);bytes[2*i+1]=uint8_t(half>>8);
    }
    std::vector<uint8_t> unused;
    check(!cameraEncodeExrHalf(0,1,{},unused) && !cameraEncodeExrFloat(2,2,rgba,unused),
          "size mismatch rejected");
    check(!cameraWriteExrRgba16F("unused.exr",w,h,std::span<const uint8_t>(bytes.data(),bytes.size()-1)),
          "short readback rejected");
    if(argc>1) {
        // 16x8 gradient with an HDR highlight, for an external reader.
        const int W=16,H=8;std::vector<float> img(size_t(W*H*4));
        for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
            float* p=&img[size_t((y*W+x)*4)];
            p[0]=float(x)/W*4.f;p[1]=float(y)/H;p[2]=0.25f;p[3]=1.f;
        }
        std::vector<uint8_t> sample;
        check(cameraEncodeExrFloat(W,H,img,sample),"sample encode");
        std::FILE* f=std::fopen(argv[1],"wb");
        check(f&&std::fwrite(sample.data(),1,sample.size(),f)==sample.size(),"sample write");
        std::fclose(f);
    }
    std::puts("PASS camera_exr");
}
