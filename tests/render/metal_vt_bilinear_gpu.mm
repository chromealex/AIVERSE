// Native Metal virtual-texture bilinear + physical-gutter reference test.
// Samples the exact production gfx::kVirtualTextureMSL from 2D array resources.
// Independent CPU oracle samples the immutable *logical texels*, not the
// shader's atlas coordinates. Old center-of-first-interior mapping must fail.
// Test intentionally separate from Shader Graph/Renderer3D's proven ICB tests.
#include "engine/gfx/Gfx.h"
#include "engine/gfx/VirtualTextureFileStreamer.h"
#include "engine/gfx/VirtualTextureSampling.h"
#include "engine/platform/Platform.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string& out) {
    out.clear();
    return false;
}
}

using namespace aiverse::gfx;
namespace {
constexpr int kPageSize=8, kGutter=1, kInterior=kPageSize-2*kGutter;
constexpr int kTiles=2, kLogicalWidth=kInterior*kTiles, kLogicalHeight=kInterior;
constexpr int kTargetWidth=4,kTargetHeight=4;
using Color=std::array<uint8_t,4>;
int failures=0;
void require(bool ok,const char* message) {
    if(!ok) {
        ++failures;
        std::fprintf(stderr,"FAIL: %s\n",message);
    }
}
float clamp01(float x) {
    return std::clamp(x,0.0f,1.0f);
}
int addressIndex(int i,int count,int mode) {
    if(mode==1) {
        const int v=i%count;
        return v<0?v+count:v;
    }
    if(mode==2) {
        const int period=count*2;
        const int v=(i%period+period)%period;
        return v>=count?period-1-v:v;
    }
    return std::clamp(i,0,count-1);
}
float addressUv(float value,int mode) {
    if(mode==1) {
        return value-std::floor(value);
    }
    if(mode==2) {
        const float repeat=value-std::floor(value/2.0f)*2.0f;
        return repeat<=1.0f?repeat:2.0f-repeat;
    }
    return clamp01(value);
}
// These are the original *logical* source texels. They are calculated
// without consulting the renderer, virtual page table or physical pages.
Color logicalTexel(int x,int y,int mode) {
    x=addressIndex(x,kLogicalWidth,mode);
    y=addressIndex(y,kLogicalHeight,0);
    const int bias=mode==1?0:22;
    return Color{
        uint8_t(28+bias+11*x+2*y),
        uint8_t(45+5*x+15*y+bias/2),
        uint8_t(218-9*x-3*y-bias/2),
        255
    };
}
// An authored physical page has 1-pixel gutters sourced from neighboring
// *logical* texels, including across the Repeat/MirrorRepeat outer edge.
// Pixel x=0 is gutter for the first interior pixel x=1.
std::array<uint8_t,kPageSize*kPageSize*4> physicalPage(int tile,int mode) {
    std::array<uint8_t,kPageSize*kPageSize*4> bytes{};
    for(int y=0;y<kPageSize;++y)
        for(int x=0;x<kPageSize;++x) {
            const Color texel=logicalTexel(
                tile*kInterior+x-kGutter,y-kGutter,mode);
            const size_t p=size_t(y*kPageSize+x)*4;
            std::copy(texel.begin(),texel.end(),bytes.begin()+std::ptrdiff_t(p));
        }
    return bytes;
}
// Bilinear reference is evaluated in full logical-texture pixel space,
// with 1/2-texel offsets and independent integer addressing. It does
// NOT reproduce shader page lookup, atlas tiling or inset math.
Color cpuReference(float u,float v,int mode) {
    const float logicalU=addressUv(u,mode)*float(kLogicalWidth)-0.5f;
    const float logicalV=clamp01(v)*float(kLogicalHeight)-0.5f;
    const int x0=int(std::floor(logicalU)),y0=int(std::floor(logicalV));
    const float fx=logicalU-float(x0),fy=logicalV-float(y0);
    const Color a=logicalTexel(x0,y0,mode),b=logicalTexel(x0+1,y0,mode);
    const Color c=logicalTexel(x0,y0+1,mode),d=logicalTexel(x0+1,y0+1,mode);
    Color result{};
    for(size_t i=0;i<4;++i) {
        const float ab=float(a[i])*(1-fx)+float(b[i])*fx;
        const float cd=float(c[i])*(1-fx)+float(d[i])*fx;
        result[i]=uint8_t(std::clamp(std::lround(ab*(1-fy)+cd*fy),0l,255l));
    }
    return result;
}
struct FixtureFiles {
    std::filesystem::path root;
    bool ready=false;
    FixtureFiles() {
        std::error_code ec;
        const auto parent=std::filesystem::temp_directory_path(ec);
        if(ec)return;
        root=parent/("aiverse-metal-vt-bilinear-"+
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ready=std::filesystem::create_directories(root,ec) && !ec;
    }
    ~FixtureFiles() {
        if(ready) {
            std::error_code ec;
            std::filesystem::remove_all(root,ec);
        }
    }
    bool write(int mode,int tile) const {
        if(!ready)return false;
        const VirtualPageKey key{0,tile,0,mode-1};
        const auto path=root/(mode==1?"repeat":"mirror")/
                        VirtualTextureDiskQueue::relativePath(key);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(),ec);
        if(ec)return false;
        const auto pixels=physicalPage(tile,mode);
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        if(!out)return false;
        out.write(reinterpret_cast<const char*>(pixels.data()),
                  std::streamsize(pixels.size()));
        out.close();
        return bool(out);
    }
};
bool awaitPage(VirtualTextureFileStreamer& stream,VirtualPageKey key) {
    const auto ticket=stream.requestPage(key,true);
    if(!ticket.valid()) {
        std::fprintf(stderr,"FAIL: VT disk request rejected: %s\n",
                     stream.lastError().c_str());
        return false;
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(7);
    bool callback=false,ok=false;
    while(!callback && std::chrono::steady_clock::now()<deadline) {
        stream.pump(2,[&](VirtualDiskPageTicket current,VirtualPageKey actual,
                          bool success,const std::string& error) {
            if(current!=ticket || !(actual==key) || !success || !error.empty()) {
                std::fprintf(stderr,"FAIL: disk-to-GPU VT bilinear page callback: %s\n",
                             error.c_str());
                ++failures;
            }
            callback=true;
            ok=current==ticket && actual==key && success && error.empty();
        });
        if(!callback)std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    if(!callback || !ok || !stream.ready() || !stream.exactResidentPage(key))
        std::fprintf(stderr,"FAIL: VT page was not published within deadline\n");
    return callback && ok && stream.ready() && stream.exactResidentPage(key);
}
bool readPixels(Device& dev,TextureHandle texture,std::vector<uint8_t>& output) {
    bool done=false;
    TextureReadbackResult read;
    dev.beginFrame(16,16);
    const bool accepted=dev.readTextureAsync(texture,{},
        [&](TextureReadbackResult value) {
            read=std::move(value);done=true;
        });
    dev.endFrame();
    if(!accepted) {
        std::fprintf(stderr,"FAIL: bilinear render target readback rejected: %s\n",
                     dev.lastError().c_str());
        return false;
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(7);
    while(!done && std::chrono::steady_clock::now()<deadline) {
        dev.beginFrame(16,16);
        dev.endFrame();
        if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(!done || !dev.gpuHealthy() || !read.success() ||
       read.format!=PixelFormat::RGBA8 ||
       read.width!=kTargetWidth || read.height!=kTargetHeight ||
       read.pixels.size()!=size_t(kTargetWidth*kTargetHeight*4)) {
        std::fprintf(stderr,"FAIL: Metal bilinear GPU RGBA8 readback failed\n");
        return false;
    }
    output=std::move(read.pixels);
    return true;
}
struct Probe {
    Device& device;
    ShaderHandle shader;
    PipelineHandle pipeline;
    TextureHandle color,depth;
    explicit Probe(Device& d):device(d){}
    bool initialize() {
        TextureDesc rt;
        rt.width=kTargetWidth;rt.height=kTargetHeight;
        rt.renderTarget=true;rt.format=PixelFormat::RGBA8;rt.mipLevels=1;
        color=device.createTexture(rt);
        rt.format=PixelFormat::Depth;
        depth=device.createTexture(rt);
        std::string msl=R"VTB(
#include <metal_stdlib>
using namespace metal;
struct BilinearOut { float4 position [[position]]; };
vertex BilinearOut vs_main(uint index [[vertex_id]]) {
    float2 points[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
    BilinearOut result;
    result.position=float4(points[index],0,1);
    return result;
}
)VTB";
        // The same literal MSL helper emitted for production Shader Graph VT.
        msl+=kVirtualTextureMSL;
        msl+=R"VTB(
fragment float4 fs_main(BilinearOut in [[stage_in]],
    texture2d_array<float,access::sample> atlas [[texture(0)]],
    texture2d_array<float,access::read> table [[texture(1)]],
    sampler atlasSampler [[sampler(0)]],
    constant float4& uvLayerGutter [[buffer(1)]],
    constant float4& wrapModes [[buffer(2)]],
    constant float4& derivatives [[buffer(3)]]) {
    if (wrapModes.z == 1.0f)
        return aiverse_sample_virtual_trilinear(atlas,table,atlasSampler,
            uvLayerGutter.xy,uint(uvLayerGutter.z),uvLayerGutter.w,
            float4(0.02f,0.03f,0.04f,1.0f),derivatives.xy,derivatives.zw,
            wrapModes.y,2u,uint(wrapModes.x),0u);
    if (wrapModes.z == 2.0f)
        return aiverse_sample_virtual_anisotropic(atlas,table,atlasSampler,
            uvLayerGutter.xy,uint(uvLayerGutter.z),uvLayerGutter.w,
            float4(0.02f,0.03f,0.04f,1.0f),derivatives.xy,derivatives.zw,
            float2(12.0f,6.0f),wrapModes.y,2u,uint(wrapModes.w),
            uint(wrapModes.x),0u);
    return aiverse_sample_virtual(atlas,table,atlasSampler,
        uvLayerGutter.xy,uint(uvLayerGutter.z),uvLayerGutter.w,
        float4(0.02f,0.03f,0.04f,1.0f),
        uint(wrapModes.x),uint(wrapModes.y));
}
)VTB";
        ShaderDesc sd;
        sd.msl=msl.c_str();
        sd.label="MetalCanonicalVirtualTextureBilinear";
        sd.textureSlots=2;
        sd.uniformSlots=3;
        shader=device.createShader(sd);
        if(shader.valid()) {
            PipelineDesc pd;
            pd.shader=shader;
            pd.attrCount=0;
            pd.stride=0;
            pd.indexType=IndexType::None;
            pd.cull=CullMode::None;
            pd.depthTest=false;pd.depthWrite=false;
            pd.colorFormat=PixelFormat::RGBA8;
            pd.colorAttachmentCount=1;
            pd.label="MetalCanonicalVirtualTextureBilinear";
            pipeline=device.createPipeline(pd);
        }
        if(!color.valid() || !depth.valid() || !shader.valid() || !pipeline.valid()) {
            std::fprintf(stderr,"FAIL: native bilinear shader/pipeline allocation: %s\n",
                         device.lastError().c_str());
            return false;
        }
        return true;
    }
    bool sample(TextureHandle atlas,TextureHandle table,
                int layer,int wrap,float u,float v,Color& found,
                float lod=0,int mode=0,int taps=1,
                std::array<float,4> derivatives={}) {
        device.beginFrame(16,16);
        RenderTargetPassDesc pass;
        pass.colorCount=1;
        pass.colors[0].texture=color;pass.colors[0].clear=true;
        pass.depth.texture=depth;pass.depth.clear=true;
        if(!device.beginRenderTargets(pass)) {
            std::fprintf(stderr,"FAIL: native bilinear pass begin: %s\n",
                         device.lastError().c_str());
            device.endFrame();
            return false;
        }
        device.setViewport({0,0,kTargetWidth,kTargetHeight});
        device.setPipeline(pipeline);
        device.setTexture(0,atlas);
        device.setTexture(1,table);
        const std::array<float,4> coord{u,v,float(layer),float(kGutter)};
        const std::array<float,4> modes{float(wrap),lod,float(mode),float(taps)};
        device.setUniforms(0,coord.data(),sizeof(coord));
        device.setUniforms(1,modes.data(),sizeof(modes));
        device.setUniforms(2,derivatives.data(),sizeof(derivatives));
        device.draw(3);
        device.endPass();
        device.endFrame();
        std::vector<uint8_t> output;
        if(!readPixels(device,color,output))return false;
        found={output[0],output[1],output[2],output[3]};
        for(size_t index=0;index<output.size();index+=4)
            for(size_t c=0;c<4;++c)
                if(std::abs(int(output[index+c])-int(found[c]))>1) {
                    std::fprintf(stderr,"FAIL: non-uniform output across probe quad\n");
                    return false;
                }
        return true;
    }
    void shutdown() {
        if(pipeline.valid())device.destroyPipeline(pipeline);
        if(shader.valid())device.destroyShader(shader);
        if(color.valid())device.destroyTexture(color);
        if(depth.valid())device.destroyTexture(depth);
        pipeline={};shader={};color={};depth={};
    }
    ~Probe(){shutdown();}
};
// Deliberately authored colors give closed-form expectations, independent of
// shader lookup/LOD code. Physical slice order differs from logical mip order.
void indexedOracle(Device& device,Probe& probe) {
    const std::array<Color,4> colors{{{0,0,240,255},{0,200,0,255},
                                    {240,0,0,255},{200,160,40,255}}};
    TextureDesc desc;
    desc.width=8;desc.height=8;desc.arrayLayers=4;desc.mipLevels=1;
    desc.dimension=TextureDimension::Texture2DArray;
    auto atlas=device.createTexture(desc);
    desc.width=2;desc.height=1;desc.filter=Filter::Nearest;
    auto table=device.createTexture(desc);
    require(atlas.valid() && table.valid(),"indexed oracle array allocation");
    if(!atlas.valid() || !table.valid()) {
        if(atlas.valid())device.destroyTexture(atlas);
        if(table.valid())device.destroyTexture(table);
        return;
    }
    for(int slice=0;slice<4;++slice) {
        std::array<Color,64> pixels;
        pixels.fill(colors[size_t(slice)]);
        TextureUploadDesc upload;
        upload.arrayLayer=slice;upload.width=8;upload.height=8;
        upload.rowPitchBytes=32;upload.slicePitchBytes=256;upload.data=pixels.data();
        require(device.updateTextureRegion(atlas,upload),"indexed oracle physical page upload");
    }
    // Logical layer 0: red/blue at mip 0, green at mip 1.
    // Logical layer 1: yellow at mip 0, one missing mip-1 entry.
    std::array<std::array<Color,2>,4> entries{{
        {{{2,0,0,255},{0,0,0,255}}},
        {{{1,0,1,255},{1,0,1,255}}},
        {{{3,0,0,255},{3,0,0,255}}},
        {{{0,0,0,0},{1,0,1,255}}}
    }};
    for(int slice=0;slice<4;++slice) {
        TextureUploadDesc upload;
        upload.arrayLayer=slice;upload.width=2;upload.height=1;
        upload.rowPitchBytes=8;upload.slicePitchBytes=8;upload.data=entries[size_t(slice)].data();
        require(device.updateTextureRegion(table,upload),"indexed oracle table upload");
    }
    unsigned samples=0;
    auto check=[&](const char* name,Color expected,int layer,int wrap,float u,
                   float lod,int mode=1,int taps=1,std::array<float,4> gradients={}) {
        Color actual{};
        if(!probe.sample(atlas,table,layer,wrap,u,.5f,actual,lod,mode,taps,gradients)) {
            require(false,name);return;
        }
        ++samples;
        for(size_t c=0;c<4;++c)if(std::abs(int(actual[c])-int(expected[c]))>2) {
            ++failures;
            std::fprintf(stderr,"FAIL: indexed %s channel=%zu expected=%u got=%u\n",
                name,c,unsigned(expected[c]),unsigned(actual[c]));
        }
    };
    check("fine left",colors[2],0,0,.25f,0);
    check("fine right",colors[0],0,0,.75f,0);
    check("quarter mip blend",{180,50,0,255},0,0,.25f,.25f);
    check("half mip blend",{0,100,120,255},0,0,.75f,.5f);
    check("upper LOD clamp",colors[1],0,0,.25f,20);
    check("negative LOD clamp",colors[2],0,0,.25f,-3);
    check("logical layer stride",colors[3],1,0,.25f,0);
    check("missing entry fallback",{5,8,10,255},1,0,.25f,1);
    check("missing entry blend",{103,84,25,255},1,0,.25f,.5f);
    check("invalid logical layer",{5,8,10,255},2,0,.25f,0);
    check("repeat addressing",colors[2],0,1,1.25f,0);
    check("mirror addressing",colors[0],0,2,1.25f,0);
    // A two-texel footprint with one tap chooses mip 1. With two or
    // more taps it straddles the red/blue page boundary at mip 0.
    check("one tap coarse",colors[1],0,0,.5f,0,2,1,{2.f/12,0,0,1.f/6});
    for(int taps:{2,4,8})
        check("cross-page anisotropic",{120,0,120,255},0,0,.5f,0,2,taps,
              {float(taps)/12,0,0,1.f/6});
    check("anisotropic bias",colors[1],0,0,.5f,1,2,2,{2.f/12,0,0,1.f/6});
    check("Y-major derivative",{120,0,120,255},0,0,.5f,0,2,2,{0,1.f/6,2.f/12,0});
    require(samples==18,"all indexed mip/anisotropic native pixel probes completed");
    std::printf("VT indexed independent color oracle: samples=%u\n",samples);
    device.destroyTexture(table);device.destroyTexture(atlas);
}
// Hand-authored physical mip colors isolate hardware derivative selection
// from virtual LOD and from the CPU mip generator tested by mip_pipeline.
void physicalMipOracle(Device& device,Probe& probe) {
    TextureDesc desc;
    desc.width=8;desc.height=8;desc.arrayLayers=1;desc.mipLevels=4;
    desc.dimension=TextureDimension::Texture2DArray;
    auto atlas=device.createTexture(desc);
    desc.width=2;desc.height=1;desc.arrayLayers=2;desc.mipLevels=1;
    desc.filter=Filter::Nearest;
    auto table=device.createTexture(desc);
    require(atlas.valid() && table.valid(),"physical mip oracle textures allocated");
    if(!atlas.valid() || !table.valid()) {
        if(atlas.valid())device.destroyTexture(atlas);
        if(table.valid())device.destroyTexture(table);
        return;
    }
    const std::array<Color,4> palette{{{240,0,0,255},{0,200,0,255},
                                      {0,0,160,255},{80,80,80,255}}};
    for(int mip=0;mip<4;++mip) {
        const int width=8>>mip;
        std::vector<Color> pixels(size_t(width*width),palette[size_t(mip)]);
        TextureUploadDesc upload;
        upload.mipLevel=mip;upload.width=upload.height=width;
        upload.rowPitchBytes=width*4;upload.slicePitchBytes=width*width*4;
        upload.data=pixels.data();
        require(device.updateTextureRegion(atlas,upload),"authored physical mip upload");
    }
    std::array<Color,2> entries{{{0,0,0,255},{0,0,0,255}}};
    for(int layer=0;layer<2;++layer) {
        TextureUploadDesc upload;
        upload.width=2;upload.height=1;upload.arrayLayer=layer;
        upload.rowPitchBytes=upload.slicePitchBytes=8;upload.data=entries.data();
        require(device.updateTextureRegion(table,upload),"physical mip table upload");
    }
    unsigned samples=0;
    for(float lod:{0.f,.5f,1.f,1.5f,2.f,3.f}) {
        const int low=int(lod),high=std::min(3,low+1);
        Color actual{};
        // 12 logical pixels / physical page mapping: rho = derivative * 12.
        require(probe.sample(atlas,table,0,0,.25f,.4f,actual,0,1,1,
                {std::exp2(lod)/12,0,0,0}),"physical mip GPU readback");
        for(size_t c=0;c<4;++c) {
            const int expected=int(std::lround(palette[size_t(low)][c]*(1-(lod-low))+
                                               palette[size_t(high)][c]*(lod-low)));
            require(std::abs(int(actual[c])-expected)<=2,"physical mip derivative color blend");
        }
        ++samples;
    }
    std::printf("VT physical mip oracle: samples=%u\n",samples);
    device.destroyTexture(table);device.destroyTexture(atlas);
}
// Nonuniform logical images are authored independently at both virtual mips.
// The mip-1 tint makes missing detail visibly different from resident detail.
Color mipTexel(int x,int y,int mip,int wrap) {
    if(!mip)return logicalTexel(x,y,wrap);
    x=addressIndex(x,6,wrap);y=addressIndex(y,3,0);
    return {uint8_t(170-13*x+3*y),uint8_t(25+17*x+9*y),
            uint8_t(45+5*x+19*y),255};
}
std::array<float,4> mipReference(float u,float v,int mip,int wrap) {
    const float x=addressUv(u,wrap)*float(12>>mip)-.5f;
    const float y=clamp01(v)*float(6>>mip)-.5f;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const float fx=x-ix,fy=y-iy;
    std::array<float,4> result{};
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
        const auto pixel=mipTexel(ix+dx,iy+dy,mip,wrap);
        const float weight=(dx?fx:1-fx)*(dy?fy:1-fy);
        for(size_t c=0;c<4;++c)result[c]+=pixel[c]*weight;
    }
    return result;
}
void streamedMipOracle(Device& device,Probe& probe) {
    unsigned samples=0,transitions=0;
    int maxDelta=0;
    for(int wrap:{0,1,2}) {
        TextureStreamConfig budget;
        budget.gpuBudgetBytes=4096;budget.stagingBudgetBytes=2048;
        budget.uploadBytesPerFrame=256;budget.uploadsPerFrame=1;
        VirtualTextureStreamer stream(device,budget);
        VirtualTextureStreamConfig config;
        config.tilesX=2;config.tilesY=1;config.mipLevels=2;
        config.physicalPages=2;config.physicalPageSize=8;
        config.physicalMipLevels=4;config.indexedMipTable=true;
        require(stream.initialize(config),"indexed nonuniform stream initialized");
        if(!stream.ready())return;
        auto stage=[&](int mip,int tile,bool pin) {
            std::array<Color,64> pixels;
            for(int y=0;y<8;++y)for(int x=0;x<8;++x)
                pixels[size_t(y*8+x)]=mipTexel(tile*6+x-1,y-1,mip,wrap);
            auto ticket=stream.stage({mip,tile,0,0},pixels.data(),sizeof(pixels),pin);
            require(ticket.valid(),"nonuniform physical mip chain staged");
            return ticket;
        };
        auto drain=[&](int mip,int tile) {
            for(int frame=0;frame<8 && !stream.resolveExact({mip,tile,0,0}).valid();++frame)
                stream.drain();
            require(stream.resolveExact({mip,tile,0,0}).valid(),"all physical mips atomically published");
        };
        stage(1,0,true);drain(1,0);
        int resident=-1;
        auto reference=[&](float u,float v,float lod) {
            const int tile=std::min(1,int(addressUv(u,wrap)*2));
            auto fine=mipReference(u,v,tile==resident?0:1,wrap);
            const auto coarse=mipReference(u,v,1,wrap);
            for(size_t c=0;c<4;++c)fine[c]=fine[c]*(1-lod)+coarse[c]*lod;
            return fine;
        };
        auto sample=[&](float u,float lod,int taps) {
            std::array<float,4> expected{};
            if(taps) {
                // Unit-minor-axis footprints: chosen counts 2/4/8 exactly
                // cover this many logical texels. Their virtual LOD is zero.
                for(int i=0;i<taps;++i) {
                    const float offset=(float(i)+.5f-float(taps)/2)/12;
                    auto tap=reference(u+offset,.41f,0);
                    for(size_t c=0;c<4;++c)expected[c]+=tap[c]/float(taps);
                }
            } else expected=reference(u,.41f,lod);
            Color actual{};
            require(probe.sample(stream.physicalAtlas(),stream.indexedMipPageTableTexture(),
                0,wrap,u,.41f,actual,lod,taps?2:1,taps?taps:1,
                taps?std::array<float,4>{float(taps)/12,0,0,1.f/6}:std::array<float,4>{}),
                "nonuniform indexed GPU readback");
            ++samples;
            for(size_t c=0;c<4;++c) {
                const int delta=std::abs(int(actual[c])-int(std::lround(expected[c])));
                maxDelta=std::max(maxDelta,delta);
                if(delta>2) {
                    ++failures;
                    std::fprintf(stderr,"FAIL: streamed wrap=%d resident=%d u=%.3f lod=%.2f taps=%d channel=%zu expected=%.2f got=%u\n",
                        wrap,resident,double(u),double(lod),taps,c,double(expected[c]),unsigned(actual[c]));
                }
            }
        };
        auto sweep=[&]() {
            for(float u:{-.02f,.02f,.25f,.48f,.5f,.52f,.75f,.98f,1.02f})
                for(float lod:{0.f,.25f,.5f,.75f,1.f})sample(u,lod,0);
            for(float u:{0.f,.5f,1.f})for(int taps:{2,4,8})sample(u,0,taps);
        };
        sweep();
        // Repeated replacement exercises LRU slice reuse and table refresh.
        for(int iteration=0;iteration<6;++iteration) {
            int tile=iteration%2;
            stage(0,tile,false);
            resident=-1; // old fine page is retired before its slice is overwritten
            stream.drain();
            require(!stream.resolveExact({0,tile,0,0}).valid(),
                    "one uploaded mip cannot publish an incomplete physical chain");
            sample(.25f,0,0);sample(.75f,0,0);
            drain(0,tile);resident=tile;++transitions;
            sweep();
        }
        stream.shutdown();
    }
    require(samples==1170 && transitions==18,"all nonuniform mip and LRU probes executed");
    std::printf("VT nonuniform indexed oracle: samples=%u transitions=%u max_delta=%d\n",
                samples,transitions,maxDelta);
}
int run() {
    id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
    if(!metal) {
        std::fprintf(stderr,"FAIL: Metal device required; no silent skip\n");
        return 2;
    }
    CAMetalLayer* layer=[CAMetalLayer layer];
    layer.device=metal;
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
    layer.frame=CGRectMake(0,0,16,16);
    layer.drawableSize=CGSizeMake(16,16);
    auto device=createMetalDevice((__bridge void*)layer);
    if(!device || !device->supportsTextureDimension(TextureDimension::Texture2DArray)) {
        std::fprintf(stderr,"FAIL: native Metal 2D-array RHI unavailable\n");
        return 2;
    }
    std::printf("Metal canonical VT bilinear: %s, page=%d interior=%d gutter=%d\n",
                device->info().name.c_str(),kPageSize,kInterior,kGutter);
    FixtureFiles files;
    require(files.ready,"trusted on-disk nonuniform physical page fixture created");
    if(!files.ready)return 1;
    for(int wrap=1;wrap<=2;++wrap)
        for(int tile=0;tile<kTiles;++tile)
            require(files.write(wrap,tile),"disk page authored with neighbor-matched gutters");
    if(failures)return 1;

    // Each domain owns a different atlas, page-table and virtual layer.
    std::array<std::unique_ptr<VirtualTextureFileStreamer>,2> streams;
    for(int wrap=1;wrap<=2;++wrap) {
        TextureStreamConfig budget;
        budget.gpuBudgetBytes=8192;
        budget.stagingBudgetBytes=1024;
        budget.uploadBytesPerFrame=512;
        budget.uploadsPerFrame=2;
        auto stream=std::make_unique<VirtualTextureFileStreamer>(*device,budget);
        VirtualTextureStreamConfig atlasConfig;
        atlasConfig.tilesX=kTiles;atlasConfig.tilesY=1;
        atlasConfig.mipLevels=1;atlasConfig.virtualLayers=2;
        atlasConfig.physicalPages=2;atlasConfig.physicalPageSize=kPageSize;
        atlasConfig.physicalFormat=PixelFormat::RGBA8;
        atlasConfig.pageTableBytesPerFlush=64;
        VirtualDiskPageConfig disk;
        disk.assetRoot=files.root/(wrap==1?"repeat":"mirror");
        disk.tilesX=kTiles;disk.tilesY=1;
        disk.mipLevels=1;disk.virtualLayers=2;
        disk.pageSize=kPageSize;disk.maxOutstanding=8;
        disk.maxReadyBytes=2048;
        require(stream->initialize(atlasConfig,disk,2),
                "physical Metal atlas/table and disk worker initialized");
        if(failures)return 1;
        const int layerIndex=wrap-1;
        for(int tile=0;tile<kTiles;++tile) {
            const VirtualPageKey key{0,tile,0,layerIndex};
            require(awaitPage(*stream,key),"immutable page published from disk to GPU");
            const Color table=stream->encodedPageTable(tile,0,layerIndex);
            require(table==Color{uint8_t(tile),0,0,255},
                    "GPU page table encodes exact physical slice and fine mip");
            if(failures)return 1;
        }
        streams[size_t(layerIndex)]=std::move(stream);
    }
    require(streams[0]->physicalAtlas()!=streams[1]->physicalAtlas() &&
            streams[0]->pageTableTexture()!=streams[1]->pageTableTexture(),
            "independent Repeat/MirrorRepeat GPU texture identities");
    if(failures)return 1;

    Probe probe(*device);
    require(probe.initialize(),"canonical MSL bilinear GPU fixture pipeline");
    if(failures)return 1;

    // The old 0.5-texel inset would return the *first* interior texel
    // instead of averaging both tile edge texels at u=0.5. This assertion
    // makes the fixture fail if the source pattern is accidentally flat.
    const auto actualMidpoint=cpuReference(.5f,.41f,1);
    const auto firstRight=logicalTexel(kInterior,2,1);
    int sensitivity=0;
    for(int i=0;i<3;++i)
        sensitivity=std::max(sensitivity,
            std::abs(int(actualMidpoint[size_t(i)])-int(firstRight[size_t(i)])));
    require(sensitivity>=4,"independent oracle detects legacy centre-inset seam error");

    constexpr std::array<float,17> uPoints{{
        -1.02f,-0.52f,-0.02f,0.0f,0.02f,
        0.25f,0.48f,0.50f,0.52f,0.75f,
        0.98f,1.0f,1.02f,1.48f,1.5f,1.52f,2.02f
    }};
    constexpr std::array<float,4> vPoints{{0.0f,0.25f,0.41f,1.0f}};
    unsigned samples=0;
    unsigned seamSamples=0;
    int largestDelta=0;
    for(int wrap=1;wrap<=2;++wrap) {
        const auto& stream=*streams[size_t(wrap-1)];
        for(float u:uPoints)
            for(float v:vPoints) {
                const Color expected=cpuReference(u,v,wrap);
                Color found{};
                if(!probe.sample(stream.physicalAtlas(),stream.pageTableTexture(),
                                 wrap-1,wrap,u,v,found)) {
                    require(false,"Metal canonical bilinear RGBA8 sample/readback");
                    continue;
                }
                ++samples;
                const bool seam=std::abs(addressUv(u,wrap)-.5f)<.03f ||
                                addressUv(u,wrap)<.03f ||
                                addressUv(u,wrap)>.97f;
                if(seam)++seamSamples;
                for(size_t channel=0;channel<4;++channel) {
                    const int delta=std::abs(int(expected[channel])-int(found[channel]));
                    largestDelta=std::max(largestDelta,delta);
                    if(delta>2) {
                        ++failures;
                        std::fprintf(stderr,
                          "FAIL: bilinear oracle wrap=%d u=%+.5f v=%.5f channel=%zu "
                          "expected=%u got=%u delta=%d\n",
                          wrap,double(u),double(v),channel,
                          unsigned(expected[channel]),unsigned(found[channel]),delta);
                        if(failures>20)break;
                    }
                }
                if(failures>20)break;
            }
    }
    require(samples==uPoints.size()*vPoints.size()*2,
            "all Repeat/MirrorRepeat native bilinear GPU probes completed");
    require(seamSamples>=20,"multiple page and wrap boundary probes really sampled");
    indexedOracle(*device,probe);
    streamedMipOracle(*device,probe);
    physicalMipOracle(*device,probe);
    require(device->gpuHealthy(),"Metal command buffers completed without GPU errors");
    std::printf("VT bilinear independent CPU oracle: samples=%u seam_samples=%u max_delta=%d\n",
                samples,seamSamples,largestDelta);
    if(!failures)std::puts(
        "PASS: native Metal canonical VT bilinear nonuniform gutter seam and Repeat/MirrorRepeat CPU pixel oracle");
    probe.shutdown();
    for(auto& stream:streams)
        if(stream)stream->shutdown();
    return failures?1:0;
}
}

int main() {
    @autoreleasepool {
        return run();
    }
}
