// Synthetic Radiance RGBE fixtures and native RHI contract tests.
// Test SOURCE ONLY: this file does not claim a test execution result.
#include "engine/render/HdrTexture.h"
#include "engine/render/EnvironmentIbl.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace {
void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
std::string hdr(const std::string& sizeLine, const std::string& pixels) {
    return std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n") +
           sizeLine + "\n" + pixels;
}
void pixel(std::string& data, uint8_t r, uint8_t g, uint8_t b, uint8_t e) {
    data.push_back(char(r)); data.push_back(char(g));
    data.push_back(char(b)); data.push_back(char(e));
}
// RLE channel runs use two bytes (length code and repeated value), not RGBE pixels.
void rleRun(std::string& data, uint8_t code, uint8_t value) {
    data.push_back(char(code));
    data.push_back(char(value));
}
struct MockGpu final : gfx::Device {
    gfx::DeviceInfo info_;
    std::string error_ = "mock texture allocation rejected";
    bool supported = true, reject = false;
    int creations = 0, destructions = 0;
    gfx::TextureDesc last;
    std::vector<uint16_t> uploaded;
    const gfx::DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    bool supports(gfx::PixelFormat format) const override {
        return supported && format == gfx::PixelFormat::RGBA16F;
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override { return {}; }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(gfx::BufferHandle) override {}
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d) override {
        if (reject || gfx::validateTextureDesc(d) != gfx::TextureDescError::None)
            return {};
        last = d;
        const auto* half = static_cast<const uint16_t*>(d.data);
        if (half && d.width > 0 && d.height > 0)
            uploaded.assign(half, half + size_t(d.width)*size_t(d.height)*4u);
        return {uint32_t(++creations)};
    }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override { ++destructions; }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {}; }
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override { return {}; }
    void destroyPipeline(gfx::PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t) override {}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override {}
};
void flatAndUpload() {
    std::string pixels;
    pixel(pixels,128,0,0,129); // red = 1
    pixel(pixels,255,0,0,130); // red = 255/64 > 1
    const auto data = hdr("-Y 1 +X 2",pixels);
    render::RadianceImage image; std::string error;
    require(render::parseRadianceHdr(data,image,error), "flat RGBE decode");
    require(error.empty() && image.width == 2 && image.height == 1 &&
            image.rgba16f.size() == 8u, "decoded RGBA16F extent");
    require(image.rgba16f[0] == 0x3c00u && image.rgba16f[3] == 0x3c00u &&
            image.rgba16f[4] > 0x3c00u, "linear HDR value above one retained");
    MockGpu gpu;
    auto texture = render::loadRadianceHdr(gpu,data,error);
    require(texture.valid() && gpu.creations == 1 &&
            gpu.last.format == gfx::PixelFormat::RGBA16F &&
            gpu.uploaded == image.rgba16f,
            "one-call upload copies all authored HDR texels");
    gpu.destroyTexture(texture);
    require(gpu.destructions == 1, "caller owns returned GPU texture");
    gpu.supported = false;
    require(!render::uploadRadianceHdr(gpu,image,error).valid() &&
            !error.empty() && gpu.creations == 1,
            "unsupported GPU format does not allocate");
    gpu.supported = true; gpu.reject = true;
    require(!render::loadRadianceHdr(gpu,data,error).valid() &&
            !error.empty(), "allocation failure surfaced");
}
void rleAndOrientation() {
    std::string pixels;
    pixel(pixels,2,2,0,8); // Radiance modern RLE scanline header
    rleRun(pixels,136,128); // 8 red values
    rleRun(pixels,136,0);   // 8 green values
    rleRun(pixels,136,0);   // 8 blue values
    rleRun(pixels,136,129); // exponent, red=1
    render::RadianceImage image; std::string error;
    require(render::parseRadianceHdr(hdr("-Y 1 +X 8",pixels),image,error) &&
            image.rgba16f.size() == 8u*4u,
            "modern RLE scanline decoded");
    for (int x=0;x<8;++x)
        require(image.rgba16f[size_t(x)*4] == 0x3c00u,
                "all eight RLE run texels decoded");
    pixels.clear();
    pixel(pixels,128,0,0,129);
    pixel(pixels,0,128,0,129);
    require(render::parseRadianceHdr(hdr("+Y 1 -X 2",pixels),image,error),
            "orientation parse");
    require(image.rgba16f[0] == 0 && image.rgba16f[1] == 0x3c00u &&
            image.rgba16f[4] == 0x3c00u, "horizontal orientation flipped");
}
void environmentConvolution(){
    using namespace render::environment_ibl;
    require(radicalInverse(0)==0.f&&radicalInverse(1)==.5f&&radicalInverse(2)==.25f,"Hammersley radical inverse");
    const auto glossy=ggxDirection(1.f/3.f,.5f,.25f),rough=ggxDirection(1.f,.5f,.25f);
    require(glossy.direction.z>.9f&&std::abs(rough.direction.z)<.00001f&&rough.weight<.00001f,"GGX roughness distribution");
    require(ggxDirection(1.f,.75f,.25f).weight==0.f,"GGX below horizon rejection");
    // At roughness=1, reflected GGX directions have uniform solid-angle
    // density. NdotL-weighted mean of cos(theta) tends to 2/3.
    float moment=0.f,weight=0.f;
    for(int i=0;i<specularSamples;++i){
        const auto q=ggxDirection(1.f,(i+.5f)/specularSamples,radicalInverse(uint32_t(i)));
        moment+=q.direction.z*q.weight;weight+=q.weight;
    }
    require(weight>0.f&&std::abs(moment/weight-2.f/3.f)<.002f,"GGX normalized hemisphere moment");
    std::string pixels;pixel(pixels,128,64,32,130);pixel(pixels,128,64,32,130);
    render::RadianceImage image;std::string error;
    const auto encoded=hdr("-Y 1 +X 2",pixels);
    require(!render::parseRadianceHdr(encoded,image,error,8),"decode budget must reject before allocation");
    require(render::parseRadianceHdr(encoded,image,error,16),"decode exact byte budget");
    const auto atlas=convolve(image);
    require(atlas.width==bandWidth&&atlas.height==bands*bandHeight,"IBL atlas geometry");
    for(size_t i=0;i<atlas.rgba16f.size();i+=4)
        require(atlas.rgba16f[i]==image.rgba16f[0]&&atlas.rgba16f[i+1]==image.rgba16f[1]&&atlas.rgba16f[i+2]==image.rgba16f[2]&&atlas.rgba16f[i+3]==0x3c00u,"constant HDR must preserve radiance in all bands and gutters");
    render::RadianceImage nonuniform;nonuniform.width=8;nonuniform.height=4;nonuniform.rgba16f.resize(8*4*4);
    for(int y=0;y<4;++y)for(int x=0;x<8;++x){
        const size_t i=size_t(y*8+x)*4;nonuniform.rgba16f[i]=x<4?0x4400u:0;nonuniform.rgba16f[i+3]=0x3c00u;
    }
    const auto mirror=integrateBrdf(.5f,0.f);
    require(std::abs(mirror[0]-.96875f)<.00001f&&std::abs(mirror[1]-.03125f)<.00001f,"mirror Schlick split sum");
    const auto roughNormal=integrateBrdf(1.f,1.f);
    require(std::abs(roughNormal[0]+roughNormal[1]-(1.f-std::log(2.f)))<.002f,"GGX Smith normal-incidence hemispherical integral");
    const auto combined=convolveWithBrdf(image);
    require(combined.width==bandWidth&&combined.height==combinedHeight&&combined.rgba16f.size()==size_t(bandWidth)*combinedHeight*4,"combined BRDF atlas geometry");
    require(std::equal(atlas.rgba16f.begin(),atlas.rgba16f.end(),combined.rgba16f.begin()),"BRDF append must preserve radiance data");
    const auto& lut=brdfLut();
    for(size_t i=0;i<lut.size();i+=4)require(halfValue(lut[i])<=1.f&&halfValue(lut[i+1])<=1.f&&lut[i+2]==0&&lut[i+3]==0x3c00u,"finite BRDF LUT channels");
    for(int y=1;y<=brdfSize;++y){
        const size_t i=size_t(y)*bandWidth*4;
        require(lut[i]==lut[i+4]&&lut[i+size_t(brdfSize+1)*4]==lut[i+size_t(brdfSize)*4],"BRDF horizontal clamp gutter");
    }
    const auto filtered=convolve(nonuniform);
    const auto at=[&](int band,int x,int y){return filtered.rgba16f[(size_t(band*bandHeight+y)*bandWidth+x)*4];};
    require(at(0,16,32)>at(0,112,32),"directional radiance preserved");
    require(at(0,0,32)==at(0,width,32)&&at(0,width+1,32)==at(0,1,32),"longitude gutters wrap");
    require(at(3,16,32)<at(0,16,32)&&at(3,112,32)>at(0,112,32),"roughness convolution changes directional radiance");
}
void errorsAndBoundary() {
    using render::hdr_detail::toHalf;
    require(toHalf(1.0f) == 0x3c00u && toHalf(0.0f) == 0 &&
            toHalf(65504.0f) == 0x7bffu, "half normal/zero/saturation");
    require(toHalf(std::ldexp(1.0f,-15)) == 0x0200u &&
            toHalf(std::ldexp(1.0f,-14) - std::ldexp(1.0f,-25)) == 0x0400u,
            "half subnormal/normal rounding boundary");
    render::RadianceImage image; std::string error;
    require(!render::parseRadianceHdr("not hdr",image,error) &&
            !error.empty() && image.rgba16f.empty(), "bad signature rejected");
    std::string pixels;
    pixel(pixels,2,2,0,8);
    rleRun(pixels,0,0); // zero-sized RLE code is malformed
    require(!render::parseRadianceHdr(hdr("-Y 1 +X 8",pixels),image,error) &&
            !error.empty() && image.rgba16f.empty(), "invalid RLE run rejected");
    require(!render::parseRadianceHdr(hdr("-Y 1 +X 2",std::string(7,'a')),
                                      image,error), "truncated flat image rejected");
    require(!render::parseRadianceHdr(hdr("-Y 20000 +X 2",std::string()),
                                      image,error), "oversized extent rejected");
}
}
int main() {
    try {
        flatAndUpload();
        rleAndOrientation();
        errorsAndBoundary();
        environmentConvolution();
    } catch (const std::exception& e) {
        std::fprintf(stderr,"HDR texture contract FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Radiance HDR decode/upload contract: OK");
    return 0;
}
