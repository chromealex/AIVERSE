// Bounded CPU-readable DDS/KTX2 snapshots for the texture DAG.
#pragma once
#include "TextureGraph.h"
#include "engine/render/DdsTexture.h"
#include "engine/render/Ktx2Transcode.h"

namespace aiverse::render::texture_graph {
inline uint32_t containerU32(std::string_view s,size_t at){
    require(at<=s.size()&&s.size()-at>=4,"truncated texture container header");
    return uint32_t(uint8_t(s[at]))|(uint32_t(uint8_t(s[at+1]))<<8)|(uint32_t(uint8_t(s[at+2]))<<16)|(uint32_t(uint8_t(s[at+3]))<<24);
}
inline size_t preflightContainerSource(const std::string& encoded,size_t budget){
    require(encoded.size()<=16u*1024u*1024u,"encoded texture container exceeds budget");
    uint32_t w=0,h=0,levels=0;
    if(isKtx2(encoded)){
        ktx2_bridge_detail::Source src;std::string error;
        if(!ktx2_bridge_detail::inspect(encoded,src,error))throw std::invalid_argument(error);
        require(!src.oneD&&!src.volume&&src.layers==1&&src.faces==1&&!src.basis,"texture graph requires non-Basis 2D KTX2");
        w=uint32_t(src.width);h=uint32_t(src.height);levels=uint32_t(src.mips);
        // All supported blocks occupy <=4 bytes/texel except small edge
        // blocks. Bound each expanded payload independently before parsing.
        size_t expanded=0;
        for(uint32_t i=0;i<levels;++i){
            const size_t at=96u+size_t(i)*24u;
            const uint64_t bytes=uint64_t(containerU32(encoded,at))|(uint64_t(containerU32(encoded,at+4))<<32);
            require(bytes<=maxImageSourceBytes-expanded,"KTX2 expanded container exceeds scratch budget");
            expanded+=size_t(bytes);
        }
    }else{
        require(encoded.size()>=128&&encoded.compare(0,4,"DDS ")==0,"invalid DDS header");
        const auto flags=containerU32(encoded,8),caps2=containerU32(encoded,112);
        w=containerU32(encoded,16);h=containerU32(encoded,12);
        levels=(flags&0x20000u)?containerU32(encoded,28):1;
        require(!(caps2&0x200200u)&&!((flags&0x800000u)&&containerU32(encoded,24)>1),"texture graph requires 2D DDS");
        if((containerU32(encoded,80)&4u)&&encoded.compare(84,4,"DX10")==0){
            require(encoded.size()>=148&&containerU32(encoded,132)==3&&!(containerU32(encoded,136)&4u)&&containerU32(encoded,140)==1,"texture graph requires single 2D DDS");
        }
    }
    require(w>0&&h>0&&w<=4096&&h<=4096&&levels>=1&&levels<=13,"texture container dimensions or mip count exceed graph limits");
    size_t texels=0;
    for(uint32_t i=0;i<levels;++i){
        const size_t count=size_t(w)*h;
        require(count<=budget/4-texels,"decoded container mip chain exceeds graph budget");
        texels+=count;
        require(w>1||h>1||i+1==levels,"container mip count exceeds extent");
        w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    return texels;
}
inline ImageSource decodeContainerSource(const std::string& encoded,size_t budget){
    const size_t texels=preflightContainerSource(encoded,budget);
    CompressedImage image;std::string error;
    if(!(isKtx2(encoded)?parseKtx2(encoded,image,error):parseDdsTexture(encoded,image,error)))throw std::invalid_argument(error);
    require(image.faces==1&&image.arrayLayers==1&&!image.oneDimensional&&!image.threeDimensional&&image.depth==1,"invalid container source topology");
    if(isAstc(image.format)||image.format==BlockFormat::BC6H_UFloat||image.format==BlockFormat::BC6H_SFloat)
        throw std::invalid_argument(std::string("texture graph has no CPU decoder for ")+blockFormatName(image.format));
    ImageSource result;result.width=image.width;result.height=image.height;result.hasAlpha=true;result.authoredMips=true;
    result.rgb.reserve(texels);
    for(size_t i=0;i<image.levels.size();++i){
        const int w=levelWidth(image,int(i)),h=levelHeight(image,int(i));
        std::vector<uint8_t> rgba;
        if(!decodeBlocks(image,int(i),rgba)||rgba.size()!=size_t(w)*h*4)throw std::invalid_argument("container mip decode failed");
        require(rgba.size()/4<=texels-result.rgb.size(),"container decoded size mismatch");
        result.mips.push_back({w,h,result.rgb.size()});
        for(size_t p=0;p<rgba.size();p+=4)result.rgb.push_back((uint32_t(rgba[p+3])<<24)|(uint32_t(rgba[p])<<16)|(uint32_t(rgba[p+1])<<8)|rgba[p+2]);
    }
    require(result.rgb.size()==texels,"container mip chain size mismatch");
    return result;
}
} // namespace aiverse::render::texture_graph
