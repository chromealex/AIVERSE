#include "engine/render/graph/TextureGraphKernelSource.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace aiverse;
using namespace render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    constexpr auto small=gfx::rgba8CopyLayout(8,8);
    static_assert(small.rowPitchBytes==256&&small.bytes==2048);
    constexpr auto odd=gfx::rgba8CopyLayout(65,3);
    static_assert(odd.rowPitchBytes==512&&odd.bytes==1536);
    const auto big=gfx::rgba8CopyLayout(1024,1024);
    check(big.rowPitchBytes==4096&&big.bytes==4194304);
    check(!gfx::rgba8CopyLayout(0,8)&&!gfx::rgba8CopyLayout(8,-1)&&!gfx::rgba8CopyLayout(16385,8));
    const gfx::BufferTextureCopy2D copy{65,3,256,512};
    check(gfx::validBufferTextureCopy2D(copy,1792,65,3));
    check(!gfx::validBufferTextureCopy2D(copy,1791,65,3));
    check(!gfx::validBufferTextureCopy2D(copy,1792,64,3));
    check(!gfx::validBufferTextureCopy2D({65,3,4,512},1792,65,3));
    check(!gfx::validBufferTextureCopy2D({65,3,0,260},1792,65,3));
    check(!gfx::validBufferTextureCopy2D({65,3,std::numeric_limits<size_t>::max(),512},1792,65,3));
    const auto scalar=packMaterializedTextureKernel(KernelValue::Scalar,8);
    check(scalar.bufferBytes==2048&&scalar.copy.rowPitchBytes==256);
    check(scalar.source.msl.find("(tid/n)*64u+tid%n")!=std::string::npos);
    check(scalar.source.glsl.find("vec3(input0[tid])")!=std::string::npos);
    const auto vector=packMaterializedTextureKernel(KernelValue::Vector,65);
    check(vector.bufferBytes==512*65&&vector.copy.width==65);
    check(vector.source.glsl.find("(tid/n)*128u+tid%n")!=std::string::npos);
    check(vector.source.glsl.find("input0[tid].xyz")!=std::string::npos);
    check(packMaterializedKernel(KernelValue::Vector,8).msl.find("result[tid]=")!=std::string::npos);
    bool rejected=false;
    try{(void)packMaterializedTextureKernel(KernelValue::PackedBytes,8);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected);rejected=false;
    try{(void)packMaterializedTextureKernel(KernelValue::Scalar,1025);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected);
    std::cout<<"PASS: aligned RGBA8 copy bounds, padded GPU packing, scalar/vector ABI, unchanged tight pack\n";
}
