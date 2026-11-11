#include "ScreenLighting.h"
#include "ScreenLightingShaders.h"
#include <algorithm>
#include <cmath>

namespace aiverse::render {
namespace {
struct Uniforms { mat4 invVP, prevVP; float4 eye, prevEye, px, params; };
float halton(int index, int base) {
    float value=0, fraction=1;
    while(index>0){fraction/=float(base);value+=fraction*float(index%base);index/=base;}
    return value;
}
struct Source { std::string glslVertex,glslFragment,msl; };
Source shader(const char* body) {
    const std::string fields="mat4 invVP; mat4 prevVP; vec4 eye; vec4 prevEye; vec4 px; vec4 params;";
    const std::string common=screenlighting::common;
    Source s;
    s.glslVertex="layout(location=0) in vec2 a_pos; out vec2 v_screen; void main(){v_screen=a_pos*0.5+0.5;gl_Position=vec4(a_pos,0.0,1.0);}";
    s.glslFragment="layout(std140) uniform U0 {"+fields+"};\nin vec2 v_screen;out vec4 o_color;\nuniform sampler2D tex0;uniform highp sampler2D tex1;uniform sampler2D tex2;\n"
        "#define COLOR(p) texture(tex0,p)\n#define DEPTH(p) texture(tex1,p).r\n#define HISTORY(p) texture(tex2,p)\n#define RETURN(c) {o_color=(c);return;}\n"+common+"void main(){vec2 s=v_screen;\n"+body+"}\n";
    s.msl="#include <metal_stdlib>\nusing namespace metal;\n#define mat4 float4x4\n#define vec2 float2\n#define vec3 float3\n#define vec4 float4\nstruct U0 {"+fields+"};\n"
        "struct VIn{float2 pos [[attribute(0)]];}; struct VOut{float4 pos [[position]];float2 screen;};\n"
        "vertex VOut vs_main(VIn in [[stage_in]]){VOut o;o.pos=float4(in.pos,0.0,1.0);o.screen=in.pos*0.5+0.5;return o;}\n"
        "#define COLOR(p) tex0.sample(s0,p)\n#define DEPTH(p) tex1.sample(s1,p)\n#define HISTORY(p) tex2.sample(s2,p)\n#define RETURN(c) return (c)\n"+common+
        "fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],depth2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]],texture2d<float> tex2 [[texture(2)]],sampler s2 [[sampler(2)]]){\n"
        "const mat4 invVP=u.invVP,prevVP=u.prevVP;const vec4 eye=u.eye,prevEye=u.prevEye,px=u.px,params=u.params;vec2 s=in.screen;\n"+body+"}\n";
    return s;
}
}
bool ScreenLighting::init(gfx::Device& d, PostFx& post) {
    dev_=&d;post_=&post;
    const float tri[]={-1,-1,3,-1,-1,3};gfx::BufferDesc vb;vb.data=tri;vb.size=sizeof tri;vb.label="screen lighting";
    triangle_=d.createBuffer(vb);
    const char* bodies[]={screenlighting::bounce,screenlighting::temporal,screenlighting::copy};
    for(int i=0;i<3;++i) {
        auto source=shader(bodies[i]);gfx::ShaderDesc desc;desc.uniformSlots=1;desc.textureSlots=3;desc.label="screen lighting";
        desc.glslVertex=source.glslVertex.c_str();desc.glslFragment=source.glslFragment.c_str();desc.msl=source.msl.c_str();
        shaders_[i]=d.createShader(desc);
        if(!shaders_[i].valid()){error_=d.lastError();return false;}
        gfx::PipelineDesc p;p.shader=shaders_[i];p.attrs[0]={0,gfx::VertexFormat::Float2,0};p.attrCount=1;p.stride=8;
        p.indexType=gfx::IndexType::None;p.cull=gfx::CullMode::None;p.depthTest=p.depthWrite=false;p.label="screen lighting";
        pipes_[i]=d.createPipeline(p);
        if(!pipes_[i].valid()){error_=d.lastError();return false;}
    }
    return triangle_.valid();
}
void ScreenLighting::releaseTargets() {
    for(auto* t:{&bounce_,&history_[0],&history_[1],&depth_}){if(t->valid())dev_->destroyTexture(*t);*t={};}
    w_=h_=0;reset();
}
void ScreenLighting::shutdown() {
    if(!dev_)return;
    releaseTargets();
    for(auto p:pipes_)if(p.valid())dev_->destroyPipeline(p);
    for(auto s:shaders_)if(s.valid())dev_->destroyShader(s);
    if(triangle_.valid())dev_->destroyBuffer(triangle_);
    dev_=nullptr;post_=nullptr;
}
RTSCamera ScreenLighting::jittered(RTSCamera c,int w,int h) {
    if(enabled_&&post_&&post_->hdrRequested()&&post_->quality()==PostFxQuality::High&&w>0&&h>0) {
        int index=(sequence_++%8)+1;c.jitter={2*(halton(index,2)-0.5f)/float(w),2*(halton(index,3)-0.5f)/float(h)};
    }
    return c;
}
gfx::PassDesc ScreenLighting::scenePass(const gfx::PassDesc& out,int w,int h) {
    scene_=post_->scenePass(out,w,h);
    running_=enabled_&&post_->linearScene()&&post_->quality()==PostFxQuality::High;
    if(!running_){reset();return scene_;}
    if(w_!=w||h_!=h){
        releaseTargets();w_=w;h_=h;
        gfx::TextureDesc t;t.width=w;t.height=h;t.renderTarget=true;t.filter=gfx::Filter::Linear;t.format=gfx::PixelFormat::RGBA16F;t.label="screen lighting HDR";
        bounce_=dev_->createTexture(t);history_[0]=dev_->createTexture(t);history_[1]=dev_->createTexture(t);
        t.format=gfx::PixelFormat::Depth;t.filter=gfx::Filter::Nearest;depth_=dev_->createTexture(t);
        if(!bounce_.valid()||!history_[0].valid()||!history_[1].valid()||!depth_.valid()){
            error_="screen lighting allocation failed";running_=false;releaseTargets();
        }
    }
    return scene_;
}
void ScreenLighting::resolve(gfx::Device& d,const gfx::PassDesc& out,const PostFrame& f) {
    if(running_) {
        if(length(f.cameraPos-previousEye_)>2.0f)valid_=false;
        Uniforms u{f.viewProj.inverse(),previous_,{f.cameraPos.x,f.cameraPos.y,f.cameraPos.z,0},
            {previousEye_.x,previousEye_.y,previousEye_.z,0},
            {1.0f/float(w_),1.0f/float(h_),d.info().renderTargetFlipY?1.0f:0.0f,d.info().clipDepthZeroToOne?1.0f:2.0f},
            {valid_?1.0f:0.0f,0,0,0}};
        auto draw=[&](int program,gfx::TextureHandle target,gfx::TextureHandle source){
            gfx::PassDesc p;p.colorTarget=target;p.depthTarget=depth_;p.clearColorEnabled=false;p.clearDepthEnabled=false;
            d.beginPass(p);d.setViewport({0,0,w_,h_});d.setPipeline(pipes_[program]);d.setUniforms(0,&u,sizeof u);
            d.setTexture(0,source);d.setTexture(1,scene_.depthTarget);d.setTexture(2,history_[1-current_]);
            d.setVertexBuffer(triangle_);d.draw(3);d.endPass();
        };
        if(diffuseBounce_)draw(0,bounce_,scene_.colorTarget);
        draw(1,history_[current_],diffuseBounce_?bounce_:scene_.colorTarget);draw(2,scene_.colorTarget,history_[current_]);
        previous_=f.viewProj;previousEye_=f.cameraPos;valid_=true;current_=1-current_;
    }
    post_->resolve(d,out,f);
}
}
