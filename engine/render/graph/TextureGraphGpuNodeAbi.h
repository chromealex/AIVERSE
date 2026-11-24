// Shared float4 storage ABI for materialized texture graph passes.
#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::render::texture_graph::gpu {
struct Source {std::string msl,reason,glsl;};
inline constexpr std::size_t maxNodeInputs=3;
inline constexpr int maxPreviewSize=128,maxBakeSize=1024;
inline constexpr std::size_t readbackBlockBytes=64*1024;
inline void replaceShaderToken(std::string& s,const std::string& from,const std::string& to){
    std::size_t at=0;
    while((at=s.find(from,at))!=std::string::npos){s.replace(at,from.size(),to);at+=to.size();}
}
inline std::string glslVectors(std::string s){
    replaceShaderToken(s,"uint3","uvec3");
    replaceShaderToken(s,"uint4","uvec4");
    replaceShaderToken(s,"float2","vec2");
    replaceShaderToken(s,"float3","vec3");
    replaceShaderToken(s,"float4","vec4");
    replaceShaderToken(s,"int2","ivec2");
    return s;
}
// Output binding is 0 (Metal) / 12 (Vulkan TextureResources), input
// bindings are 1..3 / 13..15 in authored port order. Float4 stride is 16.
// The caller owns distinct storage handles and all synchronization.
inline Source materializedKernel(int n,std::size_t inputs,const std::string& helpers,
        const std::string& body,bool pack=false){
    if(n<8||n>maxBakeSize||inputs>maxNodeInputs||(pack&&inputs!=1))
        throw std::invalid_argument("invalid texture node kernel ABI");
    const std::string type=pack?"uint":"float4";
    std::string msl="#include <metal_stdlib>\nusing namespace metal;\n"+helpers+
        "\nkernel void cs_main(device "+type+"* outPixels [[buffer(0)]]";
    std::string glsl=glslVectors(helpers)+"\nlayout(std430,set=0,binding=12) buffer Result { "+
        (pack?"uint":"vec4")+" outPixels[]; };\n";
    for(std::size_t i=0;i<inputs;++i){
        const auto index=std::to_string(i),slot=std::to_string(i+1);
        msl+=",device const float4* src"+index+" [[buffer("+slot+")]]";
        glsl+="layout(std430,set=0,binding="+std::to_string(13+i)+") readonly buffer Input"+
            index+" { vec4 src"+index+"[]; };\n";
    }
    const std::string bounds="const uint n="+std::to_string(n)+"u;if(tid>=n*n)return;\n";
    msl+=",uint tid [[thread_position_in_grid]]){\n"+bounds+body+"\n}\n";
    glsl+="void main(){uint tid=gl_GlobalInvocationID.x;\n"+bounds+glslVectors(body)+"\n}\n";
    return {std::move(msl),{},std::move(glsl)};
}
inline Source compilePackKernel(int n){
    return materializedKernel(n,1,{},
        "uint3 rgb=uint3(floor(clamp(src0[tid].xyz,0.0f,1.0f)*255.0f+0.5f));"
        "outPixels[tid]=rgb.x|(rgb.y<<8)|(rgb.z<<16)|0xff000000u;",true);
}
} // namespace aiverse::render::texture_graph::gpu
