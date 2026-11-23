// Native Metal compute preview compiler. CPU graph remains authoritative.
// Fused pointwise DAG: no GPU-side recursive graph or per-node texture allocation.
#pragma once
#include "TextureGraph.h"
#include "TextureGraphGpuPatterns.h"
#include "engine/gfx/Gfx.h"
#include <sstream>
#include <iomanip>
#include <locale>
#include <limits>

namespace aiverse::render::texture_graph::gpu {
struct Source {std::string msl,reason,glsl;};
inline constexpr int maxPreviewSize=128;
inline std::string literal(float v){
    std::ostringstream s;s.imbue(std::locale::classic());
    s<<std::scientific<<std::setprecision(std::numeric_limits<float>::max_digits10)<<v<<"f";return s.str();
}
inline Source compile(const Program& p,size_t target,int n,bool floatHeight=false){
    require(target<p.nodes.size()&&n>=8&&n<=p.size&&n<=maxPreviewSize,"GPU preview resolution must be 8..128");
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
)MSL";
    msl+=patternShader;
    if(floatHeight)require(p.nodes[target].info->output==Kind::Scalar,"height pass requires scalar target");
    // Normal is a terminal typed output. Re-evaluate its scalar DAG at four
    // wrapped neighbors, preserving float precision (no intermediate RGBA8).
    const bool normal=std::string(p.nodes[target].info->id)=="normal";
    const size_t valueTarget=normal?p.nodes[target].inputs[0]:target;
    msl+="float3 evaluatePoint(float2 uv){\n";
    for(auto i:p.order){
        if(!needed[i]||(normal&&i==target))continue;
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
        }else if(id=="pattern"||id=="patternMask"){
            const auto names=texturePatterns();const auto name=q.getStr("pattern");const auto it=std::find(names.begin(),names.end(),name);
            if(it==names.end())return {"","unknown GPU pattern",{}};
            const auto palette=texturePatternColors(name);
            auto color=[&](size_t i){const auto& v=palette[std::min(i,palette.size()-1)];return "float3("+literal(float(v[size_t(0)].toDouble()))+","+literal(float(v[size_t(1)].toDouble()))+","+literal(float(v[size_t(2)].toDouble()))+")";};
            expr="patternValue(uv,"+std::to_string(it-names.begin())+","+std::to_string(integer(q["scale"],8,1,64))+","+std::to_string(seed(q["seed"]))+"u,"+std::to_string(n)+","+(id=="patternMask"?"true":"false")+","+color(0)+","+color(1)+","+color(2)+")";
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
    msl+="return v"+std::to_string(valueTarget)+";}\n";
    std::string body="const uint n="+std::to_string(n)+"u; if(tid>=n*n)return; uint x=tid%n,y=tid/n; float2 uv=(float2(x,y)+0.5f)/float(n);\n";
    if(normal){
        const auto strength=literal(scalar(p.nodes[target].parameters["strength"],2,0,16));
        body+="float k="+strength+"*float(n)/256.0f;\n"
              "float dx=(evaluatePoint((float2((x+1u)%n,y)+0.5f)/float(n)).x-evaluatePoint((float2((x+n-1u)%n,y)+0.5f)/float(n)).x)*k;\n"
              "float dy=(evaluatePoint((float2(x,(y+1u)%n)+0.5f)/float(n)).x-evaluatePoint((float2(x,(y+n-1u)%n)+0.5f)/float(n)).x)*k;\n"
              "float len=sqrt(dx*dx+dy*dy+1.0f);float3 result=float3(0.5f-0.5f*dx/len,0.5f+0.5f*dy/len,0.5f+0.5f/len);\n";
    }else body+="float3 result=evaluatePoint(uv);\n";
    if(floatHeight)body+="output[tid]=as_type<uint>(result.x);}\n";
    else body+="uint3 rgb=uint3(floor(clamp(result,0.0f,1.0f)*255.0f+0.5f));output[tid]=rgb.x|(rgb.y<<8)|(rgb.z<<16)|0xff000000u;}\n";
    // Same validated numeric program, backend-specific vector spelling and entry ABI.
    // TextureResources uses storage buffer slot 0 => binding 12.
    auto glsl=msl.substr(msl.find("uint hash3"))+"layout(std430,set=0,binding=12) buffer Result { uint outputPixels[]; };\nvoid main(){uint tid=gl_GlobalInvocationID.x;\n"+body;
    auto replaceAll=[](std::string& s,const std::string& from,const std::string& to){
        size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,from.size(),to);at+=to.size();}
    };
    replaceAll(glsl,"float2","vec2");replaceAll(glsl,"float3","vec3");replaceAll(glsl,"uint3","uvec3");replaceAll(glsl,"int2","ivec2");replaceAll(glsl,"output[tid]","outputPixels[tid]");replaceAll(glsl,"as_type<uint>","floatBitsToUint");
    msl+="kernel void cs_main(device uint* output [[buffer(0)]],uint tid [[thread_position_in_grid]]){\n"+body;
    return {std::move(msl),{},std::move(glsl)};
}
// Explicit two-pass plan for a terminal neighborhood operation. Scratch
// contains one float per pixel, reused by the second pass, never quantized.
struct PassPlan {Source first,second;bool twoPass=false;size_t scratchBytes=0;};
inline PassPlan plan(const Program& p,size_t target,int n){
    require(target<p.nodes.size(),"invalid GPU target");
    if(std::string(p.nodes[target].info->id)!="normal")return {compile(p,target,n),{},false,0};
    auto first=compile(p,p.nodes[target].inputs[0],n,true);
    if(first.msl.empty())return {std::move(first),{},false,0};
    const auto strength=literal(scalar(p.nodes[target].parameters["strength"],2,0,16));
    std::string body="const uint n="+std::to_string(n)+"u;if(tid>=n*n)return;uint x=tid%n,y=tid/n;float k="+strength+"*float(n)/256.0f;\n"
        "float dx=(height[y*n+(x+1u)%n]-height[y*n+(x+n-1u)%n])*k;\n"
        "float dy=(height[((y+1u)%n)*n+x]-height[((y+n-1u)%n)*n+x])*k;\n"
        "float len=sqrt(dx*dx+dy*dy+1.0f);";
    Source second;
    second.msl="#include <metal_stdlib>\nusing namespace metal;\nkernel void cs_main(device uint* output [[buffer(0)]],device const float* height [[buffer(1)]],uint tid [[thread_position_in_grid]]){"+body+
        "uint3 rgb=uint3(floor(clamp(float3(0.5f-0.5f*dx/len,0.5f+0.5f*dy/len,0.5f+0.5f/len),0.0f,1.0f)*255.0f+0.5f));output[tid]=rgb.x|(rgb.y<<8)|(rgb.z<<16)|0xff000000u;}";
    second.glsl="layout(std430,set=0,binding=12) buffer Result { uint outputPixels[]; };\nlayout(std430,set=0,binding=13) readonly buffer Height { float height[]; };\nvoid main(){uint tid=gl_GlobalInvocationID.x;"+body+
        "uvec3 rgb=uvec3(floor(clamp(vec3(0.5f-0.5f*dx/len,0.5f+0.5f*dy/len,0.5f+0.5f/len),0.0f,1.0f)*255.0f+0.5f));outputPixels[tid]=rgb.x|(rgb.y<<8)|(rgb.z<<16)|0xff000000u;}";
    return {std::move(first),std::move(second),true,size_t(n)*n*sizeof(float)};
}
} // namespace aiverse::render::texture_graph::gpu
