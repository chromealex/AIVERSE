#include "engine/gfx/vulkan/Vulkan.h"
#include "texture_graph_gpu_acceptance.h"
int main(int argc,char** argv){
    const bool offscreen=argc==2&&std::string(argv[1])=="--offscreen";
    if(argc!=1&&!offscreen)return 2;
    std::string error;
    auto dev=aiverse::gfx::createVulkanDevice(aiverse::gfx::VulkanWindow{},&error);
    if(!dev){std::fprintf(stderr,"FAIL Vulkan device: %s\n",error.c_str());return 1;}
    return aiverse::tests::textureGraphGpuAcceptance(*dev,aiverse::gfx::Backend::Vulkan,offscreen);
}
