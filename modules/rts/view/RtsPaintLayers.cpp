#include "RtsPaintLayers.h"
#include "RtsView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace rts::game {
namespace {
const char* vs=R"(
layout(location=0) in vec3 pos;
layout(std140) uniform U0 { mat4 vp; vec4 bounds; vec4 color; vec4 style; };
out vec2 uv;
void main(){uv=(pos.xz-bounds.xy)/bounds.zw;gl_Position=vp*vec4(pos,1);}
)";
const char* fs=R"(
layout(std140) uniform U0 { mat4 vp; vec4 bounds; vec4 color; vec4 style; };
uniform sampler2D tex0;in vec2 uv;out vec4 outColor;
void main(){
 if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))discard;
 float v=texture(tex0,uv).a;float aa=max(fwidth(v),0.015);
 float fill=smoothstep(0.5-aa,0.5+aa,v);
 float edge=1.0-smoothstep(style.y,style.y+aa,abs(v-0.5));
 outColor=vec4(mix(color.rgb,vec3(1),edge*0.55),fill*style.x);
}
)";
const char* msl=R"(
#include <metal_stdlib>
using namespace metal;
struct U {float4x4 vp;float4 bounds,color,style;};struct In {float3 pos [[attribute(0)]];};struct Out {float4 pos [[position]];float2 uv;};
vertex Out vs_main(In i [[stage_in]],constant U& u [[buffer(1)]]){Out o;o.uv=(i.pos.xz-u.bounds.xy)/u.bounds.zw;o.pos=u.vp*float4(i.pos,1);return o;}
fragment float4 fs_main(Out i [[stage_in]],constant U& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s [[sampler(0)]]){
 if(any(i.uv<float2(0))||any(i.uv>float2(1)))discard_fragment();
 float v=tex0.sample(s,i.uv).a;float aa=max(fwidth(v),0.015f);
 float fill=smoothstep(0.5f-aa,0.5f+aa,v);float edge=1-smoothstep(u.style.y,u.style.y+aa,abs(v-0.5f));
 return float4(mix(u.color.rgb,float3(1),edge*0.55f),fill*u.style.x);
}
)";
}
void RtsPaintLayers::shutdown(){
 if(dev_){for(auto& [id,l]:layers_){l.canvas.shutdown();dev_->destroyBuffer(l.mesh.vb);dev_->destroyBuffer(l.mesh.ib);}dev_->destroyPipeline(pipe_);dev_->destroyShader(shader_);}
 layers_.clear();memory_.clear();source_.reset();dev_=nullptr;shader_={};pipe_={};w_=h_=0;lastTime_=-1;
}
void RtsPaintLayers::draw(render::Renderer3D& r,gfx::Device& dev,const std::shared_ptr<render::PaintLayers>& source,
 const MapGrid& g,const RtsView& view,const std::vector<uint8_t>* vision,double time,uint8_t viewer){
 if(!source)return; // The editor may also draw its edit scene using this view in the same frame.
 if(source!=source_||g.w!=w_||g.h!=h_||time<lastTime_||viewer!=viewer_)shutdown();
 if(!source||source->layers.empty()){if(dev_)shutdown();return;}
 if(!dev_){
  dev_=&dev;source_=source;w_=g.w;h_=g.h;viewer_=viewer;revision_=1;memory_.assign(size_t(w_)*h_,0);
  gfx::ShaderDesc sd;sd.glslVertex=vs;sd.glslFragment=fs;sd.msl=msl;sd.textureSlots=1;sd.label="paint surface";shader_=dev.createShader(sd);
  gfx::PipelineDesc pd;pd.shader=shader_;pd.attrs[0]={0,gfx::VertexFormat::Float3,0};pd.attrCount=1;pd.stride=sizeof(render::MeshVertex);
  pd.indexType=gfx::IndexType::UInt32;pd.depthWrite=false;pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Alpha;
  if(shader_.valid())pipe_=dev.createPipeline(pd);
  if(!pipe_.valid()){std::fprintf(stderr,"[paint] %s\n",dev.lastError().c_str());return;}
 }
 if(!pipe_.valid())return;
 const float dt=lastTime_<0?1.f:float(std::clamp(time-lastTime_,0.0,0.1));lastTime_=time;
 bool changed=false;
 for(size_t i=0;i<memory_.size();++i)if(!vision||(i<vision->size()&&(*vision)[i]==2)){
  uint8_t owner=i<g.territory.size()?g.territory[i]:0;if(owner!=memory_[i]){memory_[i]=owner;changed=true;}
 }
 if(changed)++revision_;
 for(auto it=layers_.begin();it!=layers_.end();)if(!source->layers.count(it->first)){it->second.canvas.shutdown();dev.destroyBuffer(it->second.mesh.vb);dev.destroyBuffer(it->second.mesh.ib);it=layers_.erase(it);}else ++it;
 for(auto& [id,p]:source->layers){
  auto& l=layers_[id];const bool field=p.territoryOwner>0;
  if(!p.textureId&&!l.canvas.handle().valid()){
   const int tw=field?w_:std::min(p.width,r.materialQuality()==render::MaterialQuality::Low?512:2048);
   const int th=field?h_:std::max(1,p.height*tw/p.width);
   if(!l.canvas.init(dev,tw,th))continue;
   l.value.assign(size_t(w_)*h_,0);l.target=l.value;l.bytes.assign(size_t(w_)*h_*4,0);
  }
  if(field){
   // Compact scalar source, smoothed in CPU space then sampled bilinearly by the GPU.
   // The authoritative ownership is never read back from the rendered texture.
   int x0=w_,y0=h_,x1=-1,y1=-1;
   for(int y=0;y<h_;++y)for(int x=0;x<w_;++x){
    int i=y*w_+x;
    if(l.revision!=revision_){
    float sum=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int xx=x+dx,yy=y+dy;if(xx>=0&&yy>=0&&xx<w_&&yy<h_&&memory_[yy*w_+xx]==p.territoryOwner)sum+=float((dx==0?2:1)*(dy==0?2:1))/16;}
    l.target[i]=0.5f*sum+0.5f*(memory_[i]==p.territoryOwner?1.f:0.f);
    }
    float& v=l.value[i];v+=std::clamp(l.target[i]-v,-dt*5,dt*5);
    uint8_t a=uint8_t(std::clamp(v*255,0.f,255.f)+0.5f);
    if(a!=l.bytes[size_t(i)*4+3]){l.bytes[size_t(i)*4+3]=a;x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
   }
   l.revision=revision_;
   if(x1>=x0)l.canvas.upload(x0,y0,x1-x0+1,y1-y0+1,l.bytes.data()+size_t(y0*w_+x0)*4,w_*4);
  }else{
   if(p.clear)l.canvas.clear();
   if(p.copyFrom){auto it=layers_.find(p.copyFrom);if(it!=layers_.end())l.canvas.blit(it->second.canvas.handle());p.copyFrom=0;}
   for(auto& stroke:p.strokes)l.canvas.stroke(stroke);
  }
  p.clear=false;p.strokes.clear();l.canvas.flush();
  if(!p.surface)continue;
  gfx::TextureHandle sample=l.canvas.handle();
  if(p.textureId){auto it=layers_.find(p.textureId);if(it==layers_.end())continue;sample=it->second.canvas.handle();}
  if(!sample.valid())continue;
  // Rebuild only if the visibility/land mask changes; terrain geometry itself is stable.
  // Small flat maps share a single surface draw per layer.
  uint64_t key=14695981039346656037ull;
  for(size_t i=0;i<g.flags.size();++i){key^=uint64_t(g.flags[i]) | (uint64_t(!vision||(i<vision->size()&&(*vision)[i]>0))<<8);key*=1099511628211ull;}
  if(key!=l.meshKey||!l.mesh.vb.valid()){
  render::MeshBuilder mb;
  for(int y=0;y<h_;++y)for(int x=0;x<w_;++x){
   int i=y*w_+x;if(p.landOnly&&!(g.flags[i]&kCellGround))continue;
   if(vision&&(size_t(i)>=vision->size()||(*vision)[i]==0))continue;
   if(x+1<p.x||y+1<p.z||x>p.x+p.w||y>p.z+p.h)continue;
   auto point=[&](float xx,float zz){return render::float3{xx,view.heightAt(g,xx,zz)+p.lift,zz};};
   mb.quad(point(float(x),float(y)),point(float(x),float(y+1)),point(float(x+1),float(y+1)),point(float(x+1),float(y)));
  }
  // Cached geometry must populate every frame-in-flight. Dynamic Metal buffers rotate;
  // updating only the current slice leaves the other slices empty while the mask is static.
  dev.destroyBuffer(l.mesh.vb);dev.destroyBuffer(l.mesh.ib);
  l.mesh=mb.upload(dev);l.meshKey=key;
  }
  struct U{render::mat4 vp;render::float4 bounds,color,style;};
  U u{r.viewProj(),{p.x,p.z,p.w,p.h},{p.color[0],p.color[1],p.color[2],p.color[3]},{p.opacity,p.border,0,0}};
  auto mesh=l.mesh;auto texture=sample;auto pipe=pipe_;
  r.drawCustom([u,mesh,texture,pipe](gfx::Device& d){d.setPipeline(pipe);d.setUniforms(0,&u,sizeof(u));d.setTexture(0,texture);d.setVertexBuffer(mesh.vb);d.setIndexBuffer(mesh.ib);d.drawIndexed(mesh.indexCount);});
 }
}
}
