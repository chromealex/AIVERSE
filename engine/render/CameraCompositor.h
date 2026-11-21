// AIVERSE LENS screen compositor: split-screen, overlays and picture-in-picture.
// All sources are previously finished offscreen color targets. One screen pass,
// one clear, ordered sub-viewport rasterization. No RTS/FPS/editor dependencies.
#pragma once
#include "../gfx/Gfx.h"
#include "../sim/CameraViewportComponents.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aiverse::render {
struct CameraCompositeLayer {
    gfx::TextureHandle color{};
    gfx::Rect viewport{};
    int order=0;
    float opacity=1.f;
    uint64_t id=0;
};
inline gfx::Rect cameraViewportRect(const CameraViewportComponent& v,int w,int h) {
    if(w<=0||h<=0)return {};
    if(!v.enabled)return {0,0,w,h};
    const float x=float(v.x.toDouble()),y=float(v.y.toDouble());
    const float width=float(v.width.toDouble()),height=float(v.height.toDouble());
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height) ||
       x<0||y<0||width<=0||height<=0||x+width>1.00001f||y+height>1.00001f) return {};
    const int left=std::clamp(int(std::lround(x*w)),0,w);
    const int top=std::clamp(int(std::lround(y*h)),0,h);
    const int right=std::clamp(int(std::lround((x+width)*w)),left,w);
    const int bottom=std::clamp(int(std::lround((y+height)*h)),top,h);
    return {left,top,right-left,bottom-top};
}
class CameraCompositor {
    gfx::Device* device_=nullptr;
    gfx::BufferHandle vb_{};
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipe_{};
public:
    bool init(gfx::Device& device) {
        if(device_ && device_!=&device)return false;
        if(pipe_.valid())return true;
        device_=&device;
        const float triangle[]={-1,-1,3,-1,-1,3};
        gfx::BufferDesc bd;bd.data=triangle;bd.size=sizeof(triangle);bd.label="lens compositor triangle";
        vb_=device.createBuffer(bd);
        gfx::ShaderDesc shader;
        shader.textureSlots=1;
        shader.uniformSlots=1;
        shader.label="lens compositor";
        shader.glslVertex=R"(
layout(location=0) in vec2 a_pos;
out vec2 uv;
void main(){uv=a_pos*.5+.5;gl_Position=vec4(a_pos,0.,1.);}
)";
        shader.glslFragment=R"(
layout(std140) uniform U0 {vec4 params;};
uniform sampler2D tex0;
in vec2 uv;out vec4 outColor;
void main(){vec2 p=vec2(uv.x,params.x>.5?1.-uv.y:uv.y);
vec4 color=texture(tex0,p);outColor=vec4(color.rgb,color.a*params.y);}
)";
        shader.msl=R"(
#include <metal_stdlib>
using namespace metal;
struct In{float2 pos [[attribute(0)]];};
struct Out{float4 pos [[position]];float2 uv;};
vertex Out vs_main(In v [[stage_in]]){Out o;o.pos=float4(v.pos,0,1);o.uv=v.pos*.5+.5;return o;}
fragment float4 fs_main(Out v [[stage_in]],constant float4& params [[buffer(1)]],
texture2d<float> source [[texture(0)]],sampler smp [[sampler(0)]]) {
float2 p=float2(v.uv.x,params.x>.5?1.-v.uv.y:v.uv.y);
float4 color=source.sample(smp,p);return float4(color.rgb,color.a*params.y);
}
)";
        shader_=device.createShader(shader);
        if(!shader_.valid()){shutdownAfterGpuIdle();return false;}
        gfx::PipelineDesc pd;
        pd.shader=shader_;pd.attrs[0]={0,gfx::VertexFormat::Float2,0};
        pd.attrCount=1;pd.stride=8;pd.indexType=gfx::IndexType::None;
        pd.depthTest=pd.depthWrite=false;pd.cull=gfx::CullMode::None;
        pd.blend=gfx::BlendMode::Alpha;pd.label="lens compositor alpha";
        pipe_=device.createPipeline(pd);
        if(!pipe_.valid()){shutdownAfterGpuIdle();return false;}
        return vb_.valid();
    }
    bool composite(const std::vector<CameraCompositeLayer>& input,int width,int height) {
        if(!device_||!pipe_.valid()||width<=0||height<=0)return false;
        auto layers=input;
        std::stable_sort(layers.begin(),layers.end(),[](const auto& a,const auto& b){
            return a.order!=b.order?a.order<b.order:a.id<b.id;
        });
        gfx::PassDesc pass;
        pass.clearColorEnabled=true;
        pass.clearDepthEnabled=false;
        device_->beginPass(pass);
        device_->setScissor({});
        device_->setPipeline(pipe_);
        device_->setVertexBuffer(vb_);
        for(const auto& layer:layers) {
            if(!layer.color.valid()||layer.viewport.w<=0||layer.viewport.h<=0)continue;
            device_->setViewport(layer.viewport);
            const float uniform[4]={device_->info().renderTargetFlipY?1.f:0.f,
                                    std::clamp(layer.opacity,0.f,1.f),0.f,0.f};
            device_->setUniforms(0,uniform,sizeof uniform);
            device_->setTexture(0,layer.color);
            device_->draw(3);
        }
        device_->endPass();
        return device_->gpuHealthy();
    }
    void shutdownAfterGpuIdle() {
        if(!device_)return;
        if(pipe_.valid())device_->destroyPipeline(pipe_);
        if(shader_.valid())device_->destroyShader(shader_);
        if(vb_.valid())device_->destroyBuffer(vb_);
        vb_={};shader_={};pipe_={};device_=nullptr;
    }
};
} // namespace aiverse::render
