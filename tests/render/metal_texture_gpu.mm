// On-device macOS Metal RHI texture acceptance (opt-in).
// This is NOT a mock: createMetalDevice, updateTextureRegion and
// readTextureAsync run through the same backend as AIVERSE applications.
// Call only on a Mac with a real Metal device. No silent device skip.
#include "engine/gfx/Gfx.h"
#include "engine/render/AlphaCoverageMips.h"
#include "engine/gfx/VirtualTextureFeedbackPass.h"
#include "engine/gfx/VirtualTextureStreamer.h"
#include "engine/gfx/VirtualTextureFileStreamer.h"
#include "engine/gfx/VirtualTextureSampling.h"
#include "engine/platform/Platform.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// MetalShaderAssets probes optional precompiled shader archives through this
// platform function. This texture test creates no shaders or game content.
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string& out) {
    out.clear();
    return false;
}
}

using namespace aiverse::gfx;

namespace {
int failures = 0;
void check(bool ok, const char* why) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); }
}
using Color = std::array<uint8_t, 4>;
using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double,std::milli>(end-start).count();
}
struct ProbeTiming {
    double submitMs = 0;
    double completeMs = 0;
    double compareMs = 0;
    double gpuCommandMs = 0;
    bool gpuCommandValid = false;
    unsigned frames = 0;
};
uint64_t nextGpuTimingTag() {
    static uint64_t index=0;
    return 0x54584d4554414c00ULL + ++index;
}

