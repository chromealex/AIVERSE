// Private CPU execution of the same bounded appearance DAG used by GLSL/MSL.
#pragma once
#include <array>
namespace aiverse::vivid::detail {
using Pixel=std::array<float,4>;
struct BakeGraph {
 enum Op { Constant,Input,Uv,Alpha,Red,Green,Multiply,Add,Subtract,Mix,Dot,Noise,Smooth,Step,Fract,Clamp,Range,Length,SetAlpha };
 struct Instruction {Op op;int a=0,b=0,c=0;Pixel value{};};
 std::vector<Instruction> code;
 int output=0;
 BakeGraph(const Json& graph) {
  const std::map<std::string,Op> ops={{"constant",Constant},{"input",Input},{"uv",Uv},{"alpha",Alpha},{"red",Red},{"green",Green},{"multiply",Multiply},{"add",Add},{"subtract",Subtract},{"mix",Mix},{"dot3",Dot},{"gradientNoise",Noise},{"smoothstep",Smooth},{"step",Step},{"fract",Fract},{"clamp",Clamp},{"clampRange",Range},{"length3",Length},{"setAlpha",SetAlpha}};
  std::map<std::string,int> ids;
  for(const auto& n:graph["nodes"].items()) {
   auto found=ops.find(n.getStr("op"));if(found==ops.end())throw std::runtime_error("Appearance bake: unsupported operation");
   Instruction i;i.op=found->second;
   auto in=[&](size_t k){return k<n["inputs"].size()?ids.at(n["inputs"][k].str()):0;};
   i.a=in(0);i.b=in(1);i.c=in(2);
   if(i.op==Constant)for(int k=0;k<4;++k)i.value[k]=float(n["value"].isNumber()?n["value"].toDouble():n["value"][size_t(k)].toDouble());
   ids.emplace(n.getStr("id"),int(code.size()));code.push_back(i);
  }
  if(code.empty()||code.size()>128)throw std::runtime_error("Appearance bake DAG budget");
  output=ids.at(graph["outputs"].getStr("color"));
 }
 static float fract(float x){return x-std::floor(x);}
 Pixel evaluate(Pixel input,float u,float v) const {
  std::array<Pixel,128> values{};
  for(size_t idx=0;idx<code.size();++idx) {
   const auto& i=code[idx];const auto a=values[i.a],b=values[i.b],c=values[i.c];Pixel r{};
   float scalar=0;
   if(i.op==Noise) {
    const float x=std::floor(a[0]),y=std::floor(a[1]),fx=fract(a[0]),fy=fract(a[1]);float h[4];
    for(int k=0;k<4;++k){const float angle=fract(std::sin((x+k%2)*127.1f+(y+k/2)*311.7f)*43758.5453f)*6.2831853f;h[k]=std::cos(angle)*(fx-k%2)+std::sin(angle)*(fy-k/2);}
    const float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);
    scalar=((h[0]+(h[1]-h[0])*sx)*(1-sy)+(h[2]+(h[3]-h[2])*sx)*sy)*.5f+.5f;
   } else if(i.op==Dot)scalar=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
   else if(i.op==Length)scalar=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
   for(int k=0;k<4;++k)switch(i.op) {
    case Constant:r[k]=i.value[k];break;case Input:r[k]=input[k];break;case Uv:r[k]=k==0?u:k==1?v:k==3?1:0;break;
    case Alpha:r[k]=a[3];break;case Red:r[k]=a[0];break;case Green:r[k]=a[1];break;
    case Multiply:r[k]=a[k]*b[k];break;case Add:r[k]=a[k]+b[k];break;case Subtract:r[k]=a[k]-b[k];break;
    case Mix:r[k]=a[k]+(b[k]-a[k])*c[k];break;case Dot:case Noise:case Length:r[k]=scalar;break;
    case Smooth:{const float t=std::clamp((c[k]-a[k])/std::max(b[k]-a[k],1e-8f),0.f,1.f);r[k]=t*t*(3-2*t);break;}
    case Step:r[k]=b[k]<a[k]?0:1;break;case Fract:r[k]=fract(a[k]);break;case Clamp:r[k]=std::clamp(a[k],0.f,1.f);break;
    case Range:r[k]=std::clamp(a[k],b[k],c[k]);break;case SetAlpha:r[k]=k==3?b[0]:a[k];break;
   }
   values[idx]=r;
  }
  return values[size_t(output)];
 }
};
inline void bakeMaterial(render::Model& model,render::Material& material,const Json& graph,int limit) {
 const BakeGraph program(graph);
 const render::Image* source=material.baseTex<0?nullptr:&model.images.at(size_t(material.baseTex));
 int w=limit,h=limit;
 if(source){if(source->width<=0||source->height<=0||source->rgba.size()!=size_t(source->width)*source->height*4)throw std::runtime_error("Appearance bake requires CPU RGBA pixels");
  const float scale=std::min(1.f,float(limit)/std::max(source->width,source->height));w=std::max(1,int(source->width*scale));h=std::max(1,int(source->height*scale));}
 render::Image out;out.name=material.name+".baked";out.width=w;out.height=h;out.rgba.resize(size_t(w)*h*4);if(source){out.repeat=source->repeat;out.nearest=source->nearest;}
 const float factors[]={material.baseColor.x,material.baseColor.y,material.baseColor.z,material.baseColor.w};
 auto linear=[](float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};
 auto srgb=[](float x){return x<=.0031308f?x*12.92f:1.055f*std::pow(x,1.f/2.4f)-.055f;};
 for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
  Pixel pixel{1,1,1,1};
  if(source){const int sx=std::min(source->width-1,int((x+.5f)*source->width/w)),sy=std::min(source->height-1,int((y+.5f)*source->height/h));for(int k=0;k<4;++k){const float v=source->rgba[(size_t(sy)*source->width+sx)*4+k]/255.f;pixel[k]=k==3?v:linear(v);}}
  for(int k=0;k<4;++k)pixel[k]*=factors[k];
  pixel=program.evaluate(pixel,(x+.5f)/w,(y+.5f)/h);
  for(int k=0;k<4;++k){float v=std::clamp(pixel[k],0.f,1.f);if(k<3)v=srgb(v);out.rgba[(size_t(y)*w+x)*4+k]=uint8_t(std::lround(v*255));}
 }
 material.baseTex=int(model.images.size());model.images.push_back(std::move(out));material.baseColor={1,1,1,1};material.mrTex=-1;material.shaderGraph.reset();
}
}
