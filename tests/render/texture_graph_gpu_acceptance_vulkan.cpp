#include "engine/gfx/vulkan/Vulkan.h"
#include "texture_graph_gpu_acceptance.h"
int main(){
    std::string error;
    auto dev=aiverse::gfx::createVulkanDevice(aiverse::gfx::VulkanWindow{},&error);
    if(!dev){std::fprintf(stderr,"FAIL Vulkan device: %s\n",error.c_str());return 1;}
    return aiverse::tests::textureGraphGpuAcceptance(*dev,aiverse::gfx::Backend::Vulkan);
}