std::vector<uint8_t> solid(int w, int h, Color color) {
    std::vector<uint8_t> rgba(size_t(w)*size_t(h)*4);
    for (size_t i=0; i<rgba.size(); i+=4)
        std::copy(color.begin(), color.end(), rgba.begin()+std::ptrdiff_t(i));
    return rgba;
}
void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}
struct Read {
    TextureHandle texture;
    TextureReadbackDesc subresource;
    int width = 0, height = 0;
    std::vector<uint8_t> expected;
    std::string label;
    int tolerance = 0; // zero for blits, <=1 LSB for sampled float->UNORM
    TextureReadbackResult output;
    bool done = false;
};
void verifyReads(Device& dev, const std::vector<std::shared_ptr<Read>>& probes,
                 const char* group) {
    // Report wall-clock scheduling separately from the GPU command interval.
    // The latter measures the entire Metal command buffer, not only blit
    // throughput; short transfers cannot be benchmarked with CTest time.
    ProbeTiming timing;
    const auto submitStart=Clock::now();
    dev.beginFrame(16,16);
    bool queued = true;
    for (const auto& p : probes) {
        const bool ok = dev.readTextureAsync(p->texture,p->subresource,
            [p](TextureReadbackResult result) {
                p->output=std::move(result);
                p->done=true;
            });
        if (!ok) {
            queued=false;
            std::fprintf(stderr,"FAIL: %s readback rejected: %s\n",
                         p->label.c_str(),dev.lastError().c_str());
        }
    }
    const uint64_t timingTag=nextGpuTimingTag();
    const bool gpuTimingRequested=dev.timeGpuFrame(timingTag);
    dev.endFrame();
    const auto submitEnd=Clock::now();
    timing.submitMs=elapsedMs(submitStart,submitEnd);
    check(queued, "all native GPU readbacks accepted");
    // Never report a skipped GPU test as passed. A timeout/failure is red.
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    while (Clock::now()<deadline) {
        dev.beginFrame(16,16);
        dev.endFrame();
        ++timing.frames;
        const bool all=std::all_of(probes.begin(),probes.end(),
            [](const auto& p) {return p->done;});
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto completionEnd=Clock::now();
    timing.completeMs=elapsedMs(submitEnd,completionEnd);
    if (gpuTimingRequested) {
        for (const auto& gpu : dev.takeGpuTimings()) {
            if (gpu.tag == timingTag && gpu.valid) {
                timing.gpuCommandMs=gpu.milliseconds;
                timing.gpuCommandValid=true;
            }
        }
    }
    check(dev.gpuHealthy(),"Metal command buffers completed without GPU error");
    const auto compareStart=Clock::now();
    for (const auto& p : probes) {
        if (!p->done) {
            ++failures;
            std::fprintf(stderr,"FAIL: %s GPU readback timed out\n",p->label.c_str());
            continue;
        }
        const bool shape=p->output.success() &&
            p->output.format==PixelFormat::RGBA8 &&
            p->output.width==p->width && p->output.height==p->height &&
            p->output.pixels.size()==p->expected.size();
        if (!shape) {
            ++failures;
            std::fprintf(stderr,"FAIL: %s GPU readback has wrong format/extent/bytes\n",
                         p->label.c_str());
            continue;
        }
        const auto mismatch=std::mismatch(
            p->expected.begin(),p->expected.end(),p->output.pixels.begin(),
            [&](uint8_t want,uint8_t got) {
                return std::abs(int(want)-int(got)) <= p->tolerance;
            });
        if (mismatch.first!=p->expected.end()) {
            ++failures;
            const size_t at=size_t(mismatch.first-p->expected.begin());
            std::fprintf(stderr,
                "FAIL: %s GPU bytes differ at %zu (wanted %u, got %u, tolerance %d)\n",
                p->label.c_str(),at,unsigned(*mismatch.first),
                unsigned(p->output.pixels[at]),p->tolerance);
        } else std::printf("PASS: %s (%dx%d GPU RGBA8 bytes; tolerance %d)\n",
                           p->label.c_str(),p->width,p->height,p->tolerance);
    }
    timing.compareMs=elapsedMs(compareStart,Clock::now());
    std::printf("TIMING %s readbacks=%zu submit_ms=%.3f callback_wait_ms=%.3f "
                "poll_frames=%u compare_ms=%.3f gpu_command_ms=",
                group,probes.size(),timing.submitMs,timing.completeMs,
                timing.frames,timing.compareMs);
    if (timing.gpuCommandValid) std::printf("%.3f\n",timing.gpuCommandMs);
    else std::puts("unavailable");
}
std::shared_ptr<Read> probe(TextureHandle h,int mip,int layer,int w,int height,
                            std::vector<uint8_t> expected,std::string label,
                            int tolerance=0) {
    auto p=std::make_shared<Read>();
    p->texture=h; p->subresource={mip,layer};
    p->width=w; p->height=height; p->expected=std::move(expected);
    p->label=std::move(label);
    p->tolerance=tolerance;
    return p;
}
// Queue successive physical-slice writes and independent GPU readbacks
// before earlier frames retire. Each readback must retain its own snapshot,
// even when a later frame overwrites the same array slice.
void orderedArrayReuse(Device& dev) {
    TextureDesc d;
    d.dimension=TextureDimension::Texture2DArray;
    d.width=d.height=4;
    d.arrayLayers=2;
    d.mipLevels=2;
    d.filter=Filter::Nearest;
    const auto atlas=dev.createTexture(d);
    check(atlas.valid(),"ordered array LRU fixture atlas allocation");
    if(!atlas.valid())return;
    const auto root=solid(4,4,{18,75,90,255});
    TextureUploadDesc base;
    base.width=base.height=4;
    base.rowPitchBytes=16;
    base.slicePitchBytes=64;
    base.arrayLayer=0;
    base.data=root.data();
    check(dev.updateTextureRegion(atlas,base),
          "ordered GPU array upload of pinned root");
    std::vector<std::shared_ptr<Read>> snapshots;
    bool submitted=true;
    for(int frame=0;frame<8;++frame) {
        const auto pixels=solid(4,4,{uint8_t(30+frame*23),
                                         uint8_t(200-frame*15),
                                         uint8_t(17+frame*9),255});
        auto read=probe(atlas,0,1,4,4,pixels,
                        "queued atlas slice reuse frame "+std::to_string(frame));
        dev.beginFrame(16,16);
        TextureUploadDesc upload=base;
        upload.arrayLayer=1;
        upload.data=pixels.data();
        submitted &= dev.updateTextureRegion(atlas,upload);
        // Readback is queued after this frame's write. Later writes must not
        // retroactively change this output, even with multiple frames in flight.
        submitted &= dev.readTextureAsync(atlas,{0,1},
            [read](TextureReadbackResult result) {
                read->output=std::move(result);
                read->done=true;
            });
        dev.endFrame();
        snapshots.push_back(read);
    }
    check(submitted,"all ordered Metal atlas transfers and reads accepted");
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    while(Clock::now()<deadline) {
        const bool complete=std::all_of(snapshots.begin(),snapshots.end(),
            [](const auto& read){return read->done;});
        if(complete)break;
        dev.beginFrame(16,16);
        dev.endFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for(const auto& read:snapshots) {
        check(read->done && read->output.success() &&
              read->output.width==4 && read->output.height==4 &&
              read->output.format==PixelFormat::RGBA8 &&
              read->output.pixels==read->expected,
              "GPU array snapshot unchanged by later LRU slice writes");
    }
    verifyReads(dev,{probe(atlas,0,0,4,4,root,
                          "pinned root survives queued LRU overwrites")},
                "ordered GPU array LRU root");
    check(dev.gpuHealthy(),"Metal queued atlas reuse GPU healthy");
    dev.destroyTexture(atlas);
}

void volume(Device& dev) {
    const auto setupStart=Clock::now();
    TextureDesc d;
    d.dimension=TextureDimension::Texture3D;
    d.width=4;d.height=4;d.depth=4;d.mipLevels=2;d.filter=Filter::Nearest;
    std::vector<uint8_t> base;
    for(int z=0;z<4;++z) append(base,solid(4,4,{uint8_t(20+z*13),42,67,255}));
    d.data=base.data();
    const auto h=dev.createTexture(d);
    check(h.valid(),"Metal creates RGBA8 Texture3D with initial depth-plane data");
    if (!h.valid()) return;

    const auto slice=solid(2,2,{191,92,30,255});
    TextureUploadDesc mip;
    mip.mipLevel=1;mip.x=0;mip.y=0;mip.z=1;
    mip.width=2;mip.height=2;mip.depth=1;
    mip.rowPitchBytes=8;mip.slicePitchBytes=16;mip.data=slice.data();
    check(dev.updateTextureRegion(h,mip),
          "Metal uploads only z=1 of mip 1 in Texture3D");

    const auto patch=solid(2,2,{225,17,88,255});
    TextureUploadDesc sub;
    sub.mipLevel=0;sub.x=1;sub.y=1;sub.z=2;
    sub.width=2;sub.height=2;sub.depth=1;
    sub.rowPitchBytes=8;sub.slicePitchBytes=16;sub.data=patch.data();
    check(dev.updateTextureRegion(h,sub),
          "Metal updates partial x/y and z=2 of mip 0");

    auto invalid=mip;
    invalid.z=2; // mip-1 depth is two, so z=2 is outside.
    check(!dev.updateTextureRegion(h,invalid),"Texture3D rejects out-of-range mip z");
    invalid=mip;invalid.rowPitchBytes=7;
    check(!dev.updateTextureRegion(h,invalid),"Texture3D rejects short row pitch");

    const auto untouched=solid(4,4,{20,42,67,255});
    auto changed=solid(4,4,{46,42,67,255});
    for (int y=1;y<=2;++y) for(int x=1;x<=2;++x)
        std::copy_n(patch.data()+size_t((y-1)*2+(x-1))*4,4,
                    changed.data()+size_t(y*4+x)*4);
    std::printf("TIMING Texture3D create_and_upload_ms=%.3f\n",
                elapsedMs(setupStart,Clock::now()));
    verifyReads(dev,{
        probe(h,0,0,4,4,untouched,"3D base z0 unchanged"),
        probe(h,0,2,4,4,changed,"3D base z2 partial rectangle"),
        probe(h,1,1,2,2,slice,"3D mip1 z1 partial depth selection")
    },"Texture3D");
    const auto before=dev.textureResources();
    check(std::any_of(before.begin(),before.end(),[&](const auto& r) {
        return r.handle==h && r.dimension==TextureDimension::Texture3D &&
            r.depth==4 && r.mipLevels==2;
    }),"Metal resource inventory reports live 3D texture");
    dev.destroyTexture(h);
    const auto after=dev.textureResources();
    check(std::none_of(after.begin(),after.end(),[&](const auto& r) {
        return r.handle==h;
    }),"Metal resource inventory removes destroyed 3D texture");
}
void layered(Device& dev, TextureDimension dimension,int layers,
             const char* name) {
    const auto setupStart=Clock::now();
    TextureDesc d;
    d.dimension=dimension;d.width=4;d.height=4;
    d.arrayLayers=layers;d.mipLevels=2;d.filter=Filter::Nearest;
    std::vector<uint8_t> base;
    for(int i=0;i<layers;++i)
        append(base,solid(4,4,{uint8_t(10+i*9),uint8_t(70+i),33,255}));
    d.data=base.data();
    const auto h=dev.createTexture(d);
    if (!h.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: Metal %s allocation: %s\n",
                     name,dev.lastError().c_str());
        return;
    }
    const int last=layers-1;
    const auto mipData=solid(2,2,{156,49,uint8_t(layers),255});
    TextureUploadDesc mip;
    mip.mipLevel=1;mip.arrayLayer=last;
    mip.width=2;mip.height=2;mip.depth=1;
    mip.rowPitchBytes=8;mip.slicePitchBytes=16;mip.data=mipData.data();
    check(dev.updateTextureRegion(h,mip),
          "Metal uploads selected array/cube face and authored mip");

    std::printf("TIMING %s create_and_upload_ms=%.3f\n",
                name,elapsedMs(setupStart,Clock::now()));
    verifyReads(dev,{
        probe(h,0,0,4,4,solid(4,4,{10,70,33,255}),
              std::string(name)+" face/layer0"),
        probe(h,0,last,4,4,
              solid(4,4,{uint8_t(10+last*9),uint8_t(70+last),33,255}),
              std::string(name)+" last face/layer"),
        probe(h,1,last,2,2,mipData,
              std::string(name)+" authored mip1")
    },name);
    dev.destroyTexture(h);
}

// This is a real fragment texture fetch, not a CPU-side upload or Metal blit.
// It closes the acceptance gap for 3D volume + manual mip + partial z updates.
constexpr const char* kVolumeSampleMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct VolumeVertexOut { float4 position [[position]]; };
vertex VolumeVertexOut vs_main(uint vertexId [[vertex_id]]) {
    float2 positions[3]={float2(-1.0,-1.0),float2(3.0,-1.0),
                          float2(-1.0,3.0)};
    VolumeVertexOut out;
    out.position=float4(positions[vertexId],0.0,1.0);
    return out;
}
fragment float4 fs_main(VolumeVertexOut in [[stage_in]],
                        texture3d<float> tex0 [[texture(0)]],
                        sampler s0 [[sampler(0)]],
                        constant float4& coord [[buffer(1)]]) {
    return tex0.sample(s0,coord.xyz,level(coord.w));
}
)MSL";

