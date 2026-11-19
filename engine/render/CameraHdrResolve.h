// AIVERSE LENS: explicitly resolve linear HDR RGBA16F to display-ready
// RGBA8 UNORM via GPU. The caller provides the source/output formats.
// No implicit reinterpretation of half-float readback bytes.
#pragma once
#include "../gfx/Gfx.h"
#include <algorithm>
#include <cmath>

namespace aiverse::render {

struct CameraHdrResolveSettings {
    float exposureEV=0.f; // exposure multiplier = 2^EV
};

inline bool validCameraHdrResolveSettings(const CameraHdrResolveSettings& s) {
    return std::isfinite(s.exposureEV) && s.exposureEV>=-16.f &&
           s.exposureEV<=16.f;
}

// CPU reference for validating GPU shader values (linear-channel input).
inline float cameraHdrToSrgb(float linear,float exposureEV) {
    const float x=std::max(0.f,linear*std::exp2(exposureEV));
    const float mapped=x/(1.f+x);
    return std::clamp(mapped<=.0031308f?12.92f*mapped:
                      1.055f*std::pow(mapped,1.f/2.4f)-.055f,0.f,1.f);
}

// Linear HDR -> photographic Reinhard -> sRGB transfer -> RGBA8 UNORM.
// No automatic exposure or calibrated display HDR metadata is implied.
class CameraHdrResolve {
    gfx::Device* device_=nullptr;
    gfx::BufferHandle vertex_{};
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
public:
    bool init(gfx::Device& device) {
        if(device_ && device_!=&device) return false;
        if(pipeline_.valid()) return true;
        device_=&device;
        const float triangle[]={-1.f,-1.f,3.f,-1.f,-1.f,3.f};
        gfx::BufferDesc vb;vb.data=triangle;vb.size=sizeof(triangle);
        vb.label="lens hdr resolve triangle";
        vertex_=device.createBuffer(vb);
        if(!vertex_.valid()){shutdownAfterGpuIdle();return false;}
        gfx::ShaderDesc shader;
        shader.label="lens hdr resolve";shader.textureSlots=1;shader.uniformSlots=1;
        shader.glslVertex=R"(
layout(location=0) in vec2 a_pos;
out vec2 uv;
void main(){uv=a_pos*.5+.5;gl_Position=vec4(a_pos,0.,1.);}
)";
        shader.glslFragment=R"(
layout(std140) uniform U0 {vec4 params;};
uniform sampler2D tex0;
in vec2 uv;
out vec4 outColor;
vec3 toSrgb(vec3 x) {
    return mix(12.92*x,1.055*pow(x,vec3(1./2.4))-.055,
               greaterThan(x,vec3(.0031308)));
}
void main() {
    vec2 p=vec2(uv.x,params.y>.5?1.-uv.y:uv.y);
    vec4 hdr=texture(tex0,p);
    vec3 linear=max(hdr.rgb*params.x,vec3(0.));
    vec3 mapped=linear/(1.+linear);
    outColor=vec4(clamp(toSrgb(mapped),0.,1.),clamp(hdr.a,0.,1.));
}
)";
        shader.msl=R"(
#include <metal_stdlib>
using namespace metal;
struct In{float2 p [[attribute(0)]];};
struct Out{float4 pos [[position]];float2 uv;};
vertex Out vs_main(In v [[stage_in]]) {
    Out o;o.pos=float4(v.p,0,1);o.uv=v.p*.5+ .5;return o;
}
fragment float4 fs_main(Out v [[stage_in]],constant float4& params [[buffer(1)]],
                        texture2d<float> tex0 [[texture(0)]],sampler smp [[sampler(0)]]) {
    float2 p=float2(v.uv.x,params.y>.5f?1.f-v.uv.y:v.uv.y);
    float4 hdr=tex0.sample(smp,p);
    float3 linear=max(hdr.rgb*params.x,float3(0.f));
    float3 mapped=linear/(1.f+linear);
    float3 srgb=select(1.055f*pow(mapped,float3(1.f/2.4f))-.055f,
                      12.92f*mapped,mapped<=float3(.0031308f));
    return float4(clamp(srgb,0.f,1.f),clamp(hdr.a,0.f,1.f));
}
)";
        shader_=device.createShader(shader);
        if(!shader_.valid()){shutdownAfterGpuIdle();return false;}
        gfx::PipelineDesc pd;
        pd.shader=shader_;
        pd.attrs[0]={0,gfx::VertexFormat::Float2,0};
        pd.attrCount=1;pd.stride=sizeof(float)*2;
        pd.indexType=gfx::IndexType::None;
        pd.cull=gfx::CullMode::None;
        pd.depthTest=false;pd.depthWrite=false;
        pd.label="lens hdr resolve";
        pipeline_=device.createPipeline(pd);
        if(!pipeline_.valid()){shutdownAfterGpuIdle();return false;}
        return true;
    }
    bool render(gfx::TextureHandle hdrSource,gfx::TextureHandle sdrTarget,
                int width,int height,const CameraHdrResolveSettings& settings={}) {
        if(!device_||!pipeline_.valid()||!hdrSource.valid()||!sdrTarget.valid()||
           hdrSource==sdrTarget||width<=0||height<=0||
           !validCameraHdrResolveSettings(settings)) return false;
        gfx::PassDesc pass;
        pass.colorTarget=sdrTarget;
        pass.clearDepthEnabled=false;
        device_->beginPass(pass);
        device_->setViewport({0,0,width,height});
        device_->setScissor({});
        device_->setPipeline(pipeline_);
        device_->setVertexBuffer(vertex_);
        const float uniforms[4]={std::exp2(settings.exposureEV),
            device_->info().renderTargetFlipY?1.f:0.f,0.f,0.f};
        device_->setUniforms(0,uniforms,sizeof uniforms);
        device_->setTexture(0,hdrSource);
        device_->draw(3);
        device_->endPass();
        return device_->gpuHealthy();
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
