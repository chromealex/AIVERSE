#include "engine/render/PhysicalSky.h"
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
    void setUniforms(int,const void* ptr,size_t bytes)override{CHECK(bytes==272);const auto* f=static_cast<const float*>(ptr);for(size_t i=0;i<bytes/4;++i)CHECK(std::isfinite(f[i]));weatherX=f[56];weatherZ=f[57];}
    void draw(uint32_t count,uint32_t)override{CHECK(count==3&&open);++draws;}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t)override{}
};
int main(int argc,char** argv){
    CHECK(!parsePhysicalSky({},{}).enabled);
    std::vector<std::string> errors;
    auto s=parsePhysicalSky(Json::parse(R"({"physicalSky":{"enabled":true,"coverage":0.7,"windX":4}})"),Json::parse(R"({"physicalSky":{"coverage":0,"godRays":false}})"),&errors);
    CHECK(s.enabled&&!s.godRays&&s.coverage==0&&s.windX==4&&errors.empty());
    parsePhysicalSky(Json::parse(R"({"physicalSky":{"enabled":1,"coverage":"bad","typo":0}})"),{},&errors);CHECK(errors.size()==3);
    s.cloudThickness=0;s.coverage=2;s.extinction=std::numeric_limits<float>::quiet_NaN();
    s=sanitizePhysicalSky(s);CHECK(s.cloudThickness==100&&s.coverage==1&&std::isfinite(s.extinction));
    CHECK(physicalSkyBudget(0).cloudSteps==0);CHECK(physicalSkyBudget(2).cloudSteps<physicalSkyBudget(3).cloudSteps);
    DeviceProbe device;PhysicalSky sky;PostFrame frame;frame.width=640;frame.height=360;
    auto render=[&](TextureHandle dst){return sky.draw(device,frame,s,3,12,{1},{2},dst,{4},{1});};
    CHECK(!render({1}));CHECK(device.shaders==0); // rejects texture feedback before creating resources
    s.enabled=false;CHECK(!render({3}));CHECK(device.shaders==0);
    s.enabled=true;CHECK(render({3}));CHECK(device.draws==1&&!device.open);
    CHECK(render({3}));CHECK(device.shaders==1); // pipeline reuse
    if(argc==2){const std::string base=argv[1];std::ofstream(base+".vert")<<"#version 330 core\n"<<device.vs;std::ofstream(base+".frag")<<"#version 330 core\n"<<device.fs;std::ofstream(base+".metal")<<device.msl;}
    const float2 displacement{12,-8};
    CHECK(sky.draw(device,frame,s,3,999,{1},{2},{3},{4},{1},&displacement));
    CHECK(device.weatherX==-12&&device.weatherZ==8); // positive world advection
    CHECK(render({3}));CHECK(device.weatherX==s.windX*12); // legacy clock unchanged
    sky.shutdown(device);CHECK(device.destroyed==2);
    device.fail=true;CHECK(!render({3}));CHECK(!sky.lastError().empty());
    const int attempts=device.shaders;CHECK(!render({3}));CHECK(device.shaders==attempts);
    sky.shutdown(device);device.fail=false;CHECK(render({3}));sky.shutdown(device);
    std::puts("Physical sky configuration, quality, GPU lifetime and failure checks passed");
}
