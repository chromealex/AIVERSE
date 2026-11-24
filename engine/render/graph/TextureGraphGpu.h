// Native Metal compute preview compiler. CPU graph remains authoritative.
// Fused pointwise DAG: no GPU-side recursive graph or per-node texture allocation.
#pragma once
#include "TextureGraph.h"
#include "TextureGraphDagLowering.h"
#include "TextureGraphFusion.h"
#include "TextureGraphKernelSource.h"
#include "TextureGraphGpuPatterns.h"
#include "engine/gfx/Gfx.h"
#include <sstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <string_view>

namespace aiverse::render::texture_graph::gpu {
inline constexpr int maxPreviewSize=128,maxBakeSize=1024;
inline constexpr size_t readbackBlockBytes=64*1024;
inline std::string literal(float v){
    std::ostringstream s;s.imbue(std::locale::classic());
    s<<std::scientific<<std::setprecision(std::numeric_limits<float>::max_digits10)<<v<<"f";return s.str();
}
inline std::string pointwiseCommon(){
    std::string msl=R"MSL(
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
    return msl;
}
template<class Port>
inline std::string pointwiseExpression(const Node& node,int n,Port var){
    const auto& q=node.parameters;const std::string id=node.info->id;
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
            if(it==names.end())throw std::invalid_argument("unknown GPU pattern");
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
        else throw std::invalid_argument("GPU pointwise preview unsupported node: "+id);
    return expr;
}
inline Source compile(const Program& p,size_t target,int n,bool floatHeight=false){
    require(target<p.nodes.size()&&n>=8&&n<=p.size&&n<=maxBakeSize,"GPU texture resolution must be 8..1024");
    std::vector<bool> needed(p.nodes.size());std::function<void(size_t)> mark=[&](size_t i){if(needed[i])return;needed[i]=true;for(auto j:p.nodes[i].inputs)mark(j);};mark(target);
    std::string msl="#include <metal_stdlib>\nusing namespace metal;\n"+pointwiseCommon();
    if(floatHeight)require(p.nodes[target].info->output==Kind::Scalar,"height pass requires scalar target");
    // Normal is a terminal typed output. Re-evaluate its scalar DAG at four
    // wrapped neighbors, preserving float precision (no intermediate RGBA8).
    const bool normal=std::string(p.nodes[target].info->id)=="normal";
    const size_t valueTarget=normal?p.nodes[target].inputs[0]:target;
    msl+="float3 evaluatePoint(float2 uv){\n";
    for(auto i:p.order){
        if(!needed[i]||(normal&&i==target))continue;
        const auto& node=p.nodes[i];
        const auto expr=pointwiseExpression(node,n,[&](size_t j){return "v"+std::to_string(node.inputs[j]);});
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
// Only a scalar output that is also the input of the declared normal
// output benefits from materializing the shared float-height closure.
inline bool sharesNormalInput(const Program& p,size_t target){
    const auto normal=p.outputs.find("normal");
    return target<p.nodes.size()&&normal!=p.outputs.end()&&
        p.nodes[target].info->output==Kind::Scalar&&
        p.nodes[normal->second].inputs[0]==target;
}
inline PassPlan plan(const Program& p,size_t target,int n,bool shareScalar=false){
    require(target<p.nodes.size(),"invalid GPU target");
    if(std::string(p.nodes[target].info->id)!="normal"){
        if(!shareScalar||!sharesNormalInput(p,target))return {compile(p,target,n),{},false,0};
        auto first=compile(p,target,n,true);
        if(first.msl.empty())return {std::move(first),{},false,0};
        const std::string body="const uint n="+std::to_string(n)+"u;if(tid>=n*n)return;uint v=uint(floor(clamp(height[tid],0.0f,1.0f)*255.0f+0.5f));";
        Source second;
        second.msl="#include <metal_stdlib>\nusing namespace metal;\nkernel void cs_main(device uint* output [[buffer(0)]],device const float* height [[buffer(1)]],uint tid [[thread_position_in_grid]]){"+body+
            "output[tid]=v|(v<<8)|(v<<16)|0xff000000u;}";
        second.glsl="layout(std430,set=0,binding=12) buffer Result { uint outputPixels[]; };\nlayout(std430,set=0,binding=13) readonly buffer Height { float height[]; };\nvoid main(){uint tid=gl_GlobalInvocationID.x;"+body+
            "outputPixels[tid]=v|(v<<8)|(v<<16)|0xff000000u;}";
        return {std::move(first),std::move(second),true,size_t(n)*n*sizeof(float)};
    }
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
// CPU-side materialized execution estimate. Scalar storage uses float32;
// RGB/packed/normal use an explicitly padded float4 (not float3 ABI).
// The existing fused native dispatcher does NOT execute this plan yet.
inline constexpr size_t materializedBudgetBytes=32u*1024u*1024u;
inline MaterializedDagPlan planMaterializedDag(const Program& p,
        const std::vector<size_t>& targets,int n,size_t budget=materializedBudgetBytes){
    require(n>=8&&n<=p.size&&n<=maxBakeSize,"invalid materialized resolution");
    std::vector<std::vector<size_t>> inputs;
    std::vector<size_t> bytes;
    inputs.reserve(p.nodes.size());bytes.reserve(p.nodes.size());
    for(const auto& node:p.nodes){
        require(node.info!=nullptr,"invalid materialized node");
        inputs.push_back(node.inputs);
        bytes.push_back(size_t(n)*size_t(n)*(node.info->output==Kind::Scalar?4u:16u));
    }
    return lowerDagSlots(inputs,p.order,targets,bytes,budget,3);
}
inline KernelValue materializedValue(Kind kind){
    return kind==Kind::Scalar?KernelValue::Scalar:KernelValue::Vector;
}
// Generate a typed node kernel from the SAME pointwise expression compiler
// used by the fused path. Normal reads its already-materialized float field.
inline Source compileMaterializedNode(const Program& p,size_t target,int n){
    require(target<p.nodes.size()&&n>=8&&n<=p.size&&n<=maxBakeSize,"invalid materialized node target");
    const auto& node=p.nodes[target];
    require(node.info&&node.inputs.size()==node.info->inputs.size()&&node.inputs.size()<=3,"invalid materialized node arity");
    std::vector<KernelValue> types;
    for(size_t j=0;j<node.inputs.size();++j){
        const auto dep=node.inputs[j];
        require(dep<p.nodes.size()&&p.nodes[dep].info&&p.nodes[dep].info->output==node.info->inputs[j],"invalid materialized input kind");
        types.push_back(materializedValue(p.nodes[dep].info->output));
    }
    const auto output=materializedValue(node.info->output);
    std::string body;
    const bool normal=std::string(node.info->id)=="normal";
    if(normal){
        const auto strength=literal(scalar(node.parameters["strength"],2,0,16));
        body="uint x=tid%n,y=tid/n;float k="+strength+"*float(n)/256.0f;\n"
            "float dx=(input0[y*n+(x+1u)%n]-input0[y*n+(x+n-1u)%n])*k;\n"
            "float dy=(input0[((y+1u)%n)*n+x]-input0[((y+n-1u)%n)*n+x])*k;\n"
            "float len=sqrt(dx*dx+dy*dy+1.0f);\n"
            "float3 value=clamp(float3(0.5f-0.5f*dx/len,0.5f+0.5f*dy/len,0.5f+0.5f/len),0.0f,1.0f);\n";
    }else{
        body="uint x=tid%n,y=tid/n;float2 uv=(float2(x,y)+0.5f)/float(n);\n";
        for(size_t j=0;j<types.size();++j){
            const auto i=std::to_string(j);
            body+="float3 v"+i+"="+(types[j]==KernelValue::Scalar?"float3(input"+i+"[tid])":"input"+i+"[tid].xyz")+";\n";
        }
        const auto expr=pointwiseExpression(node,n,[](size_t j){return "v"+std::to_string(j);});
        body+="float3 value=clamp("+expr+",0.0f,1.0f);\n";
    }
    body+=output==KernelValue::Scalar?"result[tid]=value.x;":"result[tid]=float4(value,1.0f);";
    return materializedKernel(output,types,n,body,normal||!types.empty()?std::string{}:pointwiseCommon());
}
// Only the known pointwise operations participate. Future neighborhood,
// sampling or side-effect operations remain materialization boundaries.
inline bool fusionPointwise(const Node& node){
    const std::string_view id=node.info->id;
    for(std::string_view op:{"constant","color","gradient","noise","fbm","voronoi","pattern","patternMask",
        "add","subtract","multiply","min","max","invert","threshold","remap","blend","blendColor","colorize","metallicRoughness"})
        if(id==op)return true;
    return false;
}
inline Source compileFusionRegion(const Program& p,const FusionRegion& region,int n){
    require(!region.nodes.empty()&&region.nodes.size()<=maxFusionNodes&&region.inputs.size()<=maxFusionInputs,
        "invalid pointwise fusion region");
    if(region.nodes.size()==1)return compileMaterializedNode(p,region.nodes.front(),n);
    std::vector<KernelValue> types;
    std::vector<bool> defined(p.nodes.size());
    auto var=[](size_t node){return "fusedNode"+std::to_string(node);};
    std::string body="uint x=tid%n,y=tid/n;float2 uv=(float2(x,y)+0.5f)/float(n);\n";
    for(size_t i=0;i<region.inputs.size();++i){
        const auto dep=region.inputs[i];
        require(dep<p.nodes.size()&&!defined[dep],"invalid fusion external input");
        const auto type=materializedValue(p.nodes[dep].info->output);
        types.push_back(type);defined[dep]=true;
        const auto index=std::to_string(i);
        body+="float3 "+var(dep)+"="+(type==KernelValue::Scalar?"float3(input"+index+"[tid])":"input"+index+"[tid].xyz")+";\n";
    }
    bool common=false;
    for(auto index:region.nodes){
        require(index<p.nodes.size()&&!defined[index],"invalid fusion node identity");
        const auto& node=p.nodes[index];
        require(fusionPointwise(node)&&node.inputs.size()==node.info->inputs.size(),"invalid pointwise fusion node");
        for(size_t i=0;i<node.inputs.size();++i){
            const auto dep=node.inputs[i];
            require(dep<p.nodes.size()&&defined[dep]&&p.nodes[dep].info->output==node.info->inputs[i],
                "invalid fusion dependency or kind");
        }
        const auto expression=pointwiseExpression(node,n,[&](size_t i){return var(node.inputs[i]);});
        body+="float3 "+var(index)+"=clamp("+expression+",0.0f,1.0f);\n";
        // Match the typed materialized boundary: a scalar stores X and
        // re-expands to all lanes before its consumers read it.
        if(node.info->output==Kind::Scalar)
            body+=var(index)+"=float3("+var(index)+".x);\n";
        defined[index]=true;common|=node.inputs.empty();
    }
    const auto root=region.nodes.back();
    const auto output=materializedValue(p.nodes[root].info->output);
    body+=output==KernelValue::Scalar?"result[tid]="+var(root)+".x;":"result[tid]=float4("+var(root)+",1.0f);";
    return materializedKernel(output,types,n,body,common?pointwiseCommon():std::string{});
}
struct MaterializedSources {
    MaterializedDagPlan plan;
    std::vector<Source> nodes; // indexed by source node; unreachable entries empty
    std::vector<Source> packs; // target order; shared result buffer reused
    size_t resultBytes=0,totalBytes=0;
    size_t reachableNodes=0,fusedNodes=0;
};
inline MaterializedSources compileMaterializedDag(const Program& p,const std::vector<size_t>& targets,
        int n,size_t budget=materializedBudgetBytes,bool fusePointwise=false){
    require(n>=8&&n<=p.size&&n<=maxBakeSize&&budget<=materializedBudgetBytes,"invalid materialized resolution or budget");
    MaterializedSources r;
    r.resultBytes=size_t(n)*size_t(n)*4u;
    require(r.resultBytes<budget,"materialized result exceeds budget");
    if(fusePointwise){
        std::vector<std::vector<size_t>> edges;
        std::vector<size_t> bytes;
        std::vector<bool> pointwise;
        for(const auto& node:p.nodes){
            require(node.info!=nullptr,"invalid materialized node");
            edges.push_back(node.inputs);
            bytes.push_back(size_t(n)*size_t(n)*(node.info->output==Kind::Scalar?4u:16u));
            pointwise.push_back(fusionPointwise(node));
        }
        const auto fusion=planPointwiseFusion(edges,p.order,targets,pointwise);
        r.plan=lowerDagSlots(fusion.edges,p.order,targets,bytes,budget-r.resultBytes,3);
        r.reachableNodes=fusion.reachable;r.fusedNodes=fusion.reachable-fusion.passes;
        r.nodes.resize(p.nodes.size());
        for(const auto& pass:r.plan.schedule.passes)
            r.nodes[pass.pipeline]=compileFusionRegion(p,fusion.regions.at(pass.pipeline),n);
    }else{
        r.plan=planMaterializedDag(p,targets,n,budget-r.resultBytes);
        r.reachableNodes=r.plan.schedule.passes.size();
        r.nodes.resize(p.nodes.size());
        for(const auto& pass:r.plan.schedule.passes)
            r.nodes[pass.pipeline]=compileMaterializedNode(p,pass.pipeline,n);
    }
    r.totalBytes=r.plan.allocatedBytes+r.resultBytes;
    for(auto target:targets)r.packs.push_back(packMaterializedKernel(materializedValue(p.nodes[target].info->output),n));
    return r;
}
} // namespace aiverse::render::texture_graph::gpu