void shaderSampleVolume(Device& dev) {
    const auto start=Clock::now();
    TextureDesc src;
    src.dimension=TextureDimension::Texture3D;
    src.width=4;src.height=4;src.depth=4;src.mipLevels=2;
    src.filter=Filter::Nearest;
    std::vector<uint8_t> base;
    for(int z=0;z<4;++z)
        append(base,solid(4,4,{uint8_t(11+z*35),55,77,255}));
    src.data=base.data();
    const auto volumeTex=dev.createTexture(src);
    check(volumeTex.valid(),"3D shader sample volume allocated");
    if (!volumeTex.valid()) return;
    const auto patch=solid(2,2,{225,17,88,255});
    TextureUploadDesc part;
    part.x=1;part.y=1;part.z=2;
    part.width=2;part.height=2;part.depth=1;
    part.rowPitchBytes=8;part.slicePitchBytes=16;part.data=patch.data();
    check(dev.updateTextureRegion(volumeTex,part),
          "3D shader source partial Z rectangle uploaded");
    const auto mipData=solid(2,2,{191,92,30,255});
    TextureUploadDesc nextMip;
    nextMip.mipLevel=1;nextMip.z=1;
    nextMip.width=2;nextMip.height=2;nextMip.depth=1;
    nextMip.rowPitchBytes=8;nextMip.slicePitchBytes=16;
    nextMip.data=mipData.data();
    check(dev.updateTextureRegion(volumeTex,nextMip),
          "3D shader source authored mip1 Z plane uploaded");

    TextureDesc color;
    color.width=4;color.height=4;color.renderTarget=true;
    color.format=PixelFormat::RGBA8;color.mipLevels=1;
    TextureDesc depth=color;
    depth.format=PixelFormat::Depth;
    const auto target=dev.createTexture(color);
    const auto depthTarget=dev.createTexture(depth);
    ShaderDesc shader;
    shader.msl=kVolumeSampleMSL;
    shader.textureSlots=1;shader.uniformSlots=1;
    shader.label="MetalVolumeSampleAcceptance";
    const auto sh=dev.createShader(shader);
    PipelineDesc desc;
    desc.shader=sh;
    desc.attrCount=0; desc.stride=0;
    desc.indexType=IndexType::None;
    desc.cull=CullMode::None;
    desc.depthTest=false;desc.depthWrite=false;
    desc.colorFormat=PixelFormat::RGBA8;
    desc.colorAttachmentCount=1;
    desc.label="MetalVolumeSampleAcceptance";
    const auto pipeline=sh.valid() ? dev.createPipeline(desc) : PipelineHandle{};
    if (!target.valid() || !depthTarget.valid() || !sh.valid() || !pipeline.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: native Metal fragment sampling setup: %s\n",
                     dev.lastError().c_str());
    } else {
        std::printf("TIMING Texture3D shader_pipeline_setup_ms=%.3f\n",
                    elapsedMs(start,Clock::now()));
        struct Sample {
            std::array<float,4> coord;
            Color expected;
            const char* label;
        };
        const Sample cases[] = {
            {{0.125f,0.125f,0.125f,0.0f},{11,55,77,255},
             "3D fragment samples original z0"},
            {{0.375f,0.375f,0.625f,0.0f},{225,17,88,255},
             "3D fragment samples patched z2"},
            {{0.75f,0.75f,0.75f,1.0f},{191,92,30,255},
             "3D fragment samples explicit mip1 z1"}
        };
        for(const auto& sample:cases) {
            dev.beginFrame(16,16);
            RenderTargetPassDesc pass;
            pass.colorCount=1;
            pass.colors[0].texture=target;
            pass.colors[0].clear=true;
            pass.depth.texture=depthTarget;
            pass.depth.clear=true;
            const bool active=dev.beginRenderTargets(pass);
            if (!active) {
                ++failures;
                std::fprintf(stderr,"FAIL: 3D fragment render target: %s\n",
                             dev.lastError().c_str());
            } else {
                dev.setViewport({0,0,4,4});
                dev.setPipeline(pipeline);
                dev.setTexture(0,volumeTex);
                dev.setUniforms(0,sample.coord.data(),sizeof(sample.coord));
                dev.draw(3);
                dev.endPass();
            }
            dev.endFrame();
            if (active) {
                verifyReads(dev,{
                    probe(target,0,0,4,4,solid(4,4,sample.expected),
                          sample.label,1)
                },"Texture3D fragment sampling");
            }
        }
    }
    if (pipeline.valid()) dev.destroyPipeline(pipeline);
    if (sh.valid()) dev.destroyShader(sh);
    if (target.valid()) dev.destroyTexture(target);
    if (depthTarget.valid()) dev.destroyTexture(depthTarget);
    dev.destroyTexture(volumeTex);
    std::printf("TIMING Texture3D fragment_sampling_total_ms=%.3f\n",
                elapsedMs(start,Clock::now()));
}


// Minified sparse cutouts must survive the same fragment alpha test used by
// foliage, rather than vanishing when ordinary mip averages fall below0.5.
void shaderSampleAlphaCoverage(Device& dev) {
    std::vector<uint8_t> pixels(32*32*4);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
        const size_t at=(size_t(y)*32+x)*4;
        const unsigned count=unsigned((x/4)*13+(y/4)*7)%8;
        pixels[at]=20;pixels[at+1]=150;pixels[at+2]=40;
        pixels[at+3]=unsigned((y%4)*4+x%4)<count?255:0;
    }
    std::vector<aiverse::render::AlphaCoverageMip> levels;
    check(aiverse::render::buildAlphaCoverageMips(32,32,pixels,128,levels),"alpha coverage CPU chain");
    if(levels.size()<2)return;
    const auto& selected=levels[1];size_t pixel=0;
    while(pixel<selected.rgba.size()/4&&selected.rgba[pixel*4+3]<128)++pixel;
    check(pixel<selected.rgba.size()/4,"preserved mip has leaf texel");
    if(pixel>=selected.rgba.size()/4)return;
    std::string source=kVolumeSampleMSL;
    source.replace(source.find("texture3d<float>"),16,"texture2d<float>");
    const std::string old="return tex0.sample(s0,coord.xyz,level(coord.w));";
    source.replace(source.find(old),old.size(),"float4 c=tex0.sample(s0,coord.xy,level(coord.w)); if(c.a<0.5) discard_fragment(); return float4(1.0);");
    ShaderDesc shader;shader.msl=source.c_str();shader.textureSlots=1;shader.uniformSlots=1;shader.label="AlphaCoverageMipAcceptance";
    const auto sh=dev.createShader(shader);
    PipelineDesc pd;pd.shader=sh;pd.attrCount=0;pd.stride=0;pd.indexType=IndexType::None;
    pd.cull=CullMode::None;pd.depthTest=false;pd.depthWrite=false;pd.colorFormat=PixelFormat::RGBA8;pd.colorAttachmentCount=1;
    const auto pipeline=sh.valid()?dev.createPipeline(pd):PipelineHandle{};
    check(pipeline.valid(),"alpha coverage fragment pipeline");
    if(!pipeline.valid()){if(sh.valid())dev.destroyShader(sh);return;}
    TextureDesc targetDesc;targetDesc.width=targetDesc.height=4;targetDesc.renderTarget=true;
    const auto target=dev.createTexture(targetDesc);
    for(bool preserve:{false,true}) {
        TextureDesc input;input.width=input.height=32;input.data=pixels.data();input.filter=Filter::Linear;
        input.mipmaps=!preserve;input.mipLevels=preserve?int(levels.size()+1):0;
        const auto texture=dev.createTexture(input);check(texture.valid(),"cutout texture allocation");
        if(preserve&&texture.valid())for(size_t i=0;i<levels.size();++i) {
            TextureUploadDesc upload;upload.mipLevel=int(i+1);upload.width=levels[i].width;upload.height=levels[i].height;
            upload.rowPitchBytes=upload.width*4;upload.data=levels[i].rgba.data();
            check(dev.updateTextureRegion(texture,upload),"coverage mip explicit upload");
        }
        if(texture.valid()&&target.valid()) {
            dev.beginFrame(16,16);RenderTargetPassDesc pass;pass.colorCount=1;pass.colors[0].texture=target;
            const bool active=dev.beginRenderTargets(pass);check(active,"coverage mip render pass");
            if(active) {
                dev.setViewport({0,0,4,4});dev.setPipeline(pipeline);dev.setTexture(0,texture);
                const std::array<float,4> uv{(float(pixel%size_t(selected.width))+.5f)/selected.width,
                    (float(pixel/size_t(selected.width))+.5f)/selected.height,0,2};
                dev.setUniforms(0,uv.data(),sizeof(uv));dev.draw(3);dev.endPass();
            }
            dev.endFrame();
            if(active)verifyReads(dev,{probe(target,0,0,4,4,solid(4,4,preserve?Color{255,255,255,255}:Color{0,0,0,0}),
                preserve?"preserved mip retains minified alpha-test foliage":"ordinary mip loses thin alpha-test foliage")},"alpha coverage mips");
        }
        if(texture.valid())dev.destroyTexture(texture);
    }
    if(target.valid())dev.destroyTexture(target);dev.destroyPipeline(pipeline);dev.destroyShader(sh);
}

