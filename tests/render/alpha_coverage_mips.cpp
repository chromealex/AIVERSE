#include "engine/render/AlphaCoverageMips.h"
#include <iostream>
#include <stdexcept>

using namespace aiverse::render;
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {try {
    std::vector<uint8_t> source(32*32*4);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
        const size_t at=(size_t(y)*32+x)*4;
        const unsigned count=unsigned((x/4)*13+(y/4)*7)%8;
        const bool opaque=unsigned((y%4)*4+x%4)<count;
        source[at]=opaque?20:255;source[at+1]=opaque?150:0;
        source[at+2]=opaque?40:255;source[at+3]=opaque?255:0;
    }
    const auto original=source;
    std::vector<AlphaCoverageMip> levels;
    check(buildAlphaCoverageMips(32,32,source,153,levels),"sparse leaf source rejected");
    check(source==original&&levels.size()==5,"source modified or incomplete chain");
    const double target=alphaTestCoverage(source,153);
    check(target>.15&&target<.3,"fixture is not sparse cutout coverage");
    check(std::abs(alphaTestCoverage(levels[1].rgba,153)-target)<.065,"minified leaf coverage lost");
    check(alphaTestCoverage(levels.back().rgba,153)==0,"one subpixel leaf became a solid rectangle");
    for(const auto& level:levels)for(size_t i=0;i<level.rgba.size();i+=4)if(level.rgba[i+3]) {
        check(std::abs(int(level.rgba[i])-20)<=1&&std::abs(int(level.rgba[i+1])-150)<=1&&
              std::abs(int(level.rgba[i+2])-40)<=1,"transparent magenta contaminated leaf color");
    }
    bool padded=false;
    const auto& detailed=levels.front();
    for(int y=0;y<detailed.height;++y)for(int x=0;x<detailed.width;++x) {
        const size_t at=(size_t(y)*detailed.width+x)*4;
        if(detailed.rgba[at+3]||detailed.rgba[at+1]!=150)continue;
        padded=true;
    }
    check(padded,"transparent mip edges have no color bleed for bilinear filtering");
    const std::vector<uint8_t> odd={255,0,255,0,255,0,255,0,20,150,40,255};
    check(buildAlphaCoverageMips(3,1,odd,153,levels)&&levels.size()==1,"odd edge source rejected");
    check(levels[0].rgba[1]==150&&levels[0].rgba[3]==85,"last odd-column leaf was dropped");
    check(!buildAlphaCoverageMips(0,1,odd,153,levels)&&levels.empty(),"invalid extent accepted");
    check(!buildAlphaCoverageMips(4097,1,odd,153,levels),"unbounded extent accepted");
    check(!buildAlphaCoverageMips(3,1,odd,0,levels),"zero cutoff accepted");
    check(!buildAlphaCoverageMips(3,2,odd,153,levels),"truncated source accepted");
    check(buildAlphaCoverageMips(1,1,std::span<const uint8_t>(odd).last(4),153,levels)&&levels.empty(),"single pixel rejected");
    std::cout<<"PASS alpha coverage, premultiplied linear color, odd edges, bounds and unchanged source\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
