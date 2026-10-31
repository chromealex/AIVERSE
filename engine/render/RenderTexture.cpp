#include "RenderTexture.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rts::render {
namespace {
const char* vs = R"(
layout(location=0) in vec2 pos;
layout(std140) uniform U0 { vec4 ends; vec4 color; vec4 params; };
out vec2 uv;
void main(){uv=pos;gl_Position=vec4(pos.x*2.0-1.0,(pos.y*2.0-1.0)*params.z,0,1);}
)";
const char* fs = R"(
layout(std140) uniform U0 { vec4 ends; vec4 color; vec4 params; };
in vec2 uv; out vec4 outColor;
void main(){
 vec2 p=uv*vec2(params.w,1), a=ends.xy*vec2(params.w,1), b=ends.zw*vec2(params.w,1);
 vec2 d=b-a; float t=clamp(dot(p-a,d)/max(dot(d,d),0.0000001),0.0,1.0);
 float coverage=1.0-smoothstep(params.x-params.y,params.x,length(p-a-d*t));
 outColor=vec4(color.rgb,color.a*coverage);
}
)";
const char* msl = R"(
#include <metal_stdlib>
using namespace metal;
struct U {float4 ends,color,params;};
struct In {float2 pos [[attribute(0)]];}; struct Out {float4 pos [[position]];float2 uv;};
vertex Out vs_main(In i [[stage_in]],constant U& u [[buffer(1)]]){
 Out o; o.uv=i.pos;o.pos=float4(i.pos.x*2-1,(i.pos.y*2-1)*u.params.z,0,1);return o;
}
fragment float4 fs_main(Out i [[stage_in]],constant U& u [[buffer(1)]]){
 float2 p=i.uv*float2(u.params.w,1),a=u.ends.xy*float2(u.params.w,1),b=u.ends.zw*float2(u.params.w,1);
 float2 d=b-a;float t=clamp(dot(p-a,d)/max(dot(d,d),0.0000001f),0.0f,1.0f);
 float coverage=1-smoothstep(u.params.x-u.params.y,u.params.x,length(p-a-d*t));
 return float4(u.color.rgb,u.color.a*coverage);
}
)";
const char* blitFS=R"(
layout(std140) uniform U0 { vec4 ends; vec4 color; vec4 params; };
uniform sampler2D tex0;in vec2 uv;out vec4 outColor;
void main(){outColor=texture(tex0,(uv-ends.xy)/ends.zw);}
)";
const char* blitMSL=R"(
#include <metal_stdlib>
using namespace metal;
struct U {float4 ends,color,params;};struct In {float2 pos [[attribute(0)]];};struct Out {float4 pos [[position]];float2 uv;};
vertex Out vs_main(In i [[stage_in]],constant U& u [[buffer(1)]]){Out o;o.uv=i.pos;o.pos=float4(i.pos.x*2-1,(i.pos.y*2-1)*u.params.z,0,1);return o;}
fragment float4 fs_main(Out i [[stage_in]],constant U& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s [[sampler(0)]]){return tex0.sample(s,(i.uv-u.ends.xy)/u.ends.zw);}
)";

}
bool RenderTexture::init(gfx::Device& dev,int w,int h){
 shutdown(); if(w<1||h<1||w>2048||h>2048)return false;
 dev_=&dev;width_=w;height_=h;
 gfx::TextureDesc td;td.width=w;td.height=h;td.renderTarget=true;td.label="Lua render texture";
 texture_=dev.createTexture(td);
 gfx::ShaderDesc sd;sd.glslVertex=vs;sd.glslFragment=fs;sd.msl=msl;sd.label="canvas brush";
 shader_=dev.createShader(sd);
 gfx::PipelineDesc pd;pd.shader=shader_;pd.attrs[0]={0,gfx::VertexFormat::Float2,0};pd.attrCount=1;pd.stride=sizeof(float)*2;
 pd.depthTest=false;pd.depthWrite=false;pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Alpha;pd.indexType=gfx::IndexType::None;
 if(shader_.valid())pipe_=dev.createPipeline(pd);
 sd.glslFragment=blitFS;sd.msl=blitMSL;sd.textureSlots=1;sd.label="canvas blit";blitShader_=dev.createShader(sd);
 pd.shader=blitShader_;pd.blend=gfx::BlendMode::Opaque;if(blitShader_.valid())blitPipe_=dev.createPipeline(pd);
 if(!texture_.valid()||!pipe_.valid()||!blitPipe_.valid()){
  std::fprintf(stderr,"[canvas] %s\n",dev.lastError().c_str());shutdown();return false;
 }
 clear();return true;
}
void RenderTexture::shutdown(){
 if(dev_){dev_->destroyTexture(texture_);dev_->destroyPipeline(pipe_);dev_->destroyShader(shader_);dev_->destroyPipeline(blitPipe_);dev_->destroyShader(blitShader_);}
 dev_=nullptr;texture_={};pipe_={};shader_={};blitPipe_={};blitShader_={};strokes_.clear();width_=height_=0;
}
void RenderTexture::clear(std::array<float,4> c){clear_=true;clearColor_=c;strokes_.clear();}
bool RenderTexture::stroke(const Stroke& s){
 for(float f:{s.ax,s.ay,s.bx,s.by,s.radius,s.softness,s.color[0],s.color[1],s.color[2],s.color[3]})if(!std::isfinite(f))return false;
 if(s.radius<=0||s.radius>4||s.softness<0||strokes_.size()>=8192)return false;
 strokes_.push_back(s);return true;
}
void RenderTexture::flush(){
 if(!dev_||(!clear_&&strokes_.empty()))return;
 gfx::PassDesc pass;pass.colorTarget=texture_;pass.clearColorEnabled=clear_;pass.clearDepthEnabled=false;
 std::copy(clearColor_.begin(),clearColor_.end(),pass.clearColor);dev_->beginPass(pass);dev_->setViewport({0,0,width_,height_});dev_->setScissor({});
 dev_->setPipeline(pipe_);
 // One upload for the batch; bounding quads limit fragment work to the brush footprint.
 std::vector<float> verts;verts.reserve(strokes_.size()*12);
 const float aspect=float(width_)/height_;
 for(const auto& s:strokes_){
  float rx=s.radius/aspect;float x0=std::max(0.f,std::min(s.ax,s.bx)-rx),x1=std::min(1.f,std::max(s.ax,s.bx)+rx);
  float y0=std::max(0.f,std::min(s.ay,s.by)-s.radius),y1=std::min(1.f,std::max(s.ay,s.by)+s.radius);
  for(float v:{x0,y0,x1,y0,x1,y1,x0,y0,x1,y1,x0,y1})verts.push_back(v);
 }
 gfx::BufferHandle vertices;
 if(!verts.empty()){gfx::BufferDesc bd;bd.data=verts.data();bd.size=verts.size()*sizeof(float);vertices=dev_->createBuffer(bd);dev_->setVertexBuffer(vertices);}
 uint32_t first=0;
 for(const auto& s:strokes_){
  struct {float ends[4],color[4],params[4];} u{{s.ax,s.ay,s.bx,s.by},{s.color[0],s.color[1],s.color[2],s.color[3]},
   {s.radius,std::max(0.5f/height_,std::min(s.softness,s.radius)),dev_->info().renderTargetFlipY?-1.f:1.f,aspect}};
  dev_->setUniforms(0,&u,sizeof(u));dev_->draw(6,first);first+=6;
 }
 dev_->endPass();if(vertices.valid())dev_->destroyBuffer(vertices);clear_=false;strokes_.clear();
}
bool RenderTexture::blit(gfx::TextureHandle source,float x,float y,float w,float h){
 if(!dev_||!source.valid()||source==texture_)return false;
 for(float v:{x,y,w,h})if(!std::isfinite(v))return false;
 if(w<=0||h<=0)return false;
 flush();
 const float verts[]={x,y,x+w,y,x+w,y+h,x,y,x+w,y+h,x,y+h};
 gfx::BufferDesc bd;bd.data=verts;bd.size=sizeof(verts);auto vertices=dev_->createBuffer(bd);
 gfx::PassDesc pass;pass.colorTarget=texture_;pass.clearColorEnabled=false;pass.clearDepthEnabled=false;
 dev_->beginPass(pass);dev_->setViewport({0,0,width_,height_});dev_->setScissor({});dev_->setPipeline(blitPipe_);
 struct {float ends[4],color[4],params[4];}u{{x,y,w,h},{0,0,0,0},{0,0,dev_->info().renderTargetFlipY?-1.f:1.f,0}};
 dev_->setUniforms(0,&u,sizeof(u));dev_->setTexture(0,source);dev_->setVertexBuffer(vertices);dev_->draw(6);dev_->endPass();dev_->destroyBuffer(vertices);return true;
}

void RenderTexture::upload(int x,int y,int w,int h,const void* p,int pitch){
 if(!dev_||!p||x<0||y<0||w<=0||h<=0||x>width_-w||y>height_-h||pitch<w*4)return;
 // Stage CPU bytes into a separate texture; an ordered GPU copy avoids a deferred clear or
 // brush pass overwriting an immediate replaceRegion upload later in the same frame.
 std::vector<uint8_t> bytes(size_t(w)*h*4);
 for(int row=0;row<h;++row)std::copy_n(static_cast<const uint8_t*>(p)+size_t(row)*pitch,w*4,bytes.data()+size_t(row)*w*4);
 gfx::TextureDesc td;td.width=w;td.height=h;td.data=bytes.data();td.label="canvas upload";
 auto source=dev_->createTexture(td);
 if(source.valid()){blit(source,float(x)/width_,float(y)/height_,float(w)/width_,float(h)/height_);dev_->destroyTexture(source);}
}
}