// Generic RHI bilinear acceptance, independent of Terra/material/genre.
// The centre of a 2x2 checker must average four texels, not select one.
void shaderSampleBilinear(Device& dev) {
    std::string source=kVolumeSampleMSL;
    source.replace(source.find("texture3d<float>"),16,"texture2d<float>");
    source.replace(source.find("coord.xyz"),9,"coord.xy");
    ShaderDesc shader;shader.msl=source.c_str();shader.textureSlots=1;shader.uniformSlots=1;
    shader.label="GenericBilinearAcceptance";
    const auto sh=dev.createShader(shader);
    PipelineDesc pd;pd.shader=sh;pd.attrCount=0;pd.stride=0;pd.indexType=IndexType::None;
    pd.cull=CullMode::None;pd.depthTest=false;pd.depthWrite=false;pd.colorFormat=PixelFormat::RGBA8;pd.colorAttachmentCount=1;
    const auto pipeline=sh.valid()?dev.createPipeline(pd):PipelineHandle{};
    check(sh.valid()&&pipeline.valid(),"generic bilinear shader/pipeline");
    if(!pipeline.valid()){if(sh.valid())dev.destroyShader(sh);return;}
    TextureDesc td;td.width=td.height=4;td.renderTarget=true;
    const auto target=dev.createTexture(td);td.format=PixelFormat::Depth;
    const auto depth=dev.createTexture(td);
    check(target.valid()&&depth.valid(),"generic bilinear targets");
    const uint8_t pixels[]={0,0,0,255,255,255,255,255,255,255,255,255,0,0,0,255};
    for(const auto filter:{Filter::Nearest,Filter::Linear}) {
        TextureDesc input;input.width=input.height=2;input.data=pixels;input.filter=filter;
        const auto texture=dev.createTexture(input);check(texture.valid(),"generic filter texture");
        if(texture.valid()&&target.valid()&&depth.valid()) {
            dev.beginFrame(16,16);
            RenderTargetPassDesc pass;pass.colorCount=1;pass.colors[0].texture=target;pass.colors[0].clear=true;
            pass.depth.texture=depth;pass.depth.clear=true;
            const bool active=dev.beginRenderTargets(pass);check(active,"generic bilinear render pass");
            if(active){dev.setViewport({0,0,4,4});dev.setPipeline(pipeline);dev.setTexture(0,texture);
                const std::array<float,4> uv{.5f,.5f,0,0};dev.setUniforms(0,uv.data(),sizeof(uv));dev.draw(3);dev.endPass();}
            dev.endFrame();
            if(active){const auto value=uint8_t(filter==Filter::Linear?128:0);
                verifyReads(dev,{probe(target,0,0,4,4,solid(4,4,{value,value,value,255}),
                    filter==Filter::Linear?"bilinear averages four texels":"nearest retains authored pixel sampling",1)},"generic texture filtering");}
        }
        if(texture.valid())dev.destroyTexture(texture);
    }
    dev.destroyPipeline(pipeline);dev.destroyShader(sh);
    if(target.valid())dev.destroyTexture(target);if(depth.valid())dev.destroyTexture(depth);
}

// Tests the actual *shader -> RGBA8 feedback render target -> Metal async
// readback -> CPU virtual-page decoder* contract. Does not touch disk or
// fake pending readbacks. Hardware is mandatory for this opt-in fixture.
void gpuVirtualTextureFeedback(Device& dev) {
    const auto start=Clock::now();
    VirtualTextureFeedbackConfig feedbackConfig;
    feedbackConfig.tilesX=feedbackConfig.tilesY=8;
    feedbackConfig.mipLevels=3;feedbackConfig.virtualLayers=3;
    feedbackConfig.maxReadbackPixels=16;
    feedbackConfig.maxUniquePages=8;
    feedbackConfig.maxVisibleRegions=8;
    VirtualTexturePrefetchConfig prefetchConfig;
    prefetchConfig.tilesX=prefetchConfig.tilesY=8;
    prefetchConfig.mipLevels=3;prefetchConfig.virtualLayers=3;
    prefetchConfig.maxCandidates=32;
    prefetchConfig.maxRequestsPerFrame=4;

    // Use the canonical production feedback encoder/material helper, not
    // duplicated page-ID packing arithmetic in a test-only Metal shader.
    std::string source=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct FeedbackVertexOut { float4 position [[position]]; };
vertex FeedbackVertexOut vs_main(uint index [[vertex_id]]) {
    float2 points[3]={float2(-1.0,-1.0),float2(3.0,-1.0),
                      float2(-1.0,3.0)};
    FeedbackVertexOut out;
    out.position=float4(points[index],0.0,1.0);
    return out;
}
)MSL";
    source+=kVirtualTextureFeedbackMSL;
    source+=kVirtualTextureFeedbackMaterialMSL;
    source+=R"MSL(
