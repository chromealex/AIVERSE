#include "engine/render/graph/TextureGraphKernelSource.h"
#include "engine/render/graph/TextureGraphCustomCompute.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    auto s=materializedKernel(KernelValue::Vector,{KernelValue::Scalar,KernelValue::Vector,KernelValue::Vector},128,
        "result[tid]=float4(mix(input1[tid].xyz,input2[tid].xyz,input0[tid]),1.0f);");
    check(s.msl.find("float4* result [[buffer(0)]]")!=std::string::npos);
    check(s.msl.find("float* input0 [[buffer(1)]]")!=std::string::npos);
    check(s.msl.find("float4* input2 [[buffer(3)]]")!=std::string::npos);
    check(s.glsl.find("binding=15")!=std::string::npos&&s.glsl.find("vec4 input2[]")!=std::string::npos);
    check(s.glsl.find("vec4(mix(")!=std::string::npos&&s.glsl.find("if(tid>=n*n)return")!=std::string::npos);
    auto scalar=packMaterializedKernel(KernelValue::Scalar,8),rgb=packMaterializedKernel(KernelValue::Vector,1024);
    check(scalar.msl.find("float3(input0[tid])")!=std::string::npos&&scalar.glsl.find("vec3(input0[tid])")!=std::string::npos);
    check(rgb.glsl.find("input0[tid].xyz")!=std::string::npos&&rgb.glsl.find("0xff000000u")!=std::string::npos);
    check(rgb.msl.find("1024u")!=std::string::npos&&rgb.glsl.find("uint result[]")!=std::string::npos);
    auto image=materializedKernel(KernelValue::Vector,{KernelValue::PackedBytes},64,"result[tid]=float4(float(input0[tid]));",{},true);
    check(image.msl.find("uint* input0")!=std::string::npos&&image.glsl.find("uint input0[]")!=std::string::npos);
    bool bad=false;try{(void)materializedKernel(KernelValue::Scalar,std::vector<KernelValue>(4,KernelValue::Scalar),8,"x");}catch(const std::invalid_argument&){bad=true;}check(bad);
    bad=false;try{(void)packMaterializedKernel(KernelValue::PackedBytes,8);}catch(const std::invalid_argument&){bad=true;}check(bad);
    bad=false;try{(void)packMaterializedKernel(KernelValue::Scalar,1025);}catch(const std::invalid_argument&){bad=true;}check(bad);
    namespace cc=aiverse::render::texture_graph::custom_compute;
    cc::Values values{};values[0]=.25f;values[3]=.75f;values[6]=.5f;
    check(cc::evaluate(cc::parse("a b c mix",false),values)==.5f);
    check(cc::evaluate(cc::parse("1 0 /",false),values)==16.f);
    check(cc::evaluate(cc::parse("-2 sqrt",false),values)==0.f);
    check(cc::evaluate(cc::parse("ar br c mix",true),values)==.5f);
    for(const auto* code:{"a +","a b","while","a;","17","ar","a nan +","a 1e2 *"}){
        bad=false;try{(void)cc::parse(code,false);}catch(const std::invalid_argument&){bad=true;}check(bad);
    }
    std::string tooLong="a";for(int i=0;i<25;++i)tooLong+=" neg neg";
    bad=false;try{(void)cc::parse(tooLong,false);}catch(const std::invalid_argument&){bad=true;}check(bad);
    const auto expression=cc::emit(cc::parse("a -1 -",false),[](unsigned){return std::string("a");},[](float x){return std::to_string(x);});
    check(expression.find("--")==std::string::npos);
    std::cout<<"PASS: typed Metal/Vulkan ABI, custom compute interpreter, safe source emission and bounds\\n";
}
