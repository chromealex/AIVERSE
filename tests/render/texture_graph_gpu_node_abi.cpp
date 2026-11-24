#include "engine/render/graph/TextureGraphGpuNodeAbi.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    auto leaf=materializedKernel(128,0,{},"outPixels[tid]=float4(0.5f);");
    check(leaf.msl.find("device float4* outPixels [[buffer(0)]]")!=std::string::npos);
    check(leaf.glsl.find("vec4 outPixels[]")!=std::string::npos);
    check(leaf.glsl.find("tid>=n*n")!=std::string::npos);
    check(leaf.glsl.find("binding=13")==std::string::npos);
    auto three=materializedKernel(128,3,{},"outPixels[tid]=float4(src0[tid].xyz+src1[tid].xyz*src2[tid].x,1.0f);");
    for(int i=0;i<3;++i){
        check(three.msl.find("src"+std::to_string(i)+" [[buffer("+std::to_string(i+1)+")]]")!=std::string::npos);
        check(three.glsl.find("binding="+std::to_string(i+13))!=std::string::npos);
    }
    check(three.glsl.find("binding=16")==std::string::npos);
    auto pack=compilePackKernel(1024);
    check(pack.msl.find("device uint* outPixels")!=std::string::npos);
    check(pack.glsl.find("uvec3 rgb=uvec3")!=std::string::npos);
    check(pack.glsl.find("float4")==std::string::npos);
    check(pack.glsl.find("0xff000000u")!=std::string::npos);
    bool invalid=false;
    try{materializedKernel(128,4,{},{});}catch(const std::invalid_argument&){invalid=true;}
    check(invalid);invalid=false;
    try{compilePackKernel(1025);}catch(const std::invalid_argument&){invalid=true;}
    check(invalid);invalid=false;
    try{materializedKernel(8,0,{},{},true);}catch(const std::invalid_argument&){invalid=true;}
    check(invalid);
    std::cout<<"PASS: float4 ABI, ordered bindings, RGBA8 pack, bounds\n";
}