fragment float4 fs_main(FeedbackVertexOut in [[stage_in]],
                        constant float4& params [[buffer(1)]]) {
    return aiverse_material_feedback(params.xy,int(params.z),
                                     int(params.w),int2(8,8));
}
)MSL";
    ShaderDesc shader;
    shader.msl=source.c_str();
    shader.uniformSlots=1;
    shader.label="MetalVirtualTextureFeedbackAcceptance";
    const ShaderHandle fragment=dev.createShader(shader);
    if (!fragment.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: GPU VT feedback shader: %s\n",
                     dev.lastError().c_str());
        return;
    }
    PipelineDesc pipelineConfig=virtualTextureFeedbackPipelineDesc(fragment,true);
    pipelineConfig.attrCount=0;
    pipelineConfig.stride=0;
    pipelineConfig.indexType=IndexType::None;
    pipelineConfig.cull=CullMode::None;
    const PipelineHandle pipeline=dev.createPipeline(pipelineConfig);
    if (!pipeline.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: GPU VT feedback pipeline: %s\n",
                     dev.lastError().c_str());
        dev.destroyShader(fragment);
        return;
    }
    std::printf("TIMING VT feedback shader_pipeline_setup_ms=%.3f\n",
                elapsedMs(start,Clock::now()));

    const VirtualPageKey page{1,3,2,2};
    const uint32_t packed=encodeVirtualTextureFeedback(page);
    const Color expectedId{
        uint8_t(packed),uint8_t(packed>>8),
        uint8_t(packed>>16),uint8_t(packed>>24)};
    struct Case {
        std::array<float,4> params;
        Color expected;
        bool valid;
        const char* name;
    };
    const Case cases[]={
        {{0.8125f,0.5625f,1.0f,2.0f},expectedId,true,
         "VT feedback shader emits packed page ID"},
        {{-0.125f,0.5625f,1.0f,2.0f},{0,0,0,0},false,
         "VT feedback shader invalid UV emits zero"}
    };

    for (const auto& item:cases) {
        VirtualTextureFeedbackPass pass;
        if (!pass.initialize(dev,4,4,feedbackConfig,prefetchConfig,8,true)) {
            ++failures;
            std::fprintf(stderr,"FAIL: GPU VT feedback render target: %s\n",
                         pass.lastError().c_str());
            continue;
        }
        dev.beginFrame(16,16);
        const bool rendered=pass.renderAndCapture([&](Device& active) {
            active.setPipeline(pipeline);
            active.setUniforms(0,item.params.data(),sizeof(item.params));
            active.draw(3);
            return true;
        });
        dev.endFrame();
        if (!rendered) {
            ++failures;
            std::fprintf(stderr,"FAIL: GPU VT feedback render/readback: %s\n",
                         pass.lastError().c_str());
            pass.shutdown();
            continue;
        }
        auto read=probe(pass.texture(),0,0,4,4,
                        solid(4,4,item.expected),item.name);
        verifyReads(dev,{read},"VT GPU feedback RGBA8 ID");
        check(pass.readyToSubmit(),
              "Metal GPU feedback callback decoded on the owner thread");
        VirtualTextureFeedbackDecoder decoder;
        check(decoder.initialize(feedbackConfig),
              "GPU VT feedback decoder initialized with expected topology");
        std::vector<VirtualTextureVisibleRegion> visible;
        std::string error;
        const bool decoded=decoder.decode(read->output,visible,error);
        if (!decoded) {
            ++failures;
            std::fprintf(stderr,"FAIL: native feedback ID decode: %s\n",
                         error.c_str());
        } else if (item.valid) {
            const auto stats=decoder.stats();
            check(stats.sampledPixels==16 && stats.uniquePages==1 &&
                  stats.invalidIds==0 && visible.size()==1,
                  "actual GPU feedback produces one unique visible page");
            if (visible.size()==1) {
                const auto& v=visible.front();
                check(v.desiredMip==page.mip && v.layer==page.layer &&
                      v.priority==16 &&
                      v.u0==0.75 && v.v0==0.5,
                      "real Metal encoded feedback preserves page XY/mip/layer/priority");
            }
        } else {
            const auto stats=decoder.stats();
            check(stats.sampledPixels==16 && stats.invalidIds==16 &&
                  stats.uniquePages==0 && visible.empty(),
                  "zero RGBA8 GPU feedback never requests an invalid page");
        }
        pass.shutdown();
    }
    dev.destroyPipeline(pipeline);
    dev.destroyShader(fragment);
    std::printf("TIMING VT feedback GPU acceptance_total_ms=%.3f\n",
                elapsedMs(start,Clock::now()));
}


