// Native Metal compute preview compiler. CPU graph remains authoritative.
// Fused pointwise DAG: no GPU-side recursive graph or per-node texture allocation.
#pragma once
#include "TextureGraph.h"
#include "engine/gfx/Gfx.h"
#include <sstream>
#include <iomanip>
#include <locale>
#include <limits>

namespace aiverse::render::texture_graph::gpu {
struct Source {std::string msl,reason;};
inline std::string literal(float v){
    std::ostringstream s;s.imbue(std::locale::classic());
    s<<std::scientific<<std::setprecision(std::numeric_limits<float>::max_digits10)<<v<<"f";return s.str();
}
inline Source compile(const Program& p,size_t target,int n){
    require(target<p.nodes.size()&&n>=8&&n<=p.size&&n<=32,"GPU preview resolution must be 8..32");
    std::vector<bool> needed(p.nodes.size());std::function<void(size_t)> mark=[&](size_t i){if(needed[i])return;needed[i]=true;for(auto j:p.nodes[i].inputs)mark(j);};mark(target);
    std::string msl=R"MSL(
#include <metal_stdlib>
using namespace metal;
uint hash3(int x,int y,uint seed){uint h=uint(x)*374761393u+uint(y)*668265263u+seed*2246822519u;h=(h^(h>>13))*1274126177u;return h^(h>>16);}
int wrapi(int i,int p){return ((i%p)+p)%p;}
float rand01(int x,int y,uint seed){return float(hash3(x,y,seed)&0xffffffu)/float(0xffffffu);}
float periodicNoise(float2 uv,int p,uint seed){
 float2 xy=uv*float(p);int2 cell=int2(floor(xy));float2 f=xy-float2(cell);float2 t=f*f*(3.0f-2.0f*f);
 int x=cell.x,y=cell.y;
 float a=rand01(wrapi(x,p),wrapi(y,p),seed),b=rand01(wrapi(x+1,p),wrapi(y,p),seed);
 float c=rand01(wrapi(x,p),wrapi(y+1,p),seed),d=rand01(wrapi(x+1,p),wrapi(y+1,p),seed);
 return a+(b-a)*t.x+(c-a)*t.y+(a-b-c+d)*t.x*t.y;
}
float fractal(float2 uv,int p,int oct,uint seed){float sum=0.0f,amp=0.5f,norm=0.0f;for(int o=0;o<oct;++o){sum+=periodicNoise(uv,p<<o,seed+uint(o)*101u)*amp;norm+=amp;amp*=0.5f;}return sum/norm;}
float cells(float2 uv,int p,uint seed){
 float2 xy=uv*float(p);int2 c=int2(floor(xy));float best=2.0f;
 for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int x=c.x+dx,y=c.y+dy;
 float2 point=float2(float(x)+rand01(wrapi(x,p),wrapi(y,p),seed),float(y)+rand01(wrapi(x,p),wrapi(y,p),seed+971u));
 float2 d=point-xy;best=min(best,dot(d,d));}return clamp(sqrt(best),0.0f,1.0f);
}
kernel void cs_main(device uint* output [[buffer(0)]],uint tid [[thread_position_in_grid]]){
)MSL";
    msl+="const uint n="+std::to_string(n)+"u; if(tid>=n*n)return; float2 uv=(float2(tid%n,tid/n)+0.5f)/float(n);\n";
    for(auto i:p.order){
        if(!needed[i])continue;
        const auto& node=p.nodes[i];const auto& q=node.parameters;const std::string id=node.info->id;
        auto var=[&](size_t j){return "v"+std::to_string(node.inputs[j]);};
        auto a=[&](){return var(0)+".x";};auto b=[&](){return var(1)+".x";};
        auto f=[&](const char* key,float fallback,float lo=0,float hi=1){return literal(scalar(q[key],fallback,lo,hi));};
        std::string expr;
        if(id=="constant")expr="float3("+f("value",.5f)+")";
        else if(id=="color")expr="float3("+literal(scalar(q["value"][size_t(0)],0))+","+literal(scalar(q["value"][size_t(1)],0))+","+literal(scalar(q["value"][size_t(2)],0))+")";
        else if(id=="gradient")expr=q.getStr("axis","y")=="x"?"float3(uv.x)":"float3(uv.y)";
        else if(id=="noise"||id=="fbm"||id=="voronoi"){
            const auto period=std::to_string(integer(q["period"],8,1,256));
            const auto random=std::to_string(seed(q["seed"]))+"u";
            if(id=="voronoi")expr="float3(cells(uv,"+period+","+random+"))";
            else expr="float3(fractal(uv,"+period+","+std::to_string(id=="fbm"?integer(q["octaves"],4,1,6):1)+","+random+"))";
        }else if(id=="add")expr="float3("+a()+"+"+b()+")";
        else if(id=="subtract")expr="float3("+a()+"-"+b()+")";
        else if(id=="multiply")expr="float3("+a()+"*"+b()+")";
        else if(id=="min"||id=="max")expr="float3("+id+"("+a()+","+b()+"))";
        else if(id=="invert")expr="float3(1.0f-"+a()+")";
        else if(id=="threshold")expr="float3("+a()+">="+f("threshold",.5f)+"?1.0f:0.0f)";
        else if(id=="remap")expr="float3("+f("low",0)+"+("+f("high",1)+"-"+f("low",0)+")*"+a()+")";
        else if(id=="blend"||id=="blendColor")expr="mix("+var(0)+","+var(1)+","+var(2)+".x)";
        else if(id=="colorize")expr="mix("+var(1)+","+var(2)+","+a()+")";
        else if(id=="metallicRoughness")expr="float3(1.0f,"+a()+","+b()+")";
        else return {"","GPU pointwise preview unsupported node: "+id};
        msl+="float3 v"+std::to_string(i)+"=clamp("+expr+",0.0f,1.0f);\n";
    }
    msl+="uint3 rgb=uint3(floor(v"+std::to_string(target)+"*255.0f+0.5f));output[tid]=rgb.x|(rgb.y<<8)|(rgb.z<<16)|0xff000000u;}\n";
    return {std::move(msl),{}};
}
} // namespace aiverse::render::texture_graph::gpu
