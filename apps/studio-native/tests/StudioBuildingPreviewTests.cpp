#include "apps/studio-native/StudioBuildingPanelPreview.h"
#include "engine/buildings/BuildingMaterialAssets.h"
#include "engine/systems/BuildingStructure.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;
int aiverseMain(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("engine root required");
        using namespace lib::systems;
        const auto style=readBuildingStyle(Json::parseFile(std::string(argv[1])+"/engine/assets/building-styles/underwater.json"));
        const auto built=dressStructure(style,"residence",42);
        if(built.mesh.vertices.empty())throw std::runtime_error("no underwater geometry");
        auto model=building::runtimeTexturedBuildingModel(built,{});
        if(!model)throw std::runtime_error("no textured model");
        studio::StudioBuildingPanelPreview preview;
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
        std::cout<<"Building panel native GPU orbit PASS, changed pixels="<<changed<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