// Physical virtual texture publication in the real Metal backend:
// the shader-visible RGBA8 page table must never expose a pending or
// overwritten physical slice. No mock GPU, disk I/O or test-only cache.
void gpuPhysicalVirtualAtlas(Device& dev) {
    const auto start=Clock::now();
    VirtualTextureStreamConfig cfg;
    cfg.tilesX=cfg.tilesY=4;
    cfg.mipLevels=3;
    cfg.virtualLayers=1;
    cfg.physicalPages=2;
    cfg.physicalPageSize=4;
    cfg.physicalFormat=PixelFormat::RGBA8;
    cfg.pageTableBytesPerFlush=64;
    VirtualTextureStreamer pages(dev);
    if (!pages.initialize(cfg)) {
        ++failures;
        std::fprintf(stderr,"FAIL: native VT atlas initialize: %s\n",
                     pages.lastError().c_str());
        return;
    }
    const auto atlas=pages.physicalAtlas();
    const auto table=pages.pageTableTexture();
    check(atlas.valid() && table.valid() && pages.ready(),
          "Metal physical atlas and page table have live GPU handles");
    if (!atlas.valid() || !table.valid()) { pages.shutdown(); return; }

    const VirtualPageKey coarse{2,0,0,0};
    const VirtualPageKey fine{0,3,3,0};
    const VirtualPageKey replacement{0,0,0,0};
    const Color invalid{0,0,0,0};
    const auto coarseData=solid(4,4,{23,42,63,255});
    const auto fineData=solid(4,4,{101,54,12,255});
    const auto replacementData=solid(4,4,{209,24,76,255});

    auto expectedTable=[&](int slot,int mip,int x,int y,
                           int overrideSlot=-1,int overrideMip=0) {
        std::vector<uint8_t> rgba=solid(4,4,
            {uint8_t(slot),0,uint8_t(mip),255});
        if (overrideSlot>=0) {
            const size_t offset=size_t(y*4+x)*4;
            rgba[offset]=uint8_t(overrideSlot);
            rgba[offset+1]=0;
            rgba[offset+2]=uint8_t(overrideMip);
            rgba[offset+3]=255;
        }
        return rgba;
    };
    const auto baseTable=expectedTable(0,2,0,0);
    const auto first=pages.stage(coarse,coarseData.data(),
                                 coarseData.size(),true);
    check(first.valid() && !pages.resolveExact(coarse).valid() &&
          pages.encodedPageTable(3,3,0)==invalid,
          "VT coarse page remains invisible while upload is pending");
    verifyReads(dev,{
        probe(table,0,0,4,4,solid(4,4,invalid),
              "VT GPU table invalid before first publish")
    },"VT GPU unpublished page");

    int coarseEvents=0;
    const auto firstDrain=pages.drain([&](VirtualPageKey key,bool ok) {
        if (key==coarse && ok) ++coarseEvents;
    });
    const auto coarseSlot=pages.resolveExact(coarse);
    check(firstDrain==1 && coarseEvents==1 &&
          coarseSlot.valid() && coarseSlot.ready &&
          coarseSlot.physicalSlice==0,
          "VT coarse upload publishes only after successful Metal transfer");
    check(pages.encodedPageTable(3,3,0)==(Color{0,0,2,255}),
          "VT page table encodes coarse fallback mip and physical slot");
    verifyReads(dev,{
        probe(atlas,0,0,4,4,coarseData,"VT physical pinned coarse page"),
        probe(table,0,0,4,4,baseTable,"VT GPU coarse fallback table")
    },"VT coarse publication");

    const auto second=pages.stage(fine,fineData.data(),
                                  fineData.size(),false);
    check(second.valid() && !pages.resolveExact(fine).valid(),
          "VT detail page is pending before GPU publication");
    int fineEvents=0;
    const auto secondDrain=pages.drain([&](VirtualPageKey key,bool ok) {
        if (key==fine && ok) ++fineEvents;
    });
    const auto fineSlot=pages.resolveExact(fine);
    check(secondDrain==1 && fineEvents==1 &&
          fineSlot.valid() && fineSlot.physicalSlice==1,
          "VT fine mip published into second physical page");
    check(pages.encodedPageTable(3,3,0)==(Color{1,0,0,255}),
          "VT fine page overrides pinned coarse fallback");
    auto tableWithFine=expectedTable(0,2,3,3,1,0);
    verifyReads(dev,{
        probe(atlas,0,1,4,4,fineData,"VT physical fine page"),
        probe(table,0,0,4,4,tableWithFine,"VT GPU fine-page override")
    },"VT fine publication");

    // Both atlas slots are occupied. The pinned coarse page must survive,
    // while unpinned fine is displaced. Invalidation must hit GPU table
    // before this physical slice can be written by the replacement page.
    const auto third=pages.stage(replacement,replacementData.data(),
                                 replacementData.size(),false);
    check(third.valid() && !pages.resolveExact(fine).valid() &&
          !pages.resolveExact(replacement).valid() &&
          pages.resolveExact(coarse).valid(),
          "VT LRU replacement evicts fine but not pinned coarse");
    check(pages.pageStats().evictions==1 &&
          pages.encodedPageTable(3,3,0)==(Color{0,0,2,255}),
          "VT GPU mapping invalidated before replacing physical slot");
    verifyReads(dev,{
        probe(table,0,0,4,4,baseTable,
              "VT GPU table invalidated before slot reuse")
    },"VT pre-overwrite invalidation");

    int replacementEvents=0;
    const auto thirdDrain=pages.drain([&](VirtualPageKey key,bool ok) {
        if (key==replacement && ok) ++replacementEvents;
    });
    const auto newSlot=pages.resolveExact(replacement);
    check(thirdDrain==1 && replacementEvents==1 &&
          newSlot.valid() && newSlot.physicalSlice==1,
          "VT recycled physical slot published after replacement upload");
    check(pages.encodedPageTable(0,0,0)==(Color{1,0,0,255}) &&
          pages.encodedPageTable(3,3,0)==(Color{0,0,2,255}),
          "VT GPU table redirects replacement tile, preserves coarse fallback");
    auto tableWithReplacement=expectedTable(0,2,0,0,1,0);
    verifyReads(dev,{
        probe(atlas,0,1,4,4,replacementData,
              "VT overwritten physical page contains replacement"),
        probe(table,0,0,4,4,tableWithReplacement,
              "VT GPU table after LRU republish")
    },"VT replacement publication");

    check(pages.evict(replacement) &&
          !pages.resolveExact(replacement).valid() &&
          pages.resolveExact(coarse).valid(),
          "VT explicit eviction keeps coarse physical slice resident");
    verifyReads(dev,{
        probe(table,0,0,4,4,baseTable,
              "VT GPU page table falls back after explicit eviction")
    },"VT explicit eviction");

    const auto before=dev.textureResources();
    check(std::any_of(before.begin(),before.end(),[&](const auto& x) {
        return x.handle==atlas && x.dimension==TextureDimension::Texture2DArray &&
               x.arrayLayers==2 && x.width==4 && x.height==4;
    }) && std::any_of(before.begin(),before.end(),[&](const auto& x) {
        return x.handle==table && x.dimension==TextureDimension::Texture2DArray &&
               x.arrayLayers==1 && x.width==4 && x.height==4;
    }),"VT real Metal atlas and page table inventory matches topology");
    pages.shutdown();
    const auto after=dev.textureResources();
    check(std::none_of(after.begin(),after.end(),[&](const auto& x) {
        return x.handle==atlas || x.handle==table;
    }),"VT shutdown releases physical atlas and page table GPU objects");
    std::printf("TIMING VT physical atlas publication_total_ms=%.3f\n",
                elapsedMs(start,Clock::now()));
}



// Renders the *production* RGBA8 VT page-table indirection helper.
// This proves that a shader samples the expected *physical* atlas slice,
// instead of only inspecting the two GPU textures independently by blit.
struct VtAtlasFragmentProbe {
    Device& device;
    TextureHandle atlas;
    TextureHandle table;
    TextureHandle color;
    TextureHandle depth;
    ShaderHandle shader;
    PipelineHandle pipeline;
    const Color fallback{18,79,181,255};

    explicit VtAtlasFragmentProbe(Device& dev):device(dev) {}
    VtAtlasFragmentProbe(const VtAtlasFragmentProbe&)=delete;
    VtAtlasFragmentProbe& operator=(const VtAtlasFragmentProbe&)=delete;
    ~VtAtlasFragmentProbe() { shutdown(); }

    bool initialize(TextureHandle atlasHandle,TextureHandle tableHandle) {
        atlas=atlasHandle; table=tableHandle;
        const auto start=Clock::now();
        TextureDesc output;
        output.width=4;output.height=4;
        output.format=PixelFormat::RGBA8;
        output.mipLevels=1;
        output.renderTarget=true;
        color=device.createTexture(output);
        output.format=PixelFormat::Depth;
        depth=device.createTexture(output);
        std::string msl=R"AVT(
#include <metal_stdlib>
using namespace metal;
struct VTProbeOutput { float4 position [[position]]; };
vertex VTProbeOutput vs_main(uint index [[vertex_id]]) {
    float2 positions[3]={
        float2(-1.0,-1.0),float2(3.0,-1.0),float2(-1.0,3.0)
    };
    VTProbeOutput out;
    out.position=float4(positions[index],0.0,1.0);
    return out;
}
)AVT";
        // Canonical implementation shared by the real Model VT shader.
        msl+=kVirtualTextureMSL;
        msl+=R"AVT(
fragment float4 fs_main(
        VTProbeOutput in [[stage_in]],
        texture2d_array<float, access::sample> physical [[texture(0)]],
        texture2d_array<float, access::read> pageTable [[texture(1)]],
        sampler physicalSampler [[sampler(0)]],
        constant float4& uv [[buffer(1)]]) {
    return aiverse_sample_virtual(
        physical,pageTable,physicalSampler,uv.xy,0u,0.0f,
        float4(18.0f/255.0f,79.0f/255.0f,181.0f/255.0f,1.0f),
        0u,0u);
}
)AVT";
        ShaderDesc sd;
        sd.msl=msl.c_str();
        sd.textureSlots=2;
        sd.uniformSlots=1;
        sd.label="MetalDiskVTAtlasShaderAcceptance";
        shader=device.createShader(sd);
        if (shader.valid()) {
            PipelineDesc pd;
            pd.shader=shader;
            pd.attrCount=0;
            pd.stride=0;
            pd.indexType=IndexType::None;
            pd.cull=CullMode::None;
            pd.depthTest=false;
            pd.depthWrite=false;
            pd.colorFormat=PixelFormat::RGBA8;
            pd.colorAttachmentCount=1;
            pd.label="MetalDiskVTAtlasShaderAcceptance";
            pipeline=device.createPipeline(pd);
        }
        const bool ok=color.valid() && depth.valid() &&
                      shader.valid() && pipeline.valid();
        if (!ok) {
            ++failures;
            std::fprintf(stderr,
                "FAIL: native GPU VT indirection shader setup: %s\n",
                device.lastError().c_str());
            shutdown();
            return false;
        }
        std::printf("TIMING disk VT shader_lookup_setup_ms=%.3f\n",
                    elapsedMs(start,Clock::now()));
        return true;
    }

    void expect(float u,float v,Color expected,const char* label) {
        if (!pipeline.valid()) {
            ++failures;
            std::fprintf(stderr,"FAIL: %s shader pipeline unavailable\n",label);
            return;
        }
        const float uv[4]={u,v,0.0f,0.0f};
        device.beginFrame(16,16);
        RenderTargetPassDesc pass;
        pass.colorCount=1;
        pass.colors[0].texture=color;
        pass.colors[0].clear=true;
        pass.depth.texture=depth;
        pass.depth.clear=true;
        if (!device.beginRenderTargets(pass)) {
            ++failures;
            std::fprintf(stderr,"FAIL: %s VT shader render pass: %s\n",
                         label,device.lastError().c_str());
            device.endFrame();
            return;
        }
        device.setViewport({0,0,4,4});
        device.setPipeline(pipeline);
        device.setTexture(0,atlas);
        device.setTexture(1,table);
        device.setUniforms(0,uv,sizeof(uv));
        device.draw(3);
        device.endPass();
        device.endFrame();
        verifyReads(device,{
            probe(color,0,0,4,4,solid(4,4,expected),label,1)
        },"disk VT fragment atlas indirection");
    }

    void shutdown() {
        if (pipeline.valid()) device.destroyPipeline(pipeline);
        if (shader.valid()) device.destroyShader(shader);
        if (color.valid()) device.destroyTexture(color);
        if (depth.valid()) device.destroyTexture(depth);
        pipeline={};shader={};color={};depth={};
        atlas={};table={};
    }
};

