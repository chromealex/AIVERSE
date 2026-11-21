#include "engine/render/LocalFog.h"
#include "engine/render/Renderer3D.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::gfx;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);std::exit(1);}}while(0)
struct DeviceProbe final:Device {
    DeviceInfo deviceInfo;std::string error="injected failure",vs,fs,msl;
    float weatherX=0,weatherZ=0;
    int draws=0,shaders=0,destroyed=0;bool fail=false,open=false;
    TextureHandle attachment,depthAttachment;
    const DeviceInfo& info()const override{return deviceInfo;}
    const std::string& lastError()const override{return error;}
    BufferHandle createBuffer(const BufferDesc&)override{return {1};}
    void updateBuffer(BufferHandle,const void*,size_t,size_t)override{}
    void destroyBuffer(BufferHandle)override{}
    TextureHandle createTexture(const TextureDesc&)override{return {1};}
    void updateTexture(TextureHandle,int,int,int,int,const void*,int)override{}
    void destroyTexture(TextureHandle)override{}
    ShaderHandle createShader(const ShaderDesc& s)override{++shaders;vs=s.glslVertex;fs=s.glslFragment;msl=s.msl;return fail?ShaderHandle{}:ShaderHandle{1};}
    void destroyShader(ShaderHandle)override{++destroyed;}
    PipelineHandle createPipeline(const PipelineDesc& p)override{CHECK(p.colorFormat==PixelFormat::RGBA16F);CHECK(!p.depthWrite);return {1};}
    void destroyPipeline(PipelineHandle)override{++destroyed;}
    void beginFrame(int,int)override{}
    void endFrame()override{}
    void beginPass(const PassDesc& p)override{CHECK(!open);open=true;attachment=p.colorTarget;depthAttachment=p.depthTarget;}
    void endPass()override{CHECK(open);open=false;}
    void setViewport(const Rect&)override{}
    void setScissor(const Rect&)override{}
    void setPipeline(PipelineHandle)override{}
    void setVertexBuffer(BufferHandle,size_t)override{}
    void setIndexBuffer(BufferHandle,size_t)override{}
    void setTexture(int,TextureHandle t)override{CHECK(t.valid()&&t!=attachment&&t!=depthAttachment);}
    void setUniforms(int,const void* ptr,size_t bytes)override{CHECK(bytes==2256);const auto* f=static_cast<const float*>(ptr);for(size_t i=0;i<bytes/4;++i)CHECK(std::isfinite(f[i]));weatherX=f[56];weatherZ=f[57];}
    void draw(uint32_t count,uint32_t)override{CHECK(count==3&&open);++draws;}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t)override{}
};
int main(int argc,char** argv){
    LocalFogScene scene;FogVolume v;v.id=1;v.region.radius=4;v.region.feather=1;v.density=.2f;
    CHECK(scene.upsert(v));CHECK(std::abs(scene.densityAt({0,0,0})-.2f)<1e-6);CHECK(scene.densityAt({5,0,0})==0);
    CHECK(std::abs(scene.densityAt({3.5f,0,0})-.1f)<1e-6);
    FogReveal r;r.id=1;r.region.radius=2;r.region.feather=.5f;CHECK(scene.upsert(r));CHECK(scene.densityAt({0,0,0})==0);
    r.strength=.5f;CHECK(scene.upsert(r));CHECK(std::abs(scene.densityAt({0,0,0})-.1f)<1e-6);
    FogReveal duplicate=r;duplicate.id=2;CHECK(scene.upsert(duplicate));CHECK(std::abs(scene.densityAt({0,0,0})-.1f)<1e-6);
    CHECK(scene.removeReveal(2));r.region.center={10,0,0};CHECK(scene.upsert(r));CHECK(std::abs(scene.densityAt({0,0,0})-.2f)<1e-6);
    CHECK(scene.removeReveal(1));CHECK(!scene.removeReveal(0));
    v.region.shape=FogShape::Obb;v.region.halfExtents={3,1,1};v.region.feather=0;
    v.region.orientation={0,0,.70710678f,.70710678f};CHECK(scene.upsert(v));CHECK(scene.densityAt({0,2,0})>.19f);CHECK(scene.densityAt({2,0,0})==0);
    auto bad=v;bad.region.orientation={0,0,0,0};CHECK(!scene.upsert(bad));CHECK(scene.densityAt({0,2,0})>.19f);
    bad=v;bad.region.center.x=std::numeric_limits<float>::quiet_NaN();CHECK(!scene.upsert(bad));
    for(uint32_t id=2;id<=8;++id){v.id=id;CHECK(scene.upsert(v));}v.id=9;CHECK(!scene.upsert(v));
    CHECK(scene.removeVolume(4));CHECK(scene.upsert(v));scene.clear();CHECK(scene.densityAt({0,0,0})==0);
    // Heterogeneous banks stay bounded; vertical profile and wind have world-space semantics.
    FogVolume bank;bank.id=1;bank.region.radius=100;bank.region.feather=0;bank.density=.4f;
    bank.structure.amount=1;bank.structure.scale={6,1,4};bank.structure.wind={2,0,0};
    CHECK(scene.upsert(bank));float lo=1,hi=0;
    for(int i=-20;i<20;++i){float den=scene.densityAt({float(i)*.3f,0,1});CHECK(den>=0&&den<=bank.density);lo=std::min(lo,den);hi=std::max(hi,den);}
    CHECK(hi-lo>.05f);
    bank.structure.heightVariation=3;bank.structure.warp=.8f;CHECK(scene.upsert(bank));
    CHECK(std::abs(scene.densityAt({4,0,1},2)-scene.densityAt({0,0,1},0))<1e-6f);
    CHECK(std::abs(scene.densityAt({0,0,1},768)-scene.densityAt({0,0,1},0))<1e-6f);
    CHECK(std::isfinite(scene.densityAt({0,0,1},1e300)));
    auto invalid=bank;invalid.structure.scale.y=0;CHECK(!scene.upsert(invalid));
    invalid=bank;invalid.structure.edgeErosion=2;CHECK(!scene.upsert(invalid));
    invalid=bank;invalid.structure.heightVariation=-1;CHECK(!scene.upsert(invalid));
    invalid=bank;invalid.structure.warp=std::numeric_limits<float>::quiet_NaN();CHECK(!scene.upsert(invalid));
    invalid=bank;invalid.structure.wind.x=std::numeric_limits<float>::infinity();CHECK(!scene.upsert(invalid));
    bank.structure.amount=0;bank.structure.heightVariation=0;bank.structure.heightFalloff=1;CHECK(scene.upsert(bank));
    CHECK(std::abs(scene.densityAt({0,1,0})-.4f/std::exp(1.f))<1e-6f);
    r.region.radius=100;r.region.center={};r.strength=1;CHECK(scene.upsert(r));CHECK(scene.densityAt({0,1,0},42)==0);
    CHECK(scene.removeReveal(r.id));CHECK(scene.densityAt({0,1,0},42)>0);scene.clear();
    FogVolume edgeBank;edgeBank.id=1;edgeBank.region.radius=5;edgeBank.region.feather=2;
    edgeBank.structure.amount=1;edgeBank.structure.coverage=1;edgeBank.structure.scale={2,2,2};
    float removed=0;
    for(int i=0;i<20;++i){
        const float3 point{3.05f+float(i)*.09f,0,0};
        edgeBank.structure.edgeErosion=0;CHECK(scene.upsert(edgeBank));const float before=scene.densityAt(point);
        edgeBank.structure.edgeErosion=1;CHECK(scene.upsert(edgeBank));const float after=scene.densityAt(point);
        CHECK(after<=before+1e-7f);removed+=before-after;
    }
    CHECK(removed>.001f);CHECK(scene.densityAt({6,0,0})==0);scene.clear();
    DeviceProbe device;LocalFog fog;PostFrame frame;frame.width=640;frame.height=360;LocalFogSettings settings;settings.enabled=true;
    auto render=[&](TextureHandle dst,int quality=3){return fog.draw(device,frame,settings,scene,quality,{1},{2},dst,{4},{1});};
    CHECK(!render({3}));v.id=1;CHECK(scene.upsert(v));CHECK(!render({1}));CHECK(!render({3},1));CHECK(device.shaders==0);
    CHECK(render({3}));CHECK(render({3}));CHECK(device.shaders==1&&device.draws==2);
    if(argc==2){const std::string base=argv[1];std::ofstream(base+".vert")<<"#version 330 core\n"<<device.vs;std::ofstream(base+".frag")<<"#version 330 core\n"<<device.fs;std::ofstream(base+".metal")<<device.msl;}
    fog.shutdown(device);CHECK(device.destroyed==2);device.fail=true;CHECK(!render({3}));CHECK(!fog.lastError().empty());
    const int attempts=device.shaders;CHECK(!render({3}));CHECK(device.shaders==attempts);
    fog.shutdown(device);device.fail=false;CHECK(render({3}));fog.shutdown(device);
    std::puts("Local fog regions, runtime reveal, bounds and GPU lifetime checks passed");
}
