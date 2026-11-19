// AIVERSE LENS: six 2D cubemap face textures -> equirectangular GPU RT.
// Expects face order +X,-X,+Y,-Y,+Z,-Z, each authored with a 90-degree FOV.
// No CPU readback or compression; use completed capture + optional exporter.
#pragma once
#include "../gfx/Gfx.h"
#include <array>
#include <cstdint>
namespace aiverse::render {
class CameraPanorama {
    gfx::Device* device_=nullptr;
    gfx::BufferHandle vertex_{};
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
public:
    bool init(gfx::Device& device) {
        if(device_&&device_!=&device)return false;
        if(pipeline_.valid())return true;
        device_=&device;
        const float v[]={-1,-1,3,-1,-1,3};
        gfx::BufferDesc bd;bd.data=v;bd.size=sizeof v;bd.label="lens panorama quad";
        vertex_=device.createBuffer(bd);
        gfx::ShaderDesc shader;shader.textureSlots=6;shader.uniformSlots=1;
        shader.label="lens panorama";
        shader.glslVertex=R"(
layout(location=0) in vec2 a_pos;out vec2 uv;
void main(){uv=a_pos*.5+.5;gl_Position=vec4(a_pos,0,1);}
)";
        shader.glslFragment=R"(
layout(std140) uniform U0{vec4 params;};
uniform sampler2D tex0;uniform sampler2D tex1;uniform sampler2D tex2;
uniform sampler2D tex3;uniform sampler2D tex4;uniform sampler2D tex5;
in vec2 uv;out vec4 outColor;
void main(){
float lon=(uv.x-.5)*6.28318530718;
float lat=(.5-uv.y)*3.14159265359;
vec3 dir=vec3(sin(lon)*cos(lat),sin(lat),-cos(lon)*cos(lat));
vec3 a=abs(dir);vec2 t;int f;
if(a.x>=a.y&&a.x>=a.z){
 if(dir.x>=0.){f=0;t=vec2(dir.z,dir.y)/a.x;}
 else{f=1;t=vec2(-dir.z,dir.y)/a.x;}
}else if(a.y>=a.z){
 if(dir.y>=0.){f=2;t=vec2(dir.x,dir.z)/a.y;}
 else{f=3;t=vec2(dir.x,-dir.z)/a.y;}
}else{
 if(dir.z>=0.){f=4;t=vec2(-dir.x,dir.y)/a.z;}
 else{f=5;t=vec2(dir.x,dir.y)/a.z;}
}
t=t*.5+.5;if(params.x>.5)t.y=1.-t.y;
if(f==0)outColor=texture(tex0,t);else if(f==1)outColor=texture(tex1,t);
else if(f==2)outColor=texture(tex2,t);else if(f==3)outColor=texture(tex3,t);
else if(f==4)outColor=texture(tex4,t);else outColor=texture(tex5,t);
}
)";
        shader.msl=R"(
#include <metal_stdlib>
using namespace metal;
struct In{float2 p [[attribute(0)]];};
struct Out{float4 pos [[position]];float2 uv;};
vertex Out vs_main(In in [[stage_in]]){Out o;o.pos=float4(in.p,0,1);o.uv=in.p*.5+.5;return o;}
fragment float4 fs_main(Out in [[stage_in]],constant float4& params [[buffer(1)]],
texture2d<float> tex0 [[texture(0)]],texture2d<float> tex1 [[texture(1)]],
texture2d<float> tex2 [[texture(2)]],texture2d<float> tex3 [[texture(3)]],
texture2d<float> tex4 [[texture(4)]],texture2d<float> tex5 [[texture(5)]],
sampler smp [[sampler(0)]]) {
float lon=(in.uv.x-.5f)*6.28318530718f,lat=(.5f-in.uv.y)*3.14159265359f;
float3 dir=float3(sin(lon)*cos(lat),sin(lat),-cos(lon)*cos(lat));
float3 a=abs(dir);float2 t;int face;
if(a.x>=a.y&&a.x>=a.z){
 if(dir.x>=0.f){face=0;t=float2(dir.z,dir.y)/a.x;}
 else{face=1;t=float2(-dir.z,dir.y)/a.x;}
}else if(a.y>=a.z){
 if(dir.y>=0.f){face=2;t=float2(dir.x,dir.z)/a.y;}
 else{face=3;t=float2(dir.x,-dir.z)/a.y;}
}else{
 if(dir.z>=0.f){face=4;t=float2(-dir.x,dir.y)/a.z;}
 else{face=5;t=float2(dir.x,dir.y)/a.z;}
}
t=t*.5f+.5f;if(params.x>.5f)t.y=1.f-t.y;
if(face==0)return tex0.sample(smp,t);if(face==1)return tex1.sample(smp,t);
if(face==2)return tex2.sample(smp,t);if(face==3)return tex3.sample(smp,t);
if(face==4)return tex4.sample(smp,t);return tex5.sample(smp,t);
}
)";
        shader_=device.createShader(shader);
        if(!shader_.valid()){shutdownAfterGpuIdle();return false;}
        gfx::PipelineDesc pd;pd.shader=shader_;
        pd.attrs[0]={0,gfx::VertexFormat::Float2,0};pd.attrCount=1;pd.stride=8;
        pd.indexType=gfx::IndexType::None;pd.cull=gfx::CullMode::None;
        pd.depthTest=pd.depthWrite=false;pd.label="lens panorama";
        pipeline_=device.createPipeline(pd);
        if(!pipeline_.valid()){shutdownAfterGpuIdle();return false;}
        return vertex_.valid();
    }
    bool render(const std::array<gfx::TextureHandle,6>& faces,
                gfx::TextureHandle destination,int width,int height) {
        if(!device_||!destination.valid()||!pipeline_.valid()||width<=0||height<=0)
            return false;
        for(const auto f:faces)if(!f.valid()||f==destination)return false;
        gfx::PassDesc pass;pass.colorTarget=destination;
        pass.clearDepthEnabled=false;
        device_->beginPass(pass);device_->setViewport({0,0,width,height});
        device_->setScissor({});device_->setPipeline(pipeline_);
        const float uniform[4]={device_->info().renderTargetFlipY?1.f:0.f,0,0,0};
        device_->setUniforms(0,uniform,sizeof uniform);
        for(int i=0;i<6;++i)device_->setTexture(i,faces[size_t(i)]);
        device_->setVertexBuffer(vertex_);device_->draw(3);
        device_->endPass();return device_->gpuHealthy();
    }
    void shutdownAfterGpuIdle() {
        if(!device_)return;
        if(pipeline_.valid())device_->destroyPipeline(pipeline_);
        if(shader_.valid())device_->destroyShader(shader_);
        if(vertex_.valid())device_->destroyBuffer(vertex_);
        pipeline_={};shader_={};vertex_={};device_=nullptr;
    }
};
} // namespace aiverse::render