// Real disk worker -> render-thread GPU upload -> native Metal page-table
// publication. No fake Device, synthetic worker result, or application scene.
struct VtDiskFixture {
    std::filesystem::path root;
    bool ready = false;

    VtDiskFixture() {
        std::error_code ec;
        const auto temp = std::filesystem::temp_directory_path(ec);
        if (ec) return;
        root = temp / ("aiverse-metal-vt-disk-" +
                       std::to_string(Clock::now().time_since_epoch().count()));
        ready = std::filesystem::create_directories(root,ec) && !ec;
    }
    ~VtDiskFixture() {
        if (!root.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(root,ec);
        }
    }
    bool write(VirtualPageKey key,const std::vector<uint8_t>& data) const {
        if (!ready) return false;
        const auto file=root / VirtualTextureDiskQueue::relativePath(key);
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(),ec);
        if (ec) return false;
        std::ofstream out(file,std::ios::binary|std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()),
                  std::streamsize(data.size()));
        out.close();
        return bool(out);
    }
};

bool pumpDiskPage(VirtualTextureFileStreamer& stream,
                  VirtualDiskPageTicket ticket,VirtualPageKey key,
                  bool expectedSuccess,const char* label) {
    if (!ticket.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: %s invalid disk ticket\n",label);
        return false;
    }
    const auto start=Clock::now();
    const auto deadline=start+std::chrono::seconds(6);
    bool completed=false,success=false;
    size_t callbacks=0;
    std::string error;
    size_t staged=0;
    unsigned polls=0;
    while (!completed && Clock::now()<deadline) {
        staged+=stream.pump(2,[&](VirtualDiskPageTicket current,
                                 VirtualPageKey actual,bool ok,
                                 const std::string& why) {
            if (current!=ticket || !(actual==key)) {
                ++failures;
                std::fprintf(stderr,
                    "FAIL: %s unexpected disk-to-GPU completion\n",label);
                return;
            }
            ++callbacks;
            completed=true;
            success=ok;
            error=why;
        });
        ++polls;
        if (!completed) std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    const bool valid=completed && callbacks==1 &&
                     success==expectedSuccess &&
                     (success ? error.empty() : !error.empty());
    if (!valid) {
        ++failures;
        std::fprintf(stderr,
            "FAIL: %s completed=%d callbacks=%zu success=%d wanted=%d error=%s\n",
            label,int(completed),callbacks,int(success),int(expectedSuccess),
            error.c_str());
    } else {
        std::printf("PASS: %s (%s)\n",label,
                    expectedSuccess?"disk -> Metal GPU accepted":"disk failure isolated");
    }
    std::printf("TIMING %s worker_and_upload_ms=%.3f polls=%u "
                "gpu_pages_staged=%zu\n",
                label,elapsedMs(start,Clock::now()),polls,staged);
    return valid;
}

void gpuDiskBackedVirtualAtlas(Device& dev) {
    const auto start=Clock::now();
    VtDiskFixture files;
    check(files.ready,"temporary VT disk page fixture allocated");
    if (!files.ready) return;

    const VirtualPageKey coarse{2,0,0,0};
    const VirtualPageKey fine{0,3,3,0};
    const VirtualPageKey replacement{0,0,0,0};
    const VirtualPageKey missing{0,1,1,0};
    const Color invalid{0,0,0,0};
    const auto coarseData=solid(4,4,{18,71,93,255});
    const auto fineData=solid(4,4,{82,196,41,255});
    const auto replacementData=solid(4,4,{220,44,16,255});
    const bool fixturesWritten=files.write(coarse,coarseData) &&
                               files.write(fine,fineData) &&
                               files.write(replacement,replacementData);
    check(fixturesWritten,"temporary packed RGBA8 virtual page files written");
    if (!fixturesWritten) return; // do not claim disk-to-GPU success without source files

    VirtualTextureStreamConfig atlasCfg;
    atlasCfg.tilesX=atlasCfg.tilesY=4;
    atlasCfg.mipLevels=3;
    atlasCfg.virtualLayers=1;
    atlasCfg.physicalPages=2;
    atlasCfg.physicalPageSize=4;
    atlasCfg.physicalFormat=PixelFormat::RGBA8;
    atlasCfg.pageTableBytesPerFlush=64;
    VirtualDiskPageConfig diskCfg;
    diskCfg.assetRoot=files.root;
    diskCfg.tilesX=diskCfg.tilesY=4;
    diskCfg.mipLevels=3;
    diskCfg.virtualLayers=1;
    diskCfg.pageSize=4;
    diskCfg.maxOutstanding=8;
    diskCfg.maxReadyBytes=256;
    TextureStreamConfig gpuLimits;
    gpuLimits.gpuBudgetBytes=4096;
    gpuLimits.stagingBudgetBytes=256;
    gpuLimits.uploadBytesPerFrame=128;
    gpuLimits.uploadsPerFrame=2;

    VirtualTextureFileStreamer stream(dev,gpuLimits);
    if (!stream.initialize(atlasCfg,diskCfg,2)) {
        ++failures;
        std::fprintf(stderr,"FAIL: disk-backed Metal VT init: %s\n",
                     stream.lastError().c_str());
        return;
    }
    const auto atlas=stream.physicalAtlas();
    const auto table=stream.pageTableTexture();
    check(atlas.valid() && table.valid(),
          "disk-backed Metal VT allocates physical GPU textures");
    if (!atlas.valid() || !table.valid()) {stream.shutdown();return;}

    VtAtlasFragmentProbe sampling(dev);
    const bool samplingReady=sampling.initialize(atlas,table);
    const auto allInvalid=solid(4,4,invalid);
    const auto allCoarse=solid(4,4,{0,0,2,255});
    const auto coarseTicket=stream.requestPage(coarse,true);
    check(coarseTicket.valid() && stream.pendingPage(coarse) &&
          !stream.exactResidentPage(coarse),
          "file request does not make coarse page resident before worker publication");
    verifyReads(dev,{
        probe(table,0,0,4,4,allInvalid,
              "disk VT GPU page table invalid before worker pump")
    },"disk VT before publication");
    if (samplingReady)
        sampling.expect(0.875f,0.875f,sampling.fallback,
                        "disk VT fragment uses fallback before first page");
    if (pumpDiskPage(stream,coarseTicket,coarse,true,
                     "disk VT pinned coarse page publication")) {
        check(stream.exactResidentPage(coarse) &&
              stream.encodedPageTable(3,3,0)==(Color{0,0,2,255}) &&
              stream.stats().pages.pinned==1,
              "file-backed coarse page becomes pinned and mapped after GPU upload");
        verifyReads(dev,{
            probe(atlas,0,0,4,4,coarseData,
                  "disk VT coarse physical atlas pixels"),
            probe(table,0,0,4,4,allCoarse,
                  "disk VT coarse GPU page-table bytes")
        },"disk VT coarse GPU publication");
        if (samplingReady) {
            sampling.expect(0.125f,0.125f,Color{18,71,93,255},
                            "disk VT fragment reads pinned coarse page");
            sampling.expect(0.875f,0.875f,Color{18,71,93,255},
                            "disk VT fragment samples coarse fallback at fine UV");
        }
    }

    const auto missingTicket=stream.requestPage(missing);
    check(missingTicket.valid() &&
          !stream.exactResidentPage(missing),
          "missing file can be queued without premature residency");
    if (pumpDiskPage(stream,missingTicket,missing,false,
                     "disk VT missing-page failure")) {
        check(stream.ready() && !stream.exactResidentPage(missing) &&
              !stream.pendingPage(missing),
              "missing file does not quarantine healthy GPU atlas");
        verifyReads(dev,{
            probe(table,0,0,4,4,allCoarse,
                  "disk VT missing-page preserves original GPU mapping")
        },"disk VT failed disk lookup");
        if (samplingReady)
            sampling.expect(0.375f,0.375f,Color{18,71,93,255},
                            "disk VT fragment preserves coarse after failed disk read");
    }

    const auto fineTicket=stream.requestPage(fine);
    check(fineTicket.valid() && !stream.exactResidentPage(fine),
          "file-backed fine page awaits GPU publication");
    if (pumpDiskPage(stream,fineTicket,fine,true,
                     "disk VT fine page publication")) {
        check(stream.exactResidentPage(fine) &&
              stream.encodedPageTable(3,3,0)==(Color{1,0,0,255}),
              "disk-backed fine page overrides coarse GPU fallback");
        auto tableWithFine=allCoarse;
        const size_t offset=size_t(3*4+3)*4;
        const Color fineEntry{1,0,0,255};
        std::copy(fineEntry.begin(),fineEntry.end(),
                  tableWithFine.begin()+std::ptrdiff_t(offset));
        verifyReads(dev,{
            probe(atlas,0,1,4,4,fineData,
                  "disk VT fine physical atlas pixels"),
            probe(table,0,0,4,4,tableWithFine,
                  "disk VT fine override in GPU page table")
        },"disk VT fine GPU publication");
        if (samplingReady) {
            sampling.expect(0.875f,0.875f,Color{82,196,41,255},
                            "disk VT fragment samples fine physical atlas slice");
            sampling.expect(0.125f,0.125f,Color{18,71,93,255},
                            "disk VT fragment still samples pinned coarse elsewhere");
        }
    }

    const auto replacementTicket=stream.requestPage(replacement);
    check(replacementTicket.valid() && stream.pendingPage(replacement) &&
          !stream.exactResidentPage(replacement),
          "new file-backed replacement stays pending before pump");
    if (pumpDiskPage(stream,replacementTicket,replacement,true,
                     "disk VT LRU physical page reuse")) {
        check(stream.exactResidentPage(coarse) &&
              !stream.exactResidentPage(fine) &&
              stream.exactResidentPage(replacement) &&
              stream.stats().pages.evictions==1,
              "disk VT keeps pinned coarse, evicts old fine on physical reuse");
        auto tableWithReplacement=allCoarse;
        const Color replacementEntry{1,0,0,255};
        std::copy(replacementEntry.begin(),replacementEntry.end(),
                  tableWithReplacement.begin());
        verifyReads(dev,{
            probe(atlas,0,0,4,4,coarseData,
                  "disk VT pinned coarse physical pixels survive eviction"),
            probe(atlas,0,1,4,4,replacementData,
                  "disk VT overwritten physical slice contains replacement bytes"),
            probe(table,0,0,4,4,tableWithReplacement,
                  "disk VT GPU table drops old fine, publishes replacement")
        },"disk VT LRU GPU republish");
        if (samplingReady) {
            sampling.expect(0.875f,0.875f,Color{18,71,93,255},
                            "disk VT fragment drops evicted fine and uses coarse");
            sampling.expect(0.125f,0.125f,Color{220,44,16,255},
                            "disk VT fragment samples recycled slot with replacement bytes");
        }
    }
    sampling.shutdown();
    stream.shutdown();
    const auto inventory=dev.textureResources();
    check(std::none_of(inventory.begin(),inventory.end(),[&](const auto& x) {
        return x.handle==atlas || x.handle==table;
    }),"disk-backed VT shutdown releases actual Metal atlas/page table");
    std::printf("TIMING VT disk worker to Metal publication_total_ms=%.3f\n",
                elapsedMs(start,Clock::now()));
}

} // namespace

