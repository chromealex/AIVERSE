#pragma once
#include "modules/rts/view/RtsPortrait.h"
#include "engine/render/PostFx.h"
#include "engine/fx/ParticleSystem.h"

namespace aiverse::editor {
// Uses the real material renderer, post-processing composite and particle renderer.
class ShaderGraphPreview {
public:
    ~ShaderGraphPreview() { shutdown(); }
    void shutdown() {
        if(!dev_)return;
        portrait_.shutdown();post_.shutdown();particles_.shutdown();
        dev_->destroyTexture(color_);dev_->destroyTexture(depth_);dev_->destroyBuffer(vb_);
        dev_->destroyPipeline(pipe_);dev_->destroyShader(shader_);dev_=nullptr;key_.clear();error.clear();
    }
    gfx::TextureHandle render(Editor& ed,const Json& g,double time,float dt) {
        if(!dev_ && !init(ed.device()))return {};
        const auto domain=g.getStr("domain");
        const std::string key=g.dump(-1);
        if(key!=key_) {
            key_=key;
            if(domain=="material") {portrait_.shutdown();portrait_.init(*dev_);}
            if(domain=="postfx") {
                Json e=Json::object();e.set("type",Json::string("shaderGraph"));e.set("graph",g);
                Json b=Json::object(),a=Json::array();a.push(e);b.set("effects",a);post_.configure(b,{});
            } else if(domain=="particle") {
                particles_.reloadAll();particles_.clearErrors();
                fx::EffectDef def;fx::EmitterDef em;em.shaderGraph=g;em.loop=true;em.rate={20,20};em.duration=2;
                em.life={1.2f,1.2f};em.speed={0.5f,1};em.size={0.25f,0.35f};def.emitters.push_back(em);
                particles_.play(particles_.define("shader-preview",def),{0,0,0});
            }
        }
        if(domain=="material") {
            Json m=Json::object();m.set("shaderGraph",g);portrait_.renderMaterial(ed.view(),m,0,time);error=portrait_.lastError();return portrait_.texture();
        }
        gfx::PassDesc out;out.colorTarget=color_;out.depthTarget=depth_;
        if(domain=="postfx") {
            portrait_.renderMaterial(ed.view(),Json::object(),0,time);
            dev_->beginPass(post_.scenePass(out,256,256));dev_->setViewport({0,0,256,256});
            dev_->setPipeline(pipe_);dev_->setVertexBuffer(vb_);dev_->setTexture(0,portrait_.texture());
            float u[4]={dev_->info().renderTargetFlipY?1.f:0.f,0,0,0};dev_->setUniforms(0,u,sizeof u);dev_->draw(3);dev_->endPass();
            render::PostFrame f;f.width=f.height=256;post_.resolve(*dev_,out,f);dev_->endPass();
            error=post_.lastError();
        } else {
            render::RTSCamera cam;cam.target={0,0.3f,0};cam.distance=3;cam.minDistance=0.1f;
            const auto vp=cam.proj(1.f,dev_->info().clipDepthZeroToOne)*cam.view();
            particles_.setView(cam,vp,256,256);particles_.update(dt);
            dev_->beginPass(out);dev_->setViewport({0,0,256,256});particles_.render({});dev_->endPass();
            error=particles_.lastError();
            if(!particles_.errors().empty())error=particles_.errors().back();
        }
        return color_;
    }
    std::string error;
private:
    bool init(gfx::Device& dev) {
        dev_=&dev;if(!portrait_.init(dev)||!post_.init(dev)||!particles_.init(&dev)) {error=dev.lastError();shutdown();return false;}
        post_.setQuality(render::PostFxQuality::High);
        gfx::TextureDesc td;td.width=td.height=256;td.renderTarget=true;color_=dev.createTexture(td);td.format=gfx::PixelFormat::Depth;depth_=dev.createTexture(td);
        const float vertices[]={-1,-1,3,-1,-1,3};gfx::BufferDesc bd;bd.data=vertices;bd.size=sizeof vertices;vb_=dev.createBuffer(bd);
        gfx::ShaderDesc sd;sd.textureSlots=1;
        sd.glslVertex="layout(location=0) in vec2 p;out vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);}";
        sd.glslFragment="layout(std140) uniform U0{vec4 p;};uniform sampler2D tex0;in vec2 uv;out vec4 c;void main(){c=texture(tex0,vec2(uv.x,p.x>0.5?1.-uv.y:uv.y));}";
        sd.msl=R"(#include <metal_stdlib>
using namespace metal;
struct V{float2 p [[attribute(0)]];};struct O{float4 p [[position]];float2 uv;};
vertex O vs_main(V v [[stage_in]]){O o;o.p=float4(v.p,0,1);o.uv=v.p*.5+.5;return o;}
fragment float4 fs_main(O o [[stage_in]],constant float4& p [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s [[sampler(0)]]){return tex0.sample(s,float2(o.uv.x,p.x>0.5?1.-o.uv.y:o.uv.y));})";
        shader_=dev.createShader(sd);gfx::PipelineDesc pd;pd.shader=shader_;pd.attrs[0]={0,gfx::VertexFormat::Float2,0};pd.attrCount=1;pd.stride=8;pd.indexType=gfx::IndexType::None;pd.depthTest=pd.depthWrite=false;pd.cull=gfx::CullMode::None;pipe_=dev.createPipeline(pd);
        if(!pipe_.valid()){error=dev.lastError();shutdown();return false;}return true;
    }
    gfx::Device* dev_=nullptr;game::Portrait portrait_;render::PostFx post_;fx::ParticleSystem particles_;
    gfx::TextureHandle color_,depth_;gfx::ShaderHandle shader_;gfx::PipelineHandle pipe_;gfx::BufferHandle vb_;std::string key_;
};
}
