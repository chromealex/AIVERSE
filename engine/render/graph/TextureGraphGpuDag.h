// Lower a validated texture DAG into float4 node kernels and an ordered
// storage plan. No RHI allocation, command submission, or completion here.
#pragma once
#include "TextureGraphGpu.h"
#include "TextureGraphGpuSlots.h"

namespace aiverse::render::texture_graph::gpu {
inline Source compileNodeKernel(const Program& p,size_t target,int n){
    require(target<p.nodes.size()&&n>=8&&n<=p.size&&n<=maxBakeSize,
        "invalid materialized GPU node target or resolution");
    const auto& node=p.nodes[target];
    const auto count=node.inputs.size();
    if(count>maxNodeInputs)return {"","node exceeds GPU input binding limit",{}};
    const std::string id=node.info->id;
    std::string body="uint x=tid%n,y=tid/n;\n";
    if(id=="normal"){
        const auto strength=literal(scalar(node.parameters["strength"],2,0,16));
        body+="float k="+strength+"*float(n)/256.0f;\n"
            "float dx=(src0[y*n+(x+1u)%n].x-src0[y*n+(x+n-1u)%n].x)*k;\n"
            "float dy=(src0[((y+1u)%n)*n+x].x-src0[((y+n-1u)%n)*n+x].x)*k;\n"
            "float len=sqrt(dx*dx+dy*dy+1.0f);\n"
            "float3 result=float3(0.5f-0.5f*dx/len,0.5f+0.5f*dy/len,0.5f+0.5f/len);\n";
    }else{
        body+="float2 uv=(float2(x,y)+0.5f)/float(n);\n";
        for(size_t i=0;i<count;++i){
            const auto index=std::to_string(i);
            body+="float3 v"+index+"=src"+index+"[tid].xyz;\n";
        }
        const auto expr=pointwiseExpression(node,n,[](size_t i){return "v"+std::to_string(i);});
        if(expr.empty())return {"","unsupported materialized texture node: "+id,{}};
        body+="float3 result="+expr+";\n";
    }
    body+="outPixels[tid]=float4(clamp(result,0.0f,1.0f),1.0f);";
    const bool helpers=id=="noise"||id=="fbm"||id=="voronoi"||id=="pattern"||id=="patternMask";
    return materializedKernel(n,count,helpers?pointwiseHelpers():std::string{},body);
}
struct MaterializedDagPlan {
    DagSlotPlan slots;
    std::vector<Source> nodeKernels; // exact index of corresponding slot step
    Source packKernel;
    std::string reason;
    bool ready()const{return slots.fits&&!nodeKernels.empty()&&nodeKernels.size()==slots.steps.size()&&
        !packKernel.msl.empty()&&!packKernel.glsl.empty()&&reason.empty();}
};
// A source-plan is eligible only when the owner can support the budget and
// bindings. The fused path remains available for ineligible plans.
inline MaterializedDagPlan compileMaterializedDag(const Program& p,const std::vector<size_t>& targets,
        int n,size_t budget=maxDagSlotBytes,size_t slotLimit=maxDagSlots){
    require(n>=8&&n<=p.size&&n<=maxBakeSize,"invalid materialized GPU resolution");
    std::vector<std::vector<size_t>> edges;edges.reserve(p.nodes.size());
    for(const auto& node:p.nodes)edges.push_back(node.inputs);
    auto slots=planDagSlots(edges,p.order,targets,n,budget,slotLimit);
    if(!slots.fits)return {{},{},{},"materialized DAG exceeds storage budget"};
    MaterializedDagPlan result;
    result.nodeKernels.reserve(slots.steps.size());
    for(const auto& step:slots.steps){
        auto source=compileNodeKernel(p,step.node,n);
        if(source.msl.empty()||source.glsl.empty())return {{},{},{},source.reason};
        result.nodeKernels.push_back(std::move(source));
    }
    result.packKernel=compilePackKernel(n);
    result.slots=std::move(slots);
    return result;
}
} // namespace aiverse::render::texture_graph::gpu
