#include "engine/render/Ktx2Transcode.h"
#include "engine/render/Ktx2Libktx.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace aiverse::render;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void put32(std::string& s,size_t pos,uint32_t v){
    for(int k=0;k<4;++k) s[pos+size_t(k)]=char(v>>(8*k));
}
void put64(std::string& s,size_t pos,uint64_t v){
    for(int k=0;k<8;++k) s[pos+size_t(k)]=char(v>>(8*k));
}
std::string ktx(bool basis=true,int layers=2,int faces=6,int mipCount=2){
    const size_t end=80u+size_t(mipCount)*24u;
    std::string s(end+size_t(mipCount)*16u,0);
    const char magic[12] = {char(0xab),'K','T','X',' ','2','0',char(0xbb),13,10,26,10};
    std::copy(magic,magic+12,s.begin());
    put32(s,12,basis?0u:37u); // unknown native format => Basis
    put32(s,16,1); // type size
    put32(s,20,4); put32(s,24,4); // width, height
    put32(s,28,0); put32(s,32,uint32_t(layers));
    put32(s,36,uint32_t(faces)); put32(s,40,uint32_t(mipCount));
    put32(s,44,basis?1u:0u);
    for(int mip=0;mip<mipCount;++mip){
        const size_t p=80u+size_t(mip)*24u;
        put64(s,p,end+size_t(mip)*16u);
        put64(s,p+8,16);
        put64(s,p+16,16);
    }
    return s;
}
Ktx2Transcoder good(){
    return [](std::string_view,CompressedImage& out,std::string&)->bool{
        out={};
        out.width=4;out.height=4;out.depth=1;
        out.arrayLayers=2;out.faces=6;
        out.format=BlockFormat::None;out.vkFormat=37;
        out.levels.resize(2);
        out.levels[0].assign(12u*4u*4u*4u,13);
        out.levels[1].assign(12u*2u*2u*4u,25);
        return true;
    };
}
void acceptanceAndMetadata(){
    CompressedImage out;std::string err;
    const std::string basis=ktx();
    require(!parseKtx2WithTranscoder(basis,{},out,err) &&
            !err.empty() && out.levels.empty(),"missing Basis codec rejected");
    require(parseKtx2WithTranscoder(basis,good(),out,err) &&
            out.faces==6 && out.arrayLayers==2 &&
            out.levels.size()==2 && out.levels[0].size()==768 &&
            out.levels[1].size()==192,"full cube-array mip payload preserved");
    require(out.levels[1].front()==25,"mip content from callback retained");
    require(!parseKtx2WithTranscoder(basis,
        [](std::string_view,CompressedImage&,std::string& e){
            e="decoder failed";return false;
        },out,err) && err=="decoder failed" && out.levels.empty(),
        "codec failure clears partial output");
    require(!parseKtx2WithTranscoder(basis,
        [](std::string_view,CompressedImage&,std::string&)->bool {
            throw std::runtime_error("untrusted codec error");
        },out,err) && !err.empty() && out.levels.empty(),
        "codec exceptions fail closed");
}
void corruption(){
    const std::string basis=ktx();
    CompressedImage out;std::string err;
    std::string bad=basis;
    put64(bad,80,40); // payload points into the KTX2 header
    require(!parseKtx2WithTranscoder(bad,good(),out,err),
            "payload overlapping header/index rejected");
    bad=basis; put32(bad,44,3);
    require(!parseKtx2WithTranscoder(bad,good(),out,err),
            "unsupported compression rejected");
    bad=basis; put32(bad,36,5);
    require(!parseKtx2WithTranscoder(bad,good(),out,err),
            "invalid KTX2 face topology rejected");
    bad=basis;bad.resize(140);
    require(!parseKtx2WithTranscoder(bad,good(),out,err),
            "truncated level payload rejected");
    require(!parseKtx2WithTranscoder(basis,
        [](std::string_view s,CompressedImage& out,std::string& e) {
            bool ok=good()(s,out,e);
            out.arrayLayers=1; return ok;
        },out,err) && out.levels.empty(),
        "callback cannot discard source array layer");
    require(!parseKtx2WithTranscoder(basis,
        [](std::string_view s,CompressedImage& out,std::string& e) {
            bool ok=good()(s,out,e);
            out.levels[1].pop_back(); return ok;
        },out,err) && out.levels.empty(),
        "truncated mip slice rejected");
    require(!parseKtx2WithTranscoder(basis,
        [](std::string_view s,CompressedImage& out,std::string& e) {
            bool ok=good()(s,out,e);
            out.levels[1].push_back(0); return ok;
        },out,err) && out.levels.empty(),
        "unexpected extra byte in mip rejected");
    require(!parseKtx2WithTranscoder(basis,
        [](std::string_view s,CompressedImage& out,std::string& e) {
            bool ok=good()(s,out,e);
            out.format=static_cast<BlockFormat>(255); return ok;
        },out,err) && out.levels.empty(),
        "unknown output format enum rejected");
}
void optionalCodecConfig() {
    // Even without installed libktx, API is link-safe, explicit and
    // never silently treats Basis bytes as uncompressed RGBA.
    Ktx2LibktxOptions options;
    require(options.preferNativeCompression && options.allowRgba8Fallback,
            "libktx default GPU target and fallback policy");
    using HdrImportFn = aiverse::gfx::TextureHandle (*)(
        aiverse::gfx::Device&,std::string_view,std::string&);
    // Typed initialization checks the ABI; a volatile pointer retains a link
    // reference without comparing a function's necessarily non-null address.
    [[maybe_unused]] HdrImportFn volatile hdrImport=&uploadKtx2UastcHdrWithLibktx;
#if defined(AIVERSE_TEXTURE_WITH_LIBKTX)
    // Real BasisLZ/UASTC bytes and device tests require linked libktx
    // and a production asset fixture in a dedicated CI gate.
    require(sizeof(ktxTexture2) > 0, "real libktx public header available");
    // Check APIs needed for the real LDR and HDR import paths are exported by
    // the installed development package; actual compressed sample validation
    // belongs to the separate codec and GPU asset CI suites.
    [[maybe_unused]] auto* volatile transcode=&ktxTexture2_TranscodeBasis;
    [[maybe_unused]] auto* volatile imageOffset=&ktxTexture2_GetImageOffset;
    [[maybe_unused]] auto* volatile isHdr=&ktxTexture2_IsHDR;
#endif
}
void nativePassthrough(){
    std::string raw=ktx(false,1,1,1);
    raw.resize(104+64,0);
    put64(raw,80,104);put64(raw,88,64);put64(raw,96,64);
    CompressedImage out;std::string err;
    bool invoked=false;
    auto unwanted=[&](std::string_view,CompressedImage&,std::string&){
        invoked=true;return false;
    };
    require(parseKtx2WithTranscoder(raw,unwanted,out,err) &&
            !invoked && out.arrayLayers==1 &&
            out.faces==1 && out.levels.size()==1 &&
            out.levels[0].size()==64,
            "ordinary KTX2 bypasses Basis callback");
}
}
int main(){
    try{acceptanceAndMetadata();corruption();optionalCodecConfig();nativePassthrough();}
    catch(const std::exception& e) {
        std::fprintf(stderr,"KTX2 transcoder contract FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("KTX2 transcoder contract: OK");return 0;
}
