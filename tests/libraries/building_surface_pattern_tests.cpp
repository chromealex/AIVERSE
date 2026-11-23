// Focused, platform-free PBR image generation contract for all building settings.
// Compiles with C++20 and libraries/include only; no gfx, ECS or Box3D device.
#include <aiverselib/systems/BuildingSurfacePattern.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace aiverse::lib::systems;
#define REQUIRE(x) do {if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
    try{
        REQUIRE(!validBuildingSurfacePattern("unknown"));
        REQUIRE(validBuildingSurfacePattern(""));
        for(const auto name:kBuildingSurfacePatterns){
            const auto a=generateBuildingSurface(name,"hull",421u,BuildingSurfaceChannel::BaseColor);
            const auto b=generateBuildingSurface(name,"hull",421u,BuildingSurfaceChannel::BaseColor);
            const auto m=generateBuildingSurface(name,"hull",421u,BuildingSurfaceChannel::MetallicRoughness);
            const auto n=generateBuildingSurface(name,"hull",421u,BuildingSurfaceChannel::Normal);
            REQUIRE(a.width==128 && a.height==128);
            REQUIRE(a.rgba.size()==128u*128u*4u);
            REQUIRE(a.rgba==b.rgba);
            REQUIRE(n.rgba.size()==a.rgba.size());
            REQUIRE(m.rgba.size()==a.rgba.size());
            bool nonFlat=false;
            for(size_t i=0;i<a.rgba.size();i+=4){
                REQUIRE(a.rgba[i+3]==255u);
                REQUIRE(m.rgba[i+3]==255u);
                REQUIRE(m.rgba[i]==255u);
                REQUIRE(m.rgba[i+2]==255u);
                REQUIRE(n.rgba[i+2]==255u);
                REQUIRE(n.rgba[i+3]==255u);
                if(i>0 && a.rgba[i]!=a.rgba[0])nonFlat=true;
            }
            REQUIRE(nonFlat);
            // Same semantic material with different seeded authoring
            // produces a distinct image for nonuniform, seed-dependent patterns.
            const auto changed=generateBuildingSurface(name,"hull",422u,BuildingSurfaceChannel::BaseColor);
            REQUIRE(changed.rgba.size()==a.rgba.size());
            REQUIRE(changed.rgba!=a.rgba);
        }
        REQUIRE(generateBuildingSurface("unsupported","hull",1u,BuildingSurfaceChannel::BaseColor).rgba.empty());
        std::puts("building surface pattern contracts: PASS (16 patterns, 3 PBR maps)");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building surface pattern contracts: FAIL %s\n",e.what());
        return 1;
    }
}