int main() {
    @autoreleasepool {
        const auto testStart=Clock::now();
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if (!metal) {
            std::fprintf(stderr,"FAIL: no Metal GPU available; opt-in test cannot skip\n");
            return 2;
        }
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;
        layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
        layer.frame=CGRectMake(0,0,16,16);
        layer.drawableSize=CGSizeMake(16,16);
        auto device=createMetalDevice((__bridge void*)layer);
        if (!device || device->info().backend != Backend::Metal) {
            std::fprintf(stderr,"FAIL: AIVERSE Metal backend unavailable\n");
            return 2;
        }
        const auto deviceReady=Clock::now();
        std::printf("Metal GPU acceptance device: %s\n",device->info().name.c_str());
        std::printf("TIMING Metal device_and_layer_init_ms=%.3f\n",
                    elapsedMs(testStart,deviceReady));
        orderedArrayReuse(*device);
        volume(*device);
        layered(*device,TextureDimension::Texture2DArray,2,"Texture2DArray");
        layered(*device,TextureDimension::Cube,6,"Cube");
        layered(*device,TextureDimension::CubeArray,12,"CubeArray");
        shaderSampleVolume(*device);
        shaderSampleBilinear(*device);
        shaderSampleAlphaCoverage(*device);
        gpuVirtualTextureFeedback(*device);
        gpuPhysicalVirtualAtlas(*device);
        gpuDiskBackedVirtualAtlas(*device);
        std::printf("TIMING Metal fixture_total_ms=%.3f gpu_test_body_ms=%.3f\n",
                    elapsedMs(testStart,Clock::now()),
                    elapsedMs(deviceReady,Clock::now()));
        if (failures)
            std::fprintf(stderr,"FAIL: %d Metal texture GPU acceptance checks\n",failures);
        else std::puts("PASS: Metal GPU texture subresources, fragment sampling, VT feedback/page table and async readback parity");
        return failures ? 1 : 0;
    }
}
