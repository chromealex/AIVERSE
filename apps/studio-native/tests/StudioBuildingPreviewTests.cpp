#include "apps/studio-native/StudioBuildingPanelPreview.h"
#include "engine/buildings/BuildingMaterialAssets.h"
#include "engine/systems/BuildingStructure.h"
#include "engine/render/CameraPng.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;
int aiverseMain(int argc,char** argv){
    try{
        if(argc!=2 && argc!=3)throw std::runtime_error("expected engine root and optional capture directory");
        using namespace lib::systems;
        const auto style=readBuildingStyle(Json::parseFile(std::string(argv[1])+"/engine/assets/building-styles/underwater.json"));
        const auto built=dressStructure(style,"residence",42);
        if(built.mesh.vertices.empty())throw std::runtime_error("no underwater geometry");
        auto model=building::runtimeTexturedBuildingModel(built,{});
        if(!model)throw std::runtime_error("no textured model");
#ifdef AIVERSE_TEST_PANEL_VULKAN
        std::string error;
        auto device=gfx::createVulkanDevice({},&error);
        if(!device){
            std::cerr<<error<<"\n";
            return (error=="Vulkan: loader not found" || error=="Vulkan: no GPU with graphics and present support") ? 77 : 1;
        }
        studio::StudioBuildingPanelPreview preview(std::move(device));
#else
        studio::StudioBuildingPanelPreview preview;
#endif
        preview.setModel(std::move(model));
        const auto first=preview.frame(.65f,.42f,1.f);
        const auto second=preview.frame(1.4f,.6f,1.f);
        if(first.size()!=size_t(512*512*4)||second.size()!=first.size())
            throw std::runtime_error("bad GPU capture extent");
        size_t changed=0,nonflat=0;
        for(size_t i=0;i<first.size();i+=4){
            if(first[i]!=second[i]||first[i+1]!=second[i+1]||first[i+2]!=second[i+2])++changed;
            if(first[i]!=first[0]||first[i+1]!=first[1]||first[i+2]!=first[2])++nonflat;
        }
        if(changed<100||nonflat<100)throw std::runtime_error("missing geometry or camera response: changed="+std::to_string(changed)+", nonflat="+std::to_string(nonflat));
        if(argc==3){
            const std::filesystem::path output=argv[2];
            std::filesystem::create_directories(output);
            if(!render::cameraWritePngRgba8((output/"building-front.png").string(),512,512,first) ||
               !render::cameraWritePngRgba8((output/"building-side.png").string(),512,512,second))
                throw std::runtime_error("Could not write building panel captures");
        }
        std::cout<<"Building panel native GPU orbit PASS, changed pixels="<<changed<<"\n";return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
