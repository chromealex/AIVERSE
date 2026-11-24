// Typed materialized compute ABI. Source generation only, no device calls.
#pragma once
#include "engine/gfx/BufferTextureCopy.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace aiverse::render::texture_graph::gpu {
struct Source {std::string msl,reason,glsl;};
enum class KernelValue { Scalar, Vector, PackedBytes };
inline void replaceShaderToken(std::string& source,const std::string& from,const std::string& to){
    size_t at=0;
    while((at=source.find(from,at))!=std::string::npos){source.replace(at,from.size(),to);at+=to.size();}
}
inline std::string materializedGlsl(std::string s){
    replaceShaderToken(s,"float2","vec2");replaceShaderToken(s,"float3","vec3");
    replaceShaderToken(s,"float4","vec4");replaceShaderToken(s,"uint3","uvec3");
    replaceShaderToken(s,"int2","ivec2");return s;
}
inline const char* kernelType(KernelValue type){
    switch(type){case KernelValue::Scalar:return "float";case KernelValue::Vector:return "float4";case KernelValue::PackedBytes:return "uint";}
    throw std::invalid_argument("invalid kernel value type");
}
inline Source materializedKernel(KernelValue output,const std::vector<KernelValue>& inputs,int n,
        const std::string& body,const std::string& common={}){
    if(n<8||n>1024||inputs.size()>3||body.empty())throw std::invalid_argument("invalid materialized kernel shape");
    std::string msl="#include <metal_stdlib>\nusing namespace metal;\n"+common+"\nkernel void cs_main(device "+kernelType(output)+"* result [[buffer(0)]]";
    std::string glsl=common+"\nlayout(std430,set=0,binding=12) writeonly buffer Result { "+kernelType(output)+" result[]; };\n";
    for(size_t i=0;i<inputs.size();++i){
        if(inputs[i]==KernelValue::PackedBytes)throw std::invalid_argument("packed bytes cannot feed graph node");
        const auto index=std::to_string(i),binding=std::to_string(i+1);
        msl+=",device const "+std::string(kernelType(inputs[i]))+"* input"+index+" [[buffer("+binding+")]]";
        glsl+="layout(std430,set=0,binding="+std::to_string(13+i)+") readonly buffer Input"+index+" { "+kernelType(inputs[i])+" input"+index+"[]; };\n";
    }
    const std::string guard="const uint n="+std::to_string(n)+"u;if(tid>=n*n)return;\n";
    msl+=",uint tid [[thread_position_in_grid]]){\n"+guard+body+"\n}\n";
    glsl+="void main(){uint tid=gl_GlobalInvocationID.x;\n"+guard+body+"\n}\n";
    return {std::move(msl),{},materializedGlsl(std::move(glsl))};
}
inline Source packMaterializedKernelAt(KernelValue input,int n,const std::string& index){
    if(input==KernelValue::PackedBytes)throw std::invalid_argument("already packed");
    const std::string sample=input==KernelValue::Scalar?"float3(input0[tid])":"input0[tid].xyz";
    return materializedKernel(KernelValue::PackedBytes,{input},n,
        "uint3 rgb=uint3(floor(clamp("+sample+",0.0f,1.0f)*255.0f+0.5f));\n"
        "result["+index+"]=rgb.x|(rgb.y<<8)|(rgb.z<<16)|0xff000000u;");
}
inline Source packMaterializedKernel(KernelValue input,int n){
    return packMaterializedKernelAt(input,n,"tid");
}
// GPU transfer uses the same quantization as the tight CPU-oracle readback
// path but writes only logical texels in the portable padded row layout.
struct TextureTransferPack {
    Source source;
    gfx::BufferTextureCopy2D copy;
    size_t bufferBytes=0;
};
inline TextureTransferPack packMaterializedTextureKernel(KernelValue input,int n){
    if(n<8||n>1024)throw std::invalid_argument("invalid texture transfer extent");
    const auto layout=gfx::rgba8CopyLayout(n,n);
    const auto stride=std::to_string(layout.rowPitchBytes/4);
    return {packMaterializedKernelAt(input,n,"(tid/n)*"+stride+"u+tid%n"),
        {n,n,0,layout.rowPitchBytes},layout.bytes};
}
} // namespace aiverse::render::texture_graph::gpu
