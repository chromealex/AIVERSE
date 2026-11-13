#include "apps/editor/core/Projects.h"
#include "apps/editor/core/Workspace.h"
#include "modules/genres/BoardModule.h"
#include "modules/genres/MotionModule.h"
#include <filesystem>
#include <iostream>
using namespace aiverse;
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    namespace fs=std::filesystem;fs::path scratch=argv[2];bool created=false;
    try {
        if(fs::exists(scratch))throw std::runtime_error("scratch directory must be new");
        fs::create_directories(scratch);created=true;int count=0;
        for(const auto& t:editor::listTemplates(argv[1])) {
            if(t.id.rfind("genre-",0)!=0)continue;
            std::string error;auto path=editor::createProject(argv[1],t.id,t.name,scratch.string(),t.id,&error);
            if(path.empty())throw std::runtime_error(error);
            editor::Project project;if(!project.load(path,&error))throw std::runtime_error(error);
            editor::MapDoc doc;auto mapPath=path+"/"+project.manifest["maps"][0].getStr("file");
            if(!doc.load(mapPath,&error))throw std::runtime_error(error);
            if(project.isGenre()) {
                auto expected=Json::parseFile(mapPath);
                if(doc.toJson().dump()!=expected.dump())throw std::runtime_error("raw map did not round trip: "+t.id);
                std::unique_ptr<GameModule> module;
                if(project.manifest.getStr("module")=="board")module=std::make_unique<genres::BoardModule>();else module=std::make_unique<genres::MotionModule>();
                Simulation sim({17,60,0},std::move(module));sim.setup(project.rulesJson,doc.toJson());
                for(int i=0;i<120;++i)sim.step();
            }
            ++count;
        }
        if(count!=13)throw std::runtime_error("expected thirteen genre templates, got "+std::to_string(count));
        fs::remove_all(scratch);std::cout<<"13 genre projects created, loaded and maps preserved\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(created)fs::remove_all(scratch);return 1;}
}
