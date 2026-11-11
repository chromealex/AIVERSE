#include <aiverselib/bots/NavMeshData.h>
#include "modules/fps/FpsModule.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace aiverse;using namespace aiverse::lib::bots;
int main(int argc,char** argv){try{
    if(argc!=4){std::cerr<<"Usage: aiverse_navmesh <FPS project> <bots.json with navmesh settings> <output.json>\n";return 2;}
    std::filesystem::path root=argv[1],output=argv[3];if(std::filesystem::exists(output))throw std::runtime_error("output exists; choose a new file");
    auto manifest=Json::parseFile((root/"game.json").string());if(manifest.getStr("module")!="fps")throw JsonError("navmesh: expected FPS project");
    auto rules=Json::parseFile((root/manifest.getStr("rules","rules.json")).string());auto map=Json::parseFile((root/manifest["maps"][size_t(0)].getStr("file")).string());
    Simulation sim({1,fps::readConfig(rules).tickRate,0},std::make_unique<fps::Module>());sim.setup(rules,map);const auto& module=static_cast<fps::Module&>(sim.module());const auto& motor=module.config().motor;
    auto settings=Json::parseFile(argv[2]);auto config=readNavConfig(settings["navmesh"],motor.radius,motor.height,motor.stepHeight);
    auto result=buildNavMesh(module.collision(),config);if(!result.mesh)throw std::runtime_error(result.error);
    std::ofstream file(output,std::ios::binary);if(!file)throw std::runtime_error("cannot open output");file<<navMeshJson(*result.mesh).dump(2)<<'\n';file.close();if(!file)throw std::runtime_error("cannot write output");
    std::cout<<"Built "<<result.mesh->polygons.size()<<" polygons, "<<result.mesh->components.size()<<" connected regions\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
